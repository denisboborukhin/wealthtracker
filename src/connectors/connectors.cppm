module;

// Keep Qt's instantiated friend templates in the global module on NDK Clang.
extern "C++" {
#include <QByteArray>
#include <QDate>
#include <QDateTime>
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
#include <optional>
#include <string_view>
}

export module wt.connectors;

export namespace wealthtracker::connectors {

inline constexpr int connector_contract_version = 1;

enum class Capability { AccountSync = 1, InvestmentSync, MarketData };
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
    QString ticker{};
    QString instrumentName{};
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

enum class TInvestReadMethod { Accounts, Portfolio, Instrument };

struct InstrumentMetadata {
    QString instrumentId;
    QString ticker;
    QString name;
};

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
    [[nodiscard]] static ConnectorResult<InstrumentMetadata>
    parseInstrumentResponse(const QByteArray &json, const QString &instrumentId);

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
