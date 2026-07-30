module;

#include <QByteArray>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QString>

module wt.connectors;

namespace wealthtracker::connectors {
namespace {

QString normalizedDescription(QString value)
{
    value = value.normalized(QString::NormalizationForm_KC).toCaseFolded();
    value.replace(u'ё', u'е');
    value.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")),
                  QStringLiteral(" "));
    return value.simplified();
}

QByteArray scopedExternalId(const NormalizedTransaction &transaction)
{
    if (transaction.externalId.isEmpty()) {
        return {};
    }
    return (transaction.accountExternalId + u'\0' + transaction.externalId).toUtf8();
}

void appendField(QByteArray &buffer, const QByteArray &field)
{
    buffer += QByteArray::number(field.size());
    buffer += ':';
    buffer += field;
}

} // namespace

QByteArray canonicalTransactionFingerprint(const NormalizedTransaction &transaction)
{
    QByteArray canonical;
    appendField(canonical, transaction.accountExternalId.toUtf8());
    appendField(canonical, transaction.postingDate.toString(Qt::ISODate).toLatin1());
    appendField(canonical, QByteArray::number(transaction.amount.minorUnits));
    appendField(canonical, transaction.amount.currency.trimmed().toUpper().toLatin1());
    appendField(canonical, normalizedDescription(transaction.description).toUtf8());
    return QCryptographicHash::hash(canonical, QCryptographicHash::Sha256);
}

QByteArray provenanceKey(const Provenance &provenance)
{
    if (provenance.sourceFileSha256.isEmpty() || provenance.sourceRow < 0) {
        return {};
    }
    QByteArray canonical;
    appendField(canonical, provenance.connectorId.toUtf8());
    appendField(canonical, provenance.sourceFileSha256.toHex());
    appendField(canonical, QByteArray::number(provenance.sourceRow));
    return QCryptographicHash::hash(canonical, QCryptographicHash::Sha256);
}

DuplicateDecision classifyDuplicate(const NormalizedTransaction &transaction,
                                    const DeduplicationIndex &index)
{
    const auto fingerprint = canonicalTransactionFingerprint(transaction);
    const auto externalId = scopedExternalId(transaction);
    if (!externalId.isEmpty() && index.externalIds.contains(externalId)) {
        return {DuplicateDisposition::ExactDuplicate, fingerprint,
                QStringLiteral("The source transaction ID was already imported")};
    }
    const auto provenance = provenanceKey(transaction.provenance);
    if (!provenance.isEmpty() && index.provenanceKeys.contains(provenance)) {
        return {DuplicateDisposition::ExactDuplicate, fingerprint,
                QStringLiteral("The same source file row was already imported")};
    }
    if (index.canonicalFingerprintCounts.value(fingerprint, 0) > 0) {
        return {DuplicateDisposition::AmbiguousMatch, fingerprint,
                QStringLiteral("A similar transaction exists and requires review")};
    }
    return {DuplicateDisposition::NewRecord, fingerprint, {}};
}

void addToDeduplicationIndex(const NormalizedTransaction &transaction, DeduplicationIndex &index)
{
    const auto externalId = scopedExternalId(transaction);
    if (!externalId.isEmpty()) {
        index.externalIds.insert(externalId);
    }
    const auto provenance = provenanceKey(transaction.provenance);
    if (!provenance.isEmpty()) {
        index.provenanceKeys.insert(provenance);
    }
    const auto fingerprint = canonicalTransactionFingerprint(transaction);
    index.canonicalFingerprintCounts[fingerprint] += 1;
}

} // namespace wealthtracker::connectors
