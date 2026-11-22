module;

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimeZone>
#include <QUrl>
#include <QVector>
#include <QXmlStreamReader>

#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <utility>

module wt.connectors;

namespace wealthtracker::connectors {
namespace {

constexpr qsizetype maximumBundleBytes = 64 * 1024 * 1024;
constexpr qsizetype maximumPreferencesBytes = 256 * 1024;
constexpr qsizetype maximumProtocolLineBytes = 8 * 1024 * 1024;
constexpr qsizetype maximumStateBytes = 1024 * 1024;
constexpr int maximumPreferences = 64;
constexpr int maximumAccounts = 10000;
constexpr int maximumTransactions = 1000000;

const QRegularExpression &identifierPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("^[a-z0-9][a-z0-9._-]{0,63}$"));
    return pattern;
}

const QRegularExpression &preferenceKeyPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z][A-Za-z0-9._-]{0,63}$"));
    return pattern;
}

ConnectorError invalidInput(QString message, QString detail = {})
{
    return {ErrorCode::InvalidInput, std::move(message), std::move(detail),
            false};
}

ConnectorError parseFailure(QString message, QString detail = {})
{
    return {ErrorCode::ParseFailed, std::move(message), std::move(detail),
            false};
}

bool isSensitiveKey(const QString &key)
{
    const auto normalized = key.toLower();
    static const QStringList markers{
        QStringLiteral("password"), QStringLiteral("passwd"),
        QStringLiteral("passcode"), QStringLiteral("secret"),
        QStringLiteral("token"), QStringLiteral("pin"),
        QStringLiteral("card"), QStringLiteral("cvv"),
        QStringLiteral("cvc"), QStringLiteral("otp"),
        QStringLiteral("code")};
    for (const auto &marker : markers) {
        if (normalized.contains(marker)) {
            return true;
        }
    }
    return false;
}

ZenPluginPreferenceKind preferenceKind(const QString &element,
                                       const QString &inputType)
{
    if (element == QStringLiteral("CheckBoxPreference")
        || inputType == QStringLiteral("checkbox")) {
        return ZenPluginPreferenceKind::Checkbox;
    }
    if (element == QStringLiteral("ListPreference")) {
        return ZenPluginPreferenceKind::Choice;
    }
    if (inputType.contains(QStringLiteral("Password"),
                           Qt::CaseInsensitive)) {
        return ZenPluginPreferenceKind::Password;
    }
    if (inputType == QStringLiteral("phone")) {
        return ZenPluginPreferenceKind::Phone;
    }
    if (inputType == QStringLiteral("number")) {
        return ZenPluginPreferenceKind::Number;
    }
    if (inputType == QStringLiteral("date")) {
        return ZenPluginPreferenceKind::Date;
    }
    return ZenPluginPreferenceKind::Text;
}

QString inferredPreferencesPath(const QFileInfo &bundle)
{
    const auto sibling = bundle.dir().filePath(QStringLiteral("preferences.xml"));
    if (QFileInfo::exists(sibling)) {
        return QFileInfo(sibling).canonicalFilePath();
    }

    const auto repositoryRoot = bundle.dir().dirName() == QStringLiteral("build")
        ? bundle.dir().absolutePath() + QStringLiteral("/..")
        : bundle.dir().absolutePath();
    const auto sourcePath =
        repositoryRoot + QStringLiteral("/src/plugins/") + bundle.baseName()
        + QStringLiteral("/preferences.xml");
    if (QFileInfo::exists(sourcePath)) {
        return QFileInfo(sourcePath).canonicalFilePath();
    }
    return {};
}

