module;

#include <QByteArray>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QString>

#include <expected>
#include <optional>

export module wt.vtb_browser;

import wt.connectors;

export namespace wealthtracker::app {

enum class VtbProjectionStage { Authentication, Loading, FrontendChanged, Ready };

struct VtbFinancialProjection {
    VtbProjectionStage stage{VtbProjectionStage::Authentication};
    std::optional<wealthtracker::connectors::NormalizedBatch> batch;
    QString diagnostic;
    int rejectedTransactions = 0;
};

[[nodiscard]] const QString& vtb_sync_start_script() {
    static const QString script = QStringLiteral(R"VTB((() => {
  try {
    const pageUrl = new URL(globalThis.location.href)
    if (pageUrl.protocol !== 'https:' || pageUrl.hostname !== 'online.vtb.ru' ||
        (pageUrl.port !== '' && pageUrl.port !== '443') ||
        pageUrl.username !== '' || pageUrl.password !== '') return 'wrong-origin'
    const stateKey = '__wealthtrackerVtbSync'
    const existing = globalThis[stateKey]
    if (existing && (existing.status === 'running' || existing.status === 'ready' ||
                     existing.status === 'failed'))
      return existing.status
    if (existing?.status === 'waiting' && Number(existing.retryAt) > Date.now())
      return existing.status
    if (existing?.status === 'authentication' && Number(existing.retryAt) > Date.now())
      return existing.status

    const state = globalThis[stateKey] = { status: 'running', requestStage: 'module' }
    const maximumPayloadBytes = 8 * 1024 * 1024
    const maximumHistoryTemplates = 16
    const maximumHistoryRequests = 512
    const maximumConcurrentHistoryRequests = 4
    const historyWindowDays = 31
    const historyQuietMilliseconds = 3000
    const historyTimeoutMilliseconds = 150 * 1000
    const historyPrefix =
      '/msa/api-gw/private/history-hub/history-hub-homer/v1/history/'
    const historyKinds = new Set(['byUser', 'byProducts', 'byAccount', 'byCard'])
    const unwrap = response => response && typeof response === 'object' &&
      Object.prototype.hasOwnProperty.call(response, 'data') ? response.data : response
    const validPayload = value => {
      if (!value || typeof value !== 'object') return false
      try {
        if (typeof globalThis.TextEncoder !== 'function') return false
        return new TextEncoder().encode(JSON.stringify(value)).byteLength <= maximumPayloadBytes
      } catch (ignored) { return false }
    }
    const frontendFailure = code => { throw { kind: 'frontend', code: code } }
    const httpStatus = error => Number(
      error?.response?.status ?? error?.response?.statusCode ??
      error?.status ?? error?.statusCode)
    const delay = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds))
    const matchesHistoryEndpoint = value => {
      try {
        const url = new URL(String(value ?? ''), pageUrl.origin)
        if (url.origin !== pageUrl.origin || url.username !== '' || url.password !== '' ||
            !url.pathname.startsWith(historyPrefix))
          return false
        return historyKinds.has(url.pathname.slice(historyPrefix.length))
      } catch (ignored) { return false }
    }
    const portfolioProductIds = portfolio => {
      const ids = new Set()
      const visit = (value, depth = 0) => {
        if (!value || typeof value !== 'object' || depth > 6 || ids.size >= 256) return
        if (Array.isArray(value)) {
          for (const item of value) visit(item, depth + 1)
          return
        }
        const id = String(value.publicId ?? value.productId ?? value.id ??
                          value.accountId ?? '').trim()
        if (id) {
          if (id.length <= 160) ids.add(id)
          const cards = Array.isArray(value.cards) ? value.cards :
            value.cards && typeof value.cards === 'object' ? Object.values(value.cards) : []
          for (const card of cards) visit(card, depth + 1)
          return
        }
        for (const child of Object.values(value)) visit(child, depth + 1)
      }
      for (const group of [portfolio?.cards, portfolio?.accounts,
                           portfolio?.deposits, portfolio?.saving,
                           portfolio?.loans, portfolio?.metalAccounts,
                           portfolio?.wallets]) visit(group)
      return [...ids]
    }
    const capturePageHistory = (client, requiredProductIds) => new Promise((resolve, reject) => {
      const prototype = globalThis.XMLHttpRequest?.prototype
      if (!prototype || typeof prototype.open !== 'function' ||
          typeof prototype.send !== 'function') {
        frontendFailure('VTB-H1')
      }
      const nativeOpen = prototype.open
      const nativeSend = prototype.send
      const operations = []
      const templates = []
      const templateKeys = new Set()
      let responseCount = 0
      let settleGeneration = 0
      let settleTimer = 0
      let overallTimer = 0
      let finished = false
      let lastStatus = 0
      let transportFailure = false
      const restore = () => {
        if (prototype.open === wrappedOpen) prototype.open = nativeOpen
        if (prototype.send === wrappedSend) prototype.send = nativeSend
      }
      const finish = (error = null, complete = false) => {
        if (finished) return
        finished = true
        if (settleTimer) clearTimeout(settleTimer)
        if (overallTimer) clearTimeout(overallTimer)
        restore()
        if (error) reject(error)
        else resolve({ operations, complete: complete && !transportFailure })
      }
      const normalizedKey = value => String(value).replace(/[-_]/g, '').toLowerCase()
      const historyDate = (sample, value) => {
        const source = String(sample ?? '')
        const day = String(value.getDate()).padStart(2, '0')
        const month = String(value.getMonth() + 1).padStart(2, '0')
        const year = String(value.getFullYear())
        if (/^\d{2}\.\d{2}\.\d{4}$/.test(source))
          return `${day}.${month}.${year}`
        if (/^\d{4}-\d{2}-\d{2}$/.test(source))
          return `${year}-${month}-${day}`
        if (/^\d{4}-\d{2}-\d{2}T/.test(source))
          return `${year}-${month}-${day}${source.slice(10)}`
        if (/^\d{13}$/.test(source)) return String(value.getTime())
        if (/^\d{10}$/.test(source)) return String(Math.floor(value.getTime() / 1000))
        return null
      }
      const parseTemplate = value => {
        try {
          const url = new URL(String(value ?? ''), pageUrl.origin)
          if (!matchesHistoryEndpoint(url.href ?? value)) return null
          const params = {}
          const query = String(url.search ?? '').replace(/^\?/, '')
          for (const pair of query.split('&')) {
            if (!pair) continue
            const separator = pair.indexOf('=')
            const key = decodeURIComponent(separator < 0 ? pair : pair.slice(0, separator))
            const item = decodeURIComponent(separator < 0 ? '' : pair.slice(separator + 1))
            if (!key || Object.prototype.hasOwnProperty.call(params, key)) return null
            params[key] = item
          }
          let fromKey = ''
          let toKey = ''
          let pageKey = ''
          let offsetKey = ''
          let limitKey = ''
          for (const key of Object.keys(params)) {
            const normalized = normalizedKey(key)
            if (['datefrom', 'fromdate', 'startdate'].includes(normalized)) fromKey = key
            if (['dateto', 'todate', 'enddate'].includes(normalized)) toKey = key
            if (['page', 'pagenumber', 'pageindex'].includes(normalized)) pageKey = key
            if (['offset', 'skip', 'startindex'].includes(normalized)) offsetKey = key
            if (['limit', 'pagesize', 'pagelimit', 'itemsperpage',
                 'operationslimit', 'operationsperpage'].includes(normalized)) limitKey = key
          }
          const stable = Object.keys(params).filter(
            key => key !== fromKey && key !== toKey && key !== pageKey && key !== offsetKey)
            .sort().map(key => `${key}=${params[key]}`).join('&')
          return { path: url.pathname, params, fromKey, toKey, pageKey, offsetKey, limitKey,
                   key: `${url.pathname}?${stable}` }
        } catch (ignored) { return null }
      }
      const replayable = template => template.fromKey && template.toKey &&
        historyDate(template.params[template.fromKey], new Date()) !== null &&
        historyDate(template.params[template.toKey], new Date()) !== null
      const parameterProductIds = value => {
        const source = String(value ?? '').trim()
        if (!source) return []
        try {
          const parsed = JSON.parse(source)
          if (Array.isArray(parsed)) return parsed.map(item => String(item))
        } catch (ignored) {}
        return source.split(',').map(item => item.trim()).filter(Boolean)
      }
      const productParameter = (sample, productId) => {
        const source = String(sample ?? '').trim()
        try {
          if (Array.isArray(JSON.parse(source))) return JSON.stringify([productId])
        } catch (ignored) {}
        return productId
      }
      const selectedTemplates = () => {
        const selected = templates.filter(replayable)
        const stableKeys = template => Object.keys(template.params).filter(
          key => ![template.fromKey, template.toKey, template.pageKey,
                   template.offsetKey, template.limitKey].includes(key))
        const global = selected.filter(template =>
          template.path.endsWith('/byUser') && stableKeys(template).length === 0)
        if (global.length > 0) return global
        const required = new Set(requiredProductIds)
        const expanded = [...selected]
        const expandedKeys = new Set(expanded.map(template => template.key))
        for (const template of selected) {
          if (!template.path.endsWith('/byProducts') &&
              !template.path.endsWith('/byAccount') &&
              !template.path.endsWith('/byCard')) continue
          for (const key of Object.keys(template.params)) {
            if ([template.fromKey, template.toKey, template.pageKey,
                 template.offsetKey, template.limitKey].includes(key)) continue
            const parameterIds = parameterProductIds(template.params[key])
            if (parameterIds.length !== 1 || !required.has(parameterIds[0])) continue
            for (const productId of required) {
              const params = Object.assign({}, template.params, {
                [key]: productParameter(template.params[key], productId)
              })
              const stable = Object.keys(params).filter(
                item => item !== template.fromKey && item !== template.toKey &&
                        item !== template.pageKey && item !== template.offsetKey)
                .sort().map(item => `${item}=${params[item]}`).join('&')
              const expandedKey = `${template.path}?${stable}`
              if (expandedKeys.has(expandedKey)) continue
              expandedKeys.add(expandedKey)
              expanded.push(Object.assign({}, template, { params, key: expandedKey }))
            }
          }
        }
        const covered = new Set()
        for (const template of expanded)
          for (const value of Object.values(template.params))
            for (const productId of parameterProductIds(value))
              if (required.has(productId)) covered.add(productId)
        if (required.size > 0 && [...required].some(productId => !covered.has(productId)))
          transportFailure = true
        return expanded
      }
      const requestHeaders = () => ({
        'x-request-id': globalThis.crypto &&
          typeof globalThis.crypto.randomUUID === 'function'
            ? globalThis.crypto.randomUUID()
            : `wt-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 14)}`,
        'x-initiator-service': 'web_history_page',
        'x-smart-update-request': false
      })
      const continuationHint = history => {
        const candidates = [history, history?.pagination, history?.page, history?.meta]
        for (const candidate of candidates) {
          if (!candidate || typeof candidate !== 'object') continue
          for (const key of ['hasNext', 'hasMore', 'more'])
            if (typeof candidate[key] === 'boolean') return candidate[key]
          if (typeof candidate.last === 'boolean') return !candidate.last
        }
        return null
      }
      const operationDate = operation => {
        if (!operation || typeof operation !== 'object') return null
        const date = new Date(String(operation.operationDate ?? operation.date ??
                                     operation.createdAt ?? ''))
        return Number.isNaN(date.getTime()) ? null : date
      }
      const replayTwoYears = () => {
        restore()
        const selected = selectedTemplates()
        if (selected.length === 0) {
          finish({ status: lastStatus, kind: 'history-transport' })
          return
        }
        const rangeTo = new Date()
        rangeTo.setHours(0, 0, 0, 0)
        const rangeFrom = new Date(rangeTo)
        rangeFrom.setFullYear(rangeFrom.getFullYear() - 2)
        let requestCount = 0
        const replayPage = (template, params, windowFrom, windowTo) => {
          if (finished) return Promise.reject({ kind: 'history-cancelled' })
          if (requestCount >= maximumHistoryRequests)
            return Promise.reject({ kind: 'history-limit' })
          ++requestCount
          return client.get(template.path, { params, headers: requestHeaders() })
            .then(response => {
              if (finished) throw { kind: 'history-cancelled' }
              const history = unwrap(response)
              if (!validPayload(history) || !Array.isArray(history.operations))
                frontendFailure('VTB-H1')
              const lowerBound = new Date(windowFrom)
              lowerBound.setHours(0, 0, 0, 0)
              const upperBound = new Date(windowTo)
              upperBound.setHours(23, 59, 59, 999)
              for (const operation of history.operations) {
                const date = operationDate(operation)
                if (!date || date < lowerBound || date > upperBound) {
                  transportFailure = true
                  continue
                }
                operations.push(operation)
              }
              if (!validPayload({ operations })) frontendFailure('VTB-H1')
              const hint = continuationHint(history)
              const pageSize = template.limitKey
                ? Number(params[template.limitKey]) : 0
              const terminal = hint === false ||
                (hint === null && (history.operations.length === 0 ||
                  (Number.isInteger(pageSize) && pageSize > 0 &&
                   history.operations.length < pageSize)))
              if (terminal) return
              if (hint !== true && !(Number.isInteger(pageSize) && pageSize > 0 &&
                                     history.operations.length >= pageSize)) {
                transportFailure = true
                return
              }
              const next = Object.assign({}, params)
              if (template.pageKey) {
                const current = Number(params[template.pageKey])
                if (!Number.isInteger(current) || current < 0) {
                  transportFailure = true
                  return
                }
                next[template.pageKey] = String(current + 1)
              } else if (template.offsetKey && Number.isInteger(pageSize) && pageSize > 0) {
                const current = Number(params[template.offsetKey])
                if (!Number.isInteger(current) || current < 0) {
                  transportFailure = true
                  return
                }
                next[template.offsetKey] = String(current + pageSize)
              } else {
                transportFailure = true
                return
              }
              return replayPage(template, next, windowFrom, windowTo)
            })
        }
        const jobs = []
        for (const template of selected) {
          if (!template.fromKey || !template.toKey ||
              historyDate(template.params[template.fromKey], rangeFrom) === null ||
              historyDate(template.params[template.toKey], rangeTo) === null) {
            transportFailure = true
            continue
          }
          for (let start = new Date(rangeFrom); start <= rangeTo;) {
            const end = new Date(start)
            end.setDate(end.getDate() + historyWindowDays - 1)
            if (end > rangeTo) end.setTime(rangeTo.getTime())
            const pageFrom = new Date(start)
            const pageTo = new Date(end)
            start = new Date(end)
            start.setDate(start.getDate() + 1)
            const params = Object.assign({}, template.params)
            params[template.fromKey] = historyDate(template.params[template.fromKey], pageFrom)
            params[template.toKey] = historyDate(template.params[template.toKey], pageTo)
            if (template.pageKey) {
              const firstPage = Number(template.params[template.pageKey]) === 0 ? 0 : 1
              params[template.pageKey] = String(firstPage)
            }
            if (template.offsetKey) params[template.offsetKey] = '0'
            jobs.push({ template, params, pageFrom, pageTo })
          }
        }
        let nextJobIndex = 0
        const runNextJob = () => {
          if (finished || nextJobIndex >= jobs.length) return Promise.resolve()
          const job = jobs[nextJobIndex++]
          return replayPage(job.template, job.params, job.pageFrom, job.pageTo).then(runNextJob)
        }
        const workers = []
        for (let index = 0;
             index < Math.min(maximumConcurrentHistoryRequests, jobs.length); ++index)
          workers.push(runNextJob())
        Promise.all(workers).then(() => finish(null, true)).catch(error => {
          const status = httpStatus(error)
          if (status === 401 || status === 403 || error?.kind === 'frontend') finish(error)
          else {
            transportFailure = true
            finish(null, false)
          }
        })
      }
      const scheduleReplay = () => {
        const generation = ++settleGeneration
        settleTimer = setTimeout(() => {
          if (finished || generation !== settleGeneration) return
          if (responseCount > 0) replayTwoYears()
          else finish({ status: lastStatus, kind: 'history-transport' })
        }, historyQuietMilliseconds)
      }
      const captureResponse = request => {
        if (finished) return
        lastStatus = Number(request.status)
        if (lastStatus === 401 || lastStatus === 403) {
          finish({ status: lastStatus })
          return
        }
        if (lastStatus < 200 || lastStatus >= 300) {
          transportFailure = true
          scheduleReplay()
          return
        }
        try {
          let payload = request.response
          if (!payload || typeof payload !== 'object')
            payload = JSON.parse(String(request.responseText ?? ''))
          const history = unwrap(payload)
          if (!validPayload(history) || !Array.isArray(history.operations) ||
              responseCount >= maximumHistoryTemplates)
            frontendFailure('VTB-H1')
          const template = parseTemplate(request.__wealthtrackerVtbHistoryUrl)
          if (!template) frontendFailure('VTB-H1')
          ++responseCount
          if (!templateKeys.has(template.key)) {
            templateKeys.add(template.key)
            templates.push(template)
          }
          if (!validPayload({ operations })) frontendFailure('VTB-H1')
          scheduleReplay()
        } catch (error) {
          finish(error?.kind === 'frontend' ? error : { kind: 'frontend', code: 'VTB-H1' })
        }
      }
      function wrappedOpen(method, url, ...rest) {
        this.__wealthtrackerVtbHistoryRequest =
          String(method ?? '').toUpperCase() === 'GET' && matchesHistoryEndpoint(url)
        this.__wealthtrackerVtbHistoryUrl = this.__wealthtrackerVtbHistoryRequest
          ? String(url ?? '') : ''
        return nativeOpen.call(this, method, url, ...rest)
      }
      function wrappedSend(body) {
        if (this.__wealthtrackerVtbHistoryRequest)
          this.addEventListener('loadend', () => captureResponse(this), { once: true })
        return nativeSend.call(this, body)
      }
      prototype.open = wrappedOpen
      prototype.send = wrappedSend
      overallTimer = setTimeout(
        () => {
          transportFailure = true
          finish(null, false)
        },
        historyTimeoutMilliseconds)
      try {
        if (!globalThis.history || typeof globalThis.history.pushState !== 'function' ||
            typeof globalThis.dispatchEvent !== 'function' ||
            typeof globalThis.PopStateEvent !== 'function')
          frontendFailure('VTB-H1')
        globalThis.history.pushState({}, '', '/history')
        globalThis.dispatchEvent(new PopStateEvent('popstate'))
      } catch (error) {
        finish(error?.kind === 'frontend' ? error : { kind: 'frontend', code: 'VTB-H1' })
      }
    })
    const readPortfolio = (client, attempt = 0) => client.get(
      '/msa/api-gw/private/portfolio/portfolio-main-page/portfolios/active',
      { params: { requestProductTypes: 'CARD,ACCOUNT,SAVING,LOAN,METAL,WALLET' } })
      .then(response => {
        const portfolio = unwrap(response)
        if (!validPayload(portfolio)) frontendFailure('VTB-P1')
        if (portfolio.loadStatus === 'LOADING') {
          if (attempt >= 2) frontendFailure('VTB-P2')
          return delay(5000).then(() => readPortfolio(client, attempt + 1))
        }
        const portfolioData = portfolio.portfolio ?? portfolio
        if (!validPayload(portfolioData)) frontendFailure('VTB-P1')
        return portfolioData
      })
    let client
    let portfolio
    let balances
    const container = globalThis.mf_1488_core
    Promise.resolve()
      .then(() => {
        if (!container || typeof container.get !== 'function') throw { kind: 'not-ready' }
        return container.get('./HttpClient')
      })
      .then(factory => {
        if (typeof factory !== 'function') throw { kind: 'not-ready' }
        const exposed = factory()
        client = exposed?.http
        if (!client || typeof client.get !== 'function') throw { kind: 'not-ready' }
        state.requestStage = 'portfolio'
        return readPortfolio(client)
      })
      .then(value => {
        portfolio = value
        state.requestStage = 'balances'
        return Promise.resolve()
          .then(() => client.get(
            '/msa/api-gw/private/portfolio/portfolio-balances/financial/summary'))
          .then(response => {
            const supplement = unwrap(response)
            balances = validPayload(supplement) ? supplement : { balanceResponse: [] }
          })
          .catch(error => {
            const status = httpStatus(error)
            if (status === 401 || status === 403) throw error
            balances = { balanceResponse: [] }
          })
      })
      .then(() => {
        state.requestStage = 'history'
        return capturePageHistory(client, portfolioProductIds(portfolio))
          .then(snapshot => {
            state.history = { operations: snapshot.operations }
            state.historyComplete = snapshot.complete
          })
          .catch(error => {
            const status = httpStatus(error)
            if (status === 401 || status === 403 || error?.kind === 'frontend') throw error
            state.history = { operations: [] }
            state.historyComplete = false
          })
      })
      .then(() => {
        state.portfolio = portfolio
        state.balances = balances
        state.reason = state.historyComplete ? '' : 'VTB-H2'
        state.status = 'ready'
      })
      .catch(error => {
        delete state.portfolio
        delete state.balances
        delete state.history
        const status = httpStatus(error)
        if (status === 401 || status === 403) {
          state.status = 'authentication'
          state.retryAt = Date.now() + 1000
        } else if (error?.kind === 'not-ready') {
          state.status = 'waiting'
          state.retryAt = Date.now() + 500
        } else {
          state.status = 'failed'
          const stageCode = state.requestStage === 'module' ? 'VTB-M1' :
            state.requestStage === 'portfolio' ? 'VTB-P3' :
            state.requestStage === 'history' ? 'VTB-H2' : 'VTB-R1'
          state.reason = error?.kind === 'frontend' ? error.code : stageCode
        }
      })
    return state.status
  } catch (ignored) { return 'wrong-origin' }
})())VTB");
    return script;
}

