#include <catch2/catch_test_macros.hpp>
#include <QDate>
#include <QString>

#include <optional>
#include <utility>

import wt.connectors;

using namespace wealthtracker::connectors;

namespace {

NormalizedTransaction transaction(QString id, QByteArray fileHash, qsizetype row)
{
    return {
        std::move(id),
        QStringLiteral("account-1"),
        QDate(2026, 7, 30),
        std::nullopt,
        {-12345, QStringLiteral("RUB")},
        QStringLiteral("Магазин № 1"),
        {},
        {},
        {},
        {},
        {QStringLiteral("fixture"), std::move(fileHash), {}, row},
    };
}

} // namespace

TEST_CASE("deduplication gives source IDs and provenance priority")
{
    DeduplicationIndex index;
    const auto existing = transaction(QStringLiteral("tx-1"), QByteArrayLiteral("hash-a"), 2);
    addToDeduplicationIndex(existing, index);

    auto sameId = transaction(QStringLiteral("tx-1"), QByteArrayLiteral("hash-b"), 9);
    CHECK(classifyDuplicate(sameId, index).disposition
          == DuplicateDisposition::ExactDuplicate);

    auto sameRow = transaction({}, QByteArrayLiteral("hash-a"), 2);
    CHECK(classifyDuplicate(sameRow, index).disposition
          == DuplicateDisposition::ExactDuplicate);
}

TEST_CASE("canonical matches are reviewed rather than silently discarded")
{
    DeduplicationIndex index;
    const auto existing = transaction({}, QByteArrayLiteral("hash-a"), 2);
    addToDeduplicationIndex(existing, index);

    auto candidate = transaction({}, QByteArrayLiteral("hash-b"), 8);
    candidate.description = QStringLiteral("  магазин, № 1 ");
    const auto decision = classifyDuplicate(candidate, index);

    CHECK(decision.disposition == DuplicateDisposition::AmbiguousMatch);
    CHECK_FALSE(decision.canonicalFingerprint.isEmpty());
}

TEST_CASE("canonical fingerprint remains scoped to an account")
{
    auto first = transaction({}, {}, -1);
    auto second = first;
    second.accountExternalId = QStringLiteral("account-2");

    CHECK(canonicalTransactionFingerprint(first) != canonicalTransactionFingerprint(second));
}
