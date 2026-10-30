module;

// Keep Qt's instantiated friend templates in the global module on NDK Clang.
extern "C++" {
#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string_view>
}

export module wt.connectors;

export namespace wealthtracker::connectors {

inline constexpr int connector_contract_version = 1;

enum class Capability { FileImport, AccountSync, InvestmentSync, MarketData };
enum class ErrorCode {
    InvalidInput,
    UnsupportedFormat,
    ParseFailed,
    SecurityViolation,
    NetworkFailure,
    AuthenticationFailed,
    RateLimited,
    RemoteFailure,
    Cancelled,
};

struct ConnectorError {
    ErrorCode code{ErrorCode::InvalidInput};
    QString message;
    QString detail;
    bool retryable{false};
};

template <typename T>
using ConnectorResult = std::expected<T, ConnectorError>;

struct ConnectorManifest {
    QString id;
    QString institution;
    QString displayName;
    QString version;
    QVector<Capability> capabilities;
    QJsonObject configurationSchema;
    QStringList allowedHosts;
};

struct Provenance {
    QString connectorId;
    QByteArray sourceFileSha256;
    QString sourceRecordId;
    qsizetype sourceRow{-1};
};

struct Money {
    std::int64_t minorUnits{0};
    QString currency{"RUB"};
    friend bool operator==(const Money &, const Money &) = default;
};

struct ExternalAccount {
    QString externalId;
    QString name;
    QString kind;
    QString currency{"RUB"};
    QString maskedNumber;
    bool closed{false};
    Provenance provenance;
};

struct BalanceSnapshot {
    QString accountExternalId;
    QDateTime observedAt;
    Money balance;
    Provenance provenance;
};

enum class NormalizedTransactionKind { Regular, InternalTransfer };

struct NormalizedTransaction {
    QString externalId;
    QString accountExternalId;
    QDate postingDate;
    std::optional<QDateTime> occurredAt;
    Money amount;
    QString description;
    QString merchant;
    QString bankCategory;
    QString mcc;
    QMap<QString, QString> sourceFields;
    Provenance provenance;
    NormalizedTransactionKind kind{NormalizedTransactionKind::Regular};
};

struct InvestmentEvent {
    QString externalId;
    QString accountExternalId;
    QDateTime occurredAt;
    QString type;
    QString instrumentId;
    Money payment;
    QString quantityMantissa;
    int quantityScale{0};
    Provenance provenance;
};

struct Position {
    QString accountExternalId;
    QString instrumentId;
    QString quantityMantissa;
    int quantityScale{0};
    Money currentValue;
    QDateTime observedAt;
    Provenance provenance;
};

struct Quote {
    QString instrumentId;
    Money price;
    QDateTime observedAt;
    Provenance provenance;
};

enum class WarningSeverity { Information, ReviewRequired, RejectedRow };

struct ImportWarning {
    WarningSeverity severity{WarningSeverity::Information};
    QString code;
    QString message;
    qsizetype sourceRow{-1};
};

struct NormalizedBatch {
    int contractVersion{connector_contract_version};
    QVector<ExternalAccount> accounts;
    QVector<BalanceSnapshot> balances;
    QVector<NormalizedTransaction> transactions;
    QVector<InvestmentEvent> investmentEvents;
    QVector<Position> positions;
    QVector<Quote> quotes;
    QVector<ImportWarning> warnings;
    QByteArray nextCursor;
};

class IConnector {
public:
    virtual ~IConnector() = default;
    [[nodiscard]] virtual ConnectorManifest manifest() const = 0;
};

enum class FileFormat { Unknown, Csv, Xlsx, Ofx, Qif, OneC, Pdf };

struct ImportSource {
    QString filePath;
    QString displayName;
    QByteArray content;
    [[nodiscard]] QString effectiveName() const;
};

struct ProbeResult {
    FileFormat format{FileFormat::Unknown};
    int confidence{0};
    QString reason;
    bool requiresReview{false};
};

struct ColumnMapping {
    int date{-1};
    int amount{-1};
    int debit{-1};
    int credit{-1};
    int description{-1};
    int merchant{-1};
    int currency{-1};
    int category{-1};
    int externalId{-1};
    int account{-1};
    int balance{-1};
};

struct ImportOptions {
    QString accountExternalId;
    QString defaultCurrency{"RUB"};
    std::optional<ColumnMapping> mapping;
    QChar delimiter;
    QStringList acceptedDateFormats;
    bool reverseAmountSign{false};
    qsizetype previewRowLimit{0};
};

struct ParsedTable {
    FileFormat format{FileFormat::Unknown};
    QStringList columns;
    QVector<QStringList> rows;
    ColumnMapping inferredMapping;
    QVector<ImportWarning> warnings;
    QByteArray sourceSha256;
    qsizetype firstDataSourceRow{2};
};

struct ImportPreview {
    ProbeResult probe;
    NormalizedBatch sample;
    qsizetype parsedRowCount{0};
    bool requiresConfirmation{false};
};

class IImportConnector : public IConnector {
public:
    ~IImportConnector() override = default;
    [[nodiscard]] virtual ProbeResult probe(const ImportSource &source) const = 0;
    [[nodiscard]] virtual ConnectorResult<ParsedTable>
    parse(const ImportSource &source, const ImportOptions &options) const = 0;
    [[nodiscard]] virtual ConnectorResult<NormalizedBatch>
    normalize(const ParsedTable &table, const ImportOptions &options) const = 0;
    [[nodiscard]] ConnectorResult<ImportPreview>
    preview(const ImportSource &source, const ImportOptions &options) const;
};

[[nodiscard]] ConnectorResult<QByteArray>
readImportSource(const ImportSource &source, qsizetype maximumBytes = 50 * 1024 * 1024);
[[nodiscard]] std::shared_ptr<IImportConnector> makeGenericImportConnector();
[[nodiscard]] QVector<std::shared_ptr<IImportConnector>> makeBuiltInImportConnectors();
[[nodiscard]] std::shared_ptr<IImportConnector>
selectBestImportConnector(const QVector<std::shared_ptr<IImportConnector>> &connectors,
                          const ImportSource &source, int minimumConfidence = 25);

struct LiveSyncRequest {
    QString accountExternalId;
    QByteArray cursor;
    std::optional<QDateTime> from;
    std::optional<QDateTime> to;
    int pageSize{1000};
};

class ILiveConnector : public IConnector {
public:
    ~ILiveConnector() override = default;
    [[nodiscard]] virtual ConnectorResult<QNetworkReply *>
    discoverAccounts(const QByteArray &readOnlyCredential) = 0;
    [[nodiscard]] virtual ConnectorResult<QVector<QNetworkReply *>>
    synchronize(const LiveSyncRequest &request, const QByteArray &readOnlyCredential) = 0;
};

enum class ZenPluginPreferenceKind {
    Text,
    Password,
    Phone,
    Number,
    Date,
    Checkbox,
    Choice,
};

struct ZenPluginPreference {
    QString key;
    QString label;
    ZenPluginPreferenceKind kind{ZenPluginPreferenceKind::Text};
    QString defaultValue;
    QStringList choices;
    bool required{false};
    bool sensitive{false};
};

struct ZenPluginDescriptor {
    QString pluginId;
    QString bundlePath;
    QString preferencesPath;
    QVector<ZenPluginPreference> preferences;
};

struct ZenPluginRunRequest {
    ZenPluginDescriptor descriptor;
    QJsonObject preferences;
    QJsonObject state;
    QDateTime from;
    QDateTime to;
    QStringList allowedOrigins;
};

struct ZenPluginPrompt {
    QString requestId;
    QString message;
    QString inputType;
    int timeoutMilliseconds{120000};
};

enum class ZenPluginEventKind { Prompt, Result, Error };

struct ZenPluginEvent {
    ZenPluginEventKind kind{ZenPluginEventKind::Error};
    ZenPluginPrompt prompt;
    NormalizedBatch batch;
    QJsonObject state;
    QString message;
    bool retryable{false};
};

[[nodiscard]] ConnectorResult<ZenPluginDescriptor>
inspectZenPluginBundle(const QString &bundlePath);
[[nodiscard]] ConnectorResult<QByteArray>
makeZenPluginStartMessage(const ZenPluginRunRequest &request);
[[nodiscard]] ConnectorResult<QByteArray>
makeZenPluginInputMessage(const QString &requestId, const QByteArray &value);
[[nodiscard]] ConnectorResult<ZenPluginEvent>
parseZenPluginEvent(const QByteArray &line, const QString &pluginId,
                    const QDateTime &observedAt = QDateTime::currentDateTimeUtc());

enum class RussianBank { Generic, TBank, AlfaBank, Sber, Vtb };

struct BankPreset {
    RussianBank bank{RussianBank::Generic};
    QString connectorId;
    QString institution;
    QString displayName;
    QStringList identifyingTokens;
    QHash<QString, QStringList> columnAliases;
    bool provisional{true};
};

[[nodiscard]] BankPreset genericPreset();
[[nodiscard]] QVector<BankPreset> russianBankPresets();
[[nodiscard]] std::shared_ptr<IImportConnector> makeBankPresetConnector(const BankPreset &preset);

enum class DuplicateDisposition { NewRecord, ExactDuplicate, AmbiguousMatch };

struct DuplicateDecision {
    DuplicateDisposition disposition{DuplicateDisposition::NewRecord};
    QByteArray canonicalFingerprint;
    QString reason;
};

struct DeduplicationIndex {
    QSet<QByteArray> externalIds;
    QSet<QByteArray> provenanceKeys;
    QHash<QByteArray, qsizetype> canonicalFingerprintCounts;
};

[[nodiscard]] QByteArray canonicalTransactionFingerprint(const NormalizedTransaction &transaction);
[[nodiscard]] QByteArray provenanceKey(const Provenance &provenance);
[[nodiscard]] DuplicateDecision classifyDuplicate(const NormalizedTransaction &transaction,
                                                  const DeduplicationIndex &index);
void addToDeduplicationIndex(const NormalizedTransaction &transaction,
                             DeduplicationIndex &index);

class HostAllowlist {
public:
    explicit HostAllowlist(QStringList allowedHosts);
    [[nodiscard]] bool permits(const QUrl &url) const;
    [[nodiscard]] ConnectorResult<QNetworkRequest> makeRequest(const QUrl &url) const;
    [[nodiscard]] QStringList allowedHosts() const;

private:
    QSet<QString> hosts_;
};

enum class TInvestReadMethod { Accounts, Portfolio };

class TInvestClient final : public IConnector {
public:
    explicit TInvestClient(QNetworkAccessManager &network);
    [[nodiscard]] ConnectorManifest manifest() const override;
    [[nodiscard]] ConnectorResult<QNetworkReply *>
    post(TInvestReadMethod method, const QJsonObject &body, const QByteArray &readOnlyToken);
    [[nodiscard]] static QString restPath(TInvestReadMethod method);
    [[nodiscard]] static bool isReadOnlyPath(QStringView path);
    [[nodiscard]] static ConnectorResult<NormalizedBatch>
    parseAccountsResponse(const QByteArray &json);
    [[nodiscard]] static ConnectorResult<NormalizedBatch>
    parsePortfolioResponse(const QByteArray &json, const QString &accountExternalId,
                           const QDateTime &observedAt);

private:
    QNetworkAccessManager &network_;
    HostAllowlist allowlist_;
};

enum class AlfaBankReadMethod { Accounts, Operations };

class AlfaBankClient final : public ILiveConnector {
public:
    explicit AlfaBankClient(QNetworkAccessManager &network);
    [[nodiscard]] ConnectorManifest manifest() const override;
    [[nodiscard]] ConnectorResult<QNetworkReply *>
    discoverAccounts(const QByteArray &readOnlyToken) override;
    [[nodiscard]] ConnectorResult<QVector<QNetworkReply *>>
    synchronize(const LiveSyncRequest &request, const QByteArray &readOnlyToken) override;
    [[nodiscard]] static QString restPath(AlfaBankReadMethod method);
    [[nodiscard]] static bool isReadOnlyPath(QStringView path);
    [[nodiscard]] static ConnectorResult<NormalizedBatch>
    parseAccountsResponse(const QByteArray &json, const QDateTime &observedAt);
    [[nodiscard]] static ConnectorResult<NormalizedBatch>
    parseOperationsResponse(const QByteArray &json, const QString &accountExternalId,
                            int currentOffset, int requestedPageSize);

private:
    [[nodiscard]] ConnectorResult<QNetworkReply *>
    get(const QUrl &url, const QByteArray &readOnlyToken);

