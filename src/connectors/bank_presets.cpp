module;

#include <QByteArray>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <QStringConverter>
#include <QStringList>
#include <QTemporaryFile>
#include <QTime>
#include <QTimeZone>
#include <QVector>

#include <xlsxio_read.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

module wt.connectors;

namespace wealthtracker::connectors {
namespace {

using AliasMap = QHash<QString, QStringList>;

AliasMap commonAliases()
{
    return {
        {QStringLiteral("date"),
         {QStringLiteral("дата"), QStringLiteral("дата операции"),
          QStringLiteral("дата транзакции"), QStringLiteral("operation date"),
          QStringLiteral("transaction date"), QStringLiteral("date")}},
        {QStringLiteral("amount"),
         {QStringLiteral("сумма"), QStringLiteral("сумма операции"),
          QStringLiteral("сумма транзакции"), QStringLiteral("amount")}},
        {QStringLiteral("debit"),
         {QStringLiteral("дебет"), QStringLiteral("расход"), QStringLiteral("debit")}},
        {QStringLiteral("credit"),
         {QStringLiteral("кредит"), QStringLiteral("приход"), QStringLiteral("credit")}},
        {QStringLiteral("description"),
         {QStringLiteral("описание"), QStringLiteral("описание операции"),
          QStringLiteral("назначение платежа"), QStringLiteral("details"),
          QStringLiteral("description")}},
        {QStringLiteral("merchant"),
         {QStringLiteral("получатель"), QStringLiteral("магазин"),
          QStringLiteral("контрагент"), QStringLiteral("merchant"), QStringLiteral("payee")}},
        {QStringLiteral("currency"),
         {QStringLiteral("валюта"), QStringLiteral("валюта операции"),
          QStringLiteral("currency")}},
        {QStringLiteral("category"),
         {QStringLiteral("категория"), QStringLiteral("категория операции"),
          QStringLiteral("category")}},
        {QStringLiteral("externalId"),
         {QStringLiteral("идентификатор операции"), QStringLiteral("номер операции"),
          QStringLiteral("transaction id"), QStringLiteral("id")}},
        {QStringLiteral("account"),
         {QStringLiteral("счет"), QStringLiteral("счёт"), QStringLiteral("номер счета"),
          QStringLiteral("номер счёта"), QStringLiteral("номер карты"),
          QStringLiteral("account")}},
        {QStringLiteral("balance"),
         {QStringLiteral("остаток"), QStringLiteral("баланс"), QStringLiteral("balance")}},
    };
}

AliasMap mergedAliases(const AliasMap &specific)
{
    auto aliases = commonAliases();
    for (auto it = specific.cbegin(); it != specific.cend(); ++it) {
        auto values = aliases.value(it.key());
        for (const auto &value : it.value()) {
            if (!values.contains(value)) {
                values.push_front(value);
            }
        }
        aliases.insert(it.key(), values);
    }
    return aliases;
}

class StatementConnector final : public IImportConnector {
public:
    explicit StatementConnector(BankPreset preset) : preset_(std::move(preset)) {}

    [[nodiscard]] ConnectorManifest manifest() const override
    {
        QJsonObject schema{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("additionalProperties"), false},
        };
        return {preset_.connectorId,
                preset_.institution,
                preset_.displayName,
                QStringLiteral("1.0.0"),
                {Capability::FileImport},
                schema,
                {}};
    }