ConnectorResult<QVector<ZenPluginPreference>>
parsePreferences(const QString &path)
{
    if (path.isEmpty()) {
        return std::unexpected(invalidInput(
            QStringLiteral("Не найден preferences.xml для выбранного плагина"),
            QStringLiteral(
                "Выберите bundle из каталога build репозитория ZenPlugins")));
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 1
        || file.size() > maximumPreferencesBytes) {
        return std::unexpected(invalidInput(
            QStringLiteral("Не удалось безопасно прочитать preferences.xml"),
            path));
    }

    QXmlStreamReader xml(&file);
    QVector<ZenPluginPreference> result;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::DTD) {
            return std::unexpected(parseFailure(
                QStringLiteral("DTD не поддерживается в preferences.xml")));
        }
        if (!xml.isStartElement()) {
            continue;
        }
        const auto element = xml.name().toString();
        if (element != QStringLiteral("EditTextPreference")
            && element != QStringLiteral("CheckBoxPreference")
            && element != QStringLiteral("ListPreference")) {
            continue;
        }
        if (result.size() >= maximumPreferences) {
            return std::unexpected(parseFailure(
                QStringLiteral("Слишком много полей в preferences.xml")));
        }

        const auto attributes = xml.attributes();
        const auto key = attributes.value(QStringLiteral("key")).toString();
        const auto label =
            attributes.value(QStringLiteral("title")).toString().trimmed();
        const auto inputType =
            attributes.value(QStringLiteral("inputType")).toString();
        if (!identifierPattern().match(key.toLower()).hasMatch()
            || !preferenceKeyPattern().match(key).hasMatch()
            || label.isEmpty() || label.size() > 160) {
            return std::unexpected(parseFailure(
                QStringLiteral("Некорректное поле в preferences.xml"), key));
        }

        const auto kind = preferenceKind(element, inputType);
        const auto defaultValue =
            attributes.value(QStringLiteral("defaultValue")).toString();
        if (defaultValue.size() > 4096) {
            return std::unexpected(parseFailure(
                QStringLiteral("Слишком длинное значение по умолчанию"), key));
        }
        result.push_back(
            {key,
             label,
             kind,
             defaultValue,
             {},
             attributes.value(QStringLiteral("obligatory")).toString()
                 == QStringLiteral("true"),
             kind == ZenPluginPreferenceKind::Password || isSensitiveKey(key)});
    }
    if (xml.hasError()) {
        return std::unexpected(parseFailure(
            QStringLiteral("Некорректный preferences.xml"),
            xml.errorString()));
    }
    if (result.isEmpty()) {
        return std::unexpected(parseFailure(
            QStringLiteral("В preferences.xml нет поддерживаемых полей")));
    }
    return result;
}

ConnectorResult<void> validateScalarObject(const QJsonObject &object,
                                           const QString &label)
{
    if (object.size() > maximumPreferences) {
        return std::unexpected(
            invalidInput(label + QStringLiteral(": слишком много полей")));
    }
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!preferenceKeyPattern().match(it.key()).hasMatch()
            || it.key() == QStringLiteral("__proto__")
            || it.key() == QStringLiteral("constructor")
            || it.key() == QStringLiteral("prototype")) {
            return std::unexpected(
                invalidInput(label + QStringLiteral(": некорректный ключ"),
                             it.key()));
        }
        const auto &value = it.value();
        if (!value.isString() && !value.isBool() && !value.isDouble()
            && !value.isNull()) {
            return std::unexpected(invalidInput(
                label + QStringLiteral(": допустимы только простые значения"),
                it.key()));
        }
        if (value.isString() && value.toString().size() > 4096) {
            return std::unexpected(invalidInput(
                label + QStringLiteral(": значение слишком длинное"),
                it.key()));
        }
    }
    return {};
}

ConnectorResult<QJsonArray> validatedOrigins(const QStringList &origins)
{
    if (origins.size() > 8) {
        return std::unexpected(invalidInput(
            QStringLiteral("Слишком много разрешённых origin плагина")));
    }
    QJsonArray result;
    QSet<QString> seen;
    for (const auto &value : origins) {
        const QUrl url(value, QUrl::StrictMode);
        const bool plainRootPath = url.path().isEmpty()
            || url.path() == QStringLiteral("/");
        if (!url.isValid() || url.scheme() != QStringLiteral("https")
            || url.host().isEmpty() || !url.userInfo().isEmpty()
            || url.hasQuery() || url.hasFragment() || !plainRootPath) {
            return std::unexpected(invalidInput(
                QStringLiteral("Некорректный разрешённый origin плагина"),
                value));
        }
        QString normalized = QStringLiteral("https://") + url.host().toLower();
        if (url.port(-1) != -1 && url.port() != 443) {
            normalized += QStringLiteral(":%1").arg(url.port());
        }
        if (!seen.contains(normalized)) {
            seen.insert(normalized);
            result.push_back(normalized);
        }
    }
    return result;
}

