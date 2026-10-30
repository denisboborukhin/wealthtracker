#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>
#include <optional>

import wt.tbank_cashback;
import wt.cashback_sync;

namespace {
using namespace wealthtracker::app;

TBankCashbackContext context() {
    const auto parsed = tbank_cashback_context(R"({"resultCode":"OK","payload":[
        {"id":"account","accountType":"Current","loyaltyId":"Cashback"},
        {"id":"account-2","accountType":"Credit","loyaltyId":"Cashback"},
        {"id":"ignored","accountType":"Saving","loyaltyId":"Other"}]})");
    REQUIRE(parsed);
    return *parsed;
}

QJsonObject offer(const QDate& from,const QDate& to,const QString& name = "Кафе",const QJsonValue& rate = 5.25) {
    const QTimeZone moscow("Europe/Moscow");
    return {{"serviceType","regularEssences"},{"data",QJsonObject{{"id","offer"},
        {"accountIds",QJsonArray{"account"}}, {"activePeriod",QJsonObject{
            {"fromMs",QDateTime(from,QTime(0,0),moscow).toMSecsSinceEpoch()},
            {"toMs",QDateTime(to,QTime(23,59,59),moscow).toMSecsSinceEpoch()}}},
        {"essences",QJsonArray{QJsonObject{{"name",name},{"percent",rate},{"isActive",true}},
            QJsonObject{{"name","Unselected offer"},{"percent",99},{"isActive",false}}}}}}};
}

QByteArray response(const QJsonArray& offers) {
    return QJsonDocument(QJsonObject{{"payload",QJsonObject{{"data",offers}}}}).toJson(QJsonDocument::Compact);
}

class Reply final : public QNetworkReply {
public:
    Reply(const QNetworkRequest& request,QByteArray body,int status,QObject* parent)
        : QNetworkReply(parent),body_(std::move(body)) {
        setRequest(request); setUrl(request.url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute,status);
        QTimer::singleShot(0,this,[this] { if (isFinished()) return; setFinished(true); emit readyRead(); emit finished(); });
    }
    void abort() override { setFinished(true); emit finished(); }
    qint64 bytesAvailable() const override { return body_.size() + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char* data,qint64 maximum) override {
        const auto count = std::min(maximum,qint64(body_.size()));
        std::copy_n(body_.constData(),count,data); body_.remove(0,count); return count;
    }
private:
    QByteArray body_;
};

class Network final : public QNetworkAccessManager {
public:
    QByteArray body;
    int status = 200;
    Operation method = UnknownOperation;
    QNetworkRequest request;
    QByteArray sent;
protected:
    QNetworkReply* createRequest(Operation operation,const QNetworkRequest& input,QIODevice* outgoing) override {
        method = operation; request = input; sent = outgoing ? outgoing->readAll() : QByteArray{};
        return new Reply(input,body,status,this);
    }
};

void drain() {
    QEventLoop loop;
    QTimer::singleShot(30,&loop,&QEventLoop::quit);
    loop.exec();
}
}

TEST_CASE("T-Bank cashback reuses eligible account loyalty IDs and separates selected monthly categories") {
    const auto accounts = context();
    REQUIRE(accounts.loyaltyIds == QJsonArray{"Cashback"});
    const auto snapshot = parse_tbank_cashback(response({offer(QDate(2026,10,1),QDate(2026,10,31)),
        offer(QDate(2026,11,1),QDate(2026,11,30),"Транспорт","10")}),accounts,QDate(2026,10,29));
    REQUIRE(snapshot);
    CHECK(snapshot->months.size() == 2);
    const auto current = snapshot->rows("tbank",QDate(2026,10,1));
    REQUIRE(current.size() == 1);
    CHECK(current.front().toMap()["name"] == "Кафе");
    CHECK(current.front().toMap()["percent"] == "5,25 %");
    CHECK(snapshot->rows("tbank",QDate(2026,11,1)).front().toMap()["percent"] == "10 %");
}

