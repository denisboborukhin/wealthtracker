module;

// Keep Qt template declarations in the global module on NDK Clang.
extern "C++" {
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <expected>
#include <optional>
}

export module wt.ozon_browser;

import wt.connectors;

export namespace wealthtracker::app {

enum class OzonProjectionStage { Waiting, AuthenticationRequired, Ready, Invalid };

struct OzonFinancialProjection {
    OzonProjectionStage stage{OzonProjectionStage::Waiting};
    std::optional<wealthtracker::connectors::NormalizedBatch> batch;
    QString reasonCode;
};

[[nodiscard]] const QString& ozon_sync_start_script() {
    static const QString script = QStringLiteral(R"OZON((() => {
  try {
    if (location.origin !== 'https://finance.ozon.ru') return 'wrong-origin'
    const key = '__wealthtrackerOzonSync'
    const old = globalThis[key]
    if (old && !old.cancelled && ['running', 'ready', 'invalid'].includes(old.status)) return old.status
    if (old && !old.cancelled && old.retryAt > Date.now()) return old.status
    const attempt = old && !old.cancelled ? old.attempt + 1 : 1
    if (old?.controller) old.controller.abort()
    const state = globalThis[key] = {status:'running', cancelled:false,
      controller:new AbortController(), attempt:attempt}
    const active = () => globalThis[key] === state && !state.cancelled
    const fail = code => { throw {code:code} }
    const prefix = 'https://cdn2.ozone.ru/s3/bozon-fe-main/release/apps/main/_app/immutable/chunks/'
    // Import only the exact software asset already loaded by the bank dashboard.
    const assets = performance.getEntriesByType('resource').map(e => e.name)
    for (const script of document.scripts) if (script.src) assets.push(script.src)
    const expected = prefix + 'B6d1cMwe.js'
    if (!assets.includes(expected)) {
      state.status = 'waiting'
      state.reasonCode = 'OZON-MODULE'
      state.retryAt = Date.now() + 500
      return state.status
    }
    import(expected).then(module => {
      if (!active()) return
      const handler = module.ki
      if (typeof handler !== 'function' || handler.url !== 'mainPage' ||
          handler.declaration?.operationName !== 'GetMainPage' ||
          handler.declaration?.operationType !== 'query') fail('OZON-MODULE')
      return handler.call({}, {signal:state.controller.signal})
    }).then(response => {
      if (!active()) return
      if (!response || typeof response.isOk !== 'function' || !response.isOk()) fail('OZON-ACCESS')
      const value = response.value
      if (!value || typeof value !== 'object' || !value.mainPage ||
          typeof value.mainPage !== 'object' || value.mainPage.error ||
          !Array.isArray(value.mainPage.widgets)) fail('OZON-SCHEMA')
      if (new TextEncoder().encode(JSON.stringify(value)).byteLength > 8 * 1024 * 1024) fail('OZON-LIMIT')
      state.page = value.mainPage
      state.status = 'ready'
      state.reasonCode = ''
    }).catch(error => {
      if (!active()) return
      delete state.page
      const code = ['OZON-MODULE','OZON-ACCESS','OZON-SCHEMA','OZON-LIMIT'].includes(error?.code)
        ? error.code : 'OZON-READ'
      state.reasonCode = code
      state.status = code === 'OZON-ACCESS' ? 'authentication-required'
        : code === 'OZON-READ' ? 'waiting' : 'invalid'
      state.retryAt = Date.now() + 1000
    })
    return state.status
  } catch (ignored) { return 'waiting' }
})())OZON");
    return script;
}

