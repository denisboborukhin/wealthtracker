// SPDX-License-Identifier: MIT
//
// Opens an isolated Chromium profile and reports only bounded T-Bank API
// session identifiers observed in request URLs. It never reads cookies,
// headers, request bodies, page contents, credentials, or one-time codes.

import { spawn } from 'node:child_process'
import {
  accessSync,
  constants,
  existsSync,
  mkdtempSync,
  readFileSync,
  rmSync
} from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { pathToFileURL } from 'node:url'

const loginUrl = 'https://www.tbank.ru/login/'
const maximumSessionLength = 4096
const sessionPattern = /^[A-Za-z0-9._-]{16,4096}$/
const startupTimeoutMs = 15_000
const commandTimeoutMs = 15_000
const sessionProbeExpression = `(() => {
  const sessions = new Set()
  for (const entry of performance.getEntriesByType('resource')) {
    try {
      const url = new URL(entry.name)
      if (url.protocol !== 'https:' ||
          url.hostname !== 'www.tbank.ru' ||
          (url.port !== '' && url.port !== '443') ||
          !url.pathname.startsWith('/api/common/v1/')) continue
      const values = url.searchParams.getAll('sessionid')
      if (values.length === 1 && /^[A-Za-z0-9._-]{16,4096}$/.test(values[0])) {
        sessions.add(values[0])
      }
    } catch {}
  }
  return [...sessions]
})()`

export function extractTBankSessionId (rawUrl) {
  if (typeof rawUrl !== 'string' || rawUrl.length > 16 * 1024) {
    return null
  }

  let url
  try {
    url = new URL(rawUrl)
  } catch {
    return null
  }

  if (url.protocol !== 'https:' ||
      url.hostname !== 'www.tbank.ru' ||
      (url.port !== '' && url.port !== '443') ||
      url.username !== '' ||
      url.password !== '' ||
      !url.pathname.startsWith('/api/common/v1/')) {
    return null
  }

  const values = url.searchParams.getAll('sessionid')
  if (values.length !== 1 ||
      values[0].length > maximumSessionLength ||
      !sessionPattern.test(values[0])) {
    return null
  }
  return values[0]
}

function executableCandidate (candidate) {
  if (!candidate || !path.isAbsolute(candidate) || !existsSync(candidate)) {
    return null
  }
  try {
    accessSync(candidate, constants.X_OK)
    return candidate
  } catch {
    return null
  }
}

function pathCandidates (names) {
  const directories = String(process.env.PATH ?? '')
    .split(path.delimiter)
    .filter(Boolean)
  return directories.flatMap(directory =>
    names.map(name => path.join(directory, name)))
}