    [[nodiscard]] ProbeResult probe(const ImportSource &source) const override
    {
        constexpr qsizetype probeBytesLimit = 512 * 1024;
        QByteArray bytes;
        if (!source.content.isNull()) {
            bytes = source.content.first(probeBytesLimit);
        } else {
            QFile file(source.filePath);
            if (!file.open(QIODevice::ReadOnly)) {
                return {};
            }
            bytes = file.read(probeBytesLimit);
        }
        const auto format = detail::detectFileFormat(source, bytes);
        if (format == FileFormat::Unknown) {
            return {};
        }

        const auto text = detail::normalizedHeader(
            detail::decodeStatementText(bytes) + u' ' + source.effectiveName());
        int matchingTokens = 0;
        for (const auto &token : preset_.identifyingTokens) {
            if (text.contains(detail::normalizedHeader(token))) {
                ++matchingTokens;
            }
        }

        if (preset_.bank == RussianBank::Generic) {
            const int confidence =
                format == FileFormat::Csv || format == FileFormat::Ofx
                        || format == FileFormat::Qif || format == FileFormat::OneC
                    ? 35
                    : 25;
            return {format, confidence, QStringLiteral("Recognized statement file format"),
                    format == FileFormat::Pdf};
        }

        if (matchingTokens == 0) {
            return {format, 0, QStringLiteral("No bank-specific markers found"),
                    preset_.provisional || format == FileFormat::Pdf};
        }

        int aliasMatches = 0;
        for (auto it = preset_.columnAliases.cbegin(); it != preset_.columnAliases.cend(); ++it) {
            if (std::ranges::any_of(it.value(), [&text](const QString &alias) {
                    return text.contains(detail::normalizedHeader(alias));
                })) {
                ++aliasMatches;
            }
        }
        const int score = std::min(99, 50 + matchingTokens * 18 + aliasMatches * 5);
        return {format,
                score,
                score > 0 ? QStringLiteral("Matched %1 statement markers").arg(preset_.displayName)
                          : QStringLiteral("No bank-specific markers found"),
                preset_.provisional || format == FileFormat::Pdf};
    }

    [[nodiscard]] ConnectorResult<ParsedTable>
    parse(const ImportSource &source, const ImportOptions &options) const override
    {
        return detail::parseStatement(source, options, preset_);
    }

    [[nodiscard]] ConnectorResult<NormalizedBatch>
    normalize(const ParsedTable &table, const ImportOptions &options) const override
    {
        return detail::normalizeStatement(table, options, preset_);
    }

private:
    BankPreset preset_;
};

} // namespace

BankPreset genericPreset()
{
    return {RussianBank::Generic,
            QStringLiteral("generic-statement-v1"),
            QStringLiteral("generic"),
            QStringLiteral("Generic statement"),
            {},
            commonAliases(),
            false};
}

QVector<BankPreset> russianBankPresets()
{
    return {
        {RussianBank::TBank,
         QStringLiteral("tbank-statement-v1"),
         QStringLiteral("tbank"),
         QStringLiteral("Т-Банк"),
         {QStringLiteral("Т-Банк"), QStringLiteral("Тинькофф"), QStringLiteral("Tinkoff"),
          QStringLiteral("T-Bank"), QStringLiteral("tbank"),
          QStringLiteral("Сумма платежа")},
         mergedAliases({
             {QStringLiteral("amount"),
              {QStringLiteral("Сумма операции"), QStringLiteral("Сумма платежа")}},
             {QStringLiteral("description"),
              {QStringLiteral("Описание"), QStringLiteral("Описание операции")}},
             {QStringLiteral("externalId"), {QStringLiteral("Номер операции")}},
         }),
         true},
        {RussianBank::AlfaBank,
         QStringLiteral("alfabank-statement-v1"),
         QStringLiteral("alfabank"),
         QStringLiteral("Альфа-Банк"),
         {QStringLiteral("Альфа-Банк"), QStringLiteral("Альфа Банк"),
          QStringLiteral("Alfa-Bank"), QStringLiteral("Alfa Bank"),
          QStringLiteral("alfabank"), QStringLiteral("Сумма в валюте счета")},
         mergedAliases({
             {QStringLiteral("amount"),
              {QStringLiteral("Сумма в валюте счета"),
               QStringLiteral("Сумма в валюте операции")}},
             {QStringLiteral("description"),
              {QStringLiteral("Описание операции"), QStringLiteral("Назначение платежа")}},
         }),
         true},
        {RussianBank::Sber,
         QStringLiteral("sber-statement-v1"),
         QStringLiteral("sber"),
         QStringLiteral("Сбер"),
         {QStringLiteral("Сбер"), QStringLiteral("СберБанк"), QStringLiteral("Sberbank"),
          QStringLiteral("Sber"), QStringLiteral("Дата обработки")},
         mergedAliases({
             {QStringLiteral("date"),
              {QStringLiteral("Дата операции"), QStringLiteral("Дата обработки")}},
             {QStringLiteral("amount"), {QStringLiteral("Сумма операции")}},
             {QStringLiteral("description"), {QStringLiteral("Описание операции")}},
         }),
         true},
        {RussianBank::Vtb,
         QStringLiteral("vtb-statement-v1"),
         QStringLiteral("vtb"),
         QStringLiteral("ВТБ"),
         {QStringLiteral("ВТБ"), QStringLiteral("VTB"), QStringLiteral("Детали операции")},
         mergedAliases({
             {QStringLiteral("date"), {QStringLiteral("Дата операции")}},
             {QStringLiteral("amount"),
              {QStringLiteral("Сумма операции"), QStringLiteral("Сумма")}},
             {QStringLiteral("description"),
              {QStringLiteral("Описание операции"), QStringLiteral("Детали операции")}},
         }),
         true},
    };
}