[[nodiscard]] const QString& ozon_financial_projection_script() {
    static const QString script = QStringLiteral(R"OZON((() => {
  const result = {version:1,stage:'waiting',authenticated:false,reasonCode:'',accounts:[]}
  const invalid = code => { throw {code:code} }
  try {
    if (location.origin !== 'https://finance.ozon.ru') return JSON.stringify(result)
    if (location.pathname === '/apps/auth/signin') {
      result.stage = 'authentication-required'
      return JSON.stringify(result)
    }
    const state = globalThis.__wealthtrackerOzonSync
    if (!state || state.cancelled) return JSON.stringify(result)
    const codes = ['OZON-ORIGIN','OZON-MODULE','OZON-READ','OZON-SCHEMA','OZON-LIMIT','OZON-ACCESS']
    result.reasonCode = codes.includes(state.reasonCode) ? state.reasonCode : ''
    if (state.status === 'invalid') { result.stage = 'invalid'; return JSON.stringify(result) }
    if (state.status === 'authentication-required') {
      result.stage = 'authentication-required'
      return JSON.stringify(result)
    }
    if (state.status !== 'ready') return JSON.stringify(result)
    if (!state.page || state.page.error || !Array.isArray(state.page.widgets)) invalid('OZON-SCHEMA')
    const main = [], singles = [], summaries = []
    let count = 0
    const walk = (widgets, depth) => {
      if (!Array.isArray(widgets) || depth > 4) invalid('OZON-LIMIT')
      for (const widget of widgets) {
        if (++count > 1024) invalid('OZON-LIMIT')
        if (!widget || typeof widget !== 'object' || Array.isArray(widget)) invalid('OZON-SCHEMA')
        const mapping = {MAIN_ACCOUNT:['MainAccountData',main],
          SAVINGS_B2C_SINGLE_PRODUCT:['SavingsB2cSingleProductData',singles],
          SAVINGS_B2C_SUMMARY:['SavingsB2cSummaryData',summaries]}
        const selected = mapping[widget.type]
        if (selected) {
          if (widget.status !== 'READY' || (widget.version !== 1 && widget.version !== '1') ||
              !widget.data || widget.data.__typename !== selected[0]) invalid('OZON-SCHEMA')
          selected[1].push(widget.data)
        }
        if (widget.widgets !== undefined && widget.widgets !== null) walk(widget.widgets, depth + 1)
      }
    }
    walk(state.page.widgets, 0)
    if (main.length > 1 || summaries.length > 1 || (!summaries.length && singles.length > 1)) invalid('OZON-SCHEMA')
    const money = value => {
      if (typeof value !== 'number' || !Number.isSafeInteger(value)) invalid('OZON-SCHEMA')
      return String(value)
    }
    const name = value => {
      if (typeof value !== 'string' || !value.trim() || value.length > 256 || /[\x00-\x1f]/.test(value)) invalid('OZON-SCHEMA')
      return value.trim()
    }
    // Validate recognized balances even when their aggregate is selected instead.
    for (const item of singles) { money(item.balance?.cents); name(item.title) }
    for (const item of summaries) { money(item.activeProductsTotalAmount?.cents); name(item.title) }
    if (main.length) result.accounts.push({id:'ozon-main',name:'Основной счёт',kind:'current',
      currency:'RUB',balanceMinor:money(main[0].balanceCents)})
    const savings = summaries[0] || singles[0]
    if (savings) result.accounts.push({id:'ozon-savings',name:name(savings.title),kind:'savings',
      currency:'RUB',balanceMinor:money(summaries.length ? savings.activeProductsTotalAmount.cents : savings.balance.cents)})
    if (!result.accounts.length) invalid('OZON-SCHEMA')
    result.stage = 'ready'
    result.authenticated = true
    result.reasonCode = ''
    return JSON.stringify(result)
  } catch (error) {
    result.stage = 'invalid'
    result.authenticated = false
    result.accounts = []
    result.reasonCode = ['OZON-SCHEMA','OZON-LIMIT'].includes(error?.code) ? error.code : 'OZON-SCHEMA'
    return JSON.stringify(result)
  }
})())OZON");
    return script;
}