[[nodiscard]] const QString& vtb_financial_projection_script() {
    static const QString script = QStringLiteral(R"VTB((() => {
  const result = { version: 1, stage: 'authentication', authenticated: false,
                   diagnostic: '', rejectedTransactions: 0,
                   accounts: [], transactions: [] }
  try {
    const pageUrl = new URL(globalThis.location.href)
    if (pageUrl.protocol !== 'https:' || pageUrl.hostname !== 'online.vtb.ru' ||
        (pageUrl.port !== '' && pageUrl.port !== '443') ||
        pageUrl.username !== '' || pageUrl.password !== '') return JSON.stringify(result)
    const state = globalThis.__wealthtrackerVtbSync
    if (!state || state.status === 'authentication') return JSON.stringify(result)
    result.authenticated = true
    result.stage = 'loading'
    if (state.status === 'running' || state.status === 'waiting') return JSON.stringify(result)
    if (state.status !== 'ready') {
      result.stage = 'frontend-changed'
      result.diagnostic = ['VTB-M1', 'VTB-P1', 'VTB-P2', 'VTB-P3',
                           'VTB-H1', 'VTB-H2', 'VTB-R1']
        .includes(state.reason) ? state.reason : 'VTB-X1'
      return JSON.stringify(result)
    }
    let projectionIncomplete = state.historyComplete === false
    result.diagnostic = projectionIncomplete ? 'VTB-H2' : ''

    const text = value => {
      if (value === null || value === undefined) return ''
      return String(value).replace(/[\r\n\t]/g, ' ').trim().slice(0, 256)
    }
    const identifier = value => text(value).slice(0, 160)
    const currency = value => {
      if (value && typeof value === 'object')
        value = value.currency ?? value.code ?? value.name ?? value.strCode
      const code = text(value).toUpperCase()
      return code === 'RUR' || code === '643' ? 'RUB' : /^[A-Z]{3}$/.test(code) ? code : ''
    }
    const money = value => {
      if (value === null || value === undefined) return null
      let raw = value
      if (raw && typeof raw === 'object' && raw.amount && typeof raw.amount === 'object')
        raw = raw.amount
      const numeric = raw && typeof raw === 'object'
        ? raw.sum ?? raw.value ?? raw.amount ?? raw.balance : raw
      const match = String(numeric ?? '').trim().replace(',', '.')
        .match(/^([+-]?)(\d{1,16})(?:\.(\d{1,2}))?$/)
      if (!match) return null
      const magnitude = Number(match[2]) * 100 + Number(`${match[3] ?? ''}00`.slice(0, 2))
      const minor = match[1] === '-' ? -magnitude : magnitude
      if (!Number.isSafeInteger(minor)) return null
      return { minor: String(minor), currency: currency(raw && typeof raw === 'object'
        ? raw.currency ?? raw.currencyCode ?? raw.currencyIso : '') }
    }
    const lastFour = value => {
      const digits = text(value).replace(/\D/g, '')
      return digits.length >= 4 ? `•••• ${digits.slice(-4)}` : ''
    }
    const values = (value, depth = 0) => {
      if (Array.isArray(value)) return value.slice(0, 512)
      if (!value || typeof value !== 'object' || depth > 6) return []
      if (value.publicId || value.productId || value.id) return [value]
      const flattened = []
      for (const child of Object.values(value)) {
        flattened.push(...values(child, depth + 1))
        if (flattened.length >= 512) break
      }
      return flattened.slice(0, 512)
    }
    const balanceById = new Map()
    for (const entry of values(state.balances?.balanceResponse)) {
      const data = entry?.data ?? entry
      const id = identifier(data?.publicId ?? data?.productId ?? data?.id)
      const amount = money(data?.balance ?? data?.amount ?? data?.currentBalance)
      if (id && amount?.currency) balanceById.set(id, amount)
    }
    const seenAccounts = new Set()
    const accountAliases = new Map()
    const ambiguousAccountAliases = new Set()
    const accountCurrency = new Map()
    const addAlias = (aliasValue, accountId) => {
      const alias = identifier(aliasValue)
      if (!alias || ambiguousAccountAliases.has(alias)) return
      const existing = accountAliases.get(alias)
      if (existing && existing !== accountId) {
        accountAliases.delete(alias)
        ambiguousAccountAliases.add(alias)
        return
      }
      accountAliases.set(alias, accountId)
    }
    const kind = (item, fallback) => {
      const source = `${text(item?.type)} ${text(item?.productType)} ${text(item?.name)}`.toLowerCase()
      if (/credit.*card|card.*credit|кредитн.*карт/.test(source)) return 'credit-card'
      if (fallback === 'deposit' || /deposit|saving|вклад|сберег/.test(source)) return 'deposit'
      if (fallback === 'loan' || /loan|credit|кредит/.test(source)) return 'loan'
      if (fallback === 'wallet' || /wallet|кошел/.test(source)) return 'wallet'
      return 'checking'
    }
    const addAccount = (item, fallbackKind) => {
      if (!item || typeof item !== 'object' || result.accounts.length >= 256) return
      const id = identifier(item.publicId ?? item.productId ?? item.id ?? item.accountId)
      const productMoney = money(item.balance ?? item.amount ?? item.currentBalance ?? item.availableBalance)
      const amount = balanceById.get(id) ?? productMoney
      const code = amount?.currency || currency(item.currency ?? item.currencyCode)
      if (!id || !amount || !code || seenAccounts.has(id)) return
      let minor = amount.minor
      const productKind = kind(item, fallbackKind)
      if (productKind === 'loan' && !minor.startsWith('-') && minor !== '0') minor = `-${minor}`
      seenAccounts.add(id)
      accountCurrency.set(id, code)
      result.accounts.push({
        id,
        name: text(item.name ?? item.displayName ?? item.title ?? item.productName) || 'Счёт ВТБ',
        kind: productKind,
        currency: code,
        maskedNumber: lastFour(item.maskedNumber ?? item.number ?? item.accountNumber ?? item.cardNumber),
        balanceMinor: minor
      })
      addAlias(id, id)
      addAlias(item.accountId, id)
      addAlias(item.accountNumber, id)
      addAlias(item.number, id)
      for (const card of values(item.cards)) {
        addAlias(card?.publicId, id)
        addAlias(card?.productId, id)
        addAlias(card?.id, id)
        addAlias(card?.number, id)
        addAlias(card?.maskedNumber, id)
        if (!result.accounts[result.accounts.length - 1].maskedNumber) {
          const masked = lastFour(card?.number ?? card?.maskedNumber)
          if (masked) result.accounts[result.accounts.length - 1].maskedNumber = masked
        }
      }
    }
    const portfolio = state.portfolio?.data ?? state.portfolio
    const groups = [
      [portfolio?.cards, 'checking'], [portfolio?.accounts, 'checking'],
      [portfolio?.deposits, 'deposit'], [portfolio?.saving, 'deposit'],
      [portfolio?.loans, 'loan'], [portfolio?.metalAccounts, 'checking'],
      [portfolio?.wallets, 'wallet']
    ]
    for (const [group, fallback] of groups)
      for (const item of values(group)) addAccount(item, fallback)
    if (result.accounts.length === 0) {
      result.stage = 'frontend-changed'
      result.diagnostic = 'VTB-A1'
      return JSON.stringify(result)
    }

    const internalCodes = new Set([3154,3155,3516,3729,3953,4424,4809,6124,
                                    7760,7761,7775,9775,5111])
    const internalCategories = new Set([101,92,103])
    const seenTransactions = new Map()
    const rawOperations = Array.isArray(state.history?.operations) ? state.history.operations : []
    if (rawOperations.length > 20000) projectionIncomplete = true
    const operations = rawOperations.slice(0, 20000)
    for (const operation of operations) {
      if (!operation || typeof operation !== 'object') {
        projectionIncomplete = true
        ++result.rejectedTransactions
        continue
      }
      const accountCandidates = [
        operation.accountProductId, operation.accountId, operation.cardProductId,
        operation.cardId
      ]
      const resolvedAccounts = new Set()
      for (const candidate of accountCandidates) {
        const alias = identifier(candidate)
        if (!alias || ambiguousAccountAliases.has(alias)) continue
        const resolved = accountAliases.get(alias)
        if (resolved) resolvedAccounts.add(resolved)
      }
      const accountId = resolvedAccounts.size === 1 ? [...resolvedAccounts][0] : ''
      const date = new Date(text(operation.operationDate ?? operation.date ?? operation.createdAt))
      const amount = money(operation.transactionAmount ?? operation.operationAmount ?? operation.amount)
      if (!seenAccounts.has(accountId) || Number.isNaN(date.getTime()) || !amount) {
        projectionIncomplete = true
        ++result.rejectedTransactions
        continue
      }
      let signedMinor = amount.minor
      if (operation.debet === true && !signedMinor.startsWith('-')) signedMinor = `-${signedMinor}`
      else if (operation.debet === false && signedMinor.startsWith('-')) signedMinor = signedMinor.slice(1)
      if (signedMinor === '0' || signedMinor === '-0') {
        projectionIncomplete = true
        ++result.rejectedTransactions
        continue
      }
      const id = identifier(operation.operationId ?? operation.internalId ?? operation.id)
      const description = text(operation.fullOperationName ?? operation.operationName ??
                               operation.description ?? operation.title)
      const categoryId = Number(operation.parentCategory?.id ?? operation.category?.id)
      const operationCode = Number(operation.operationCode)
      const internalTransfer = internalCodes.has(operationCode) || internalCategories.has(categoryId)
      const category = internalTransfer ? 'Внутренний перевод' :
        text(operation.parentCategory?.name ?? operation.category?.name ?? operation.category)
      const merchant = text(operation.merchant?.title ?? operation.merchant?.name ??
                            operation.merchant)
      const mcc = text(operation.merchant?.mcc ?? operation.mcc).slice(0, 8)
      const amountCurrency = amount.currency || accountCurrency.get(accountId) || ''
      const fallbackIdentity = [date.toISOString(), signedMinor, description,
                                merchant, category, mcc].join('|')
      const identity = `${accountId}\n${id || fallbackIdentity}`
      const fingerprint = [date.toISOString(), signedMinor, amountCurrency,
                           description, merchant, category, mcc,
                           String(internalTransfer)].join('|')
      const previousFingerprint = seenTransactions.get(identity)
      if (previousFingerprint !== undefined) {
        if (previousFingerprint !== fingerprint) {
          projectionIncomplete = true
          ++result.rejectedTransactions
        }
        continue
      }
      seenTransactions.set(identity, fingerprint)
      result.transactions.push({
        id,
        accountId,
        postingDate: date.toISOString().slice(0, 10),
        occurredAt: date.toISOString(),
        amountMinor: signedMinor,
        currency: amountCurrency,
        description: description || 'Операция ВТБ',
        merchant,
        category,
        mcc,
        internalTransfer
      })
    }
    if (projectionIncomplete) result.diagnostic = 'VTB-H2'
    result.stage = 'ready'
    if (typeof globalThis.TextEncoder === 'function') {
      const encoder = new TextEncoder()
      const allTransactions = result.transactions
      if (encoder.encode(JSON.stringify(result)).byteLength > 8 * 1024 * 1024) {
        let lower = 0
        let upper = allTransactions.length
        while (lower < upper) {
          const middle = Math.ceil((lower + upper) / 2)
          result.transactions = allTransactions.slice(0, middle)
          if (encoder.encode(JSON.stringify(result)).byteLength <= 8 * 1024 * 1024)
            lower = middle
          else upper = middle - 1
        }
        result.transactions = allTransactions.slice(0, lower)
        if (lower < allTransactions.length) result.diagnostic = 'VTB-H2'
      }
    }
  } catch (ignored) {
    result.stage = result.authenticated ? 'frontend-changed' : 'authentication'
    if (result.authenticated) result.diagnostic = 'VTB-X1'
  }
  return JSON.stringify(result)
})())VTB");
    return script;
}