std::shared_ptr<IImportConnector> makeBankPresetConnector(const BankPreset &preset)
{
    return std::make_shared<StatementConnector>(preset);
}

} // namespace wealthtracker::connectors

namespace wealthtracker::connectors::detail {
namespace {

QString decodeCp1251(const QByteArray &bytes)
{
    QString result;
    result.reserve(bytes.size());
    static constexpr std::array<char16_t, 64> special = {
        u'\u0402', u'\u0403', u'\u201A', u'\u0453', u'\u201E', u'\u2026', u'\u2020',
        u'\u2021', u'\u20AC', u'\u2030', u'\u0409', u'\u2039', u'\u040A', u'\u040C',
        u'\u040B', u'\u040F', u'\u0452', u'\u2018', u'\u2019', u'\u201C', u'\u201D',
        u'\u2022', u'\u2013', u'\u2014', u'\u0000', u'\u2122', u'\u0459', u'\u203A',
        u'\u045A', u'\u045C', u'\u045B', u'\u045F', u'\u00A0', u'\u040E', u'\u045E',
        u'\u0408', u'\u00A4', u'\u0490', u'\u00A6', u'\u00A7', u'\u0401', u'\u00A9',
        u'\u0404', u'\u00AB', u'\u00AC', u'\u00AD', u'\u00AE', u'\u0407', u'\u00B0',
        u'\u00B1', u'\u0406', u'\u0456', u'\u0491', u'\u00B5', u'\u00B6', u'\u00B7',
        u'\u0451', u'\u2116', u'\u0454', u'\u00BB', u'\u0458', u'\u0405', u'\u0455',
        u'\u0457'};
    for (const unsigned char byte : bytes) {
        if (byte < 0x80) {
            result.append(QChar(byte));
        } else if (byte >= 0xC0) {
            result.append(QChar(0x0410 + byte - 0xC0));
        } else {
            const auto character = special.at(byte - 0x80);
            result.append(character == 0 ? QChar::ReplacementCharacter : QChar(character));
        }
    }
    return result;
}

bool isValidUtf8(const QByteArray &bytes)
{
    const auto decoded = QString::fromUtf8(bytes);
    return !decoded.contains(QChar::ReplacementCharacter);
}

QChar detectDelimiter(const QString &text)
{
    const auto firstLine = text.first(text.indexOf(u'\n') >= 0 ? text.indexOf(u'\n') : text.size());
    const std::array candidates{u';', u',', u'\t'};
    QChar best = u';';
    qsizetype count = -1;
    for (const auto candidate : candidates) {
        const auto candidateCount = firstLine.count(candidate);
        if (candidateCount > count) {
            count = candidateCount;
            best = candidate;
        }
    }
    return best;
}

QVector<QStringList> parseDelimited(const QString &text, const QChar delimiter, bool &wellFormed)
{
    wellFormed = true;
    QVector<QStringList> records;
    QStringList record;
    QString field;
    bool quoted = false;
    for (qsizetype index = 0; index < text.size(); ++index) {
        const auto character = text.at(index);
        if (character == u'"') {
            if (quoted && index + 1 < text.size() && text.at(index + 1) == u'"') {
                field += u'"';
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (character == delimiter && !quoted) {
            record.push_back(field.trimmed());
            field.clear();
        } else if ((character == u'\n' || character == u'\r') && !quoted) {
            if (character == u'\r' && index + 1 < text.size() && text.at(index + 1) == u'\n') {
                ++index;
            }
            record.push_back(field.trimmed());
            field.clear();
            if (std::ranges::any_of(record, [](const QString &item) { return !item.isEmpty(); })) {
                records.push_back(record);
            }
            record.clear();
        } else {
            field += character;
        }
    }
    if (!field.isEmpty() || !record.isEmpty()) {
        record.push_back(field.trimmed());
        records.push_back(record);
    }
    if (quoted) {
        wellFormed = false;
    }
    return records;
}

QString extractTag(const QString &block, const QString &tag)
{
    const QRegularExpression expression(
        QStringLiteral("<%1>\\s*([^<\\r\\n]+)").arg(QRegularExpression::escape(tag)),
        QRegularExpression::CaseInsensitiveOption);
    return expression.match(block).captured(1).trimmed();
}

ParsedTable parseOfx(const QString &text)
{
    ParsedTable table;
    table.format = FileFormat::Ofx;
    table.columns = {QStringLiteral("date"), QStringLiteral("amount"),
                     QStringLiteral("description"), QStringLiteral("id"),
                     QStringLiteral("currency"), QStringLiteral("account")};
    const auto currency = extractTag(text, QStringLiteral("CURDEF"));
    const auto account = extractTag(text, QStringLiteral("ACCTID"));
    const QRegularExpression transactionExpression(
        QStringLiteral("<STMTTRN>(.*?)(?=<STMTTRN>|</BANKTRANLIST>|</STMTTRN>)"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    auto iterator = transactionExpression.globalMatch(text);
    while (iterator.hasNext()) {
        const auto block = iterator.next().captured(1);
        auto date = extractTag(block, QStringLiteral("DTPOSTED"));
        if (date.size() >= 8) {
            date = QStringLiteral("%1-%2-%3").arg(date.first(4), date.sliced(4, 2),
                                                  date.sliced(6, 2));
        }
        auto description = extractTag(block, QStringLiteral("NAME"));
        const auto memo = extractTag(block, QStringLiteral("MEMO"));
        if (description.isEmpty()) {
            description = memo;
        } else if (!memo.isEmpty() && memo != description) {
            description += QStringLiteral(" — ") + memo;
        }
        table.rows.push_back({date, extractTag(block, QStringLiteral("TRNAMT")), description,
                              extractTag(block, QStringLiteral("FITID")), currency, account});
    }
    return table;
}

ParsedTable parseQif(const QString &text)
{
    ParsedTable table;
    table.format = FileFormat::Qif;
    table.columns = {QStringLiteral("date"), QStringLiteral("amount"),
                     QStringLiteral("description"), QStringLiteral("id"),
                     QStringLiteral("category")};
    QString date;
    QString amount;
    QString payee;
    QString memo;
    QString id;
    QString category;
    const auto lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")));
    for (const auto &line : lines) {
        if (line == u"^") {
            if (!date.isEmpty() || !amount.isEmpty()) {
                table.rows.push_back(
                    {date, amount, payee.isEmpty() ? memo : payee, id, category});
            }
            date.clear();
            amount.clear();
            payee.clear();
            memo.clear();
            id.clear();
            category.clear();
            continue;
        }
        if (line.isEmpty() || line.startsWith(u'!')) {
            continue;
        }
        const auto value = line.sliced(1).trimmed();
        switch (line.at(0).toLatin1()) {
        case 'D':
            date = value;
            date.remove(u' ');
            date.replace(u'\'', u'/');
            break;
        case 'T': amount = value; break;
        case 'P': payee = value; break;
        case 'M': memo = value; break;
        case 'N': id = value; break;
        case 'L': category = value; break;
        default: break;
        }
    }
    return table;
}

ParsedTable parseOneC(const QString &text)
{
    ParsedTable table;
    table.format = FileFormat::OneC;
    table.columns = {QStringLiteral("date"), QStringLiteral("amount"),
                     QStringLiteral("description"), QStringLiteral("id"),
                     QStringLiteral("account")};
    QMap<QString, QString> record;
    bool inDocument = false;
    const auto flush = [&table, &record]() {
        if (record.isEmpty()) {
            return;
        }
        const auto debitAccount = record.value(QStringLiteral("ПлательщикСчет"));
        const auto creditAccount = record.value(QStringLiteral("ПолучательСчет"));
        table.rows.push_back(
            {record.value(QStringLiteral("Дата")), record.value(QStringLiteral("Сумма")),
             record.value(QStringLiteral("НазначениеПлатежа")),
             record.value(QStringLiteral("Номер")),
             debitAccount.isEmpty() ? creditAccount : debitAccount});
        record.clear();
    };
    const auto lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")));
    for (const auto &line : lines) {
        if (line.startsWith(QStringLiteral("СекцияДокумент="), Qt::CaseInsensitive)) {
            flush();
            inDocument = true;
            continue;
        }
        if (line.compare(QStringLiteral("КонецДокумента"), Qt::CaseInsensitive) == 0) {
            flush();
            inDocument = false;
            continue;
        }
        if (!inDocument) {
            continue;
        }
        const auto separator = line.indexOf(u'=');
        if (separator > 0) {
            record.insert(line.first(separator).trimmed(), line.sliced(separator + 1).trimmed());
        }
    }
    flush();
    return table;
}

ConnectorResult<ParsedTable> parseXlsx(const QByteArray &bytes)
{
    QTemporaryFile temporary;
    temporary.setAutoRemove(true);
    if (!temporary.open() || temporary.write(bytes) != bytes.size() || !temporary.flush()) {
        return std::unexpected(ConnectorError{
            ErrorCode::ParseFailed, QStringLiteral("Cannot stage the XLSX statement safely"),
            temporary.errorString(), false});
    }
    const auto temporaryPath = temporary.fileName();
    temporary.close();

    using Reader = std::remove_pointer_t<xlsxioreader>;
    const std::unique_ptr<Reader, decltype(&xlsxioread_close)> reader(
        xlsxioread_open(temporaryPath.toUtf8().constData()), &xlsxioread_close);
    if (!reader) {
        return std::unexpected(ConnectorError{
            ErrorCode::ParseFailed, QStringLiteral("Cannot open the XLSX workbook"), {}, false});
    }
    using Sheet = std::remove_pointer_t<xlsxioreadersheet>;
    const std::unique_ptr<Sheet, decltype(&xlsxioread_sheet_close)> sheet(
        xlsxioread_sheet_open(reader.get(), nullptr, XLSXIOREAD_SKIP_EMPTY_ROWS),
        &xlsxioread_sheet_close);
    if (!sheet) {
        return std::unexpected(ConnectorError{
            ErrorCode::ParseFailed, QStringLiteral("The XLSX workbook contains no readable sheet"),
            {}, false});
    }

    constexpr qsizetype maximumRows = 250'000;
    constexpr qsizetype maximumColumns = 512;
    QVector<QStringList> records;
    while (xlsxioread_sheet_next_row(sheet.get())) {
        if (records.size() >= maximumRows) {
            return std::unexpected(ConnectorError{
                ErrorCode::SecurityViolation,
                QStringLiteral("The XLSX workbook exceeds the safe row limit"), {}, false});
        }
        QStringList row;
        while (char *cell = xlsxioread_sheet_next_cell(sheet.get())) {
            const std::unique_ptr<char, decltype(&std::free)> value(cell, &std::free);
            if (row.size() >= maximumColumns) {
                return std::unexpected(ConnectorError{
                    ErrorCode::SecurityViolation,
                    QStringLiteral("The XLSX workbook exceeds the safe column limit"), {}, false});
            }
            if (std::char_traits<char>::length(value.get()) > 1024 * 1024) {
                return std::unexpected(ConnectorError{
                    ErrorCode::SecurityViolation,
                    QStringLiteral("The XLSX workbook contains an oversized cell"), {}, false});
            }
            row.push_back(QString::fromUtf8(value.get()).trimmed());
        }
        records.push_back(std::move(row));
    }
    if (records.size() < 2) {
        return std::unexpected(ConnectorError{
            ErrorCode::ParseFailed, QStringLiteral("The XLSX statement has no data rows"), {},
            false});
    }
    ParsedTable table;
    table.format = FileFormat::Xlsx;
    table.columns = records.takeFirst();
    table.rows = std::move(records);
    return table;
}

std::optional<std::int64_t> parseMinorUnits(QString value)
{
    value = value.trimmed();
    const bool negative =
        value.startsWith(u'-') || value.startsWith(u'\u2212') || value.endsWith(u'-')
        || (value.startsWith(u'(') && value.endsWith(u')'));
    value.remove(QRegularExpression(QStringLiteral("[^0-9,.]")));
    if (value.isEmpty()) {
        return std::nullopt;
    }

    const auto comma = value.lastIndexOf(u',');
    const auto dot = value.lastIndexOf(u'.');
    const auto separator = std::max(comma, dot);
    QString whole = value;
    QString fraction;
    if (separator >= 0 && value.size() - separator - 1 <= 2) {
        whole = value.first(separator);
        fraction = value.sliced(separator + 1);
    }
    whole.remove(u',');
    whole.remove(u'.');
    while (fraction.size() < 2) {
        fraction += u'0';
    }
    if (fraction.size() > 2) {
        fraction = fraction.first(2);
    }
    bool wholeOk = false;
    bool fractionOk = false;
    const auto wholeValue = whole.isEmpty() ? 0 : whole.toLongLong(&wholeOk);
    const auto fractionValue = fraction.isEmpty() ? 0 : fraction.toLongLong(&fractionOk);
    if ((!whole.isEmpty() && !wholeOk) || (!fraction.isEmpty() && !fractionOk)
        || wholeValue > (std::numeric_limits<std::int64_t>::max() - 99) / 100) {
        return std::nullopt;
    }
    const auto result = wholeValue * 100 + fractionValue;
    return negative ? -result : result;
}

std::optional<QDate> parseDate(QString value, const QStringList &customFormats)
{
    value = value.trimmed();
    value.replace(u'\'', u'/');
    const QStringList formats =
        customFormats
        + QStringList{QStringLiteral("dd.MM.yyyy"), QStringLiteral("dd/MM/yyyy"),
                      QStringLiteral("dd-MM-yyyy"), QStringLiteral("yyyy-MM-dd"),
                      QStringLiteral("M/d/yyyy"), QStringLiteral("M/d/yy"),
                      QStringLiteral("dd.MM.yyyy HH:mm"),
                      QStringLiteral("dd.MM.yyyy HH:mm:ss")};
    for (const auto &format : formats) {
        const auto date = QDate::fromString(value, format);
        if (date.isValid()) {
            return date;
        }
        const auto dateTime = QDateTime::fromString(value, format);
        if (dateTime.isValid()) {
            return dateTime.date();
        }
    }
    const auto iso = QDateTime::fromString(value, Qt::ISODate);
    if (iso.isValid()) {
        return iso.date();
    }
    return std::nullopt;
}

QString valueAt(const QStringList &row, const int index)
{
    return index >= 0 && index < row.size() ? row.at(index).trimmed() : QString{};
}

void setMappingField(ColumnMapping &mapping, const QString &field, const int index)
{
    if (field == u"date") {
        mapping.date = index;
    } else if (field == u"amount") {
        mapping.amount = index;
    } else if (field == u"debit") {
        mapping.debit = index;
    } else if (field == u"credit") {
        mapping.credit = index;
    } else if (field == u"description") {
        mapping.description = index;
    } else if (field == u"merchant") {
        mapping.merchant = index;
    } else if (field == u"currency") {
        mapping.currency = index;
    } else if (field == u"category") {
        mapping.category = index;
    } else if (field == u"externalId") {
        mapping.externalId = index;
    } else if (field == u"account") {
        mapping.account = index;
    } else if (field == u"balance") {
        mapping.balance = index;
    }
}

} // namespace

QString decodeStatementText(const QByteArray &bytes)
{
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        return QString::fromUtf8(bytes.sliced(3));
    }
    if (bytes.startsWith("\xFF\xFE")) {
        QStringDecoder decoder(QStringConverter::Utf16LE);
        return decoder(bytes.sliced(2));
    }
    if (bytes.startsWith("\xFE\xFF")) {
        QStringDecoder decoder(QStringConverter::Utf16BE);
        return decoder(bytes.sliced(2));
    }
    return isValidUtf8(bytes) ? QString::fromUtf8(bytes) : decodeCp1251(bytes);
}

FileFormat detectFileFormat(const ImportSource &source, const QByteArray &bytes)
{
    const auto extension = QFileInfo(source.effectiveName()).suffix().toLower();
    const auto trimmed = bytes.trimmed();
    const auto textHead = decodeStatementText(bytes.first(64 * 1024));
    if (bytes.startsWith("%PDF-") || extension == u"pdf") {
        return FileFormat::Pdf;
    }
    if ((bytes.startsWith("PK\x03\x04") && extension == u"xlsx") || extension == u"xlsx") {
        return FileFormat::Xlsx;
    }
    if (textHead.contains(QStringLiteral("1CClientBankExchange"), Qt::CaseInsensitive)) {
        return FileFormat::OneC;
    }
    if (textHead.contains(QStringLiteral("<OFX>"), Qt::CaseInsensitive)
        || textHead.contains(QStringLiteral("OFXHEADER:"), Qt::CaseInsensitive)
        || extension == u"ofx") {
        return FileFormat::Ofx;
    }
    if (trimmed.startsWith("!Type:") || extension == u"qif") {
        return FileFormat::Qif;
    }
    if (extension == u"csv" || extension == u"tsv" || extension == u"txt"
        || textHead.contains(u';') || textHead.contains(u'\t') || textHead.contains(u',')) {
        return FileFormat::Csv;
    }
    return FileFormat::Unknown;
}

QString normalizedHeader(QString value)
{
    value = value.normalized(QString::NormalizationForm_KC).toCaseFolded().trimmed();
    value.replace(u'ё', u'е');
    value.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")),
                  QStringLiteral(" "));
    return value.simplified();
}

ColumnMapping inferColumnMapping(const QStringList &columns, const BankPreset &preset)
{
    ColumnMapping mapping;
    for (int index = 0; index < columns.size(); ++index) {
        const auto header = normalizedHeader(columns.at(index));
        for (auto it = preset.columnAliases.cbegin(); it != preset.columnAliases.cend(); ++it) {
            if (std::ranges::any_of(it.value(), [&header](const QString &alias) {
                    return header == normalizedHeader(alias);
                })) {
                setMappingField(mapping, it.key(), index);
            }
        }
    }
    return mapping;
}

int mappingScore(const ColumnMapping &mapping)
{
    int score = mapping.date >= 0 ? 3 : 0;
    score += mapping.amount >= 0 || mapping.debit >= 0 || mapping.credit >= 0 ? 3 : 0;
    score += mapping.description >= 0 ? 1 : 0;
    score += mapping.externalId >= 0 ? 1 : 0;
    score += mapping.account >= 0 ? 1 : 0;
    return score;
}

ConnectorResult<ParsedTable> parseStatement(const ImportSource &source,
                                            const ImportOptions &options,
                                            const BankPreset &preset)
{
    const auto bytes = readImportSource(source);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    const auto format = detectFileFormat(source, *bytes);
    if (format == FileFormat::Unknown) {
        return std::unexpected(ConnectorError{
            ErrorCode::UnsupportedFormat, QStringLiteral("Unknown statement file format"), {},
            false});
    }
    if (format == FileFormat::Pdf) {
        return std::unexpected(ConnectorError{
            ErrorCode::UnsupportedFormat,
            QStringLiteral("PDF statements require the reviewed Qt PDF import backend"),
            QStringLiteral("Use CSV/XLSX where possible; PDF rows must never be auto-committed"),
            false});
    }

    const auto text = decodeStatementText(*bytes);
    ParsedTable table;
    if (format == FileFormat::Xlsx) {
        auto parsed = parseXlsx(*bytes);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        table = std::move(*parsed);
    } else if (format == FileFormat::Ofx) {
        table = parseOfx(text);
    } else if (format == FileFormat::Qif) {
        table = parseQif(text);
    } else if (format == FileFormat::OneC) {
        table = parseOneC(text);
    } else {
        table.format = FileFormat::Csv;
        const auto delimiter = options.delimiter.isNull() ? detectDelimiter(text) : options.delimiter;
        bool wellFormed = false;
        auto records = parseDelimited(text, delimiter, wellFormed);
        if (!wellFormed) {
            return std::unexpected(ConnectorError{
                ErrorCode::ParseFailed,
                QStringLiteral("The delimited statement contains an unterminated quoted field"),
                {}, false});
        }
        if (records.size() < 2) {
            return std::unexpected(ConnectorError{
                ErrorCode::ParseFailed,
                QStringLiteral("The delimited statement has no data rows"), {}, false});
        }
        if (records.size() > 250'001
            || std::ranges::any_of(records,
                                   [](const QStringList &row) { return row.size() > 512; })) {
            return std::unexpected(ConnectorError{
                ErrorCode::SecurityViolation,
                QStringLiteral("The delimited statement exceeds the safe row or column limit"), {},
                false});
        }
        table.columns = records.takeFirst();
        table.rows = std::move(records);
    }

    auto bestMapping = inferColumnMapping(table.columns, preset);
    int bestScore = mappingScore(bestMapping);
    qsizetype headerRow = 0;
    const auto rowsToInspect = std::min<qsizetype>(20, table.rows.size());
    for (qsizetype index = 0; index < rowsToInspect; ++index) {
        const auto candidate = inferColumnMapping(table.rows.at(index), preset);
        const auto score = mappingScore(candidate);
        if (score > bestScore) {
            bestScore = score;
            bestMapping = candidate;
            headerRow = index + 1;
        }
    }
    if (headerRow > 0) {
        table.columns = table.rows.at(headerRow - 1);
        table.rows.erase(table.rows.begin(), table.rows.begin() + headerRow);
        table.firstDataSourceRow += headerRow;
        table.warnings.push_back(
            {WarningSeverity::Information, QStringLiteral("statement-preamble-skipped"),
             QStringLiteral("Skipped %1 statement preamble row(s)").arg(headerRow), -1});
    }
    table.sourceSha256 = QCryptographicHash::hash(*bytes, QCryptographicHash::Sha256);
    table.inferredMapping = bestMapping;
    if (preset.provisional) {
        table.warnings.push_back(
            {WarningSeverity::ReviewRequired, QStringLiteral("provisional-bank-preset"),
             QStringLiteral("This bank preset is provisional; review the imported rows"), -1});
    }
    return table;
}

ConnectorResult<NormalizedBatch> normalizeStatement(const ParsedTable &table,
                                                    const ImportOptions &options,
                                                    const BankPreset &preset)
{
    const auto mapping = options.mapping.value_or(table.inferredMapping);
    if (mapping.date < 0
        || (mapping.amount < 0 && mapping.debit < 0 && mapping.credit < 0)) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput,
            QStringLiteral("Map at least the date and amount columns before importing"), {}, false});
    }

