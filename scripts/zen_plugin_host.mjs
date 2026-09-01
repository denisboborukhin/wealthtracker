// SPDX-License-Identifier: MIT
//
// Local compatibility host for separately obtained ZenMoney plugin bundles.
// The bundle remains a separate work and is never distributed by WealthTracker.

import { createRequire } from 'node:module'
import path from 'node:path'
import readline from 'node:readline'

const maximumInputBytes = 8 * 1024 * 1024
const maximumStateBytes = 1024 * 1024
const identifierPattern = /^[a-z0-9][a-z0-9._-]{0,63}$/
const keyPattern = /^[A-Za-z][A-Za-z0-9._-]{0,63}$/
const blockedKeys = new Set(['__proto__', 'constructor', 'prototype'])
const nativeFetch = globalThis.fetch
const NativeHeaders = globalThis.Headers
const NativeBlob = globalThis.Blob
const NativeRequest = globalThis.Request
const NativeResponse = globalThis.Response

let started = false
let completed = false
let inputBufferBytes = 0
let pendingPrompt = null
let preferenceSecrets = []
let promptSecrets = []

const input = readline.createInterface({
  input: process.stdin,
  crlfDelay: Infinity,
  terminal: false
})

function send (event) {
  if (completed && event.type !== 'result' && event.type !== 'error') {
    return
  }
  process.stdout.write(`${JSON.stringify(event)}\n`)
}

function finish () {
  completed = true
  pendingPrompt?.resolve(null)
  pendingPrompt = null
  preferenceSecrets = []
  promptSecrets = []
  input.close()
  process.stdin.pause()
}

function redact (message) {
  let result = String(message ?? 'Plugin execution failed')
  for (const secret of [...preferenceSecrets, ...promptSecrets]) {
    if (secret.length >= 3) {
      result = result.split(secret).join('[redacted]')
    }
  }
  return result.replace(/[\r\n\t]+/g, ' ').slice(0, 4096)
}

function fail (error, retryable = false) {
  if (completed) {
    return
  }
  send({
    type: 'error',
    message: redact(error?.message ?? error),
    retryable: Boolean(retryable || error?.allowRetry || error?.allow_retry)
  })
  finish()
  process.exitCode = 1
}

function assertObject (value, label) {
  if (value === null || typeof value !== 'object' || Array.isArray(value)) {
    throw new Error(`${label} must be an object`)
  }
}

function assertScalarObject (value, label) {
  assertObject(value, label)
  if (Object.keys(value).length > 64) {
    throw new Error(`${label} has too many fields`)
  }
  for (const [key, field] of Object.entries(value)) {
    if (!keyPattern.test(key) || blockedKeys.has(key)) {
      throw new Error(`${label} contains an invalid key`)
    }
    if (field !== null &&
        typeof field !== 'string' &&
        typeof field !== 'number' &&
        typeof field !== 'boolean') {
      throw new Error(`${label} contains a non-scalar value`)
    }
    if (typeof field === 'string' && field.length > 4096) {
      throw new Error(`${label} contains an oversized value`)
    }
  }
}

function cloneJson (value) {
  return JSON.parse(JSON.stringify(value))
}

function makeFetch (allowedOrigins) {
  if (allowedOrigins.length === 0) {
    return nativeFetch
  }
  const allowed = new Set(allowedOrigins)
  return async function restrictedFetch (input, init = {}) {
    const rawUrl = input instanceof NativeRequest ? input.url : input
    let url
    try {
      url = new URL(String(rawUrl))
    } catch {
      throw new Error('Plugin requested an invalid URL')
    }
    if (url.protocol !== 'https:' || !allowed.has(url.origin)) {
      throw new Error('Plugin network origin is not allowed')
    }
    const method = String(
      init.method ?? (input instanceof NativeRequest ? input.method : 'GET')
    ).toUpperCase()
    if (method !== 'GET' && method !== 'HEAD') {
      throw new Error('Plugin network method is not allowed')
    }
    const response = await nativeFetch(input, { ...init, redirect: 'manual' })
    if (response.status >= 300 && response.status < 400) {
      throw new Error('Plugin network redirect was blocked')
    }
    return response
  }
}

function makeConsole () {
  const ignored = () => {}
  return {
    log: ignored,
    info: ignored,
    debug: ignored,
    warn: ignored,
    error: ignored,
    dir: ignored,
    time: ignored,
    timeEnd: ignored,
    assert (condition) {
      if (!condition) {
        throw new Error('Plugin assertion failed')
      }
    }
  }
}

