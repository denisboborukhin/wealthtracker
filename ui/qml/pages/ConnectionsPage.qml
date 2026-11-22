pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as PlatformDialogs
import QtQuick.Layouts
import "../components"

Item {
    id: page
    required property var app
    readonly property var hostWindow: ApplicationWindow.window
    readonly property bool mobileLogin: Qt.platform.os === "android" && connectionDialog.retailLoginActive

    function stateTone(state) {
        return state === "ready" ? "positive"
             : state === "error" ? "warning" : "neutral"
    }

    function openConnection(connectorId, institution, method, status,
                            connected, available) {
        page.app.describeBankConnection(connectorId)
        connectionDialog.connectorId = connectorId
        connectionDialog.institution = institution
        connectionDialog.method = method
        connectionDialog.status = status
        connectionDialog.connected = connected
        connectionDialog.available = available
        connectionDialog.open()
        selectedRetail.prepare()
    }

    RetailConnection {
        id: selectedRetail
        app: page.app
        connectorId: connectionDialog.connectorId
        fallbackConnected: connectionDialog.connected
    }

    PlatformDialogs.FileDialog {
        id: pluginBundleDialog
        title: qsTr("Выберите собранный ZenPlugins bundle")
        nameFilters: [qsTr("JavaScript bundle (*.js *.cjs)")]
        onAccepted: {
            pluginEditor.resetPreferences()
            page.app.inspectZenPluginBundle(selectedFile)
        }
    }

    Dialog {
        id: connectionDialog
        objectName: "connectionDialog"
        parent: Overlay.overlay
        modal: true
        focus: true
        width: page.mobileLogin ? parent.width : Math.min(parent.width - 32,
                        connectionDialog.retailLoginActive ? 760 : 520)
        height: page.mobileLogin ? parent.height : Math.min(parent.height - 32,
                         connectionDialog.retailLoginActive ? 760 : implicitHeight)
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 0
        topPadding: page.mobileLogin ? SafeArea.margins.top : 0
        bottomPadding: page.mobileLogin ? SafeArea.margins.bottom : 0
        leftPadding: page.mobileLogin ? SafeArea.margins.left : 0
        rightPadding: page.mobileLogin ? SafeArea.margins.right : 0

        property string connectorId: ""
        property string institution: ""
        property string method: ""
        property string status: ""
        property bool connected: false
        property bool available: false
        readonly property var currentState: page.app.connectionStates[connectorId] || ({})
        readonly property bool builtinTBank:
            connectorId === "tbank-retail"
        readonly property bool builtinAlfa:
            connectorId === "alfa-retail"
        readonly property bool builtinVtb:
            connectorId === "vtb-retail"
        readonly property bool builtinOzon:
            connectorId === "ozon-retail"
        readonly property bool builtinRetail: selectedRetail.supported
        readonly property bool localPlugin: connectorId === "zenplugins-local"
        readonly property bool effectiveConnected: selectedRetail.connected
        readonly property bool retailLoginActive: selectedRetail.loginActive
        readonly property bool retailSyncRunning: selectedRetail.syncRunning
        readonly property bool retailAutoUpdate: selectedRetail.autoUpdate
        readonly property string retailLastSync: selectedRetail.lastSync
        readonly property string retailNextSync: selectedRetail.nextSync
        readonly property string retailLoginStatus: selectedRetail.loginStatus
        readonly property var retailWebView: selectedRetail.webView

        background: Rectangle {
            color: Theme.surface
            radius: 12
            border.color: Theme.border
        }

        contentItem: Item {
            implicitHeight: connectionScroll.implicitHeight

        ScrollView {
            anchors.fill: parent
            visible: !page.mobileLogin
            id: connectionScroll
            objectName: "connectionScroll"
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                id: connectionContent
                width: connectionScroll.availableWidth
                spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 24
                spacing: 8

                Text {
                    textFormat: Text.PlainText
                    objectName: "connectionInstitution"
                    text: connectionDialog.institution
                    color: Theme.text
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    text: connectionDialog.method
                    color: Theme.textMuted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: connectionDialog.builtinRetail
                                   && connectionDialog.retailLoginActive
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: 20
                Layout.bottomMargin: 24
                spacing: 14

                Text {
                    Layout.fillWidth: true
                    visible: !connectionDialog.retailLoginActive
                    textFormat: Text.PlainText
                    text: page.app.connectionMessage
                    color: Theme.textMuted
                    font.pixelSize: 13
                    lineHeight: 1.35
                    wrapMode: Text.WordWrap
                }

                BankTokenEditor {
                    id: tokenEditor
                    Layout.fillWidth: true
                    connectorId: connectionDialog.connectorId
                    connected: connectionDialog.connected
                    visible: connectionDialog.available
                               && !connectionDialog.connected
                               && !connectionDialog.builtinRetail
                               && !connectionDialog.localPlugin
                }

                StatusBadge {
                    visible: connectionDialog.available
                             && connectionDialog.effectiveConnected
                             && !connectionDialog.builtinRetail
                             && !connectionDialog.localPlugin
                    Layout.fillWidth: true
                    text: connectionDialog.currentState.text || connectionDialog.status
                    tone: page.stateTone(connectionDialog.currentState.state)
                }

                RetailLoginContent {
                    Layout.fillWidth: true
                    Layout.fillHeight: connectionDialog.retailLoginActive
                    visible: connectionDialog.builtinRetail
                             && (!connectionDialog.effectiveConnected
                                 || connectionDialog.retailLoginActive)
                    loginActive: connectionDialog.retailLoginActive
                    mobileLogin: page.mobileLogin
                    tbank: connectionDialog.builtinTBank
                    status: connectionDialog.retailLoginStatus
                    webView: connectionDialog.retailWebView
                }

                PluginConnectionEditor {
                    id: pluginEditor
                    Layout.fillWidth: true
                    visible: connectionDialog.localPlugin
                    app: page.app
                    onChooseBundle: pluginBundleDialog.open()
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: connectionDialog.builtinRetail
                             && connectionDialog.effectiveConnected
                             && !connectionDialog.retailLoginActive
                    spacing: 10

                    StatusBadge {
                        Layout.fillWidth: true
                        text: connectionDialog.currentState.text || connectionDialog.status
                        tone: page.stateTone(connectionDialog.currentState.state)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Автообновление")
                            color: Theme.text
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }
                        MinimalSwitch {
                            accessibleName: qsTr("Автообновление банковского подключения")
                            checked: connectionDialog.retailAutoUpdate
                            enabled: !connectionDialog.retailSyncRunning
                            onToggled: selectedRetail.setAutoUpdate(checked)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Последнее: ") + connectionDialog.retailLastSync
                              + qsTr(" · Следующее: ") + connectionDialog.retailNextSync
                        color: Theme.textMuted
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        objectName: "connectedRetailStatus"
                        textFormat: Text.PlainText
                        text: connectionDialog.retailLoginStatus
                        color: connectionDialog.retailLoginStatus.toLowerCase().indexOf("не удалось") >= 0
                               || connectionDialog.retailLoginStatus.toLowerCase().indexOf("ошиб") >= 0
                               ? Theme.negative : Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        objectName: "closeConnectionDialog"
                        text: qsTr("Закрыть")
                        onClicked: connectionDialog.close()
                    }
                    ActionButton {
                        visible: !connectionDialog.available
                        text: qsTr("Плагин не установлен")
                        enabled: false
                    }
                    ActionButton {
                        visible: connectionDialog.available
                                 && !connectionDialog.effectiveConnected
                                 && !connectionDialog.builtinRetail
                                 && !connectionDialog.localPlugin
                        text: qsTr("Сохранить токен")
                        primary: true
                        enabled: tokenEditor.canSave
                        onClicked: {
                            if (page.app.saveBankToken(connectionDialog.connectorId,
                                                  tokenEditor.token)) {
                                tokenEditor.clear()
                                connectionDialog.close()
                            }
                        }
                    }
                    ActionButton {
                        visible: connectionDialog.available
                                 && connectionDialog.effectiveConnected
                                 && !connectionDialog.builtinRetail
                                 && !connectionDialog.localPlugin
                        text: qsTr("Синхронизировать")
                        primary: true
                        onClicked: {
                            page.app.synchronizeBank(connectionDialog.connectorId)
                            connectionDialog.close()
                        }
                    }
                    ActionButton {
                        visible: connectionDialog.available
                                 && connectionDialog.effectiveConnected
                                 && !connectionDialog.builtinRetail
                                 && !connectionDialog.localPlugin
                        text: qsTr("Отключить")
                        destructive: true
                        onClicked: {
                            page.app.removeBankToken(connectionDialog.connectorId)
                            connectionDialog.close()
                        }
                    }
                    ActionButton {
                        visible: connectionDialog.builtinRetail
                                 && (!connectionDialog.effectiveConnected
                                     || connectionDialog.builtinVtb || connectionDialog.builtinAlfa
                                     || connectionDialog.builtinOzon)
                                 && !connectionDialog.retailLoginActive
                                 && (!connectionDialog.retailSyncRunning
                                     || connectionDialog.builtinVtb || connectionDialog.builtinAlfa
                                     || connectionDialog.builtinOzon)
                        text: !connectionDialog.effectiveConnected ? qsTr("Войти")
                              : connectionDialog.builtinVtb ? qsTr("Открыть ВТБ") : qsTr("Открыть банк")
                        primary: true
                        onClicked: selectedRetail.beginLogin()
                    }
                    ActionButton {
                        visible: connectionDialog.builtinRetail
                                 && connectionDialog.retailLoginActive
                        text: qsTr("Повторить")
                        onClicked: selectedRetail.reloadLogin()
                    }
                    ActionButton {
                        visible: connectionDialog.builtinAlfa
                                 && connectionDialog.retailLoginActive
                                 && selectedRetail.certificateHelpRequired
                        objectName: "certificateInstructions"
                        text: qsTr("Как установить сертификаты")
                        onClicked: selectedRetail.openCertificateInstructions()
                    }
                    ActionButton {
                        visible: connectionDialog.builtinRetail
                                 && connectionDialog.retailLoginActive
                        text: qsTr("Отмена")
                        destructive: true
                        onClicked: selectedRetail.cancelLogin()
                    }
                    ActionButton {
                        visible: connectionDialog.localPlugin
                                 && page.app.zenPluginRunning
                                 && page.app.zenPluginPrompt.length === 0
                        text: qsTr("Отменить")
                        destructive: true
                        onClicked: page.app.cancelZenPlugin()
                    }
                    ActionButton {
                        visible: connectionDialog.localPlugin
                                 && page.app.zenPluginRunning
                                 && page.app.zenPluginPrompt.length > 0
                        text: qsTr("Передать")
                        primary: true
                        enabled: pluginEditor.canAnswer
                        onClicked: pluginEditor.answerPrompt()
                    }
                    ActionButton {
                        visible: connectionDialog.localPlugin
                                 && !page.app.zenPluginRunning
                                 && page.app.zenPluginId.length > 0
                        text: qsTr("Обновить")
                        onClicked: page.app.synchronizeZenPlugin()
                    }
                    ActionButton {
                        visible: connectionDialog.localPlugin
                                 && !page.app.zenPluginRunning
                                 && page.app.zenPluginId.length > 0
                        text: qsTr("Запустить")
                        primary: true
                        enabled: pluginEditor.canStart
                        onClicked: pluginEditor.start()
                    }
                    ActionButton {
                        visible: connectionDialog.localPlugin
                                 && !page.app.zenPluginRunning
                                 && page.app.zenPluginId.length > 0
                        text: qsTr("Удалить")
                        destructive: true
                        onClicked: {
                            page.app.removeZenPlugin()
                            connectionDialog.close()
                        }
                    }
                    ActionButton {
                        visible: connectionDialog.builtinRetail
                                 && (connectionDialog.effectiveConnected || selectedRetail.pendingSessionValidation)
                                 && !connectionDialog.retailSyncRunning
                                 && !connectionDialog.retailLoginActive
                        text: qsTr("Обновить")
                        primary: true
                        onClicked: selectedRetail.synchronize()
                    }
                    ActionButton {
                        visible: connectionDialog.builtinRetail
                                 && (connectionDialog.effectiveConnected || selectedRetail.savedSession)
                                 && !connectionDialog.retailSyncRunning
                                 && !connectionDialog.retailLoginActive
                        text: qsTr("Отключить")
                        destructive: true
                        onClicked: {
                            selectedRetail.disconnectBank()
                            connectionDialog.close()
                        }
                    }
                }
            }
        }
        }

        MobileRetailLoginContent {
            anchors.fill: parent
            anchors.margins: 12
            visible: page.mobileLogin
            institution: connectionDialog.institution
            status: connectionDialog.retailLoginStatus
            mobileLogin: page.mobileLogin
            webView: connectionDialog.retailWebView
            onCloseRequested: connectionDialog.close()
            onReloadRequested: selectedRetail.reloadLogin()
        }

        }

        onClosed: {
            if (connectionDialog.builtinRetail
                    && connectionDialog.retailLoginActive)
                selectedRetail.cancelLogin()
            const hostWindow = page.hostWindow
            if (Qt.platform.os === "android" && hostWindow) Qt.callLater(() => hostWindow.requestActivate())
            tokenEditor.clear()
            pluginEditor.clear()
        }

    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pageMargin
        spacing: 20

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 5
            Text {
                text: qsTr("Подключения")
                color: Theme.text
                font.pixelSize: 26
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Встроенные подключения, официальные API и проверяемые локальные плагины. Токены хранятся в системном хранилище ключей; браузерные сессии — в системном WebView.")
                color: Theme.textMuted
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
        }

        ConnectionList {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: page.app.bankConnections
            pageWidth: page.width
            onConnectionRequested: function(connectorId, institution, method, status, connected, available) {
                page.openConnection(connectorId, institution, method, status, connected, available)
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Т-Банк, Альфа-Банк, ВТБ и Ozon Банк используют встроенные локальные коннекторы. Для Сбера нужен отдельный проверяемый адаптер.")
            color: Theme.textMuted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
    }
}
