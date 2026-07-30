module;

#include <QDate>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>

#include <expected>

module wt.connectors;

namespace wealthtracker::connectors {
namespace {

constexpr auto primaryHost = "www.cbr.ru";
constexpr qsizetype maximumXmlBytes = 2 * 1024 * 1024;

} // namespace

CbrFxClient::CbrFxClient(QNetworkAccessManager &network)
    : network_(network),
      allowlist_({QStringLiteral("www.cbr.ru"), QStringLiteral("cbr.ru")})
{
}

ConnectorManifest CbrFxClient::manifest() const
{
    return {QStringLiteral("cbr-fx-v1"),
            QStringLiteral("cbr"),
            QStringLiteral("Курсы Банка России"),
            QStringLiteral("1.0.0"),
            {Capability::MarketData},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                        {QStringLiteral("additionalProperties"), false}},
            {QStringLiteral("www.cbr.ru"), QStringLiteral("cbr.ru")}};
}

ConnectorResult<QNetworkReply *> CbrFxClient::requestDailyRates(const QDate &date)
{
    if (!date.isValid()) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput, QStringLiteral("A valid FX rate date is required"), {}, false});
    }
    QUrl url(QStringLiteral("https://%1/scripts/XML_daily.asp")
                 .arg(QString::fromLatin1(primaryHost)));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("date_req"), date.toString(QStringLiteral("dd/MM/yyyy")));
    url.setQuery(query);
    auto request = allowlist_.makeRequest(url);
    if (!request) {
        return std::unexpected(request.error());
    }
    request->setRawHeader("Accept", "application/xml,text/xml");
    return network_.get(*request);
}

ConnectorResult<QVector<FxRate>> CbrFxClient::parseDailyRates(const QByteArray &xml)
{
    if (xml.isEmpty() || xml.size() > maximumXmlBytes) {
        return std::unexpected(ConnectorError{
            ErrorCode::SecurityViolation,
            QStringLiteral("The Bank of Russia response is empty or exceeds the safety limit"), {},
            false});
    }

    const auto decoded = detail::decodeStatementText(xml);
    QXmlStreamReader reader(decoded);
    QVector<FxRate> rates;
    QDate effectiveDate;
    FxRate current;
    bool insideValute = false;
    qsizetype elementCount = 0;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::DTD) {
            return std::unexpected(ConnectorError{
                ErrorCode::SecurityViolation,
                QStringLiteral("DTD declarations are not accepted in FX responses"), {}, false});
        }
        if (token == QXmlStreamReader::StartElement) {
            if (++elementCount > 20'000) {
                return std::unexpected(ConnectorError{
                    ErrorCode::SecurityViolation,
                    QStringLiteral("The FX response contains too many XML elements"), {}, false});
            }
            if (reader.name() == u"ValCurs") {
                effectiveDate =
                    QDate::fromString(reader.attributes().value(u"Date").toString(),
                                      QStringLiteral("dd.MM.yyyy"));
            } else if (reader.name() == u"Valute") {
                insideValute = true;
                current = FxRate{};
            } else if (insideValute) {
                const auto name = reader.name().toString();
                const auto value = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                if (name == u"CharCode") {
                    current.currency = value.toUpper();
                } else if (name == u"Nominal") {
                    bool ok = false;
                    current.nominal = value.toInt(&ok);
                    if (!ok) {
                        current.nominal = 0;
                    }
                } else if (name == u"Value") {
                    current.rublesPerNominal = value;
                    current.rublesPerNominal.replace(u',', u'.');
                }
            }
        } else if (token == QXmlStreamReader::EndElement && reader.name() == u"Valute") {
            insideValute = false;
            current.effectiveDate = effectiveDate;
            static const QRegularExpression isoCurrency(QStringLiteral("^[A-Z]{3}$"));
            static const QRegularExpression decimal(QStringLiteral("^[0-9]+(?:\\.[0-9]+)?$"));
            if (isoCurrency.match(current.currency).hasMatch() && current.nominal > 0
                && decimal.match(current.rublesPerNominal).hasMatch()) {
                rates.push_back(current);
            }
        }
    }
    if (reader.hasError()) {
        return std::unexpected(ConnectorError{
            ErrorCode::ParseFailed, QStringLiteral("Cannot parse the Bank of Russia FX response"),
            reader.errorString(), false});
    }
    if (!effectiveDate.isValid() || rates.isEmpty()) {
        return std::unexpected(ConnectorError{
            ErrorCode::ParseFailed,
            QStringLiteral("The Bank of Russia response contains no usable daily rates"), {}, false});
    }
    return rates;
}

} // namespace wealthtracker::connectors
