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
        "- reportingPeriodCard.implicitHeight - metricGrid.implicitHeight")));
    CHECK(source.contains(QByteArrayLiteral(
        "Layout.minimumHeight: page.recentOperationsMinimumHeight")));
    CHECK(source.contains(QByteArrayLiteral(
        "Layout.preferredHeight: page.recentOperationsPreferredHeight")));
    CHECK_FALSE(source.contains(QByteArrayLiteral("id: categoryRepeater")));
    CHECK(source.contains(QByteArrayLiteral("value: page.app.savings")));
    CHECK_FALSE(source.contains(QByteArrayLiteral("savingsRate")));
    CHECK(source.contains(QByteArrayLiteral(
        "columns: page.width > 900 ? 4 : page.width < 600 ? 1 : 2")));
    CHECK(source.contains(QByteArrayLiteral(
        "columns: page.width > 860 ? 2 : 1")));

}

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

    const auto connections =
        readQml(QStringLiteral("pages/ConnectionsPage.qml"));
    CHECK(connections.contains(QByteArrayLiteral("ScrollView {")));
    CHECK(connections.contains(QByteArrayLiteral("parent.height - 32")));

    const auto actionButton =
        readQml(QStringLiteral("components/ActionButton.qml"));
    CHECK(actionButton.contains(QByteArrayLiteral("root.activeFocus ? 2 : 1")));

    const auto investments =
        readQml(QStringLiteral("pages/InvestmentsPage.qml"));
    CHECK(investments.contains(QByteArrayLiteral("Layout.minimumWidth: 0")));
    CHECK(investments.contains(QByteArrayLiteral("elide: Text.ElideRight")));
}

TEST_CASE("retail connection dialogs never display another bank's sync status") {
    const auto source = readQml(QStringLiteral("pages/ConnectionsPage.qml"));
    CHECK_FALSE(source.contains(QByteArrayLiteral("page.app.syncStatus")));
    const auto adapter = readQml(QStringLiteral("components/RetailConnection.qml"));
    CHECK_FALSE(adapter.contains(QByteArrayLiteral("app.syncStatus")));
    CHECK(source.contains(QByteArrayLiteral("selectedRetail.loginStatus")));

}