std::optional<std::int64_t> toMinorUnits(const QJsonValue &value)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        return std::nullopt;
    }
    const long double scaled =
        static_cast<long double>(value.toDouble()) * 100.0L;
    if (scaled
            > static_cast<long double>(
                std::numeric_limits<std::int64_t>::max())
        || scaled
            < static_cast<long double>(
                std::numeric_limits<std::int64_t>::min())) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(std::llround(scaled));
}

QString normalizedCurrency(QString value)
{
    value = value.trimmed().toUpper();
    if (value == QStringLiteral("RUR")) {
        return QStringLiteral("RUB");
    }
    if (value.size() != 3) {
        return {};
    }
    for (const auto character : value) {
        if (!character.isLetter()) {
            return {};
        }
    }
    return value;
}

QString accountKind(const QString &value)
{
    static const QHash<QString, QString> kinds{
        {QStringLiteral("card"), QStringLiteral("card")},
        {QStringLiteral("ccard"), QStringLiteral("credit-card")},
        {QStringLiteral("checking"), QStringLiteral("checking")},
        {QStringLiteral("deposit"), QStringLiteral("deposit")},
        {QStringLiteral("loan"), QStringLiteral("loan")},
        {QStringLiteral("investment"), QStringLiteral("investment")},
        {QStringLiteral("emoney"), QStringLiteral("wallet")}};
    return kinds.value(value.toLower(), QStringLiteral("checking"));
}

QString maskedNumber(const QJsonObject &account)
{
    const auto syncValue = account.value(QStringLiteral("syncID"));
    QString candidate;
    if (syncValue.isString()) {
        candidate = syncValue.toString();
    } else if (syncValue.isArray() && !syncValue.toArray().isEmpty()) {
        candidate = syncValue.toArray().first().toString();
    }
    QString digits;
    for (const auto character : candidate) {
        if (character.isDigit()) {
            digits += character;
        }
    }
    return digits.size() >= 4
        ? QStringLiteral("•••• %1").arg(digits.last(4))
        : QString{};
}

QString transactionDescription(const QJsonObject &transaction)
{
    const auto merchant = transaction.value(QStringLiteral("merchant"));
    if (merchant.isObject()) {
        const auto title =
            merchant.toObject().value(QStringLiteral("fullTitle")).toString();
        if (!title.trimmed().isEmpty()) {
            return title.trimmed().left(512);
        }
    } else if (merchant.isString() && !merchant.toString().trimmed().isEmpty()) {
        return merchant.toString().trimmed().left(512);
    }
    for (const auto &key : {QStringLiteral("payee"),
                            QStringLiteral("comment")}) {
        const auto value = transaction.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            return value.left(512);
        }
    }
    return QStringLiteral("Банковская операция");
}

QString transactionCategory(const QJsonObject &transaction)
{
    const auto category = transaction.value(QStringLiteral("category"));
    if (category.isString()) {
        return category.toString().trimmed().left(160);
    }
    if (category.isObject()) {
        return category.toObject().value(QStringLiteral("name"))
            .toString().trimmed().left(160);
    }
    return {};
}

std::optional<QDateTime> transactionDate(const QJsonValue &value)
{
    if (value.isString()) {
        auto result =
            QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
        if (!result.isValid()) {
            result = QDateTime::fromString(value.toString(), Qt::ISODate);
        }
        return result.isValid() ? std::optional{result.toUTC()} : std::nullopt;
    }
    if (value.isDouble() && std::isfinite(value.toDouble())) {
        const auto milliseconds =
            static_cast<qint64>(std::llround(value.toDouble()));
        const auto result =
            QDateTime::fromMSecsSinceEpoch(milliseconds, QTimeZone::UTC);
        return result.isValid() ? std::optional{result} : std::nullopt;
    }
    return std::nullopt;
}

