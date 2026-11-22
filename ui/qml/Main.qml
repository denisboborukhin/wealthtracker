pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"
import "pages"

ApplicationWindow {
    id: window
    required property var app
    width: 1320
    height: 820
    minimumWidth: Qt.platform.os === "android" ? 320 : 1020
    minimumHeight: Qt.platform.os === "android" ? 0 : 680
    visible: true
    title: "WealthTracker"
    color: Theme.background
    property alias currentPage: navigation.currentIndex
    readonly property real navigationWidth: navigation.navigationWidth
    readonly property bool compact: navigation.compact
    readonly property var navigationItems: navigation.navigationItems

    Item {
        anchors.fill: parent

        AppNavigation {
            id: navigation
            anchors.fill: parent
            locked: window.app.locked
        }

        StackLayout {
            anchors.fill: parent
            anchors.leftMargin: navigation.contentLeft
            anchors.topMargin: navigation.contentTop
            currentIndex: navigation.currentIndex

            DashboardPage { app: window.app }
            HistoryPage { app: window.app }
            InvestmentsPage { app: window.app }
            CryptoPage { app: window.app }
            ConnectionsPage { app: window.app }
            SettingsPage { app: window.app }
            CashbackPage { app: window.app }
        }
    }

    PrivacyScreen {
        anchors.fill: parent
        visible: window.app.locked
        onUnlockRequested: window.app.unlock()
    }

}
