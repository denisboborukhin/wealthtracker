#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

import wt.connectors;

using namespace wealthtracker::connectors;

namespace {

QByteArray readProtocolLine(QProcess &process)
{
    QByteArray buffer;
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (!process.canReadLine()) {
            process.waitForReadyRead(200);
        }
        buffer += process.readLine();
        if (buffer.endsWith('\n')) {
            return buffer.trimmed();
        }
        if (process.state() == QProcess::NotRunning) {
            break;
        }
    }
    return buffer.trimmed();
}

} // namespace

TEST_CASE("Zen plugin descriptors validate bundle and preference metadata")
{
    const auto fixture =
        QStringLiteral(WEALTHTRACKER_SOURCE_DIR
                       "/tests/fixtures/zen_plugin_fixture.cjs");
    const auto descriptor = inspectZenPluginBundle(fixture);
    REQUIRE(descriptor);
    CHECK(descriptor->pluginId == QStringLiteral("zen_plugin_fixture"));
    CHECK(QFileInfo(descriptor->bundlePath).isAbsolute());
    REQUIRE(descriptor->preferences.size() == 3);
    CHECK(descriptor->preferences.at(1).kind
          == ZenPluginPreferenceKind::Password);
    CHECK(descriptor->preferences.at(1).sensitive);
    CHECK(descriptor->preferences.at(2).kind
          == ZenPluginPreferenceKind::Date);

    const auto missing =
        inspectZenPluginBundle(QStringLiteral("/does/not/exist.js"));
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == ErrorCode::InvalidInput);
}

TEST_CASE("Zen plugin result events normalize accounts and signed operations")
{
    const auto event = parseZenPluginEvent(
        QByteArrayLiteral(
            R"({"type":"result","accounts":[{"id":"account-1","title":"Основной","type":"checking","instrument":"RUR","balance":123.45,"syncID":["40817810000000001234"]}],"transactions":[{"id":"tx-1","date":"2026-07-30T10:00:00.000Z","income":0,"outcome":42.75,"incomeAccount":"cash#RUB","outcomeAccount":"account-1","comment":"Кофе","mcc":5812}],"state":{"pluginData":{},"cookies":[]}})"),
        QStringLiteral("fixture"),
        QDateTime::fromString(QStringLiteral("2026-07-30T11:00:00Z"),
                              Qt::ISODate));
    REQUIRE(event);
    CHECK(event->kind == ZenPluginEventKind::Result);
    REQUIRE(event->batch.accounts.size() == 1);
    REQUIRE(event->batch.balances.size() == 1);
    REQUIRE(event->batch.transactions.size() == 1);
    CHECK(event->batch.accounts.front().currency == QStringLiteral("RUB"));
    CHECK(event->batch.accounts.front().maskedNumber
          == QStringLiteral("•••• 1234"));
    CHECK(event->batch.balances.front().balance
          == Money{12345, QStringLiteral("RUB")});
    CHECK(event->batch.transactions.front().amount
          == Money{-4275, QStringLiteral("RUB")});
    CHECK(event->batch.transactions.front().mcc == QStringLiteral("5812"));
}

TEST_CASE("Zen plugin protocol rejects malformed events and secret-sized input")
{
    const auto unknown = parseZenPluginEvent(
        QByteArrayLiteral(R"({"type":"unknown"})"),
        QStringLiteral("fixture"));
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().code == ErrorCode::ParseFailed);

    const auto oversizedInput = makeZenPluginInputMessage(
        QStringLiteral("prompt-1"), QByteArray(4097, 'x'));
    REQUIRE_FALSE(oversizedInput);
    CHECK(oversizedInput.error().code == ErrorCode::InvalidInput);
}

TEST_CASE("local host executes an external bundle and handles an OTP prompt")
{
    const auto node = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (node.isEmpty()) {
        SKIP("Node.js is not installed");
    }

    const auto fixture =
        QStringLiteral(WEALTHTRACKER_SOURCE_DIR
                       "/tests/fixtures/zen_plugin_fixture.cjs");
    auto descriptor = inspectZenPluginBundle(fixture);
    REQUIRE(descriptor);

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(
        node,
        {QStringLiteral(WEALTHTRACKER_SOURCE_DIR
                        "/scripts/zen_plugin_host.mjs")});
    REQUIRE(process.waitForStarted(5000));

    const ZenPluginRunRequest request{
        .descriptor = *descriptor,
        .preferences =
            QJsonObject{{QStringLiteral("login"), QStringLiteral("fixture")},
                        {QStringLiteral("password"),
                         QStringLiteral("never-log-this-secret")},
                        {QStringLiteral("startDate"),
                         QStringLiteral("2026-01-01T00:00:00.000Z")}},
        .state = {},
        .from = QDateTime::fromString(
            QStringLiteral("2026-07-01T00:00:00Z"), Qt::ISODate),
        .to = QDateTime::fromString(
            QStringLiteral("2026-07-30T00:00:00Z"), Qt::ISODate),
    };
    const auto startMessage = makeZenPluginStartMessage(request);
    REQUIRE(startMessage);
    process.write(*startMessage);
    REQUIRE(process.waitForBytesWritten(5000));

    const auto promptLine = readProtocolLine(process);
    const auto prompt = parseZenPluginEvent(
        promptLine, descriptor->pluginId);
    INFO(process.readAllStandardError().constData());
    REQUIRE(prompt);
    REQUIRE(prompt->kind == ZenPluginEventKind::Prompt);
    CHECK(prompt->prompt.inputType == QStringLiteral("number"));

    const auto inputMessage = makeZenPluginInputMessage(
        prompt->prompt.requestId, QByteArrayLiteral("123456"));
    REQUIRE(inputMessage);
    process.write(*inputMessage);
    REQUIRE(process.waitForBytesWritten(5000));

    const auto resultLine = readProtocolLine(process);
    const auto result = parseZenPluginEvent(
        resultLine, descriptor->pluginId,
        QDateTime::fromString(QStringLiteral("2026-07-30T12:00:00Z"),
                              Qt::ISODate));
    INFO(process.readAllStandardError().constData());
    REQUIRE(result);
    REQUIRE(result->kind == ZenPluginEventKind::Result);
    REQUIRE(result->batch.accounts.size() == 1);
    REQUIRE(result->batch.transactions.size() == 1);
    CHECK(result->batch.transactions.front().amount
          == Money{-4275, QStringLiteral("RUB")});
    CHECK(result->state.value(QStringLiteral("pluginData"))
              .toObject()
              .value(QStringLiteral("session"))
              .toString()
          == QStringLiteral("fixture-session"));
    CHECK_FALSE(process.readAllStandardError().contains(
        QByteArrayLiteral("never-log-this-secret")));
    CHECK(process.waitForFinished(5000));
}