QString stableTransactionId(const QString &pluginId,
                            const QJsonObject &transaction, int index)
{
    const auto supplied = transaction.value(QStringLiteral("id"))
                              .toString().trimmed().left(256);
    if (!supplied.isEmpty()) {
        return supplied;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(pluginId.toUtf8());
    hash.addData(QJsonDocument(transaction).toJson(QJsonDocument::Compact));
    hash.addData(QByteArray::number(index));
    return QString::fromLatin1(hash.result().toHex());
}

ConnectorResult<NormalizedBatch>
normalizeResult(const QJsonObject &object, const QString &pluginId,
                const QDateTime &observedAt)
{
    const auto accountsValue = object.value(QStringLiteral("accounts"));
    const auto transactionsValue =
        object.value(QStringLiteral("transactions"));
    if (!accountsValue.isArray() || !transactionsValue.isArray()) {
        return std::unexpected(parseFailure(
            QStringLiteral("Плагин вернул данные неизвестного формата")));
    }
    const auto accountObjects = accountsValue.toArray();
    const auto transactionObjects = transactionsValue.toArray();
    if (accountObjects.isEmpty() || accountObjects.size() > maximumAccounts
        || transactionObjects.size() > maximumTransactions) {
        return std::unexpected(parseFailure(
            QStringLiteral("Плагин вернул недопустимое количество записей")));
    }

    NormalizedBatch batch;
    QHash<QString, QString> accountCurrencies;
    QSet<QString> accountIds;
    batch.accounts.reserve(accountObjects.size());
    batch.balances.reserve(accountObjects.size());
    for (const auto &value : accountObjects) {
        if (!value.isObject()) {
            return std::unexpected(
                parseFailure(QStringLiteral("Некорректная запись счёта")));
        }
        const auto account = value.toObject();
        const auto id =
            account.value(QStringLiteral("id")).toString().trimmed();
        const auto title =
            account.value(QStringLiteral("title")).toString().trimmed();
        const auto currency = normalizedCurrency(
            account.value(QStringLiteral("instrument")).toString());
        const auto balance =
            toMinorUnits(account.value(QStringLiteral("balance")));
        if (id.isEmpty() || id.size() > 256 || accountIds.contains(id)
            || title.isEmpty() || title.size() > 256 || currency.isEmpty()
            || !balance) {
            return std::unexpected(parseFailure(
                QStringLiteral("Некорректные поля счёта"), id.left(64)));
        }
        accountIds.insert(id);
        accountCurrencies.insert(id, currency);
        const Provenance provenance{pluginId, {}, id, -1};
        batch.accounts.push_back(
            {id,
             title,
             accountKind(
                 account.value(QStringLiteral("type")).toString()),
             currency,
             maskedNumber(account),
             false,
             provenance});
        batch.balances.push_back(
            {id, observedAt, Money{*balance, currency}, provenance});
    }

    int transactionIndex = 0;
    for (const auto &value : transactionObjects) {
        if (!value.isObject()) {
            return std::unexpected(parseFailure(
                QStringLiteral("Некорректная запись операции")));
        }
        const auto transaction = value.toObject();
        const auto date =
            transactionDate(transaction.value(QStringLiteral("date")));
        const auto income =
            toMinorUnits(transaction.value(QStringLiteral("income")));
        const auto outcome =
            toMinorUnits(transaction.value(QStringLiteral("outcome")));
        const auto incomeAccount =
            transaction.value(QStringLiteral("incomeAccount"))
                .toString().trimmed();
        const auto outcomeAccount =
            transaction.value(QStringLiteral("outcomeAccount"))
                .toString().trimmed();
        if (!date || !income || !outcome || *income < 0 || *outcome < 0) {
            return std::unexpected(parseFailure(
                QStringLiteral("Некорректная сумма или дата операции")));
        }

        const auto baseId =
            stableTransactionId(pluginId, transaction, transactionIndex++);
        const auto description = transactionDescription(transaction);
        const auto category = transactionCategory(transaction);
        QString mcc;
        const auto mccValue = transaction.value(QStringLiteral("mcc"));
        if (mccValue.isDouble()) {
            mcc = QString::number(mccValue.toInt());
        } else if (mccValue.isString()) {
            mcc = mccValue.toString().left(8);
        }
        const auto occurredAt = std::optional{*date};
        const bool knownIncome = accountIds.contains(incomeAccount);
        const bool knownOutcome = accountIds.contains(outcomeAccount);
        const auto kind = knownIncome && knownOutcome && incomeAccount != outcomeAccount &&
            *income > 0 && *outcome > 0
            ? NormalizedTransactionKind::InternalTransfer : NormalizedTransactionKind::Regular;

        auto append = [&](const QString &accountId,
                          const std::int64_t amount, const QString &suffix) {
            const auto currency = accountCurrencies.value(accountId);
            QMap<QString, QString> fields{
                {QStringLiteral("pluginId"), pluginId}};
            if (transaction.value(QStringLiteral("hold")).toBool()) {
                fields.insert(QStringLiteral("hold"),
                              QStringLiteral("true"));
            }
            batch.transactions.push_back(
                {baseId + suffix,
                 accountId,
                 date->date(),
                 occurredAt,
                 Money{amount, currency},
                 description,
                 description,
                 category,
                 mcc,
                 std::move(fields),
                 Provenance{pluginId, {}, baseId, -1},
                 kind});
        };

        if (knownOutcome && *outcome > 0) {
            append(outcomeAccount, -*outcome,
                   knownIncome ? QStringLiteral(":out") : QString{});
        }
        if (knownIncome && *income > 0) {
            append(incomeAccount, *income,
                   knownOutcome ? QStringLiteral(":in") : QString{});
        }
        if (!knownIncome && !knownOutcome) {
            return std::unexpected(parseFailure(
                QStringLiteral("Операция не относится ни к одному счёту"),
                baseId.left(64)));
        }
    }
    return batch;
}

} // namespace

