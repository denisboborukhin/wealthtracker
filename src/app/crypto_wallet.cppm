module;

// Keep Qt template declarations in the global module on NDK Clang.
extern "C++" {
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrlQuery>
#include <QVariantList>
#include <QVariantMap>

#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <limits>
#include <optional>

}

export module wt.crypto_wallet;
import wt.core.decimal;
import wt.connectors;

export namespace wealthtracker::app {
inline constexpr auto ethereumUsdtContract = "0xdac17f958d2ee523a2206206994597c13d831ec7";

bool valid_ethereum_address(const QString& address) {
    static const QRegularExpression pattern(QStringLiteral("^0x[0-9a-fA-F]{40}$"));
    return pattern.match(address).hasMatch();
}

struct CryptoPortfolio {
    QVariantList holdings;
    std::int64_t usdtMinor = 0;
    int unpricedCount = 0;
    QStringList supportedChains;
    QStringList usedChains;
};

QString usdt_label(const std::int64_t value) {
    return QStringLiteral("%1.%2 USDT").arg(value / 100).arg(value % 100, 2, 10, QLatin1Char('0'));
}

// Preserve numeric JSON lexemes before Qt can round monetary quotes to doubles.
std::expected<QJsonDocument, QString> parse_crypto_json(const QByteArray& body) {
    QJsonParseError error;
    const auto invalid = [] { return std::unexpected(QStringLiteral("Некорректный ответ Rabby.")); };
    static const QRegularExpression number(QStringLiteral("^-?(?:0|[1-9][0-9]*)(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?$"));
    QByteArray exact;
    exact.reserve(body.size() + body.size() / 4);
    bool quoted = false;
    for (qsizetype index = 0; index < body.size(); ++index) {
        const auto character = body[index];
        if (quoted) {
            exact += character;
            if (character == '\\' && index + 1 < body.size()) exact += body[++index];
            else if (character == '"') quoted = false;
        } else if (character == '"') {
            quoted = true;
            exact += character;
        } else if (character == '-' || (character >= '0' && character <= '9')) {
            const auto start = index;
            exact += '"';
            do { exact += body[index++]; }
            while (index < body.size() && ((body[index] >= '0' && body[index] <= '9') ||
                body[index] == '.' || body[index] == 'e' || body[index] == 'E' || body[index] == '+' || body[index] == '-'));
            if (index - start > 512 || !number.match(QString::fromLatin1(body.mid(start, index - start))).hasMatch()) return invalid();
            auto next = index;
            while (next < body.size() && (body[next] == ' ' || body[next] == '\t' || body[next] == '\r' || body[next] == '\n')) ++next;
            if (next < body.size() && body[next] == ':') return invalid();
            exact += '"';
            --index;
        } else exact += character;
    }
    const auto result = QJsonDocument::fromJson(exact, &error);
    if (error.error != QJsonParseError::NoError || (!result.isArray() && !result.isObject()))
        return std::unexpected(QStringLiteral("Некорректный ответ Rabby."));
    return result;
}

namespace crypto_detail {
using core::Decimal;
using boost::multiprecision::cpp_int;

std::optional<Decimal> positivePrice(const QJsonValue& value) {
    if (!value.isString() || value.toString().size() > 100) return {};
    static const QRegularExpression number(QStringLiteral("^([0-9]+(?:\\.[0-9]+)?)(?:[eE]([+-]?[0-9]{1,3}))?$"));
    const auto match = number.match(value.toString());
    if (!match.hasMatch()) return {};
    auto coefficient = Decimal::parse(match.captured(1).toStdString());
    if (!coefficient || *coefficient <= Decimal{}) return {};
    const int exponent = match.captured(2).isEmpty() ? 0 : match.captured(2).toInt();
    const int scale = coefficient->scale() - exponent;
    if (scale > Decimal::max_scale || scale < -100) return {};
    auto mantissa = coefficient->mantissa();
    for (int index = scale; index < 0; ++index) mantissa *= 10;
    const auto result = Decimal::from_parts(mantissa, std::max(0, scale));
    return result ? std::optional(*result) : std::nullopt;
}

cpp_int powerOfTen(const int exponent) {
    cpp_int value = 1;
    for (int index = 0; index < exponent; ++index) value *= 10;
    return value;
}

std::optional<std::int64_t> usdtMinor(const cpp_int& scaledUsd, const Decimal& quote) {
    const cpp_int numerator = scaledUsd * powerOfTen(quote.scale() + 2);
    const cpp_int denominator = quote.mantissa() * powerOfTen(Decimal::max_scale * 2);
    cpp_int rounded = numerator / denominator;
    if ((numerator % denominator) * 2 >= denominator) ++rounded;
    if (rounded < 0 || rounded > std::numeric_limits<std::int64_t>::max()) return {};
    return rounded.convert_to<std::int64_t>();
}

bool chainId(const QString& id) {
    static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9_]{0,31}$"));
    return pattern.match(id).hasMatch();
}

std::optional<cpp_int> rawAmount(const QJsonValue& value) {
    if (!value.isString()) return {};
    static const QRegularExpression pattern(QStringLiteral("^0x[0-9a-fA-F]{1,64}$"));
    const auto text = value.toString();
    if (!pattern.match(text).hasMatch()) return {};
    cpp_int raw = 0;
    for (const auto digit : text.mid(2).toLower()) {
        raw *= 16;
        raw += digit >= QLatin1Char('a') ? digit.unicode() - 'a' + 10 : digit.digitValue();
    }
    return raw;
}

std::optional<int> precision(const QJsonValue& value) {
    if (!value.isString()) return {};
    static const QRegularExpression pattern(QStringLiteral("^(0|[1-9][0-9]{0,2})$"));
    if (!pattern.match(value.toString()).hasMatch()) return {};
    const auto result = value.toString().toInt();
    return result <= 255 ? std::optional(result) : std::nullopt;
}

std::expected<QMap<QString, QJsonObject>, QString> chainCatalog(const QJsonArray& chains) {
    QMap<QString, QJsonObject> catalog;
    if (chains.isEmpty() || chains.size() > 128)
        return std::unexpected(QStringLiteral("Некорректный список сетей Rabby."));
    for (const auto& item : chains) {
        const auto chain = item.toObject();
        const auto id = chain.value("id").toString();
        const auto name = chain.value("name").toString();
        if (!item.isObject() || !chainId(id) || catalog.contains(id) || name.isEmpty() || name.size() > 80 ||
            !chainId(chain.value("native_token_id").toString()))
            return std::unexpected(QStringLiteral("Некорректный список сетей Rabby."));
        catalog[id] = chain;
    }
    return catalog;
}
} // namespace crypto_detail

