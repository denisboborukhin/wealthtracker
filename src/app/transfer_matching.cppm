module;

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <optional>
#include <span>

export module wt.transfer_matching;

export namespace wealthtracker::app {

struct OwnedTransaction {
    QString connectorId;
    QString accountExternalId;
    QString externalId;
    QDate postingDate;
    std::optional<QDateTime> occurredAt;
    std::int64_t minorUnits{0};
    QString currency;
    bool alreadyInternal{false};
};

[[nodiscard]] QSet<qsizetype>
findOwnAccountTransfers(std::span<const OwnedTransaction> transactions) {
    constexpr qint64 maximumPostingDayDistance = 3;
    constexpr qint64 maximumOccurredAtDistanceSeconds = 3 * 24 * 60 * 60;
    constexpr qsizetype maximumCandidatesPerOutgoing = 64;

    struct Edge {
        qsizetype outgoing{-1};
        qsizetype incoming{-1};
        bool hasExplicitMarker{false};
        qint64 distanceSeconds{0};
        QString stableKey;
    };

    QSet<qsizetype> transfers;
    QVector<Edge> edges;
    using ConnectorBuckets = QHash<QString, QVector<qsizetype>>;
    using DateBuckets = QHash<qint64, ConnectorBuckets>;
    QHash<QString, DateBuckets> incomingByAmount;
    for (qsizetype index = 0; index < std::ssize(transactions); ++index) {
        const auto& transaction = transactions[static_cast<std::size_t>(index)];
        if (transaction.alreadyInternal) {
            transfers.insert(index);
        }
        if (transaction.minorUnits <= 0 || !transaction.postingDate.isValid() ||
            transaction.connectorId.isEmpty() || transaction.accountExternalId.isEmpty() ||
            transaction.externalId.isEmpty() || transaction.currency.isEmpty()) {
            continue;
        }
        const auto amountKey =
            transaction.currency + QChar(0x1f) + QString::number(transaction.minorUnits);
        incomingByAmount[amountKey][transaction.postingDate.toJulianDay()][transaction.connectorId]
            .push_back(index);
    }

    for (qsizetype outgoingIndex = 0; outgoingIndex < std::ssize(transactions); ++outgoingIndex) {
        const auto& outgoing = transactions[static_cast<std::size_t>(outgoingIndex)];
        if (outgoing.minorUnits >= 0 || !outgoing.postingDate.isValid() ||
            outgoing.connectorId.isEmpty() || outgoing.accountExternalId.isEmpty() ||
            outgoing.externalId.isEmpty() || outgoing.currency.isEmpty() ||
            outgoing.minorUnits == std::numeric_limits<std::int64_t>::min()) {
            continue;
        }
        const auto amountKey =
            outgoing.currency + QChar(0x1f) + QString::number(-outgoing.minorUnits);
        const auto amountBucket = incomingByAmount.constFind(amountKey);
        if (amountBucket == incomingByAmount.cend()) {
            continue;
        }

        QVector<Edge> outgoingEdges;
        bool ambiguous = false;
        const auto outgoingKey = outgoing.connectorId + QChar(0x1f) + outgoing.accountExternalId +
                                 QChar(0x1f) + outgoing.externalId;
        for (qint64 dayOffset = -maximumPostingDayDistance;
             dayOffset <= maximumPostingDayDistance && !ambiguous; ++dayOffset) {
            const auto dateBucket =
                amountBucket->constFind(outgoing.postingDate.toJulianDay() + dayOffset);
            if (dateBucket == amountBucket->cend()) {
                continue;
            }
            auto connectors = dateBucket->keys();
            std::sort(connectors.begin(), connectors.end());
            for (const auto& connector : connectors) {
                if (connector == outgoing.connectorId) {
                    continue;
                }
                const auto connectorBucket = dateBucket->constFind(connector);
                if (connectorBucket == dateBucket->cend()) {
                    continue;
                }
                for (const auto incomingIndex : *connectorBucket) {
                    const auto& incoming = transactions[static_cast<std::size_t>(incomingIndex)];
                    qint64 distanceSeconds = std::abs(dayOffset) * 24 * 60 * 60;
                    if (outgoing.occurredAt && incoming.occurredAt &&
                        outgoing.occurredAt->isValid() && incoming.occurredAt->isValid()) {
                        distanceSeconds =
                            std::abs(outgoing.occurredAt->secsTo(*incoming.occurredAt));
                        if (distanceSeconds > maximumOccurredAtDistanceSeconds) {
                            continue;
                        }
                    }
                    const auto incomingKey = incoming.connectorId + QChar(0x1f) +
                                             incoming.accountExternalId + QChar(0x1f) +
                                             incoming.externalId;
                    outgoingEdges.push_back({outgoingIndex, incomingIndex,
                                             outgoing.alreadyInternal || incoming.alreadyInternal,
                                             distanceSeconds,
                                             outgoingKey + QChar(0x1e) + incomingKey});
                    if (outgoingEdges.size() > maximumCandidatesPerOutgoing) {
                        ambiguous = true;
                        break;
                    }
                }
                if (ambiguous) {
                    break;
                }
            }
        }
        if (!ambiguous) {
            edges.append(std::move(outgoingEdges));
        }
    }

    std::sort(edges.begin(), edges.end(), [](const Edge& left, const Edge& right) {
        if (left.hasExplicitMarker != right.hasExplicitMarker) {
            return left.hasExplicitMarker;
        }
        if (left.distanceSeconds != right.distanceSeconds) {
            return left.distanceSeconds < right.distanceSeconds;
        }
        return left.stableKey < right.stableKey;
    });
    QSet<qsizetype> pairedOutgoing;
    QSet<qsizetype> pairedIncoming;
    for (const auto& edge : edges) {
        if (pairedOutgoing.contains(edge.outgoing) || pairedIncoming.contains(edge.incoming)) {
            continue;
        }
        pairedOutgoing.insert(edge.outgoing);
        pairedIncoming.insert(edge.incoming);
        transfers.insert(edge.outgoing);
        transfers.insert(edge.incoming);
    }
    return transfers;
}

} // namespace wealthtracker::app