    QNetworkAccessManager &network_;
    HostAllowlist allowlist_;
};

enum class TBankBusinessReadMethod { Accounts, Statement };

class TBankBusinessClient final : public ILiveConnector {
public:
    explicit TBankBusinessClient(QNetworkAccessManager &network);
    [[nodiscard]] ConnectorManifest manifest() const override;
    [[nodiscard]] ConnectorResult<QNetworkReply *>
    discoverAccounts(const QByteArray &readOnlyToken) override;
    [[nodiscard]] ConnectorResult<QVector<QNetworkReply *>>
    synchronize(const LiveSyncRequest &request, const QByteArray &readOnlyToken) override;
    [[nodiscard]] static QString restPath(TBankBusinessReadMethod method);
    [[nodiscard]] static bool isReadOnlyPath(QStringView path);
    [[nodiscard]] static ConnectorResult<NormalizedBatch>
    parseAccountsResponse(const QByteArray &json, const QDateTime &observedAt);
    [[nodiscard]] static ConnectorResult<NormalizedBatch>
    parseStatementResponse(const QByteArray &json, const QString &accountExternalId);

private:
    [[nodiscard]] ConnectorResult<QNetworkReply *>
    get(const QUrl &url, const QByteArray &readOnlyToken);

    QNetworkAccessManager &network_;
    HostAllowlist allowlist_;
};

enum class ModulbankReadMethod { Accounts, OperationHistory };

class ModulbankClient final : public ILiveConnector {
public:
    explicit ModulbankClient(QNetworkAccessManager &network);
    [[nodiscard]] ConnectorManifest manifest() const override;
    [[nodiscard]] ConnectorResult<QNetworkReply *>
    discoverAccounts(const QByteArray &readOnlyToken) override;
    [[nodiscard]] ConnectorResult<QVector<QNetworkReply *>>
    synchronize(const LiveSyncRequest &request, const QByteArray &readOnlyToken) override;
    [[nodiscard]] static QString restPath(ModulbankReadMethod method,
                                          QStringView accountExternalId = {});
    [[nodiscard]] static bool isReadOnlyPath(QStringView path);
    [[nodiscard]] static ConnectorResult<NormalizedBatch>
    parseAccountsResponse(const QByteArray &json, const QDateTime &observedAt);
    [[nodiscard]] static ConnectorResult<NormalizedBatch>
    parseOperationsResponse(const QByteArray &json, const QString &accountExternalId,
                            int currentOffset, int requestedPageSize);

private:
    [[nodiscard]] ConnectorResult<QNetworkReply *>
    post(const QUrl &url, const QJsonObject &body, const QByteArray &readOnlyToken);

