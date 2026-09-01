module;

#include <QByteArray>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <expected>
#include <optional>

export module wt.alfa_browser;

import wt.connectors;

export namespace wealthtracker::app {

enum class AlfaProjectionStage {
    Authentication,
    CertificateRequired,
    DashboardLoading,
    OperationsLoading,
    OperationsFailed,
    Ready
};

struct AlfaFinancialProjection {
    AlfaProjectionStage stage{AlfaProjectionStage::Authentication};
    std::optional<wealthtracker::connectors::NormalizedBatch> batch;
    bool historyCaptured = false;
    bool historyComplete = false;
};

[[nodiscard]] const QString& alfa_history_capture_script() {
    static const QString script = QStringLiteral(R"ALFA((() => {
  try {
    const pageUrl = new URL(globalThis.location.href)
    if (pageUrl.protocol !== 'https:' || pageUrl.hostname !== 'web.alfabank.ru' ||
        (pageUrl.port !== '' && pageUrl.port !== '443') ||
        pageUrl.username !== '' || pageUrl.password !== '') return 'wrong-origin'
    const stateKey = '__wealthtrackerAlfaHistory'
    const existing = globalThis[stateKey]
    if (existing && (existing.status === 'waiting' || existing.status === 'ready'))
      return existing.status

    const state = globalThis[stateKey] = {
      status: 'waiting', payload: { responses: [] }, capturedCharacters: 0,
      settleGeneration: 0, complete: true
    }
    const prototype = globalThis.XMLHttpRequest?.prototype
    if (!prototype || typeof prototype.open !== 'function' ||
        typeof prototype.send !== 'function') {
      state.status = 'failed'
      return state.status
    }
    const nativeOpen = prototype.open
    const nativeSend = prototype.send
    const nativeSetRequestHeader = prototype.setRequestHeader
    const endpoint = '/api/v1/operations-history/operations'
    const maximumCapturedCharacters = 8 * 1024 * 1024
    const maximumResponses = 200
    const requestedOperationsPerResponse = 100
    const maximumRequestCharacters = 64 * 1024
    const settleDelayMilliseconds = 1200
    const matchesEndpoint = value => {
      try {
        const url = new URL(String(value ?? ''), pageUrl.origin)
        return url.origin === pageUrl.origin && url.pathname === endpoint
      } catch (ignored) { return false }
    }
    const restore = () => {
      if (prototype.open === wrappedOpen) prototype.open = nativeOpen
      if (prototype.send === wrappedSend) prototype.send = nativeSend
    }
    const normalizedKey = value => String(value).replace(/[-_]/g, '').toLowerCase()
    const setAtPath = (root, path, value) => {
      let target = root
      for (let index = 0; index + 1 < path.length; ++index) target = target[path[index]]
      target[path[path.length - 1]] = value
    }
    const dateText = (sample, value) => {
      const day = String(value.getDate()).padStart(2, '0')
      const month = String(value.getMonth() + 1).padStart(2, '0')
      const year = String(value.getFullYear())
      const source = String(sample ?? '')
      if (/^\d{2}\.\d{2}\.\d{4}$/.test(source)) return `${day}.${month}.${year}`
      if (/^\d{4}-\d{2}-\d{2}$/.test(source)) return `${year}-${month}-${day}`
      if (/^\d{4}-\d{2}-\d{2}T/.test(source)) return value.toISOString()
      if (typeof sample === 'number' && Number.isFinite(sample))
        return sample < 100000000000 ? Math.floor(value.getTime() / 1000) : value.getTime()
      return null
    }
    const prepareRequest = body => {
      const result = { body: body, pageSize: 0, pagePath: null, offsetPath: null,
                       rangeComplete: true }
      if (typeof body !== 'string' || body.length > maximumRequestCharacters) {
        result.rangeComplete = false
        return result
      }
      try {
        const payload = JSON.parse(body)
        if (!payload || typeof payload !== 'object' || Array.isArray(payload)) {
          result.rangeComplete = false
          return result
        }
        const limitKeys = new Set([
          'limit', 'pagesize', 'pagelimit', 'itemsperpage', 'operationslimit',
          'operationsperpage'
        ])
        const containerKeys = new Set([
          'pagination', 'paging', 'pagerequest', 'pageable'
        ])
        const pageKeys = new Set(['page', 'pagenumber', 'pageindex'])
        const offsetKeys = new Set(['offset', 'skip', 'startindex'])
        const fromKeys = new Set(['datefrom', 'fromdate', 'startdate', 'periodfrom', 'from'])
        const toKeys = new Set(['dateto', 'todate', 'enddate', 'periodto', 'to'])
        const now = new Date()
        const rangeTo = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 23, 59, 59, 999)
        const rangeFrom = new Date(rangeTo)
        rangeFrom.setFullYear(rangeFrom.getFullYear() - 2)
        rangeFrom.setHours(0, 0, 0, 0)
        let sawFrom = false
        let sawTo = false
        const expand = (value, path, allowSize, depth) => {
          if (!value || typeof value !== 'object' || Array.isArray(value) || depth > 4) return
          for (const [key, current] of Object.entries(value)) {
            const normalized = normalizedKey(key)
            const isLimit = limitKeys.has(normalized) || (allowSize && normalized === 'size')
            const numeric = Number(current)
            const currentPath = path.concat([key])
            if (isLimit && Number.isInteger(numeric) && numeric > 0) {
              value[key] = typeof current === 'string'
                ? String(requestedOperationsPerResponse) : requestedOperationsPerResponse
              result.pageSize = requestedOperationsPerResponse
            } else if (!result.pagePath && pageKeys.has(normalized) &&
                       Number.isInteger(numeric) && numeric >= 0) {
              result.pagePath = currentPath
            } else if (!result.offsetPath && offsetKeys.has(normalized) &&
                       Number.isInteger(numeric) && numeric >= 0) {
              result.offsetPath = currentPath
            } else if (fromKeys.has(normalized)) {
              const replacement = dateText(current, rangeFrom)
              if (replacement !== null) {
                value[key] = replacement
                sawFrom = true
              }
            } else if (toKeys.has(normalized)) {
              const replacement = dateText(current, rangeTo)
              if (replacement !== null) {
                value[key] = replacement
                sawTo = true
              }
            } else if (current && typeof current === 'object') {
              expand(current, currentPath, containerKeys.has(normalized), depth + 1)
            }
          }
        }
        expand(payload, [], true, 0)
        if (!sawFrom || !sawTo) result.rangeComplete = false
        if (result.pagePath) {
          const current = result.pagePath.reduce((value, key) => value[key], payload)
          setAtPath(payload, result.pagePath, typeof current === 'string'
            ? (Number(current) === 0 ? '0' : '1') : (Number(current) === 0 ? 0 : 1))
        }
        if (result.offsetPath) {
          const current = result.offsetPath.reduce((value, key) => value[key], payload)
          setAtPath(payload, result.offsetPath, typeof current === 'string' ? '0' : 0)
        }
        const expanded = JSON.stringify(payload)
        if (expanded.length > maximumRequestCharacters) {
          result.rangeComplete = false
          return result
        }
        result.body = expanded
        return result
      } catch (ignored) {
        result.rangeComplete = false
        return result
      }
    }
    const continuationHint = payload => {
      const candidates = [payload, payload?.data, payload?.pagination,
                          payload?.data?.pagination, payload?.page, payload?.data?.page]
      for (const candidate of candidates) {
        if (!candidate || typeof candidate !== 'object') continue
        for (const key of ['hasNext', 'hasMore', 'more'])
          if (typeof candidate[key] === 'boolean') return candidate[key]
        if (typeof candidate.last === 'boolean') return !candidate.last
      }
      return null
    }
    const nextRequest = (request, payload, operationCount) => {
      const hint = continuationHint(payload)
      if (hint === false || (hint === null && request.pageSize > 0 &&
                             operationCount < request.pageSize)) return null
      if (hint !== true && (request.pageSize <= 0 || operationCount === 0)) {
        state.complete = false
        return null
      }
      if (!request.pagePath && !request.offsetPath) {
        state.complete = false
        return null
      }
      const next = prepareRequest(request.body)
      const payloadCopy = JSON.parse(next.body)
      if (request.pagePath) {
        const current = request.pagePath.reduce((value, key) => value[key],
                                                JSON.parse(request.body))
        setAtPath(payloadCopy, request.pagePath,
                  typeof current === 'string' ? String(Number(current) + 1)
                                              : Number(current) + 1)
      } else {
        const current = request.offsetPath.reduce((value, key) => value[key],
                                                  JSON.parse(request.body))
        setAtPath(payloadCopy, request.offsetPath,
                  typeof current === 'string'
                    ? String(Number(current) + request.pageSize)
                    : Number(current) + request.pageSize)
      }
      next.body = JSON.stringify(payloadCopy)
      next.pagePath = request.pagePath
      next.offsetPath = request.offsetPath
      next.pageSize = request.pageSize
      next.rangeComplete = request.rangeComplete
      return next
    }
    const scheduleSettlement = () => {
      const generation = ++state.settleGeneration
      setTimeout(() => {
        if (state.status !== 'waiting' || generation !== state.settleGeneration) return
        state.status = 'ready'
        restore()
      }, settleDelayMilliseconds)
    }
    const captureResponse = (request, requestState) => {
      if (state.status !== 'waiting') return
      if (request.status < 200 || request.status >= 300 ||
          typeof request.responseText !== 'string') {
        state.complete = false
        if (state.payload.responses.length > 0) scheduleSettlement()
        else {
          state.status = 'failed'
          restore()
        }
        return
      }
      if (state.capturedCharacters + request.responseText.length >
            maximumCapturedCharacters ||
          state.payload.responses.length >= maximumResponses) {
        state.complete = false
        scheduleSettlement()
        return
      }
      try {
        const payload = JSON.parse(request.responseText)
        const operations = payload?.operations ?? payload?.data?.operations
        if (!payload || typeof payload !== 'object' || !Array.isArray(operations))
          throw new Error('invalid response')
        state.capturedCharacters += request.responseText.length
        state.payload.responses.push(payload)
        state.complete = state.complete && requestState.rangeComplete
        const next = nextRequest(requestState, payload, operations.length)
        if (!next) {
          scheduleSettlement()
          return
        }
        if (state.payload.responses.length >= maximumResponses) {
          state.complete = false
          scheduleSettlement()
          return
        }
        const continuation = new XMLHttpRequest()
        nativeOpen.call(continuation, 'POST', endpoint, true)
        if (typeof nativeSetRequestHeader === 'function')
          nativeSetRequestHeader.call(continuation, 'Content-Type', 'application/json')
        continuation.addEventListener(
          'loadend', () => captureResponse(continuation, next), { once: true })
        nativeSend.call(continuation, next.body)
      } catch (ignored) {
        state.complete = false
        if (state.payload.responses.length > 0) scheduleSettlement()
        else {
          state.status = 'failed'
          restore()
        }
      }
    }
    function wrappedOpen(method, url, ...rest) {
      this.__wealthtrackerAlfaHistoryRequest =
        String(method ?? '').toUpperCase() === 'POST' && matchesEndpoint(url)
      return nativeOpen.call(this, method, url, ...rest)
    }
    function wrappedSend(body) {
      if (this.__wealthtrackerAlfaHistoryRequest) {
        const requestState = prepareRequest(body)
        this.addEventListener('loadend', () => captureResponse(this, requestState),
                              { once: true })
        return nativeSend.call(this, requestState.body)
      }
      return nativeSend.call(this, body)
    }
    prototype.open = wrappedOpen
    prototype.send = wrappedSend
    globalThis.history.pushState({}, '', '/history')
    globalThis.dispatchEvent(new PopStateEvent('popstate'))
    return state.status
  } catch (ignored) {
    if (globalThis.__wealthtrackerAlfaHistory)
      globalThis.__wealthtrackerAlfaHistory.status = 'failed'
    return 'failed'
  }
})())ALFA");
    return script;
}

