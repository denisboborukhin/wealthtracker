#include <catch2/catch_test_macros.hpp>

#include <QJSEngine>
#include <QNetworkRequest>
#include <QString>
#include <QUrlQuery>

import wt.tbank_auth;
import wt.tbank_browser;

TEST_CASE("T-Bank CLIENT verification uses the guarded bank request policy") {
    const auto request = wealthtracker::app::tbank_authorization_request(
        QByteArrayLiteral("synthetic-session-identifier"));
    REQUIRE(request);
    CHECK(request->url().scheme() == QStringLiteral("https"));
    CHECK(request->url().host() == QStringLiteral("www.tbank.ru"));
    CHECK(request->url().path() == QStringLiteral("/api/common/v1/ping"));
    CHECK(QUrlQuery(request->url()).queryItemValue(QStringLiteral("sessionid")) ==
          QStringLiteral("synthetic-session-identifier"));
    CHECK(request->attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() ==
          QNetworkRequest::ManualRedirectPolicy);
    CHECK(request->attribute(QNetworkRequest::CookieLoadControlAttribute).toInt() ==
          QNetworkRequest::Manual);
    CHECK(request->attribute(QNetworkRequest::CookieSaveControlAttribute).toInt() ==
          QNetworkRequest::Manual);
    CHECK_FALSE(request->attribute(QNetworkRequest::Http2AllowedAttribute).toBool());
    CHECK(request->transferTimeout() == 15'000);
    CHECK(request->attribute(QNetworkRequest::MaximumDownloadBufferSizeAttribute).toLongLong() == 64 * 1024);
    CHECK_FALSE(wealthtracker::app::tbank_authorization_request(QByteArrayLiteral("bad&session")));
}

TEST_CASE("An anonymous T-Bank API session does not establish client access") {
    const auto anonymous = wealthtracker::app::parse_tbank_access_level(
        QByteArrayLiteral(R"({"resultCode":"OK","payload":{"accessLevel":"ANONYMOUS"}})"));
    REQUIRE(anonymous);
    CHECK(*anonymous == wealthtracker::app::TBankAccessLevel::anonymous);
    const auto client = wealthtracker::app::parse_tbank_access_level(
        QByteArrayLiteral(R"({"resultCode":"OK","payload":{"accessLevel":"CLIENT"}})"));
    REQUIRE(client);
    CHECK(*client == wealthtracker::app::TBankAccessLevel::client);
    CHECK_FALSE(wealthtracker::app::parse_tbank_access_level(
        QByteArrayLiteral(R"({"resultCode":"OK","payload":{}})")));
}

TEST_CASE("T-Bank session discovery retains the most recently observed sessions") {
    QJSEngine engine;
    const auto setup = engine.evaluate(QStringLiteral(R"JS(
        var globalThis = this
        var location = {origin: 'https://www.tbank.ru'}
        globalThis.URL = function(name) {
            return { protocol: 'https:', hostname: 'www.tbank.ru', port: '',
                     username: '', password: '', pathname: '/api/common/v1/ping',
                     searchParams: { getAll: function() { return [name] } } }
        }
        globalThis.performance = { getEntriesByType: function() {
            const entries = Array.from({ length: 10 }, function(_, index) {
                return { name: 'session-identifier-' + index }
            })
            entries.push({ name: 'session-identifier-2' })
            return entries
        } }
    )JS"));
    REQUIRE_FALSE(setup.isError());
    const auto evaluated = engine.evaluate(wealthtracker::app::tbank_session_probe_script());
    INFO(evaluated.toString().toStdString());
    REQUIRE_FALSE(evaluated.isError());
    const auto sessions = wealthtracker::app::parse_tbank_session_projection(evaluated.toString());
    REQUIRE(sessions);
    REQUIRE(sessions->size() == 8);
    CHECK(sessions->front() == QByteArrayLiteral("session-identifier-3"));
    CHECK(sessions->back() == QByteArrayLiteral("session-identifier-2"));
    CHECK(sessions->contains(QByteArrayLiteral("session-identifier-9")));
    CHECK_FALSE(sessions->contains(QByteArrayLiteral("session-identifier-0")));
}
