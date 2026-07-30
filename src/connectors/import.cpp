module;

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <expected>

module wt.connectors;

namespace wealthtracker::connectors {

QString ImportSource::effectiveName() const
{
    if (!displayName.isEmpty()) {
        return displayName;
    }
    return QFileInfo(filePath).fileName();
}

ConnectorResult<QByteArray> readImportSource(const ImportSource &source, const qsizetype maximumBytes)
{
    if (maximumBytes <= 0) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput, QStringLiteral("Invalid import size limit"), {}, false});
    }
    if (!source.content.isNull()) {
        if (source.content.size() > maximumBytes) {
            return std::unexpected(ConnectorError{
                ErrorCode::SecurityViolation,
                QStringLiteral("The import is larger than the configured safety limit"), {}, false});
        }
        return source.content;
    }
    if (source.filePath.isEmpty()) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput, QStringLiteral("Import source has no content or file path"), {},
            false});
    }

    QFile file(source.filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(ConnectorError{
            ErrorCode::InvalidInput, QStringLiteral("Cannot open the import file"),
            file.errorString(), false});
    }
    if (file.size() > maximumBytes) {
        return std::unexpected(ConnectorError{
            ErrorCode::SecurityViolation,
            QStringLiteral("The import is larger than the configured safety limit"), {}, false});
    }
    const auto bytes = file.read(maximumBytes + 1);
    if (bytes.size() > maximumBytes) {
        return std::unexpected(ConnectorError{
            ErrorCode::SecurityViolation,
            QStringLiteral("The import grew beyond the configured safety limit"), {}, false});
    }
    return bytes;
}

ConnectorResult<ImportPreview>
IImportConnector::preview(const ImportSource &source, const ImportOptions &options) const
{
    auto table = parse(source, options);
    if (!table) {
        return std::unexpected(table.error());
    }

    ImportOptions previewOptions = options;
    previewOptions.previewRowLimit =
        options.previewRowLimit > 0 ? options.previewRowLimit : 100;
    auto batch = normalize(*table, previewOptions);
    if (!batch) {
        return std::unexpected(batch.error());
    }

    const auto result = probe(source);
    const bool warningsNeedReview = std::ranges::any_of(
        batch->warnings, [](const ImportWarning &warning) {
            return warning.severity == WarningSeverity::ReviewRequired
                || warning.severity == WarningSeverity::RejectedRow;
        });
    return ImportPreview{result, std::move(*batch), table->rows.size(),
                         result.requiresReview || warningsNeedReview};
}

std::shared_ptr<IImportConnector> makeGenericImportConnector()
{
    return makeBankPresetConnector(genericPreset());
}

QVector<std::shared_ptr<IImportConnector>> makeBuiltInImportConnectors()
{
    QVector<std::shared_ptr<IImportConnector>> result;
    for (const auto &preset : russianBankPresets()) {
        result.push_back(makeBankPresetConnector(preset));
    }
    result.push_back(makeGenericImportConnector());
    return result;
}

std::shared_ptr<IImportConnector>
selectBestImportConnector(const QVector<std::shared_ptr<IImportConnector>> &connectors,
                          const ImportSource &source, const int minimumConfidence)
{
    std::shared_ptr<IImportConnector> best;
    int bestConfidence = minimumConfidence - 1;
    for (const auto &connector : connectors) {
        if (!connector) {
            continue;
        }
        const auto result = connector->probe(source);
        if (result.confidence > bestConfidence) {
            best = connector;
            bestConfidence = result.confidence;
        }
    }
    return best;
}

} // namespace wealthtracker::connectors