export function findChromiumExecutable () {
  const configured = executableCandidate(
    process.env.WEALTHTRACKER_CHROME_PATH)
  if (configured) {
    return configured
  }

  let candidates
  if (process.platform === 'darwin') {
    candidates = [
      '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
      '/Applications/Chromium.app/Contents/MacOS/Chromium',
      '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge',
      '/Applications/Brave Browser.app/Contents/MacOS/Brave Browser'
    ]
  } else if (process.platform === 'win32') {
    const roots = [
      process.env.PROGRAMFILES,
      process.env['PROGRAMFILES(X86)'],
      process.env.LOCALAPPDATA
    ].filter(Boolean)
    candidates = roots.flatMap(root => [
      path.join(root, 'Google', 'Chrome', 'Application', 'chrome.exe'),
      path.join(root, 'Chromium', 'Application', 'chrome.exe'),
      path.join(root, 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
      path.join(root, 'BraveSoftware', 'Brave-Browser', 'Application',
        'brave.exe')
    ])
  } else {
    candidates = pathCandidates([
      'google-chrome',
      'google-chrome-stable',
      'chromium',
      'chromium-browser',
      'microsoft-edge',
      'brave-browser'
    ])
  }

  return candidates.map(executableCandidate).find(Boolean) ?? null
}

function send (event) {
  process.stdout.write(`${JSON.stringify(event)}\n`)
}

function delay (milliseconds) {
  return new Promise(resolve => setTimeout(resolve, milliseconds))
}

async function waitForDevToolsEndpoint (profile, browserProcess) {
  const activePortPath = path.join(profile, 'DevToolsActivePort')
  const deadline = Date.now() + startupTimeoutMs
  while (Date.now() < deadline) {
    if (browserProcess.exitCode !== null || browserProcess.signalCode !== null) {
      throw new Error('browser-exited')
    }
    try {
      const lines = readFileSync(activePortPath, 'utf8').trim().split(/\r?\n/)
      const port = Number(lines[0])
      const endpoint = lines[1]
      if (Number.isInteger(port) && port >= 1024 && port <= 65535 &&
          /^\/devtools\/browser\/[A-Za-z0-9-]+$/.test(endpoint)) {
        return `ws://127.0.0.1:${port}${endpoint}`
      }
    } catch {}
    await delay(50)
  }
  throw new Error('browser-timeout')
}

class CdpConnection {
  constructor (socket) {
    this.socket = socket
    this.nextId = 1
    this.pending = new Map()
    socket.binaryType = 'arraybuffer'
    socket.addEventListener('message', event => this.handleMessage(event.data))
    socket.addEventListener('close', () => this.rejectPending())
    socket.addEventListener('error', () => this.rejectPending())
  }

  call (method, params = {}, sessionId = undefined) {
    const id = this.nextId++
    const message = { id, method, params }
    if (sessionId) {
      message.sessionId = sessionId
    }
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pending.delete(id)
        reject(new Error('devtools-timeout'))
      }, commandTimeoutMs)
      this.pending.set(id, { resolve, reject, timeout })
      this.socket.send(JSON.stringify(message))
    })
  }

  handleMessage (data) {
    let message
    try {
      const text = typeof data === 'string'
        ? data
        : new TextDecoder().decode(data)
      message = JSON.parse(text)
    } catch {
      return
    }

    if (Number.isInteger(message.id)) {
      const pending = this.pending.get(message.id)
      if (!pending) {
        return
      }
      this.pending.delete(message.id)
      clearTimeout(pending.timeout)
      if (message.error) {
        pending.reject(new Error('devtools-command'))
      } else {
        pending.resolve(message.result ?? {})
      }
      return
    }
  }

  rejectPending () {
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timeout)
      pending.reject(new Error('devtools-closed'))
    }
    this.pending.clear()
  }

  close () {
    this.rejectPending()
    try {
      this.socket.close()
    } catch {}
  }
}

async function connectDevTools (endpoint) {
  if (typeof globalThis.WebSocket !== 'function') {
    throw new Error('node-version')
  }
  return await new Promise((resolve, reject) => {
    const socket = new WebSocket(endpoint)
    const timeout = setTimeout(() => {
      socket.close()
      reject(new Error('devtools-timeout'))
    }, startupTimeoutMs)
    socket.addEventListener('open', () => {
      clearTimeout(timeout)
      resolve(new CdpConnection(socket))
    }, { once: true })
    socket.addEventListener('error', () => {
      clearTimeout(timeout)
      reject(new Error('devtools-connect'))
    }, { once: true })
  })
}

function publicError (error) {
  switch (error?.message) {
    case 'browser-missing':
      return 'Не найден Google Chrome, Chromium, Edge или Brave'
    case 'node-version':
      return 'Для безопасного входа в Т-Банк нужен Node.js 22.4 или новее'
    case 'browser-exited':
      return 'Браузер закрылся до завершения входа'
    case 'browser-timeout':
      return 'Браузер не запустил изолированный профиль вовремя'
    default:
      return 'Не удалось установить защищённую связь с изолированным браузером'
  }
}