function makeRuntime ({ preferences, state, allowedOrigins }) {
  const accounts = []
  const transactions = []
  const pluginData = cloneJson(state.pluginData ?? {})
  let cookies = Array.isArray(state.cookies) ? cloneJson(state.cookies) : []
  let saveDataRequested = false
  let terminalResolve
  const terminal = new Promise(resolve => {
    terminalResolve = resolve
  })

  function addRecords (target, records) {
    const values = Array.isArray(records) ? records : [records]
    for (const value of values) {
      if (value === null || typeof value !== 'object' || Array.isArray(value)) {
        throw new Error('Plugin emitted a malformed record')
      }
      target.push(value)
    }
  }

  const runtime = {
    application: { platform: 'desktop', version: '1', build: '1' },
    user: {
      id: 'local',
      locale: 'ru_RU',
      subscription: {
        id: 'LocalExecution',
        endDate: Date.now() + 3650 * 24 * 60 * 60 * 1000
      }
    },
    device: {
      id: 'wealthtracker-local',
      manufacturer: 'WealthTracker',
      model: 'Desktop',
      brand: 'WealthTracker',
      os: { name: process.platform, version: process.version }
    },
    features: {
      j2v8Date: true,
      dateProcessing: true,
      binaryRequestBody: true,
      binaryResponseBody: true,
      investmentAccount: true,
      networkCallbacks: false,
      transactionDtoV2: false
    },
    locale: 'ru_RU',
    fetch: makeFetch(allowedOrigins),
    Headers: NativeHeaders,
    Blob: NativeBlob,
    Request: NativeRequest,
    Response: NativeResponse,
    __cookies: cookies,
    getLevel: () => 0,
    trace: () => {},
    logEvent: () => {},
    setExceptions: () => {},
    setDefaultCharset: () => {},
    isAvailable: () => true,
    isAccountSkipped: () => false,
    getPreferences: () => preferences,
    getData (key, defaultValue = undefined) {
      return Object.prototype.hasOwnProperty.call(pluginData, key)
        ? pluginData[key]
        : defaultValue
    },
    setData (key, value) {
      if (!keyPattern.test(key) || blockedKeys.has(key)) {
        throw new Error('Plugin attempted to store an invalid key')
      }
      pluginData[key] = cloneJson(value)
    },
    saveData () {
      saveDataRequested = true
    },
    clearData () {
      for (const key of Object.keys(pluginData)) {
        delete pluginData[key]
      }
      saveDataRequested = true
    },
    async restoreCookies () {
      runtime.__cookies = cloneJson(cookies)
    },
    async saveCookies () {
      cookies = Array.isArray(runtime.__cookies)
        ? cloneJson(runtime.__cookies)
        : []
      saveDataRequested = true
    },
    async getCookies () {
      return cloneJson(cookies)
    },
    async setCookie () {
      throw new Error('Direct cookie API is unavailable in the local host')
    },
    async clearCookies () {
      cookies = []
      runtime.__cookies = []
      saveDataRequested = true
    },
    setClientPfx () {
      throw new Error('Client certificates are unavailable in the local host')
    },
    trustCertificates () {
      throw new Error('Custom certificate trust is unavailable in the local host')
    },
    request () {
      throw new Error('Synchronous network requests are unavailable; use a modular fetch-based plugin')
    },
    requestGet () {
      throw new Error('Synchronous network requests are unavailable; use a modular fetch-based plugin')
    },
    requestPost () {
      throw new Error('Synchronous network requests are unavailable; use a modular fetch-based plugin')
    },
    addAccount (records) {
      addRecords(accounts, records)
    },
    addTransaction (records) {
      addRecords(transactions, records)
    },
    async alert (message) {
      await runtime.readLine(String(message), { inputType: 'text' })
    },
    readLine (message, options = {}) {
      if (pendingPrompt !== null) {
        return Promise.reject(new Error('Plugin requested overlapping prompts'))
      }
      const requestId = `prompt-${Date.now().toString(36)}`
      const timeoutMs = Math.min(
        Math.max(Number(options.time) || 120000, 1000),
        300000
      )
      send({
        type: 'prompt',
        requestId,
        message: String(message).slice(0, 2048),
        inputType: options.inputType === 'number' ? 'number' : 'password',
        timeoutMs
      })
      return new Promise(resolve => {
        const timer = setTimeout(() => {
          if (pendingPrompt?.requestId === requestId) {
            pendingPrompt = null
            resolve(null)
          }
        }, timeoutMs)
        pendingPrompt = {
          requestId,
          resolve (value) {
            clearTimeout(timer)
            resolve(value)
          }
        }
      })
    },
    setResult (result) {
      if (completed) {
        throw new Error('Plugin completed more than once')
      }
      assertObject(result, 'Plugin result')
      if (!result.success) {
        fail(result)
        terminalResolve()
        return
      }
      const outputState = {
        pluginData: saveDataRequested ? pluginData : cloneJson(state.pluginData ?? {}),
        cookies: cloneJson(runtime.__cookies ?? cookies)
      }
      if (Buffer.byteLength(JSON.stringify(outputState)) > maximumStateBytes) {
        fail(new Error('Plugin state exceeds 1 MiB'))
        terminalResolve()
        return
      }
      send({
        type: 'result',
        accounts,
        transactions,
        state: outputState
      })
      finish()
      terminalResolve()
    }
  }
  runtime.setOptions = () => {}
  runtime.setAuthentication = () => {}
  runtime.clearAuthentication = () => {}
  runtime.getLastError = () => ''
  runtime.getLastStatusCode = () => 0
  runtime.getLastStatusString = () => ''
  runtime.getLastResponseHeader = () => null
  runtime.getLastResponseHeaders = () => ({})
  runtime.getLastUrl = () => ''
  runtime.getLastResponseParameters = () => ({})
  return { runtime, terminal }
}