[[nodiscard]] std::expected<VtbFinancialProjection, QString>
parse_vtb_financial_projection(const QString& projection, const QDateTime& observedAt) {
    constexpr qsizetype maximumProjectionCharacters = 8 * 1024 * 1024;
    if (projection.isEmpty() || projection.size() > maximumProjectionCharacters) {
        return std::unexpected(
            QStringLiteral("Встроенная страница ВТБ вернула ответ недопустимого размера"));
    }
    QJsonParseError parseError;
    const auto encoded = projection.toUtf8();
    const auto document = QJsonDocument::fromJson(encoded, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(
            QStringLiteral("Встроенная страница ВТБ вернула некорректный ответ"));
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("version")).toInt() != 1) {
        return std::unexpected(
            QStringLiteral("Версия ответа встроенной страницы ВТБ не поддерживается"));
    }
    const auto stageText = object.value(QStringLiteral("stage")).toString();
    VtbProjectionStage stage{};
    if (stageText == QStringLiteral("authentication")) {
        stage = VtbProjectionStage::Authentication;
    } else if (stageText == QStringLiteral("loading")) {
        stage = VtbProjectionStage::Loading;
    } else if (stageText == QStringLiteral("frontend-changed")) {
        stage = VtbProjectionStage::FrontendChanged;
    } else if (stageText == QStringLiteral("ready")) {
        stage = VtbProjectionStage::Ready;
    } else {
        return std::unexpected(
            QStringLiteral("Этап ответа встроенной страницы ВТБ неизвестен"));
    }
    const bool authenticated = object.value(QStringLiteral("authenticated")).toBool();
    if ((stage == VtbProjectionStage::Authentication && authenticated) ||
        (stage != VtbProjectionStage::Authentication && !authenticated)) {
        return std::unexpected(
            QStringLiteral("Состояние встроенной страницы ВТБ противоречиво"));
    }
    const auto diagnostic = object.value(QStringLiteral("diagnostic")).toString();
    static const QSet<QString> supportedDiagnostics{
        QStringLiteral("VTB-M1"), QStringLiteral("VTB-P1"),
        QStringLiteral("VTB-P2"), QStringLiteral("VTB-P3"),
        QStringLiteral("VTB-H1"), QStringLiteral("VTB-H2"),
        QStringLiteral("VTB-R1"),
        QStringLiteral("VTB-A1"), QStringLiteral("VTB-X1")};
    if (!diagnostic.isEmpty() && !supportedDiagnostics.contains(diagnostic)) {
        return std::unexpected(
            QStringLiteral("Код совместимости встроенной страницы ВТБ неизвестен"));
    }
    if (stage == VtbProjectionStage::Ready && !diagnostic.isEmpty() &&
        diagnostic != QStringLiteral("VTB-H2")) {
        return std::unexpected(
            QStringLiteral("Готовый ответ встроенной страницы ВТБ противоречив"));
    }
    if (stage != VtbProjectionStage::Ready) {
        return VtbFinancialProjection{stage, std::nullopt, diagnostic, 0};
    }
    const auto rejectedValue = object.value(QStringLiteral("rejectedTransactions"));
    const auto rejectedTransactions = rejectedValue.isUndefined() ? 0 : rejectedValue.toInt(-1);
    if (rejectedTransactions < 0 || rejectedTransactions > 20'000) {
        return std::unexpected(
            QStringLiteral("Счётчик отклонённых операций ВТБ некорректен"));
    }
    auto batch = wealthtracker::connectors::VtbRetailClient::parseProjection(encoded, observedAt);
    if (!batch) {
        return std::unexpected(batch.error().message);
    }
    if (diagnostic == QStringLiteral("VTB-H2")) {
        batch->warnings.push_back(
            {wealthtracker::connectors::WarningSeverity::Information,
             QStringLiteral("vtb-history-incomplete"),
             QStringLiteral(
                 "ВТБ вернул только часть двухлетней истории операций"),
             rejectedTransactions});
    }
    return VtbFinancialProjection{VtbProjectionStage::Ready, std::move(*batch), diagnostic,
                                  rejectedTransactions};
}

} // namespace wealthtracker::app