[[nodiscard]] const QString& alfa_financial_projection_script() {
    static const QString script = QStringLiteral(R"ALFA((() => {
  const result = { version: 1, stage: 'authentication', authenticated: false,
                   historyCaptured: false, historyComplete: false,
                   accounts: [], transactions: [] }
  try {
    const pageUrl = new URL(globalThis.location.href)
    if (pageUrl.protocol !== 'https:' || pageUrl.hostname !== 'web.alfabank.ru' ||
        (pageUrl.port !== '' && pageUrl.port !== '443') ||
        pageUrl.username !== '' || pageUrl.password !== '') return JSON.stringify(result)

    const pageLinks = globalThis.document?.links
      ? Array.from(globalThis.document.links).slice(0, 8) : []
    const certificateLink = pageLinks.some(element => {
      try {
        const url = new URL(String(element.href ?? ''), pageUrl.origin)
        return url.protocol === 'https:' && url.hostname === 'alfa.bank' &&
               (url.port === '' || url.port === '443') &&
               (url.pathname === '/cert' || url.pathname === '/cert/') &&
               url.username === '' && url.password === ''
      } catch (ignored) { return false }
    })
    const pageScripts = globalThis.document?.scripts
      ? Array.from(globalThis.document.scripts) : []
    const pageForms = globalThis.document?.forms?.length ?? 0
    if (pageUrl.pathname === '/' && certificateLink && pageScripts.length === 0 &&
        pageForms === 0) {
      result.stage = 'certificate-required'
      return JSON.stringify(result)
    }

    result.stage = 'dashboard-loading'
    const roots = []
    const seenRoots = new Set()
    const addRoot = value => {
      if (!value || typeof value !== 'object' || seenRoots.has(value)) return
      seenRoots.add(value)
      roots.push(value)
    }
    addRoot(globalThis.initialState)
    addRoot(globalThis.__INITIAL_STATE__)
    addRoot(globalThis.__PRELOADED_STATE__)
    addRoot(globalThis.__NEXT_DATA__)
    const historyState = globalThis.__wealthtrackerAlfaHistory

    const parseObjectAt = (source, start) => {
      if (start < 0 || source[start] !== '{') return null
      let depth = 0
      let quoted = false
      let escaped = false
      const limit = Math.min(source.length, start + 8 * 1024 * 1024)
      for (let index = start; index < limit; ++index) {
        const character = source[index]
        if (quoted) {
          if (escaped) escaped = false
          else if (character === '\\') escaped = true
          else if (character === '"') quoted = false
          continue
        }
        if (character === '"') quoted = true
        else if (character === '{') ++depth
        else if (character === '}' && --depth === 0) {
          try { return JSON.parse(source.slice(start, index + 1)) } catch (ignored) { return null }
        }
      }
      return null
    }
    const parseAfter = (source, marker) => {
      const markerIndex = source.indexOf(marker)
      if (markerIndex < 0) return null
      return parseObjectAt(source, source.indexOf('{', markerIndex + marker.length))
    }
    let scannedCharacters = 0
    const inlineScripts = globalThis.document?.scripts
      ? Array.from(globalThis.document.scripts).slice(0, 64) : []
    for (const element of inlineScripts) {
      if (element.src) continue
      const source = String(element.textContent ?? '')
      scannedCharacters += source.length
      if (source.length > 8 * 1024 * 1024 || scannedCharacters > 16 * 1024 * 1024) break
      const runApp = parseAfter(source, 'window.runApp(')
      addRoot(runApp?.appState)
      addRoot(parseAfter(source, 'window.initialState ='))
      addRoot(parseAfter(source, 'window.initialState='))
      addRoot(parseAfter(source, 'globalThis.initialState ='))
      addRoot(parseAfter(source, 'globalThis.initialState='))
    }
    addRoot(historyState?.payload)
    if (roots.length === 0) return JSON.stringify(result)

    const text = value => {
      if (value === null || value === undefined) return ''
      return String(value).replace(/[\r\n\t]/g, ' ').trim().slice(0, 256)
    }
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
        ? raw.value ?? raw.sum ?? raw.amount ?? raw.balance : raw
      const divisor = raw && typeof raw === 'object' ? Number(raw.minorUnits ?? 1) : 1
      const number = Number(String(numeric ?? '').replace(',', '.'))
      if (!Number.isFinite(number) || !Number.isFinite(divisor) || divisor <= 0) return null
      const minor = Math.round(number * 100 / divisor)
      if (!Number.isSafeInteger(minor)) return null
      const code = currency(raw && typeof raw === 'object'
        ? raw.currency ?? raw.currencyCode ?? raw.currencyIso : '')
      return { minor: String(minor), currency: code }
    }
    const lastFour = value => {
      const digits = text(value).replace(/\D/g, '')
      return digits.length >= 4 ? `•••• ${digits.slice(-4)}` : ''
    }
    const accountType = (value, fallback, hasCard) => {
      const kind = text(value).toLowerCase()
      if (/credit.*card|card.*credit|кредитн.*карт/.test(kind) ||
          (hasCard && /credit|кредит/.test(kind))) return 'credit-card'
      if (/deposit|вклад/.test(kind)) return 'deposit'
      if (fallback === 'loan' || /loan|credit|кредит/.test(kind)) return 'loan'
      if (/wallet|кошел/.test(kind)) return 'wallet'
      return 'checking'
    }
    const seenAccounts = new Set()
    const accountAliases = new Map()
    const identifier = value => text(value).slice(0, 160)
    const registerAlias = (value, accountId) => {
      const alias = identifier(value)
      if (!alias) return
      if (!accountAliases.has(alias)) accountAliases.set(alias, accountId)
      else if (accountAliases.get(alias) !== accountId) accountAliases.set(alias, '')
      const digits = alias.replace(/\D/g, '')
      if (digits.length >= 4) {
        const suffix = digits.slice(-4)
        if (!accountAliases.has(suffix)) accountAliases.set(suffix, accountId)
        else if (accountAliases.get(suffix) !== accountId) accountAliases.set(suffix, '')
      }
    }
    const registerRouteAliases = (value, accountId) => {
      try {
        const url = new URL(String(value ?? ''), pageUrl.origin)
        if (url.origin !== pageUrl.origin) return
        for (const segment of url.pathname.split('/'))
          if (/^[A-Za-z0-9_-]{6,160}$/.test(segment)) registerAlias(segment, accountId)
      } catch (ignored) {}
    }
    const addAccount = (item, fallbackKind) => {
      if (!item || typeof item !== 'object' || result.accounts.length >= 256) return
      const card = Array.isArray(item.cards) && item.cards.length > 0 ? item.cards[0] : null
      const id = text(item.id ?? item.accountId ?? item.productId ?? item.publicId ??
                      item.number ?? item.accountNumber)
      const amount = money(item.amount ?? item.balance ?? item.moneyAmount ??
                           item.accountBalance ?? item.currentBalance)
      const code = amount?.currency || currency(item.currency ?? item.currencyCode)
      if (!id || !amount || !code || seenAccounts.has(id)) return
      seenAccounts.add(id)
      result.accounts.push({
        id,
        name: text(item.displayName ?? item.name ?? item.title ?? item.productName ?? card?.name) ||
              'Счёт Альфа-Банка',
        kind: accountType(item.type ?? item.kind ?? item.productType,
                          fallbackKind, card !== null),
        currency: code,
        maskedNumber: lastFour(item.maskedNumber ?? item.number ?? item.accountNumber ?? card?.number),
        balanceMinor: amount.minor
      })
      registerAlias(id, id)
      registerAlias(item.accountId ?? item.number ?? item.accountNumber, id)
      registerAlias(item.productId ?? item.publicId, id)
      registerAlias(item.maskedNumber ?? card?.number, id)
      if (Array.isArray(item.cards))
        for (const child of item.cards) {
          registerAlias(child?.id ?? child?.cardId ?? child?.productId, id)
          registerAlias(child?.number ?? child?.maskedNumber, id)
        }
    }
    const lineItems = line => Array.isArray(line?.items) ? line.items : []
    const lineText = line => {
      for (const item of lineItems(line)) {
        const value = item?.content?.value
        if (typeof value === 'string' && text(value)) return text(value)
      }
      return ''
    }
    const lineMoney = line => {
      for (const item of lineItems(line)) {
        const value = money(item?.content?.value)
        if (value?.currency) return value
      }
      return null
    }
    const addDashboardProduct = item => {
      if (!item || typeof item !== 'object' || result.accounts.length >= 256) return
      const main = item.content?.main
      const amount = lineMoney(main?.thirdLine) ?? lineMoney(main?.secondLine) ??
                     lineMoney(main?.firstLine)
      const id = identifier(item.id)
      if (!id || !amount?.currency || seenAccounts.has(id)) return
      const masked = item.content?.rightAddon?.content?.primary?.maskedNumber ??
                     item.content?.rightAddon?.content?.secondary?.maskedNumber
      seenAccounts.add(id)
      result.accounts.push({
        id,
        name: lineText(main?.firstLine) || lineText(main?.secondLine) ||
              'Счёт Альфа-Банка',
        kind: accountType(`${text(item.sectionId)} ${text(item.type)}`, 'checking',
                          Boolean(masked)),
        currency: amount.currency,
        maskedNumber: lastFour(masked),
        balanceMinor: amount.minor
      })
      registerAlias(id, id)
      registerAlias(item.properties?.action?.id, id)
      registerAlias(item.properties?.action?.productId, id)
      registerAlias(masked, id)
      registerRouteAliases(item.content?.deeplink, id)
    }
    for (const root of roots) {
      const layout = root.layoutData ?? root.data?.layoutData ?? root.props?.pageProps?.layoutData
      if (!layout || typeof layout !== 'object') continue
      const groups = [
        [layout.accountsWithCards, 'checking'], [layout.accounts, 'checking'],
        [layout.accountsWithoutCards, 'checking'], [layout.capitalAccounts, 'checking'],
        [layout.savingsAccounts, 'checking'],
        [layout.deposits, 'deposit'], [layout.credits, 'loan'], [layout.loans, 'loan'],
        [layout.products?.accounts, 'checking'], [layout.products?.deposits, 'deposit'],
        [layout.products?.credits, 'loan']
      ]
      for (const [group, kind] of groups)
        if (Array.isArray(group)) for (const item of group) addAccount(item, kind)
      if (Array.isArray(layout.allProducts))
        for (const item of layout.allProducts) addDashboardProduct(item)
    }
    if (result.accounts.length === 0) return JSON.stringify(result)
    if (historyState?.status === 'waiting') {
      result.stage = 'operations-loading'
      return JSON.stringify(result)
    }
    if (historyState?.status === 'failed') {
      result.stage = 'operations-failed'
      return JSON.stringify(result)
    }

    const operationKey = /(operation|transaction|history|statement|recent|activity)/i
    const dateValue = value => {
      if (value && typeof value === 'object')
        value = value.milliseconds ?? value.epochMillis ?? value.timestamp ?? value.value
      if (typeof value === 'number' || /^\d{10,13}$/.test(text(value))) {
        const number = Number(value)
        const date = new Date(number < 100000000000 ? number * 1000 : number)
        return Number.isNaN(date.getTime()) ? null : date
      }
      const date = new Date(text(value))
      return Number.isNaN(date.getTime()) ? null : date
    }
    const seenTransactions = new Set()
    const addTransaction = item => {
      if (!item || typeof item !== 'object' || result.transactions.length >= 20000) return
      const amount = money(item.accountAmount ?? item.operationAmount ?? item.amount ??
                           item.moneyAmount ?? item.transactionAmount ??
                           item.amountInAccountCurrency ?? item.sum)
      const date = dateValue(item.operationTime ?? item.operationDate ??
                             item.transactionDate ?? item.debitingTime ?? item.occurredAt ??
                             item.authorizationDate ?? item.postingDate ?? item.date ??
                             item.dateTime ?? item.createdAt)
      if (!amount || !date) return
      const candidates = [item.accountId, item.accountExternalId, item.accountProductId,
                          item.accountNumber, item.productId, item.cardId, item.cardNumber,
                          item.account?.id, item.account?.productId, item.account?.number,
                          item.product?.id, item.card?.id, item.card?.number,
                          item.sender?.accountNumber, item.sender?.maskedCardNumber,
                          item.clickReference]
      if (Array.isArray(item.actions))
        for (const action of item.actions)
          candidates.push(action?.accountNumber, action?.referenceId)
      let accountId = ''
      for (const candidate of candidates) {
        const alias = identifier(candidate)
        const digits = alias.replace(/\D/g, '')
        const resolved = accountAliases.get(alias) ||
                         (digits.length >= 4 ? accountAliases.get(digits.slice(-4)) : '')
        if (resolved) { accountId = resolved; break }
      }
      if (!accountId && result.accounts.length === 1) accountId = result.accounts[0].id
      if (!seenAccounts.has(accountId)) return
      const description = text(item.description ?? item.title ?? item.comment ?? item.message ??
                               item.purpose ?? item.operationName)
      const merchant = text(item.merchant?.name ?? item.merchant ?? item.brand?.name ??
                            item.terminal?.name)
      const category = item.cashout === true ? 'Наличные' :
        text(item.category?.name ?? item.category ?? item.spendingCategory?.name ??
             item.operationCategory?.name)
      const id = text(item.id ?? item.operationId ?? item.transactionId ?? item.reference)
      const direction = text(item.direction ?? item.operationDirection ??
                             item.operationType ?? item.type).toLowerCase()
      let signedMinor = amount.minor
      if ((item.cashout === true || item.isDebit === true || item.debet === true ||
           /debit|expense|outcome|outgoing|spend|purchase|payment|расход|покуп|оплат/.test(direction)) &&
          !signedMinor.startsWith('-')) signedMinor = `-${signedMinor}`
      else if ((item.isDebit === false || item.debet === false ||
                /credit|income|incoming|top.?up|deposit|доход|пополн/.test(direction)) &&
               signedMinor.startsWith('-')) signedMinor = signedMinor.slice(1)
      const identity = `${accountId}\n${id || `${date.toISOString()}|${signedMinor}|${description}`}`
      if (seenTransactions.has(identity)) return
      seenTransactions.add(identity)
      const transferText = `${description} ${category} ${text(item.type)} ${direction}`.toLowerCase()
      result.transactions.push({
        id,
        accountId,
        postingDate: date.toISOString().slice(0, 10),
        occurredAt: date.toISOString(),
        amountMinor: signedMinor,
        currency: amount.currency || result.accounts.find(account => account.id === accountId)?.currency || '',
        description,
        merchant,
        category,
        mcc: text(item.mcc ?? item.merchantCategoryCode).slice(0, 8),
        internalTransfer: item.isInner === true || item.internal === true ||
          item.isInternalTransfer === true || /между сво(ими|их) счет|внутренн.*перевод/.test(transferText)
      })
    }
    let visited = 0
    const visit = (value, key, depth) => {
      if (!value || typeof value !== 'object' || depth > 8 || visited++ > 50000 ||
          result.transactions.length >= 20000) return
      if (Array.isArray(value)) {
        if (operationKey.test(key)) for (const item of value) addTransaction(item)
        for (const item of value) visit(item, key, depth + 1)
        return
      }
      for (const [childKey, child] of Object.entries(value))
        visit(child, childKey, depth + 1)
    }
    for (const root of roots) visit(root, '', 0)
    result.stage = 'ready'
    result.authenticated = true
    result.historyCaptured = historyState?.status === 'ready'
    result.historyComplete = result.historyCaptured && historyState?.complete === true
    if (result.historyCaptured) {
      delete historyState.payload
      historyState.status = 'consumed'
    }
  } catch (ignored) {}
  return JSON.stringify(result)
})())ALFA");
    return script;
}