ConnectorResult<ZenPluginDescriptor>
inspectZenPluginBundle(const QString &bundlePath)
{
    const QFileInfo bundle(bundlePath);
    const auto canonical = bundle.canonicalFilePath();
    const QFileInfo canonicalBundle(canonical);
    if (canonical.isEmpty() || !canonicalBundle.isFile()
        || !canonicalBundle.isReadable()
        || canonicalBundle.size() < 1
        || canonicalBundle.size() > maximumBundleBytes
        || (canonicalBundle.suffix() != QStringLiteral("js")
            && canonicalBundle.suffix() != QStringLiteral("cjs"))) {
        return std::unexpected(invalidInput(
            QStringLiteral("Выберите читаемый JavaScript bundle размером до 64 МБ"),
            bundlePath));
    }
    const auto pluginId = canonicalBundle.completeBaseName().toLower();
    if (!identifierPattern().match(pluginId).hasMatch()) {
        return std::unexpected(invalidInput(
            QStringLiteral("Имя bundle не является допустимым id плагина"),
            pluginId));
    }
    const auto preferencesPath = inferredPreferencesPath(canonicalBundle);
    auto preferences = parsePreferences(preferencesPath);
    if (!preferences) {
        return std::unexpected(preferences.error());
    }
    QFile source(canonical);
    if (!source.open(QIODevice::ReadOnly)) {
        return std::unexpected(invalidInput(QStringLiteral("Не удалось прочитать bundle")));
    }
    const auto code = source.read(maximumBundleBytes + 1);
    if (code.isEmpty() || code.size() > maximumBundleBytes || source.error() != QFileDevice::NoError) {
        return std::unexpected(invalidInput(QStringLiteral("Некорректный размер bundle")));
    }
    return ZenPluginDescriptor{pluginId, canonical, preferencesPath, std::move(*preferences),
                               QCryptographicHash::hash(code, QCryptographicHash::Sha256).toHex()};
}

ConnectorResult<QByteArray>
makeZenPluginStartMessage(const ZenPluginRunRequest &request)
{
    const QFileInfo bundle(request.descriptor.bundlePath);
    if (!bundle.isAbsolute() || !bundle.isFile()
        || !identifierPattern().match(request.descriptor.pluginId).hasMatch()) {
        return std::unexpected(invalidInput(
            QStringLiteral("Конфигурация локального плагина недействительна")));
    }
    if (!request.from.isValid() || !request.to.isValid()
        || request.from > request.to) {
        return std::unexpected(
            invalidInput(QStringLiteral("Некорректный период синхронизации")));
    }
    if (const auto valid = validateScalarObject(
            request.preferences, QStringLiteral("Параметры плагина"));
        !valid) {
        return std::unexpected(valid.error());
    }
    const auto stateBytes =
        QJsonDocument(request.state).toJson(QJsonDocument::Compact);
    if (stateBytes.size() > maximumStateBytes) {
        return std::unexpected(invalidInput(
            QStringLiteral("Состояние плагина превышает 1 МБ")));
    }
    auto allowedOrigins = validatedOrigins(request.allowedOrigins);
    if (!allowedOrigins) {
        return std::unexpected(allowedOrigins.error());
    }

    const QJsonObject object{
        {QStringLiteral("type"), QStringLiteral("start")},
        {QStringLiteral("pluginId"), request.descriptor.pluginId},
        {QStringLiteral("bundlePath"),
         QFileInfo(request.descriptor.bundlePath).canonicalFilePath()},
        {QStringLiteral("bundleDigest"), QString::fromLatin1(request.descriptor.bundleDigest)},
        {QStringLiteral("preferences"), request.preferences},
        {QStringLiteral("state"), request.state},
        {QStringLiteral("from"),
         request.from.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("to"),
         request.to.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("allowedOrigins"), *allowedOrigins}};
    auto line = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (line.size() > maximumProtocolLineBytes) {
        return std::unexpected(invalidInput(
            QStringLiteral("Запрос плагина превышает безопасный лимит")));
    }
    line += '\n';
    return line;
}

