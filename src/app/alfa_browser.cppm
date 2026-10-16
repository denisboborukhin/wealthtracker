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
    Ready
};

struct AlfaFinancialProjection {
    AlfaProjectionStage stage{AlfaProjectionStage::Authentication};
    std::optional<wealthtracker::connectors::NormalizedBatch> batch;
};

[[nodiscard]] const QString& alfa_financial_projection_script() {
    static const QString script = QStringLiteral(R"ALFA((() => {
  const result = { version: 1, stage: 'authentication', authenticated: false,
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
    const inlineScripts = pageScripts.slice(0, 64)
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
      if (typeof numeric !== 'number' && typeof numeric !== 'string') return null
      const divisor = String(raw && typeof raw === 'object' ? raw.minorUnits ?? 1 : 1)
      if (!/^10{0,9}$/.test(divisor)) return null
      const match = String(numeric).trim().replace(',', '.')
        .match(/^([+-]?)(\d{1,16})(?:\.(\d{1,12}))?$/)
      if (!match) return null
      let digits = match[2] + (match[3] ?? '')
      const scale = 2 - (divisor.length - 1) - (match[3]?.length ?? 0)
      if (scale >= 0) digits += '0'.repeat(scale)
      else {
        if (!digits.endsWith('0'.repeat(-scale))) return null
        digits = digits.slice(0, scale) || '0'
      }
      const minor = Number(digits) * (match[1] === '-' ? -1 : 1)
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
    const identifier = value => text(value).slice(0, 160)
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
    result.stage = 'ready'
    result.authenticated = true
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
        return AlfaFinancialProjection{stage, std::nullopt};
    }
    if (!object.value(QStringLiteral("authenticated")).toBool()) {
        return std::unexpected(
            QStringLiteral("Состояние встроенной страницы Альфа-Банка противоречиво"));
    }
    auto batch = wealthtracker::connectors::AlfaRetailClient::parseProjection(encoded, observedAt);
    if (!batch) {
        return std::unexpected(batch.error().message);
    }
    return AlfaFinancialProjection{AlfaProjectionStage::Ready, std::move(*batch)};
}

} // namespace wealthtracker::app
