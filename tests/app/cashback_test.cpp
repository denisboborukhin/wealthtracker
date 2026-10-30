#include "app/AppController.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDate>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTimeZone>

#include <algorithm>
#include <functional>
#include <memory>

import wt.cashback;

namespace {
using wealthtracker::app::CashbackBook;
using wealthtracker::app::AppController;

struct SettingsScope {
    QTemporaryDir directory;
    QString organization = QCoreApplication::organizationName();
    QString application = QCoreApplication::applicationName();
    QSettings::Format format = QSettings::defaultFormat();
    SettingsScope() {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName(QStringLiteral("CashbackTests"));
        QCoreApplication::setApplicationName(QStringLiteral("CashbackTests"));
    }
    ~SettingsScope() {
        QCoreApplication::setOrganizationName(organization);
        QCoreApplication::setApplicationName(application);
        QSettings::setDefaultFormat(format);
    }
};

class Credentials final : public wealthtracker::app::CredentialBackend {
public:
    bool supported() const noexcept override { return true; }
    std::expected<void, QString> put(const QString&, const QByteArray&) override { return {}; }
    std::expected<void, QString> remove(const QString&) override { return {}; }
    std::expected<std::optional<QByteArray>, QString> get(const QString& key) override {
        if (key == QStringLiteral("tbank-retail-session")) return std::optional<QByteArray>{"fixture-session-123456789"};
        return std::optional<QByteArray>{};
    }
};

class AccountReply final : public QNetworkReply {
public:
    explicit AccountReply(const QNetworkRequest& request, bool fail, QObject* parent,
        QByteArray body = R"({"resultCode":"OK","payload":[{"id":"fixture-account","accountType":"Current","moneyAmount":{"value":"125.50","currency":"RUB"}}]})") : QNetworkReply(parent) {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, [this, fail, body = std::move(body)] {
            if (isFinished()) return;
            if (fail) setError(QNetworkReply::TimeoutError, QStringLiteral("Fixture timeout"));
            else body_ = body;
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }
    void abort() override { setFinished(true); emit finished(); }
    qint64 bytesAvailable() const override { return body_.size() + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char* data, qint64 maximum) override {
        const auto count = std::min(maximum, qint64(body_.size()));
        std::copy_n(body_.constData(), count, data);
        body_.remove(0, count);
        return count;
    }
private:
    QByteArray body_;
};

class AccountNetwork final : public QNetworkAccessManager {
public:
    bool fail = false;
    bool automaticCashback = false;
    bool cashbackFail = false;
    QByteArray cashbackBody;
    int accountReads = 0;
    int cashbackReads = 0;
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
        if (request.url().path() == QStringLiteral("/api/ms-gateway/loyalty_api/api/bonuses")) {
            ++cashbackReads;
            return new AccountReply(request,cashbackFail,this,cashbackBody);
        }
        ++accountReads;
        if (automaticCashback) return new AccountReply(request,fail,this,
            R"({"resultCode":"OK","payload":[{"id":"fixture-account","accountType":"Current","loyaltyId":"Cashback","moneyAmount":{"value":"125.50","currency":"RUB"}}]})");
        return new AccountReply(request, fail, this);
    }
};
}

TEST_CASE("Cashback stores exact percentages and isolates banks and months") {
    CashbackBook book;
    const QDate today(2026, 12, 29);
    REQUIRE(book.set("tbank", "2026-12-01", "Супермаркеты", "5,25", today));
    REQUIRE(book.set("alfa", "2026-12-01", "Супермаркеты", "3", today));
    REQUIRE(book.set("tbank", "2027-01-01", "Супермаркеты", "10", today));
    REQUIRE(book.set("tbank", "2026-12-01", "супермаркеты", "0.01", today));
    REQUIRE(book.rows("tbank", QDate(2026, 12, 1)).size() == 1);
    CHECK(book.rows("tbank", QDate(2026, 12, 1)).front().toMap()["percent"] == "0,01 %");
    CHECK(book.rows("alfa", QDate(2026, 12, 1)).front().toMap()["percent"] == "3 %");
    CHECK(book.rows("vtb", QDate(2026, 12, 1)).isEmpty());
    const auto restored = CashbackBook::parse(book.serialize());
    REQUIRE(restored);
    CHECK(restored->rows("tbank", QDate(2027, 1, 1)).front().toMap()["percent"] == "10 %");
    REQUIRE(book.set("vtb", "2027-01-01", "Кафе", "100", today.addMonths(1)));
    CHECK(book.rows("tbank", QDate(2026, 12, 1)).isEmpty());
    CHECK(book.rows("tbank", QDate(2027, 1, 1)).size() == 1);
    CHECK(book.remove("alfa", "2027-01-01", "Супермаркеты") == false);
    CHECK(book.remove("tbank", "2027-01-01", "Супермаркеты"));
    CHECK(book.rows("vtb", QDate(2027, 1, 1)).size() == 1);
}