std::expected<CryptoPortfolio, QString> parse_crypto_portfolio(
    const QJsonArray& chains, const QJsonArray& tokens, const QJsonObject& usdt) {
    using namespace crypto_detail;
    const auto invalid = [] { return std::unexpected(QStringLiteral("Rabby вернул некорректные данные кошелька.")); };
    const auto catalog = chainCatalog(chains);
    if (!catalog || tokens.size() > 20000) return invalid();
    if (usdt.value("chain").toString() != "eth" ||
        usdt.value("id").toString().compare(QString::fromLatin1(ethereumUsdtContract), Qt::CaseInsensitive) != 0)
        return invalid();
    const auto usdtPrice = positivePrice(usdt.value("price"));
    if (!usdtPrice) return std::unexpected(QStringLiteral("Нет котировки USDT. Предыдущая оценка сохранена."));
    CryptoPortfolio result;
    for (const auto& chain : *catalog) result.supportedChains.push_back(chain.value("name").toString());
    result.supportedChains.sort();
    cpp_int totalUsd = 0;
    QSet<QString> seen;
    for (const auto& item : tokens) {
        if (!item.isObject()) return invalid();
        const auto token = item.toObject();
        const auto chain = token.value("chain").toString();
        const auto contract = token.value("id").toString().toLower();
        const auto raw = rawAmount(token.value("raw_amount_hex_str"));
        if (!catalog->contains(chain) || (!valid_ethereum_address(contract) &&
            contract != catalog->value(chain).value("native_token_id").toString()) || !raw ||
            seen.contains(chain + ':' + contract)) return invalid();
        seen.insert(chain + ':' + contract);
        const auto chainName = catalog->value(chain).value("name").toString();
        if (!result.usedChains.contains(chainName)) result.usedChains.push_back(chainName);
        const auto decimals = precision(token.value("decimals"));
        const auto amount = decimals && *decimals <= Decimal::max_scale
            ? Decimal::from_parts(*raw, *decimals) : std::unexpected(core::DecimalError::scale_out_of_range);
        auto symbol = token.value("symbol").toString().simplified().left(32);
        if (symbol.isEmpty()) symbol = contract.left(12);
        QVariantMap holding{{"symbol", symbol}, {"name", token.value("name").toString().simplified().left(120)},
            {"chain", chainName}, {"chainId", chain}, {"contract", valid_ethereum_address(contract) ? contract : QString{}},
            {"quantity", amount ? QString::fromStdString(amount->to_string())
                : QString::fromStdString(raw->convert_to<std::string>()) + QStringLiteral(" (минимальные единицы)")},
            {"priced", false}, {"value", QStringLiteral("—")}};
        const bool suspicious = token.value("is_scam").toBool() || token.value("is_suspicious").toBool();
        const auto price = suspicious ? std::nullopt : positivePrice(token.value("price"));
        if (amount && (amount->is_zero() || price)) {
            // Common scale 76 holds every supported quantity/quote product exactly.
            const cpp_int usd = amount->is_zero() ? cpp_int(0) :
                cpp_int(amount->mantissa() * price->mantissa() *
                    powerOfTen(Decimal::max_scale * 2 - amount->scale() - price->scale()));
            const auto rounded = usdtMinor(usd, *usdtPrice);
            if (!rounded) return invalid();
            totalUsd += usd;
            holding["priced"] = true;
            holding["value"] = usdt_label(*rounded);
        } else ++result.unpricedCount;
        result.holdings.push_back(holding);
    }
    const auto rounded = usdtMinor(totalUsd, *usdtPrice);
    if (!rounded) return invalid();
    result.usdtMinor = *rounded;
    return result;
}

