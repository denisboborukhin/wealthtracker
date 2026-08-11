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

TEST_CASE("dashboard keeps populated summaries inside responsive surfaces") {
    const auto source = readQml(QStringLiteral("pages/DashboardPage.qml"));

    CHECK(source.contains(QByteArrayLiteral("ScrollBar.vertical")));
    CHECK(source.contains(QByteArrayLiteral("id: categoryList")));
    CHECK(source.contains(QByteArrayLiteral("clip: true")));
    CHECK(source.contains(QByteArrayLiteral(
        "Math.max(0, Math.min(1, categoryRow.share))")));
    CHECK(source.contains(QByteArrayLiteral("Layout.minimumWidth: 0")));
    CHECK(source.contains(QByteArrayLiteral(
        "id: recentOperationsCard")));
    CHECK(source.contains(QByteArrayLiteral(
        "recentOperationsPreferredHeight: Math.max(")));
    CHECK(source.contains(QByteArrayLiteral(
        "height - Theme.pageMargin * 2 - dashboardHeader.implicitHeight")));
    CHECK(source.contains(QByteArrayLiteral(
        "Layout.minimumHeight: page.recentOperationsMinimumHeight")));
    CHECK(source.contains(QByteArrayLiteral(
        "Layout.preferredHeight: page.recentOperationsPreferredHeight")));
    CHECK_FALSE(source.contains(QByteArrayLiteral("id: categoryRepeater")));
}

TEST_CASE("privacy screen describes its scope and limitations") {
    const auto settings = readQml(QStringLiteral("pages/SettingsPage.qml"));
    const auto window = readQml(QStringLiteral("Main.qml"));

    CHECK(settings.contains(QByteArrayLiteral("Экран приватности")));
    CHECK(settings.contains(QByteArrayLiteral("Для возврата пароль не нужен")));
    CHECK(settings.contains(QByteArrayLiteral("останавливает текущие входы")));
    CHECK(settings.contains(QByteArrayLiteral("Данные не удаляются")));
    CHECK_FALSE(settings.contains(
        QByteArrayLiteral("Скрыть финансовые данные")));

    CHECK(window.contains(QByteArrayLiteral("Экран приватности включён")));
    CHECK(window.contains(QByteArrayLiteral("пароль не требуется")));
    CHECK(window.contains(QByteArrayLiteral("color: Theme.background")));
}