[[nodiscard]] std::expected<AlfaFinancialProjection, QString>
parse_alfa_financial_projection(const QString& projection, const QDateTime& observedAt) {
    constexpr qsizetype maximumProjectionCharacters = 8 * 1024 * 1024;
    if (projection.isEmpty() || projection.size() > maximumProjectionCharacters) {
        return std::unexpected(
            QStringLiteral("Встроенная страница Альфа-Банка вернула ответ недопустимого размера"));
    }
    QJsonParseError parseError;
    const auto encoded = projection.toUtf8();
    const auto document = QJsonDocument::fromJson(encoded, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(
            QStringLiteral("Встроенная страница Альфа-Банка вернула некорректный ответ"));
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("version")).toInt() != 1) {
        return std::unexpected(
            QStringLiteral("Версия ответа встроенной страницы Альфа-Банка не поддерживается"));
    }
    const auto stageText = object.value(QStringLiteral("stage")).toString();
    AlfaProjectionStage stage{};
    if (stageText == QStringLiteral("authentication")) {
        stage = AlfaProjectionStage::Authentication;
    } else if (stageText == QStringLiteral("certificate-required")) {
        stage = AlfaProjectionStage::CertificateRequired;
    } else if (stageText == QStringLiteral("dashboard-loading")) {
        stage = AlfaProjectionStage::DashboardLoading;
    } else if (stageText == QStringLiteral("operations-loading")) {
        stage = AlfaProjectionStage::OperationsLoading;
    } else if (stageText == QStringLiteral("operations-failed")) {
        stage = AlfaProjectionStage::OperationsFailed;
    } else if (stageText == QStringLiteral("ready")) {
        stage = AlfaProjectionStage::Ready;
    } else {
        return std::unexpected(
            QStringLiteral("Этап ответа встроенной страницы Альфа-Банка неизвестен"));
    }
    if (stage != AlfaProjectionStage::Ready) {
        if (object.value(QStringLiteral("authenticated")).toBool()) {
            return std::unexpected(
                QStringLiteral("Состояние встроенной страницы Альфа-Банка противоречиво"));
        }
        return AlfaFinancialProjection{stage, std::nullopt, false, false};
    }
    if (!object.value(QStringLiteral("authenticated")).toBool()) {
        return std::unexpected(
            QStringLiteral("Состояние встроенной страницы Альфа-Банка противоречиво"));
    }
    auto batch = wealthtracker::connectors::AlfaRetailClient::parseProjection(encoded, observedAt);
    if (!batch) {
        return std::unexpected(batch.error().message);
    }
    const bool historyCaptured = object.value(QStringLiteral("historyCaptured")).toBool();
    const bool historyComplete = object.value(QStringLiteral("historyComplete")).toBool();
    if (historyCaptured && !historyComplete) {
        batch->warnings.push_back(
            {wealthtracker::connectors::WarningSeverity::Information,
             QStringLiteral("alfa-history-incomplete"),
             QStringLiteral(
                 "Альфа-Банк вернул только часть двухлетней истории операций"),
             -1});
    }
    return AlfaFinancialProjection{AlfaProjectionStage::Ready, std::move(*batch),
                                   historyCaptured, historyComplete};
}

} // namespace wealthtracker::app
