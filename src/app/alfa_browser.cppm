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

struct AlfaFinancialProjection {
    bool authenticated = false;
    std::optional<wealthtracker::connectors::NormalizedBatch> batch;
};

[[nodiscard]] const QString& alfa_financial_projection_script() {
    static const QString script = QStringLiteral(R"ALFA((() => {
  const result = { version: 1, authenticated: false, accounts: [], transactions: [] }
  try {
    const pageUrl = new URL(globalThis.location.href)
    if (pageUrl.protocol !== 'https:' || pageUrl.hostname !== 'web.alfabank.ru' ||
        (pageUrl.port !== '' && pageUrl.port !== '443') ||
        pageUrl.username !== '' || pageUrl.password !== '') return JSON.stringify(result)

    const roots = [globalThis.initialState, globalThis.__INITIAL_STATE__,
                   globalThis.__PRELOADED_STATE__, globalThis.__NEXT_DATA__]
      .filter(value => value && typeof value === 'object')
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
        ? raw.value ?? raw.amount ?? raw.balance : raw
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
    const addAccount = (item, fallbackKind) => {
      if (!item || typeof item !== 'object' || result.accounts.length >= 256) return
      const card = Array.isArray(item.cards) && item.cards.length > 0 ? item.cards[0] : null
      const id = text(item.id ?? item.accountId ?? item.number ?? item.accountNumber)
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
    for (const root of roots) {
      const layout = root.layoutData ?? root.data?.layoutData ?? root.props?.pageProps?.layoutData
      if (!layout || typeof layout !== 'object') continue
      const groups = [
        [layout.accountsWithCards, 'checking'], [layout.accounts, 'checking'],
        [layout.accountsWithoutCards, 'checking'], [layout.savingsAccounts, 'checking'],
        [layout.deposits, 'deposit'], [layout.credits, 'loan'], [layout.loans, 'loan'],
        [layout.products?.accounts, 'checking'], [layout.products?.deposits, 'deposit'],
        [layout.products?.credits, 'loan']
      ]
      for (const [group, kind] of groups)
        if (Array.isArray(group)) for (const item of group) addAccount(item, kind)
    }
    if (result.accounts.length === 0) return JSON.stringify(result)

    const operationKey = /(operation|transaction|history|statement|recent|activity)/i
    const dateValue = value => {
      if (value && typeof value === 'object') value = value.milliseconds ?? value.value
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
      const amount = money(item.accountAmount ?? item.amount ?? item.moneyAmount ??
                           item.transactionAmount ?? item.sum)
      const date = dateValue(item.operationTime ?? item.debitingTime ?? item.occurredAt ??
                             item.postingDate ?? item.date ?? item.createdAt)
      if (!amount || !date) return
      let accountId = text(item.accountId ?? item.accountExternalId ?? item.accountNumber ??
                           item.account?.id ?? item.account?.number)
      if (!accountId && result.accounts.length === 1) accountId = result.accounts[0].id
      if (!seenAccounts.has(accountId)) return
      const description = text(item.description ?? item.title ?? item.message ??
                               item.purpose ?? item.operationName)
      const merchant = text(item.merchant?.name ?? item.merchant ?? item.brand?.name)
      const category = text(item.category?.name ?? item.category ??
                            item.spendingCategory?.name ?? item.operationCategory?.name)
      const id = text(item.id ?? item.operationId ?? item.transactionId ?? item.reference)
      const identity = `${accountId}\n${id || `${date.toISOString()}|${amount.minor}|${description}`}`
      if (seenTransactions.has(identity)) return
      seenTransactions.add(identity)
      const transferText = `${description} ${category} ${text(item.type)}`.toLowerCase()
      result.transactions.push({
        id,
        accountId,
        postingDate: date.toISOString().slice(0, 10),
        occurredAt: date.toISOString(),
        amountMinor: amount.minor,
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
    result.authenticated = true
  } catch {}
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
    if (!object.value(QStringLiteral("authenticated")).toBool()) {
        return AlfaFinancialProjection{};
    }
    auto batch = wealthtracker::connectors::AlfaRetailClient::parseProjection(encoded, observedAt);
    if (!batch) {
        return std::unexpected(batch.error().message);
    }
    return AlfaFinancialProjection{true, std::move(*batch)};
}

} // namespace wealthtracker::app
