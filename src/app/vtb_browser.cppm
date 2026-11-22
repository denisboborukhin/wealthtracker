module;

// Keep Qt template declarations in the global module on NDK Clang.
extern "C++" {

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QString>

#include <expected>
#include <optional>

}

export module wt.vtb_browser;

import wt.connectors;

export namespace wealthtracker::app {

enum class VtbProjectionStage { Authentication, Loading, FrontendChanged, Ready };

struct VtbFinancialProjection {
    VtbProjectionStage stage{VtbProjectionStage::Authentication};
    std::optional<wealthtracker::connectors::NormalizedBatch> batch;
    QString diagnostic;
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
        // The loaded core exports the same boolean auth proxy used by its UI.
        // Do not initialize factories or inspect the proxy's token properties.
        let runtime = globalThis.__wealthtrackerVtbAuthRuntime
        if (typeof runtime !== 'function') {
          const chunks = globalThis.webpackChunkmf_1488_core
          if (Array.isArray(chunks)) {
            const entry = [['wealthtracker-vtb-auth-reader'], {}, require => { runtime = require }]
            try { chunks.push(entry) } finally {
              const index = chunks.indexOf(entry)
              if (index >= 0) chunks.splice(index, 1)
            }
            if (typeof runtime === 'function') globalThis.__wealthtrackerVtbAuthRuntime = runtime
          }
        }
        let awaitingAuthentication = false
        try { awaitingAuthentication = runtime?.c?.[11740]?.exports?.y1?.isAuthorized === false }
        catch (ignored) { throw { kind: 'not-ready' } }
        if (awaitingAuthentication) throw { kind: 'awaiting-authentication' }
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
        state.portfolio = portfolio
        state.balances = balances
        state.reason = ''
        state.status = 'ready'
      })
      .catch(error => {
        delete state.portfolio
        delete state.balances
        const status = httpStatus(error)
        if (status === 401 || error?.kind === 'awaiting-authentication') {
          state.status = 'authentication'
          state.reason = error?.kind === 'awaiting-authentication' ? '' :
            state.requestStage === 'balances' ? 'VTB-AUTH-B' : 'VTB-AUTH-P'
          state.retryAt = Date.now() + 1000
        } else if (error?.kind === 'not-ready') {
          state.status = 'waiting'
          state.retryAt = Date.now() + 500
        } else {
          state.status = 'failed'
          const stageCode = state.requestStage === 'module' ? 'VTB-M1' :
            state.requestStage === 'portfolio' ? 'VTB-P3' :
            'VTB-R1'
          state.reason = status === 403 ? 'VTB-DENIED' :
            error?.kind === 'frontend' ? error.code : stageCode
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
                   diagnostic: '',
                   accounts: [] }
  try {
    const pageUrl = new URL(globalThis.location.href)
    if (pageUrl.protocol !== 'https:' || pageUrl.hostname !== 'online.vtb.ru' ||
        (pageUrl.port !== '' && pageUrl.port !== '443') ||
        pageUrl.username !== '' || pageUrl.password !== '') return JSON.stringify(result)
    const state = globalThis.__wealthtrackerVtbSync
    if (!state) return JSON.stringify(result)
    if (state.status === 'authentication') {
      result.diagnostic = ['VTB-AUTH-P', 'VTB-AUTH-B'].includes(state.reason) ? state.reason : ''
      return JSON.stringify(result)
    }
    result.stage = 'loading'
    if (state.status === 'running' || state.status === 'waiting') return JSON.stringify(result)
    if (state.status !== 'ready') {
      result.stage = 'frontend-changed'
      result.diagnostic = ['VTB-M1', 'VTB-P1', 'VTB-P2', 'VTB-P3',
                           'VTB-R1', 'VTB-DENIED']
        .includes(state.reason) ? state.reason : 'VTB-X1'
      return JSON.stringify(result)
    }

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
      result.accounts.push({
        id,
        name: text(item.name ?? item.displayName ?? item.title ?? item.productName) || 'Счёт ВТБ',
        kind: productKind,
        currency: code,
        maskedNumber: lastFour(item.maskedNumber ?? item.number ?? item.accountNumber ?? item.cardNumber),
        balanceMinor: minor
      })
      for (const card of values(item.cards)) {
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

    result.authenticated = true
    result.stage = 'ready'
  } catch (ignored) {
    if (result.stage !== 'authentication') {
      result.stage = 'frontend-changed'
      result.diagnostic = 'VTB-X1'
    }
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
    if (encoded.size() > maximumProjectionCharacters) {
        return std::unexpected(
            QStringLiteral("Размер ответа встроенной страницы превышает безопасный лимит"));
    }
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
    if (authenticated != (stage == VtbProjectionStage::Ready)) {
        return std::unexpected(
            QStringLiteral("Состояние встроенной страницы ВТБ противоречиво"));
    }
    const auto diagnostic = object.value(QStringLiteral("diagnostic")).toString();
    static const QSet<QString> supportedDiagnostics{
        QStringLiteral("VTB-M1"), QStringLiteral("VTB-P1"),
        QStringLiteral("VTB-P2"), QStringLiteral("VTB-P3"),
        QStringLiteral("VTB-R1"),
        QStringLiteral("VTB-A1"), QStringLiteral("VTB-X1"),
        QStringLiteral("VTB-DENIED"), QStringLiteral("VTB-AUTH-P"),
        QStringLiteral("VTB-AUTH-B")};
    if (!diagnostic.isEmpty() && !supportedDiagnostics.contains(diagnostic)) {
        return std::unexpected(
            QStringLiteral("Код совместимости встроенной страницы ВТБ неизвестен"));
    }
    if (stage == VtbProjectionStage::Ready && !diagnostic.isEmpty()) {
        return std::unexpected(
            QStringLiteral("Готовый ответ встроенной страницы ВТБ противоречив"));
    }
    if (stage != VtbProjectionStage::Ready) {
        return VtbFinancialProjection{stage, std::nullopt, diagnostic};
    }
    auto batch = wealthtracker::connectors::parseRetailAccounts(
        object.value(QStringLiteral("accounts")).toArray(), observedAt,
        wealthtracker::connectors::RetailBank::Vtb);
    if (!batch) {
        return std::unexpected(batch.error().message);
    }
    return VtbFinancialProjection{VtbProjectionStage::Ready, std::move(*batch), diagnostic};
}

} // namespace wealthtracker::app
