#include <catch2/catch_test_macros.hpp>

#include <QJSEngine>
#include <QString>

import wt.tbank_browser;

TEST_CASE("T-Bank session discovery retains the most recently observed sessions") {
    QJSEngine engine;
    const auto setup = engine.evaluate(QStringLiteral(R"JS(
        var globalThis = this
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