ConnectorResult<QByteArray>
makeZenPluginInputMessage(const QString &requestId, const QByteArray &value)
{
    if (!identifierPattern().match(requestId.toLower()).hasMatch()
        || requestId.size() > 64 || value.size() > 4096
        || value.contains('\0')) {
        return std::unexpected(invalidInput(
            QStringLiteral("Некорректный ответ на запрос плагина")));
    }
    QJsonObject object{
        {QStringLiteral("type"), QStringLiteral("input")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("value"), QString::fromUtf8(value)}};
    auto line = QJsonDocument(object).toJson(QJsonDocument::Compact);
    line += '\n';
    return line;
}

ConnectorResult<ZenPluginEvent>
parseZenPluginEvent(const QByteArray &line, const QString &pluginId,
                    const QDateTime &observedAt)
{
    if (line.isEmpty() || line.size() > maximumProtocolLineBytes) {
        return std::unexpected(parseFailure(
            QStringLiteral("Сообщение плагина превышает безопасный лимит")));
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        return std::unexpected(parseFailure(
            QStringLiteral("Плагин вернул некорректный JSON")));
    }
    const auto object = document.object();
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("prompt")) {
        const auto requestId =
            object.value(QStringLiteral("requestId")).toString();
        const auto message =
            object.value(QStringLiteral("message")).toString().trimmed();
        const auto inputType =
            object.value(QStringLiteral("inputType")).toString();
        const auto timeout =
            object.value(QStringLiteral("timeoutMs")).toInt(120000);
        if (!identifierPattern().match(requestId.toLower()).hasMatch()
            || requestId.size() > 64 || message.isEmpty()
            || message.size() > 2048
            || (inputType != QStringLiteral("text")
                && inputType != QStringLiteral("number")
                && inputType != QStringLiteral("password"))
            || timeout < 1000 || timeout > 300000) {
            return std::unexpected(parseFailure(
                QStringLiteral("Плагин запросил некорректное поле")));
        }
        ZenPluginEvent event;
        event.kind = ZenPluginEventKind::Prompt;
        event.prompt = {requestId, message, inputType, timeout};
        return event;
    }
    if (type == QStringLiteral("error")) {
        const auto message =
            object.value(QStringLiteral("message")).toString().trimmed();
        if (message.isEmpty() || message.size() > 4096) {
            return std::unexpected(parseFailure(
                QStringLiteral("Плагин вернул некорректную ошибку")));
        }
        ZenPluginEvent event;
        event.kind = ZenPluginEventKind::Error;
        event.message = message;
        event.retryable =
            object.value(QStringLiteral("retryable")).toBool();
        return event;
    }
    if (type == QStringLiteral("result")) {
        auto batch = normalizeResult(object, pluginId, observedAt);
        if (!batch) {
            return std::unexpected(batch.error());
        }
        const auto state = object.value(QStringLiteral("state"));
        if (!state.isObject()
            || QJsonDocument(state.toObject())
                       .toJson(QJsonDocument::Compact)
                       .size()
                > maximumStateBytes) {
            return std::unexpected(parseFailure(
                QStringLiteral("Плагин вернул некорректное состояние")));
        }
        ZenPluginEvent event;
        event.kind = ZenPluginEventKind::Result;
        event.batch = std::move(*batch);
        event.state = state.toObject();
        return event;
    }
    return std::unexpected(parseFailure(
        QStringLiteral("Плагин вернул неизвестный тип сообщения")));
}

} // namespace wealthtracker::connectors