[[nodiscard]] std::expected<OzonFinancialProjection, QString>
parse_ozon_financial_projection(const QString& projection, const QDateTime& observedAt) {
    const auto invalid = [] { return std::unexpected(QStringLiteral("Некорректный ответ встроенной страницы Ozon Банка")); };
    constexpr qsizetype maximumBytes = 8 * 1024 * 1024;
    if (projection.isEmpty() || projection.size() > maximumBytes) return invalid();
    const auto bytes = projection.toUtf8();
    if (bytes.size() > maximumBytes) return invalid();
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return invalid();
    const auto object = document.object();
    if (object.value(QStringLiteral("version")) != QJsonValue(1) ||
        !object.value(QStringLiteral("authenticated")).isBool() ||
        !object.value(QStringLiteral("accounts")).isArray() ||
        !object.value(QStringLiteral("reasonCode")).isString()) return invalid();
    const auto stageName = object.value(QStringLiteral("stage")).toString();
    OzonProjectionStage stage;
    if (stageName == QStringLiteral("waiting")) stage = OzonProjectionStage::Waiting;
    else if (stageName == QStringLiteral("authentication-required")) stage = OzonProjectionStage::AuthenticationRequired;
    else if (stageName == QStringLiteral("ready")) stage = OzonProjectionStage::Ready;
    else if (stageName == QStringLiteral("invalid")) stage = OzonProjectionStage::Invalid;
    else return invalid();
    const auto reason = object.value(QStringLiteral("reasonCode")).toString();
    static const QSet<QString> reasons{QStringLiteral("OZON-ORIGIN"), QStringLiteral("OZON-MODULE"),
        QStringLiteral("OZON-READ"), QStringLiteral("OZON-SCHEMA"), QStringLiteral("OZON-LIMIT"), QStringLiteral("OZON-ACCESS")};
    const auto accounts = object.value(QStringLiteral("accounts")).toArray();
    if ((!reason.isEmpty() && !reasons.contains(reason)) || accounts.size() > 128 ||
        object.value(QStringLiteral("authenticated")).toBool() != (stage == OzonProjectionStage::Ready)) return invalid();
    if (stage != OzonProjectionStage::Ready) {
        if (!accounts.isEmpty()) return invalid();
        return OzonFinancialProjection{stage, std::nullopt, reason};
    }
    if (!reason.isEmpty() || accounts.isEmpty() || !observedAt.isValid()) return invalid();
    wealthtracker::connectors::NormalizedBatch batch;
    QSet<QString> ids;
    static const QRegularExpression integer(QStringLiteral("^-?(?:0|[1-9][0-9]{0,15})$"));
    for (const auto& value : accounts) {
        if (!value.isObject()) return invalid();
        const auto account = value.toObject();
        const auto id = account.value(QStringLiteral("id")).toString();
        const auto name = account.value(QStringLiteral("name")).toString();
        const auto kind = account.value(QStringLiteral("kind")).toString();
        const auto amount = account.value(QStringLiteral("balanceMinor"));
        if ((id != QStringLiteral("ozon-main") && id != QStringLiteral("ozon-savings")) ||
            ids.contains(id) || name.isEmpty() || name.size() > 256 || name.trimmed() != name ||
            name.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f]"))) ||
            kind != (id == QStringLiteral("ozon-main") ? QStringLiteral("current") : QStringLiteral("savings")) ||
            account.value(QStringLiteral("currency")) != QJsonValue(QStringLiteral("RUB")) ||
            !amount.isString() || !integer.match(amount.toString()).hasMatch()) return invalid();
        bool converted = false;
        const auto minor = amount.toString().toLongLong(&converted);
        if (!converted || minor < -9007199254740991LL || minor > 9007199254740991LL) return invalid();
        ids.insert(id);
        const wealthtracker::connectors::Provenance provenance{QStringLiteral("ozon-retail"), {}, id, batch.accounts.size()};
        batch.accounts.push_back({id, name, kind, QStringLiteral("RUB"), {}, false, provenance});
        batch.balances.push_back({id, observedAt.toUTC(), {minor, QStringLiteral("RUB")}, provenance});
    }
    return OzonFinancialProjection{stage, std::move(batch), reason};
}

} // namespace wealthtracker::app