class CryptoWalletSync final : public QObject {
public:
    using Completed = std::function<void(CryptoPortfolio)>;
    using Failed = std::function<void(QString)>;
    CryptoWalletSync(QNetworkAccessManager& network, Completed completed, Failed failed, QObject* parent)
        : QObject(parent), network_(network), completed_(std::move(completed)), failed_(std::move(failed)) {
        deadline_.setSingleShot(true);
        connect(&deadline_, &QTimer::timeout, this, [this] { fail(QStringLiteral("Обновление кошелька превысило лимит времени.")); });
    }
    ~CryptoWalletSync() override { cancel(); }
    void start(QString address) {
        if (active_) return;
        chains_ = {}; tokens_ = {}; quote_ = {}; pending_.clear(); used_.clear();
        throttles_ = 0;
        address_ = std::move(address);
        active_ = true;
        if (!valid_ethereum_address(address_)) { fail(QStringLiteral("Проверьте публичный EVM-адрес.")); return; }
        deadline_.start(300000);
        request("/v1/chain/list", Phase::Chains);
    }
    void cancel() {
        active_ = false;
        ++generation_;
        deadline_.stop();
        if (reply_) {
            disconnect(reply_, nullptr, this, nullptr);
            reply_->abort(); reply_->deleteLater(); reply_.clear();
        }
    }
private:
    enum class Phase { Chains, Used, Quote, Tokens };
    void fail(QString message) {
        if (!active_) return;
        cancel(); failed_(std::move(message));
    }
    void nextChain() {
        if (!active_) return;
        if (pending_.isEmpty()) {
            auto portfolio = parse_crypto_portfolio(chains_, tokens_, quote_);
            if (!portfolio) { fail(portfolio.error()); return; }
            portfolio->usedChains = used_;
            cancel(); completed_(std::move(*portfolio));
            return;
        }
        currentChain_ = pending_.takeFirst();
        request("/v1/user/token_list", Phase::Tokens);
    }
    void request(const QString& path, const Phase phase) {
        if (!active_) return;
        QUrl url(QStringLiteral("https://api.rabby.io") + path);
        QUrlQuery query;
        if (phase != Phase::Chains) query.addQueryItem("id", address_);
        if (phase == Phase::Quote) {
            query.addQueryItem("chain_id", "eth");
            query.addQueryItem("token_id", QString::fromLatin1(ethereumUsdtContract));
        } else if (phase == Phase::Tokens) {
            query.addQueryItem("chain_id", currentChain_);
            query.addQueryItem("is_all", "true");
        }
        url.setQuery(query);
        connectors::HostAllowlist allowlist({QStringLiteral("api.rabby.io")});
        auto prepared = allowlist.makeRequest(url);
        if (!prepared) { fail(QStringLiteral("Запрос кошелька не прошёл проверку безопасности.")); return; }
        prepared->setRawHeader("Accept", "application/json");
        auto* reply = network_.get(*prepared);
        reply_ = reply;
        auto body = std::make_shared<QByteArray>();
        const auto collect = [this, reply, body] {
            if (!active_ || !reply->isOpen()) return;
            if (reply->bytesAvailable() > 4 * 1024 * 1024 - body->size()) {
                fail(QStringLiteral("Ответ Rabby превышает допустимый размер.")); return;
            }
            body->append(reply->readAll());
        };
        connect(reply, &QNetworkReply::readyRead, this, collect);
        connect(reply, &QNetworkReply::finished, this, [this, reply, body, collect, phase, path] {
            if (!active_) return;
            collect(); if (!active_) return;
            reply_.clear(); reply->deleteLater();
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 429) {
                if (throttles_++ < 2) {
                    bool validDelay = false;
                    const auto seconds = reply->rawHeader("Retry-After").toLongLong(&validDelay);
                    const auto delay = validDelay && seconds > 0 ? std::clamp<qint64>(seconds, 1, 60) * 1000 : 10000;
                    const auto generation = generation_;
                    QTimer::singleShot(std::max<qint64>(1100, delay), this, [this, path, phase, generation] {
                        if (active_ && generation_ == generation) request(path, phase);
                    });
                } else fail(QStringLiteral("Rabby ограничил частоту запросов. Повторите позже."));
                return;
            }
            if (status == 401 || status == 403) { fail(QStringLiteral("Rabby отклонил публичный запрос. Предыдущие данные сохранены.")); return; }
            if (reply->error() != QNetworkReply::NoError || status != 200) {
                fail(QStringLiteral("Не удалось получить данные Rabby. Предыдущие данные сохранены.")); return;
            }
            const auto json = parse_crypto_json(*body);
            if (!json) { fail(json.error()); return; }
            const auto schedule = [this](const std::function<void()>& action) {
                const auto generation = generation_;
                QTimer::singleShot(1100, this, [this, action, generation] { if (active_ && generation_ == generation) action(); });
            };
            if (phase == Phase::Chains) {
                if (!json->isArray() || !crypto_detail::chainCatalog(json->array())) {
                    fail(QStringLiteral("Некорректный список сетей Rabby.")); return;
                }
                chains_ = json->array();
                schedule([this] { request("/v1/user/used_chain_list", Phase::Used); });
            } else if (phase == Phase::Used) {
                if (!json->isArray() || json->array().size() > 128) { fail(QStringLiteral("Некорректный список активных сетей.")); return; }
                const auto catalog = crypto_detail::chainCatalog(chains_);
                QSet<QString> seen;
                for (const auto& item : json->array()) {
                    const auto id = item.toObject().value("id").toString();
                    if (!item.isObject() || !catalog->contains(id) || seen.contains(id)) {
                        fail(QStringLiteral("Неизвестная активная сеть Rabby.")); return;
                    }
                    seen.insert(id); pending_.push_back(id);
                    used_.push_back(catalog->value(id).value("name").toString());
                }
                schedule([this] { request("/v1/user/token", Phase::Quote); });
            } else if (phase == Phase::Quote) {
                if (!json->isObject()) { fail(QStringLiteral("Некорректная котировка USDT.")); return; }
                quote_ = json->object();
                schedule([this] { nextChain(); });
            } else {
                if (!json->isArray() || json->array().size() > 5000) { fail(QStringLiteral("Список токенов превышает допустимый размер.")); return; }
                for (const auto& item : json->array()) {
                    if (item.toObject().value("chain").toString() != currentChain_) {
                        fail(QStringLiteral("Rabby вернул токен из другой сети.")); return;
                    }
                    tokens_.push_back(item);
                }
                if (tokens_.size() > 20000) { fail(QStringLiteral("Список токенов превышает допустимый размер.")); return; }
                schedule([this] { nextChain(); });
            }
        });
    }
    QNetworkAccessManager& network_;
    Completed completed_;
    Failed failed_;
    QTimer deadline_;
    QPointer<QNetworkReply> reply_;
    QString address_, currentChain_;
    QJsonArray chains_, tokens_;
    QJsonObject quote_;
    QStringList pending_, used_;
    std::uint64_t generation_ = 0;
    int throttles_ = 0;
    bool active_ = false;
};
} // namespace wealthtracker::app