TEST_CASE("T-Bank cashback rejects ambiguous rates invalid periods and unrelated accounts") {
    auto first = offer(QDate(2026,10,1),QDate(2026,10,31));
    QJsonArray offers{first};
    SECTION("conflicting programs") { offers.append(offer(QDate(2026,10,1),QDate(2026,10,31),"Кафе",10)); }
    SECTION("unknown selected flag") {
        auto data = first["data"].toObject(); auto categories = data["essences"].toArray();
        auto row = categories[0].toObject(); row["isActive"] = "true"; categories[0] = row;
        data["essences"] = categories; first["data"] = data; offers[0] = first;
    }
    SECTION("unrelated account") {
        auto data = first["data"].toObject(); data["accountIds"] = QJsonArray{"unrelated"}; first["data"] = data; offers[0] = first;
    }
    SECTION("wrong year") { offers = QJsonArray{offer(QDate(2025,10,1),QDate(2025,10,31))}; }
    SECTION("missing period") { auto data = first["data"].toObject(); data.remove("activePeriod"); first["data"] = data; offers[0] = first; }
    CHECK_FALSE(parse_tbank_cashback(response(offers),context(),QDate(2026,10,29)));
}

TEST_CASE("T-Bank cashback preserves Moscow dates and a known empty selection") {
    auto item = offer(QDate(2026,12,1),QDate(2027,1,31));
    auto data = item["data"].toObject();
    data["essences"] = QJsonArray{QJsonObject{{"isActive",false}}}; item["data"] = data;
    const auto result = parse_tbank_cashback(response({item}),context(),QDate(2026,12,31));
    REQUIRE(result);
    CHECK(result->months.contains(QDate(2027,1,1)));
    CHECK(result->rows("tbank",QDate(2026,12,1)).isEmpty());
}

TEST_CASE("T-Bank cashback preserves different rates for distinct card programs") {
    auto first = offer(QDate(2026,10,1),QDate(2026,10,31));
    auto second = offer(QDate(2026,10,1),QDate(2026,10,31),"Кафе",10);
    auto data = second["data"].toObject(); data["accountIds"] = QJsonArray{"account-2"}; second["data"] = data;
    const auto snapshot = parse_tbank_cashback(response({first,second}),context(),QDate(2026,10,29));
    REQUIRE(snapshot);
    const auto rows = snapshot->rows("tbank",QDate(2026,10,1));
    REQUIRE(rows.size() == 2);
    CHECK(rows.at(0).toMap()["name"] != rows.at(1).toMap()["name"]);
    CHECK(rows.at(0).toMap()["percent"] != rows.at(1).toMap()["percent"]);
    CHECK(rows.at(0).toMap()["name"].toString().contains("Кафе"));
    CHECK(rows.at(1).toMap()["name"].toString().contains("Кафе"));
}

TEST_CASE("T-Bank cashback uses the exact read POST with JSON and never exports a session") {
    Network network;
    const auto today = QDate::currentDate();
    network.body = response({offer(QDate(today.year(),today.month(),1),today)});
    std::optional<CashbackSnapshot> result;
    int failures = 0;
    TBankCashbackSync sync(network,[&](CashbackSnapshot value) {result = std::move(value);},[&](QString) {++failures;},nullptr);
    sync.start("fixture-session-123456789",context());
    drain();
    REQUIRE(result);
    CHECK(failures == 0);
    CHECK(network.method == QNetworkAccessManager::PostOperation);
    CHECK(network.request.url().host() == "www.tbank.ru");
    CHECK(network.request.url().path() == "/api/ms-gateway/loyalty_api/api/bonuses");
    CHECK(QUrlQuery(network.request.url()).queryItemValue("origin") == "web,ib5,platform");
    CHECK(network.request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::ManualRedirectPolicy);
    CHECK(network.request.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt() == QNetworkRequest::Manual);
    const auto body = QJsonDocument::fromJson(network.sent).object();
    CHECK(body["loyaltyIds"] == QJsonArray{"Cashback"});
    CHECK_FALSE(body.contains("sessionid"));
    CHECK_FALSE(body.contains("bundle"));
}

TEST_CASE("T-Bank cashback failures limits and cancellation never publish a snapshot") {
    Network network;
    int completions = 0;
    int failures = 0;
    bool cancel = false;
    SECTION("oversized body") { network.body = QByteArray(512 * 1024 + 1,' '); }
    SECTION("authentication failure") { network.status = 401; }
    SECTION("cancel before completion") { cancel = true; }
    TBankCashbackSync sync(network,[&](CashbackSnapshot) {++completions;},[&](QString error) {
        CHECK_FALSE(error.contains("fixture-session")); ++failures;
    },nullptr);
    sync.start("fixture-session-123456789",context());
    if (cancel) sync.cancel();
    drain();
    CHECK(completions == 0);
    CHECK(failures == (cancel ? 0 : 1));
}
