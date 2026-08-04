module;

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>

#include <expected>

export module wt.read_only_resource;

export namespace wealthtracker::app {

[[nodiscard]] std::expected<bool, QString> install_read_only_resource(
    const QString& destination, const QByteArray& contents) {
    QFile existing(destination);
    if (existing.open(QIODevice::ReadOnly)) {
        const bool unchanged = existing.readAll() == contents;
        existing.close();
        if (unchanged) {
            if (!QFile::setPermissions(destination, QFileDevice::ReadOwner)) {
                return std::unexpected(
                    QStringLiteral("cannot protect unchanged resource"));
            }
            return false;
        }
    }

    const bool replacing = QFileInfo::exists(destination);
    if (replacing
        && !QFile::setPermissions(
            destination,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        return std::unexpected(
            QStringLiteral("cannot unlock stale resource"));
    }

    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(contents) != contents.size()
        || !output.commit()) {
        output.cancelWriting();
        if (replacing) {
            (void)QFile::setPermissions(destination, QFileDevice::ReadOwner);
        }
        return std::unexpected(QStringLiteral("cannot replace resource"));
    }
    if (!QFile::setPermissions(destination, QFileDevice::ReadOwner)) {
        return std::unexpected(QStringLiteral("cannot protect new resource"));
    }
    return true;
}

}  // namespace wealthtracker::app