async function stopBrowser (browserProcess) {
  if (!browserProcess || browserProcess.exitCode !== null ||
      browserProcess.signalCode !== null) {
    return
  }
  try {
    if (process.platform === 'win32') {
      browserProcess.kill()
    } else {
      process.kill(-browserProcess.pid, 'SIGTERM')
    }
  } catch {
    try {
      browserProcess.kill()
    } catch {}
  }

  const deadline = Date.now() + 1500
  while (browserProcess.exitCode === null &&
         browserProcess.signalCode === null && Date.now() < deadline) {
    await delay(50)
  }
  if (browserProcess.exitCode === null && browserProcess.signalCode === null) {
    try {
      if (process.platform === 'win32') {
        browserProcess.kill('SIGKILL')
      } else {
        process.kill(-browserProcess.pid, 'SIGKILL')
      }
    } catch {}
  }
}

export async function main () {
  let browserProcess
  let profile
  let cdp
  let stopping = false
  let ready = false
  let probeTimer

  const cleanup = async () => {
    if (stopping) {
      return
    }
    stopping = true
    clearInterval(probeTimer)
    cdp?.close()
    await stopBrowser(browserProcess)
    if (profile) {
      try {
        rmSync(profile, { recursive: true, force: true })
      } catch {}
    }
  }

  const terminate = () => {
    void cleanup().finally(() => process.exit(0))
  }
  process.once('SIGINT', terminate)
  process.once('SIGTERM', terminate)
  process.stdin.setEncoding('utf8')
  process.stdin.on('data', data => {
    if (String(data).includes('"type":"stop"')) {
      terminate()
    }
  })
  process.stdin.on('end', terminate)

  try {
    const executable = findChromiumExecutable()
    if (!executable) {
      throw new Error('browser-missing')
    }
    profile = mkdtempSync(path.join(tmpdir(), 'wealthtracker-tbank-'))
    browserProcess = spawn(executable, [
      '--remote-debugging-port=0',
      '--remote-debugging-address=127.0.0.1',
      `--user-data-dir=${profile}`,
      '--no-first-run',
      '--no-default-browser-check',
      '--disable-sync',
      '--disable-default-apps',
      '--disable-component-update',
      'about:blank'
    ], {
      detached: process.platform !== 'win32',
      stdio: 'ignore'
    })
    browserProcess.once('error', () => {
      if (!stopping) {
        void cleanup().finally(() => process.exit(1))
      }
    })
    browserProcess.once('exit', () => {
      if (!stopping) {
        if (ready) {
          send({ type: 'error', message: publicError(new Error('browser-exited')) })
        }
        void cleanup().finally(() => process.exit(ready ? 1 : 0))
      }
    })

    const endpoint = await waitForDevToolsEndpoint(profile, browserProcess)
    cdp = await connectDevTools(endpoint)
    const observed = new Set()

    const targets = await cdp.call('Target.getTargets')
    const page = targets.targetInfos?.find(target => target.type === 'page')
    if (!page?.targetId) {
      throw new Error('devtools-target')
    }
    const attached = await cdp.call('Target.attachToTarget', {
      targetId: page.targetId,
      flatten: true
    })
    await cdp.call('Page.navigate', { url: loginUrl }, attached.sessionId)
    ready = true
    send({ type: 'ready' })
    let probing = false
    probeTimer = setInterval(async () => {
      if (stopping || probing) {
        return
      }
      probing = true
      try {
        const evaluated = await cdp.call('Runtime.evaluate', {
          expression: sessionProbeExpression,
          returnByValue: true,
          awaitPromise: false,
          userGesture: false
        }, attached.sessionId)
        const values = evaluated.result?.value
        if (!Array.isArray(values) || values.length > 32) {
          return
        }
        for (const value of values) {
          if (typeof value !== 'string' || !sessionPattern.test(value) ||
              observed.has(value)) {
            continue
          }
          observed.add(value)
          send({ type: 'session', sessionId: value })
        }
      } catch {
        // Cross-document navigation temporarily destroys the execution
        // context. The next bounded probe uses the new page context.
      } finally {
        probing = false
      }
    }, 250)
  } catch (error) {
    if (!stopping) {
      send({ type: 'error', message: publicError(error) })
    }
    await cleanup()
    process.exitCode = 1
  }
}

const invokedPath = process.argv[1]
  ? pathToFileURL(path.resolve(process.argv[1])).href
  : ''
if (invokedPath === import.meta.url) {
  await main()
}