    QNetworkAccessManager &network_;
    HostAllowlist allowlist_;
};

[[nodiscard]] bool isValidTBankRetailSessionId(std::string_view value);

class TBankRetailClient final : public IConnector {
public:
    explicit TBankRetailClient(QNetworkAccessManager &network);
    [[nodiscard]] ConnectorManifest manifest() const override;
    [[nodiscard]] ConnectorResult<QNetworkReply *>
    discoverAccounts(const QByteArray &sessionId);
    [[nodiscard]] static bool isReadOnlyPath(QStringView path);
    [[nodiscard]] static ConnectorResult<NormalizedBatch>
    parseAccountsResponse(const QByteArray &json, const QDateTime &observedAt);

private:
    [[nodiscard]] ConnectorResult<QNetworkReply *>
    get(const QUrl &url, const QByteArray &sessionId);

    QNetworkAccessManager &network_;
    HostAllowlist allowlist_;
};

enum class RetailBank { Alfa, Vtb };

[[nodiscard]] ConnectorResult<NormalizedBatch>
parseRetailAccounts(const QJsonArray &accounts, const QDateTime &observedAt,
                    RetailBank bank);

} // namespace wealthtracker::connectors

namespace wealthtracker::connectors::detail {

[[nodiscard]] QString decodeStatementText(const QByteArray &bytes);
[[nodiscard]] FileFormat detectFileFormat(const ImportSource &source, const QByteArray &bytes);
[[nodiscard]] ColumnMapping inferColumnMapping(const QStringList &columns,
                                               const BankPreset &preset);
[[nodiscard]] ConnectorResult<ParsedTable>
parseStatement(const ImportSource &source, const ImportOptions &options, const BankPreset &preset);
[[nodiscard]] ConnectorResult<NormalizedBatch>
normalizeStatement(const ParsedTable &table, const ImportOptions &options,
                   const BankPreset &preset);
[[nodiscard]] QString normalizedHeader(QString value);

} // namespace wealthtracker::connectors::detail
