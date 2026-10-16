#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QDateTime>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QVector>

#include <algorithm>
#include <expected>
#include <optional>

import wt.bank_sync;
import wt.connectors;

namespace {

using namespace wealthtracker::connectors;

class StreamingReply final : public QNetworkReply {
  public:
    explicit StreamingReply(QObject* parent) : QNetworkReply(parent) {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    void append(const QByteArray& bytes) {
        body_ += bytes;
        emit readyRead();
    }

    void finish(const int status = 200, const NetworkError error = NoError) {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (error != NoError) {
            setError(error, QStringLiteral("Scripted failure"));
        }
        setFinished(true);
        emit finished();
    }

    void abort() override {
        aborted = true;
        if (!isFinished()) {
            finish(200, OperationCanceledError);
        }
    }

    qint64 bytesAvailable() const override { return body_.size() + QIODevice::bytesAvailable(); }

    bool aborted = false;
    qint64 bytesRead = 0;

  protected:
    qint64 readData(char* data, const qint64 maximumSize) override {
        const auto size = std::min(maximumSize, qint64(body_.size()));
        std::copy_n(body_.constData(), size, data);
        body_.remove(0, size);
        bytesRead += size;
        return size;
    }

  private:
    QByteArray body_;
};

class SyncFixture final : public QObject, public ILiveConnector {
  public:
    ConnectorManifest manifest() const override { return {}; }
    ConnectorResult<QNetworkReply*> discoverAccounts(const QByteArray&) override {
        return new StreamingReply(this);
    }
    ConnectorResult<QVector<QNetworkReply*>> synchronize(const LiveSyncRequest&,
                                                        const QByteArray&) override {
        auto* reply = new StreamingReply(this);
        pages.push_back(reply);
        return QVector<QNetworkReply*>{reply};
    }

    StreamingReply* start(const int accountCount = 1) {
        wealthtracker::app::BankRuntime runtime{
            this, 50,
            [this, accountCount](const QByteArray& body, const QDateTime&)
                -> ConnectorResult<NormalizedBatch> {
                ++accountParses;
                accountBody = body;
                NormalizedBatch batch;
                for (int index = 0; index < accountCount; ++index) {
                    ExternalAccount account;
                    account.externalId = QString::number(index);
                    batch.accounts.push_back(account);
                }
                return batch;
            },
            [this](const QByteArray& body, const QString&, const QByteArray&, int)
                -> ConnectorResult<NormalizedBatch> {
                ++pageParses;
                pageBody = body;
                return NormalizedBatch{};
            },
        };
        runtime.maximumResponseBytes = 16;
        coordinator = new wealthtracker::app::BankSyncCoordinator(
            std::move(runtime), []() -> std::expected<QByteArray, QString> { return "test"; },
            [this](NormalizedBatch) { ++completions; },
            [this](ConnectorError error) {
                ++failures;
                lastError = std::move(error);
            }, this);
        auto* discovery = new StreamingReply(this);
        coordinator->start(discovery);
        return discovery;
    }

    wealthtracker::app::BankSyncCoordinator* coordinator = nullptr;
    QVector<StreamingReply*> pages;
    std::optional<ConnectorError> lastError;
    QByteArray accountBody;
    QByteArray pageBody;
    int accountParses = 0;
    int pageParses = 0;
    int completions = 0;
    int failures = 0;
};

} // namespace

TEST_CASE("Bank sync recognizes expired credentials in HTTP responses") {
    for (const int status : {401, 403}) {
        SECTION("Account discovery") {
            SyncFixture fixture;
            auto* discovery = fixture.start();
            discovery->finish(status, QNetworkReply::AuthenticationRequiredError);
            REQUIRE(fixture.lastError);
            CHECK(fixture.lastError->code == ErrorCode::AuthenticationFailed);
            CHECK_FALSE(fixture.lastError->retryable);
            CHECK(fixture.accountParses == 0);
            CHECK(fixture.failures == 1);
        }
        SECTION("History retrieval") {
            SyncFixture fixture;
            auto* discovery = fixture.start();
            discovery->finish();
            REQUIRE(fixture.pages.size() == 1);
            fixture.pages.front()->finish(status, QNetworkReply::ContentAccessDenied);
            REQUIRE(fixture.lastError);
            CHECK(fixture.lastError->code == ErrorCode::AuthenticationFailed);
            CHECK(fixture.pageParses == 0);
            CHECK(fixture.completions == 0);
        }
    }
}

TEST_CASE("Bank sync distinguishes HTTP throttling and service errors from transport failures") {
    for (const int status : {429, 503}) {
        SyncFixture fixture;
        fixture.start()->finish(status, QNetworkReply::UnknownContentError);
        REQUIRE(fixture.lastError);
        CHECK(fixture.lastError->code ==
              (status == 429 ? ErrorCode::RateLimited : ErrorCode::RemoteFailure));
        CHECK(fixture.lastError->retryable);
        CHECK(fixture.accountParses == 0);
    }
}

TEST_CASE("Bank sync aborts oversized account responses while still receiving") {
    SyncFixture fixture;
    auto* discovery = fixture.start();
    discovery->append(QByteArray(12, 'a'));
    CHECK_FALSE(fixture.lastError);
    discovery->append(QByteArray(100, 'b'));
    REQUIRE(fixture.lastError);
    CHECK(fixture.lastError->code == ErrorCode::SecurityViolation);
    CHECK(discovery->aborted);
    CHECK(discovery->bytesRead == 17);
    CHECK(fixture.accountParses == 0);
    CHECK(fixture.failures == 1);
}

TEST_CASE("Bank sync cancels all in-flight history after an oversized response") {
    SyncFixture fixture;
    fixture.start(2)->finish();
    REQUIRE(fixture.pages.size() == 2);
    fixture.pages.front()->append(QByteArray(100, 'x'));
    REQUIRE(fixture.lastError);
    CHECK(fixture.lastError->code == ErrorCode::SecurityViolation);
    CHECK(fixture.pages.front()->aborted);
    CHECK(fixture.pages.back()->aborted);
    CHECK(fixture.pages.front()->bytesRead == 17);
    CHECK(fixture.pageParses == 0);
    CHECK(fixture.failures == 1);
    CHECK(fixture.completions == 0);
}

TEST_CASE("Bank sync preserves chunked responses at the exact size limit") {
    SyncFixture fixture;
    auto* discovery = fixture.start();
    discovery->append(QByteArray(8, 'a'));
    discovery->append(QByteArray(8, 'b'));
    discovery->finish();
    REQUIRE(fixture.pages.size() == 1);
    fixture.pages.front()->append(QByteArray(8, 'c'));
    fixture.pages.front()->append(QByteArray(8, 'd'));
    fixture.pages.front()->finish();
    CHECK_FALSE(fixture.lastError);
    CHECK(fixture.accountBody == QByteArray(8, 'a') + QByteArray(8, 'b'));
    CHECK(fixture.pageBody == QByteArray(8, 'c') + QByteArray(8, 'd'));
    CHECK(fixture.completions == 1);
}