TEST_CASE("Cashback rejects invalid input without replacing selected categories") {
    CashbackBook book;
    const QDate today(2026, 10, 29);
    REQUIRE(book.set("vtb", "2026-10-01", "Кафе", "5", today));
    const auto before = book.serialize();
    for (const auto& rate : {"0", "-1", "100.01", "1.234", "nan", "5%", "1e2", ""}) {
        CHECK_FALSE(book.set("vtb", "2026-10-01", "Кафе", rate, today));
        CHECK(book.serialize() == before);
    }
    CHECK_FALSE(book.set("unknown", "2026-10-01", "Кафе", "5", today));
    CHECK_FALSE(book.set("vtb", "2026-09-01", "Кафе", "5", today));
    CHECK_FALSE(book.set("vtb", "2026-12-01", "Кафе", "5", today));
    CHECK_FALSE(book.set("vtb", "2026-10-02", "Кафе", "5", today));
    CHECK_FALSE(book.set("vtb", "2026-10-01", "\n", "5", today));
    CHECK_FALSE(book.set("vtb", "2026-10-01", QString(101, QLatin1Char('x')), "5", today));
    CHECK(book.serialize() == before);
    for (int index = 1; index < CashbackBook::maximumCategories; ++index)
        REQUIRE(book.set("vtb", "2026-10-01", QString::number(index), "5", today));
    CHECK_FALSE(book.set("vtb", "2026-10-01", "overflow", "5", today));
}

TEST_CASE("Cashback persistence fails closed on unknown schemas and malformed rates") {
    CHECK(CashbackBook::parse({}));
    CHECK_FALSE(CashbackBook::parse("not json"));
    CHECK_FALSE(CashbackBook::parse(R"({"version":2,"categories":[]})"));
    CHECK_FALSE(CashbackBook::parse(QByteArray(128 * 1024 + 1, ' ')));
    const auto category = QJsonObject{{"bank", "vtb"}, {"month", "2026-10-01"},
        {"name", "Кафе"}, {"basisPoints", 525}};
    auto root = QJsonObject{{"version", 1}, {"categories", QJsonArray{category, category}}};
    CHECK_FALSE(CashbackBook::parse(QJsonDocument(root).toJson()));
    auto invalid = category;
    invalid["basisPoints"] = 5.25;
    root["categories"] = QJsonArray{invalid};
    CHECK_FALSE(CashbackBook::parse(QJsonDocument(root).toJson()));
    invalid["basisPoints"] = "525";
    root["categories"] = QJsonArray{invalid};
    CHECK_FALSE(CashbackBook::parse(QJsonDocument(root).toJson()));
}

TEST_CASE("Controller cashback persists manual choices and protects them during privacy and refresh") {
    SettingsScope settings;
    const auto month = CashbackBook::monthStart(QDate::currentDate()).toString("yyyy-MM-dd");
    {
        AppController controller(nullptr, nullptr);
        REQUIRE(controller.cashbackBanks().size() == 3);
        REQUIRE(controller.cashbackMonths().size() == 2);
        REQUIRE(controller.saveCashbackCategory("tbank", month, "Супермаркеты", "5.25"));
        controller.refreshCashbackBanks();
        CHECK(controller.cashbackBanks().front().toMap()["current"].toList().size() == 1);
        controller.lock();
        CHECK(controller.cashbackBanks().isEmpty());
        CHECK_FALSE(controller.saveCashbackCategory("tbank", month, "Кафе", "10"));
        CHECK_FALSE(controller.removeCashbackCategory("tbank", month, "Супермаркеты"));
        REQUIRE(controller.unlock());
        controller.loadDemoData();
        CHECK(controller.cashbackBanks().isEmpty());
        CHECK_FALSE(controller.saveCashbackCategory("tbank", month, "Кафе", "10"));
        controller.clearDemoData();
    }
    AppController reopened(nullptr, nullptr);
    CHECK(reopened.cashbackBanks().front().toMap()["current"].toList().front().toMap()["percent"] == "5,25 %");
    REQUIRE(reopened.removeCashbackCategory("tbank", month, "Супермаркеты"));
    AppController afterRemoval(nullptr, nullptr);
    CHECK(afterRemoval.cashbackBanks().front().toMap()["current"].toList().isEmpty());
}

TEST_CASE("Controller preserves unreadable cashback settings") {
    SettingsScope settings;
    QSettings{}.setValue("cashback/categories", QByteArrayLiteral("unreadable"));
    AppController controller(nullptr, nullptr);
    CHECK_FALSE(controller.cashbackError().isEmpty());
    CHECK_FALSE(controller.saveCashbackCategory("vtb", QDate::currentDate().toString("yyyy-MM-01"), "Кафе", "5"));
    CHECK(QSettings{}.value("cashback/categories").toByteArray() == "unreadable");
}