    NormalizedBatch batch;
    batch.warnings = table.warnings;
    const auto limit =
        options.previewRowLimit <= 0 ? table.rows.size()
                                     : std::min(options.previewRowLimit, table.rows.size());
    for (qsizetype rowIndex = 0; rowIndex < limit; ++rowIndex) {
        const auto &row = table.rows.at(rowIndex);
        const auto date = parseDate(valueAt(row, mapping.date), options.acceptedDateFormats);
        auto rawAmount = valueAt(row, mapping.amount);
        bool amountFromDebit = false;
        if (rawAmount.isEmpty() && mapping.debit >= 0) {
            rawAmount = valueAt(row, mapping.debit);
            amountFromDebit = !rawAmount.isEmpty();
        }
        if (rawAmount.isEmpty() && mapping.credit >= 0) {
            rawAmount = valueAt(row, mapping.credit);
        }
        auto amount = parseMinorUnits(rawAmount);
        if (!date || !amount) {
            batch.warnings.push_back(
                {WarningSeverity::RejectedRow, QStringLiteral("invalid-required-field"),
                 QStringLiteral("Row has an invalid date or amount"),
                 table.firstDataSourceRow + rowIndex});
            continue;
        }
        if (amountFromDebit && *amount > 0) {
            *amount = -*amount;
        }
        if (options.reverseAmountSign) {
            *amount = -*amount;
        }

        auto currency = valueAt(row, mapping.currency).toUpper();
        currency.remove(QRegularExpression(QStringLiteral("[^A-ZА-Я]")));
        if (currency == QStringLiteral("РУБ") || currency == QStringLiteral("РУБЛЬ")
            || currency == QStringLiteral("RUR") || currency == QStringLiteral("₽")) {
            currency = QStringLiteral("RUB");
        }
        if (currency.isEmpty()) {
            currency = options.defaultCurrency.toUpper();
        }

        auto accountId = valueAt(row, mapping.account);
        if (accountId.isEmpty()) {
            accountId = options.accountExternalId;
        }
        if (accountId.isEmpty()) {
            accountId = preset.institution + QStringLiteral(":imported");
        }

        Provenance provenance{preset.connectorId,
                              table.sourceSha256,
                              valueAt(row, mapping.externalId),
                              table.firstDataSourceRow + rowIndex};
        NormalizedTransaction transaction{
            valueAt(row, mapping.externalId),
            accountId,
            *date,
            std::nullopt,
            {*amount, currency},
            valueAt(row, mapping.description),
            valueAt(row, mapping.merchant),
            valueAt(row, mapping.category),
            {},
            {},
            provenance,
        };
        for (int column = 0; column < std::min(table.columns.size(), row.size()); ++column) {
            transaction.sourceFields.insert(table.columns.at(column), row.at(column));
        }
        batch.transactions.push_back(std::move(transaction));

        if (mapping.balance >= 0) {
            if (const auto balance = parseMinorUnits(valueAt(row, mapping.balance)); balance) {
                batch.balances.push_back(
                    {accountId, QDateTime(*date, QTime(23, 59, 59), QTimeZone::UTC),
                     {*balance, currency}, provenance});
            }
        }
    }
    return batch;
}

} // namespace wealthtracker::connectors::detail
