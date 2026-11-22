#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QFile>
#include <QString>

namespace {

QByteArray readQml(const QString& relativePath) {
    QFile file(QStringLiteral(WEALTHTRACKER_SOURCE_DIR "/ui/qml/") +
               relativePath);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

}  // namespace

TEST_CASE("privacy screen describes its scope and limitations") {
    const auto settings = readQml(QStringLiteral("pages/SettingsPage.qml"));
    const auto privacy = readQml(QStringLiteral("components/PrivacyScreen.qml"));

    CHECK(settings.contains(QByteArrayLiteral("Экран приватности")));
    CHECK(settings.contains(QByteArrayLiteral("Для возврата пароль не нужен")));
    CHECK(settings.contains(QByteArrayLiteral("останавливает текущие входы")));
    CHECK(settings.contains(QByteArrayLiteral("Данные не удаляются")));
    CHECK_FALSE(settings.contains(
        QByteArrayLiteral("Скрыть финансовые данные")));

    CHECK(privacy.contains(QByteArrayLiteral("Экран приватности включён")));
    CHECK(privacy.contains(QByteArrayLiteral("пароль не требуется")));

}