TEST_CASE("Cashback observes the existing bank synchronization without replacing manual selections") {
    SettingsScope settings;
    auto network = std::make_unique<AccountNetwork>();
    auto* fixture = network.get();
    AppController controller(std::make_unique<Credentials>(), std::move(network), nullptr);
    const auto month = CashbackBook::monthStart(QDate::currentDate()).toString("yyyy-MM-dd");
    REQUIRE(controller.saveCashbackCategory("tbank", month, "Кафе", "5"));
    int updates = 0;
    QObject::connect(&controller, &AppController::cashbackChanged, &controller, [&] { ++updates; });
    const auto refresh = [&] {
        QEventLoop loop;
        QObject::connect(&controller, &AppController::tbankConnectionChanged, &loop, [&] {
            if (!controller.tbankSyncRunning()) loop.quit();
        });
        QTimer::singleShot(1000, &loop, &QEventLoop::quit);
        controller.refreshCashbackBanks();
        CHECK(controller.cashbackBanks().front().toMap()["syncRunning"].toBool());
        loop.exec();
        CHECK_FALSE(controller.tbankSyncRunning());
    };
    refresh();
    REQUIRE(updates >= 2);
    const auto bank = controller.cashbackBanks().front().toMap();
    CHECK_FALSE(bank["lastSync"].toString().isEmpty());
    CHECK(bank["current"].toList().size() == 1);
    fixture->fail = true;
    refresh();
    CHECK(controller.cashbackBanks().front().toMap()["current"] == bank["current"]);
    CHECK(controller.cashbackBanks().front().toMap()["lastSync"] == bank["lastSync"]);
    CHECK_FALSE(controller.cashbackBanks().front().toMap()["syncStatus"].toString().isEmpty());
}

TEST_CASE("T-Bank balance synchronization also publishes bank cashback without repeating account discovery") {
    SettingsScope settings;
    auto network = std::make_unique<AccountNetwork>();
    auto* fixture = network.get();
    fixture->automaticCashback = true;
    const auto today = QDate::currentDate();
    const auto month = QDate(today.year(),today.month(),1);
    const QTimeZone moscow("Europe/Moscow");
    fixture->cashbackBody = QJsonDocument(QJsonObject{{"payload",QJsonObject{{"data",QJsonArray{
        QJsonObject{{"serviceType","regularEssences"},{"data",QJsonObject{
            {"accountIds",QJsonArray{"fixture-account"}}, {"activePeriod",QJsonObject{
                {"fromMs",QDateTime(month,QTime(0,0),moscow).toMSecsSinceEpoch()},
                {"toMs",QDateTime(month.addMonths(1).addDays(-1),QTime(23,59,59),moscow).toMSecsSinceEpoch()}}},
            {"essences",QJsonArray{QJsonObject{{"name","Кафе"},{"percent",5},{"isActive",true}}}}}}}
    }}}}}).toJson();
    AppController controller(std::make_unique<Credentials>(),std::move(network),nullptr);
    REQUIRE(controller.saveCashbackCategory("tbank",month.toString(Qt::ISODate),"Manual fallback","3"));
    controller.refreshCashbackBanks();
    const auto wait = [&](const std::function<bool()>& ready) {
        QEventLoop loop;
        QTimer poll;
        QObject::connect(&poll,&QTimer::timeout,&loop,[&] {if (ready()) loop.quit();});
        poll.start(5);
        QTimer::singleShot(1000,&loop,&QEventLoop::quit);
        loop.exec();
        return ready();
    };
    REQUIRE(wait([&] {return controller.cashbackBanks().front().toMap()["currentFromBank"].toBool();}));
    CHECK(fixture->accountReads == 1);
    CHECK(fixture->cashbackReads == 1);
    const auto initial = controller.cashbackBanks().front().toMap();
    CHECK(initial["current"].toList().front().toMap()["name"] == "Кафе");
    CHECK_FALSE(initial["currentLastSync"].toString().isEmpty());
    fixture->cashbackFail = true;
    REQUIRE(controller.synchronizeTBank());
    REQUIRE(wait([&] {return !controller.tbankSyncRunning() &&
        controller.cashbackBanks().front().toMap()["cashbackStatus"] != initial["cashbackStatus"] &&
        !controller.cashbackBanks().front().toMap()["cashbackRunning"].toBool();}));
    CHECK(controller.cashbackBanks().front().toMap()["current"] == initial["current"]);
    CHECK(controller.cashbackBanks().front().toMap()["currentLastSync"] == initial["currentLastSync"]);
    controller.disconnectTBank();
    CHECK_FALSE(controller.cashbackBanks().front().toMap()["currentFromBank"].toBool());
    CHECK(controller.cashbackBanks().front().toMap()["current"].toList().front().toMap()["name"] == "Manual fallback");
}