async function start (command) {
  assertObject(command, 'Start command')
  if (!identifierPattern.test(command.pluginId ?? '')) {
    throw new Error('Invalid plugin id')
  }
  if (typeof command.bundlePath !== 'string' ||
      !path.isAbsolute(command.bundlePath) ||
      !['.js', '.cjs'].includes(path.extname(command.bundlePath))) {
    throw new Error('Invalid plugin bundle path')
  }
  if (path.basename(command.bundlePath, path.extname(command.bundlePath)).toLowerCase() !== command.pluginId) {
    throw new Error('Plugin id does not match bundle name')
  }
  assertScalarObject(command.preferences, 'Plugin preferences')
  assertObject(command.state, 'Plugin state')
  if (!Array.isArray(command.allowedOrigins) || command.allowedOrigins.length > 8 ||
      command.allowedOrigins.some(origin => typeof origin !== 'string' ||
        new URL(origin).origin !== origin || !origin.startsWith('https://'))) {
    throw new Error('Invalid plugin network allowlist')
  }
  if (Buffer.byteLength(JSON.stringify(command.state)) > maximumStateBytes) {
    throw new Error('Plugin state exceeds 1 MiB')
  }
  const fromDate = new Date(command.from)
  const toDate = new Date(command.to)
  if (!Number.isFinite(fromDate.getTime()) ||
      !Number.isFinite(toDate.getTime()) ||
      fromDate > toDate) {
    throw new Error('Invalid synchronization period')
  }

  preferenceSecrets = Object.values(command.preferences)
    .filter(value => typeof value === 'string' && value.length >= 3)
  globalThis.global = globalThis
  globalThis.self = globalThis
  globalThis.console = makeConsole()
  const { runtime, terminal } = makeRuntime(command)
  globalThis.ZenMoney = runtime
  globalThis.fetch = runtime.fetch

  const require = createRequire(import.meta.url)
  require(command.bundlePath)
  if (typeof globalThis.main !== 'function') {
    throw new Error('Bundle does not expose a ZenMoney main function')
  }
  Promise.resolve(globalThis.main({
    fromDate,
    toDate,
    isInBackground: false
  })).catch(fail)
  await terminal
}

input.on('line', line => {
  inputBufferBytes += Buffer.byteLength(line)
  if (inputBufferBytes > maximumInputBytes) {
    fail(new Error('Input exceeds the protocol limit'))
    return
  }
  let command
  try {
    command = JSON.parse(line)
  } catch {
    fail(new Error('Invalid JSON command'))
    return
  }
  inputBufferBytes = 0
  if (!started) {
    if (command?.type !== 'start') {
      fail(new Error('The first command must start a plugin'))
      return
    }
    started = true
    start(command).catch(fail)
    return
  }
  if (command?.type !== 'input' ||
      pendingPrompt === null ||
      command.requestId !== pendingPrompt.requestId ||
      typeof command.value !== 'string' ||
      command.value.length > 4096) {
    fail(new Error('Invalid prompt response'))
    return
  }
  const prompt = pendingPrompt
  pendingPrompt = null
  if (command.value.length >= 3) {
    promptSecrets.push(command.value)
  }
  prompt.resolve(command.value)
})

input.on('close', () => {
  if (!completed) {
    fail(new Error('Plugin host input closed before completion'))
  }
})

process.on('uncaughtException', fail)
process.on('unhandledRejection', fail)
