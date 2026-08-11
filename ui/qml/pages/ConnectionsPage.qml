pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as PlatformDialogs
import QtQuick.Layouts
import "../components"

Item {
    id: page
    required property var app

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
        if (connectorId === "tbank-retail")
            page.app.prepareTBankConnection()
    }

    PlatformDialogs.FileDialog {
        id: pluginBundleDialog
        title: qsTr("Выберите собранный ZenPlugins bundle")
        nameFilters: [qsTr("JavaScript bundle (*.js *.cjs)")]
        onAccepted: {
            connectionDialog.pluginValues = ({})
            page.app.inspectZenPluginBundle(selectedFile)
        }
    }

    Dialog {
        id: connectionDialog
        parent: Overlay.overlay
        modal: true
        focus: true
        width: page.app.tbankLoginActive
               ? Math.min(parent.width - 32, 760) : 520
        height: page.app.tbankLoginActive
                ? Math.min(parent.height - 32, 760) : implicitHeight
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 0

        property string connectorId: ""
        property string institution: ""
        property string method: ""
        property string status: ""
        property bool connected: false
        property bool available: false
        property var pluginValues: ({})
        signal resetSensitivePluginFields()
        readonly property bool builtinTBank:
            connectorId === "tbank-retail"
        readonly property bool localPlugin:
            connectorId === "zenplugins-local"
        readonly property bool effectiveConnected:
            builtinTBank ? page.app.tbankConnected : connected

        function pluginPreferences() {
            return pluginValues
        }

        function setPluginPreference(key, value) {
            let values = Object.assign({}, pluginValues)
            values[key] = value
            pluginValues = values
        }

        function clearSensitivePluginFields() {
            resetSensitivePluginFields()
        }

        background: Rectangle {
            color: Theme.surface
            radius: 12
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 24
                spacing: 8

                Text {
                    text: connectionDialog.institution
                    color: Theme.text
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
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
                Layout.fillHeight: connectionDialog.builtinTBank
                                   && page.app.tbankLoginActive
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: 20
                Layout.bottomMargin: 24
                spacing: 14

                Text {
                    Layout.fillWidth: true
                    visible: !page.app.tbankLoginActive
                    text: page.app.connectionMessage
                    color: Theme.textMuted
                    font.pixelSize: 13
                    lineHeight: 1.35
                    wrapMode: Text.WordWrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: connectionDialog.available
                               && !connectionDialog.connected
                               && !connectionDialog.builtinTBank
                               && !connectionDialog.localPlugin
                    spacing: 7

                    Text {
                        text: qsTr("Токен только для чтения")
                        color: Theme.text
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                    TextField {
                        id: tokenField
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        echoMode: TextInput.Password
                        placeholderText: connectionDialog.connectorId === "alfa-api"
                                         ? qsTr("Access token Альфа-Банка")
                                         : connectionDialog.connectorId === "tbank-business"
                                           ? qsTr("T-API токен")
                                           : connectionDialog.connectorId === "modulbank"
                                             ? qsTr("API-токен или sandboxtoken")
                                             : qsTr("Read-only токен Т-Инвест")
                        color: Theme.text
                        selectByMouse: true
                        background: Rectangle {
                            color: Theme.surface
                            border.color: tokenField.activeFocus
                                          ? Theme.primary : Theme.borderStrong
                            radius: 7
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Токен хранится в системном хранилище ключей и используется только для чтения.")
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }

                StatusBadge {
                    visible: connectionDialog.available
                             && connectionDialog.effectiveConnected
                             && !connectionDialog.builtinTBank
                             && !connectionDialog.localPlugin
                    text: qsTr("Подключено")
                    tone: "positive"
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: page.app.tbankLoginActive
                    visible: connectionDialog.builtinTBank
                             && !connectionDialog.effectiveConnected
                    spacing: 10

                    Text {
                        Layout.fillWidth: true
                        text: page.app.tbankLoginStatus.length > 0
                              ? page.app.tbankLoginStatus
                              : qsTr("Нажмите «Войти», чтобы открыть официальную страницу Т-Банка внутри приложения.")
                        color: Theme.text
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 340
                        visible: page.app.tbankLoginActive
                        color: Theme.surface
                        radius: 8
                        border.color: Theme.border
                        clip: true

                        WindowContainer {
                            anchors.fill: parent
                            anchors.margins: 1
                            visible: page.app.tbankLoginActive
                            window: page.app.tbankWebView
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: page.app.tbankLoginActive
                        text: qsTr("Телефон и код обрабатывает официальная страница банка. Веб-хранилище непостоянно; WealthTracker получает только идентификатор API-сессии и удаляет cookies после завершения.")
                        color: Theme.textMuted
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: connectionDialog.localPlugin
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: page.app.zenPluginId.length > 0
                                      ? page.app.zenPluginId
                                      : qsTr("Bundle не выбран")
                                color: Theme.text
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }
                            Text {
                                Layout.fillWidth: true
                                text: page.app.zenPluginBundlePath.length > 0
                                      ? page.app.zenPluginBundlePath
                                      : qsTr("Сначала соберите plugin через yarn build <plugin-id>")
                                color: Theme.textMuted
                                font.pixelSize: 10
                                elide: Text.ElideMiddle
                            }
                        }
                        ActionButton {
                            text: qsTr("Выбрать bundle")
                            enabled: !page.app.zenPluginRunning
                            onClicked: pluginBundleDialog.open()
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: page.app.zenPluginId.length > 0
                                 && !page.app.zenPluginRunning
                        spacing: 7

                        Text {
                            text: qsTr("Название банка")
                            color: Theme.text
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }
                        TextField {
                            id: pluginInstitutionField
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            text: page.app.zenPluginInstitution
                            placeholderText: qsTr("Как показывать банк в счетах")
                            color: Theme.text
                            selectByMouse: true
                            background: Rectangle {
                                color: Theme.surface
                                border.color: pluginInstitutionField.activeFocus
                                              ? Theme.primary : Theme.borderStrong
                                radius: 7
                            }
                        }

                        Repeater {
                            id: preferenceRepeater
                            model: page.app.zenPluginFields

                            delegate: ColumnLayout {
                                id: preferenceField
                                required property var modelData
                                readonly property string fieldKey: preferenceField.modelData.key
                                readonly property bool sensitive: preferenceField.modelData.sensitive
                                readonly property var fieldValue:
                                    preferenceField.modelData.kind === "checkbox"
                                    ? checkboxField.checked
                                    : valueField.text

                                function clearValue() {
                                    valueField.clear()
                                }

                                Component.onCompleted:
                                    connectionDialog.setPluginPreference(
                                        preferenceField.fieldKey,
                                        preferenceField.fieldValue)

                                Connections {
                                    target: connectionDialog
                                    function onResetSensitivePluginFields() {
                                        if (preferenceField.sensitive)
                                            preferenceField.clearValue()
                                    }
                                }

                                Layout.fillWidth: true
                                spacing: 5

                                Text {
                                    text: preferenceField.modelData.label
                                          + (preferenceField.modelData.required ? " *" : "")
                                    color: Theme.text
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                }
                                TextField {
                                    id: valueField
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 40
                                    visible: preferenceField.modelData.kind !== "checkbox"
                                    text: preferenceField.modelData.defaultValue
                                    echoMode: preferenceField.modelData.sensitive
                                              ? TextInput.Password
                                              : TextInput.Normal
                                    inputMethodHints:
                                        preferenceField.modelData.kind === "number"
                                        || preferenceField.modelData.kind === "phone"
                                        ? Qt.ImhDigitsOnly : Qt.ImhNone
                                    color: Theme.text
                                    selectByMouse: true
                                    onTextChanged:
                                        connectionDialog.setPluginPreference(
                                            preferenceField.fieldKey, text)
                                    background: Rectangle {
                                        color: Theme.surface
                                        border.color: valueField.activeFocus
                                                      ? Theme.primary
                                                      : Theme.borderStrong
                                        radius: 7
                                    }
                                }
                                MinimalSwitch {
                                    id: checkboxField
                                    visible: preferenceField.modelData.kind === "checkbox"
                                    checked: preferenceField.modelData.defaultValue === true
                                             || preferenceField.modelData.defaultValue === "true"
                                    onCheckedChanged:
                                        connectionDialog.setPluginPreference(
                                            preferenceField.fieldKey, checked)
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Поля входа живут только до завершения запуска. Bundle — сторонний код без песочницы; запускайте только проверенную сборку.")
                            color: Theme.warning
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: page.app.zenPluginId.length > 0
                        spacing: 7

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
                                checked: page.app.zenPluginAutoUpdate
                                enabled: !page.app.zenPluginRunning
                                onToggled: page.app.setZenPluginAutoUpdate(checked)
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Последнее: ") + page.app.zenPluginLastSync
                                  + qsTr(" · Следующее: ") + page.app.zenPluginNextSync
                            color: Theme.textMuted
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: page.app.zenPluginRunning
                        spacing: 7

                        StatusBadge {
                            text: page.app.zenPluginPrompt.length > 0
                                  ? qsTr("Требуется ответ")
                                  : qsTr("Синхронизация")
                            tone: page.app.zenPluginPrompt.length > 0
                                  ? "warning" : "neutral"
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: page.app.zenPluginPrompt.length > 0
                            text: page.app.zenPluginPrompt
                            color: Theme.text
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                        TextField {
                            id: pluginPromptField
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            visible: page.app.zenPluginPrompt.length > 0
                            echoMode: page.app.zenPluginPromptSecret
                                      ? TextInput.Password
                                      : TextInput.Normal
                            inputMethodHints: Qt.ImhDigitsOnly
                            color: Theme.text
                            selectByMouse: true
                            background: Rectangle {
                                color: Theme.surface
                                border.color: pluginPromptField.activeFocus
                                              ? Theme.primary
                                              : Theme.borderStrong
                                radius: 7
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: connectionDialog.builtinTBank
                             && connectionDialog.effectiveConnected
                    spacing: 10

                    StatusBadge {
                        text: page.app.tbankSyncRunning
                              ? qsTr("Синхронизация") : qsTr("Подключено")
                        tone: page.app.tbankSyncRunning ? "neutral" : "positive"
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
                            checked: page.app.tbankAutoUpdate
                            enabled: !page.app.tbankSyncRunning
                            onToggled: page.app.setTBankAutoUpdate(checked)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Последнее: ") + page.app.tbankLastSync
                              + qsTr(" · Следующее: ") + page.app.tbankNextSync
                        color: Theme.textMuted
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    Item { Layout.fillWidth: true }
                    ActionButton {
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
                                 && !connectionDialog.builtinTBank
                                 && !connectionDialog.localPlugin
                        text: qsTr("Сохранить токен")
                        primary: true
                        enabled: tokenField.text.trim().length > 0
                        onClicked: {
                            if (page.app.saveBankToken(connectionDialog.connectorId,
                                                  tokenField.text)) {
                                tokenField.clear()
                                connectionDialog.close()
                            }
                        }
                    }
                    ActionButton {
                        visible: connectionDialog.available
                                 && connectionDialog.effectiveConnected
                                 && !connectionDialog.builtinTBank
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
                                 && !connectionDialog.builtinTBank
                                 && !connectionDialog.localPlugin
                        text: qsTr("Отключить")
                        destructive: true
                        onClicked: {
                            page.app.removeBankToken(connectionDialog.connectorId)
                            connectionDialog.close()
                        }
                    }
                    ActionButton {
                        visible: connectionDialog.builtinTBank
                                 && !connectionDialog.effectiveConnected
                                 && !page.app.tbankLoginActive
                                 && !page.app.tbankSyncRunning
                        text: qsTr("Войти")
                        primary: true
                        onClicked: page.app.beginTBankLogin()
                    }
                    ActionButton {
                        visible: connectionDialog.builtinTBank
                                 && page.app.tbankLoginActive
                        text: qsTr("Повторить")
                        onClicked: page.app.reloadTBankLoginPage()
                    }
                    ActionButton {
                        visible: connectionDialog.builtinTBank
                                 && page.app.tbankLoginActive
                        text: qsTr("Отмена")
                        destructive: true
                        onClicked: {
                            page.app.cancelTBankLogin()
                        }
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
                        enabled: pluginPromptField.text.length > 0
                        onClicked: {
                            if (page.app.answerZenPluginPrompt(
                                        pluginPromptField.text))
                                pluginPromptField.clear()
                        }
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
                        enabled: pluginInstitutionField.text.trim().length > 0
                        onClicked: {
                            if (page.app.startZenPlugin(
                                        pluginInstitutionField.text,
                                        connectionDialog.pluginPreferences()))
                                connectionDialog.clearSensitivePluginFields()
                        }
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
                        visible: connectionDialog.builtinTBank
                                 && connectionDialog.effectiveConnected
                                 && !page.app.tbankSyncRunning
                        text: qsTr("Обновить")
                        primary: true
                        onClicked: page.app.synchronizeTBank()
                    }
                    ActionButton {
                        visible: connectionDialog.builtinTBank
                                 && connectionDialog.effectiveConnected
                                 && !page.app.tbankSyncRunning
                        text: qsTr("Отключить")
                        destructive: true
                        onClicked: {
                            page.app.disconnectTBank()
                            connectionDialog.close()
                        }
                    }
                }
            }
        }

        onClosed: {
            if (connectionDialog.builtinTBank && page.app.tbankLoginActive) {
                page.app.cancelTBankLogin()
            }
            tokenField.clear()
            pluginPromptField.clear()
            clearSensitivePluginFields()
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
                text: qsTr("Официальные API и отдельно собранные локальные плагины. Токены и состояние сессий остаются в системном хранилище ключей.")
                color: Theme.textMuted
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.preferredHeight: 48
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Банк и способ")
                        color: Theme.text
                        font.pixelSize: 11
                    }
                    Text {
                        Layout.preferredWidth: 190
                        text: qsTr("Данные")
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }
                    Text {
                        Layout.preferredWidth: 132
                        text: qsTr("Статус")
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }
                    Item { Layout.preferredWidth: 116 }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.border
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: page.app.bankConnections

                    delegate: Item {
                        id: connectorRow

                        required property string connectorId
                        required property string institution
                        required property string scope
                        required property string method
                        required property string status
                        required property bool connected
                        required property bool available

                        width: ListView.view.width
                        height: 76

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 20
                            anchors.rightMargin: 20
                            spacing: 18

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 230
                                spacing: 3
                                Text {
                                    Layout.fillWidth: true
                                    text: connectorRow.institution
                                    color: Theme.text
                                    font.pixelSize: 14
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: connectorRow.method
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }

                            Text {
                                Layout.preferredWidth: 190
                                text: connectorRow.scope
                                color: Theme.textMuted
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }

                            StatusBadge {
                                Layout.preferredWidth: 132
                                text: connectorRow.status
                                tone: connectorRow.connected ? "positive"
                                      : connectorRow.available ? "neutral"
                                      : "warning"
                            }

                            ActionButton {
                                Layout.preferredWidth: 116
                                text: connectorRow.connected ? qsTr("Управление")
                                      : connectorRow.available ? qsTr("Подключить")
                                                               : qsTr("Подробнее")
                                onClicked: page.openConnection(
                                    connectorRow.connectorId,
                                    connectorRow.institution,
                                    connectorRow.method,
                                    connectorRow.status,
                                    connectorRow.connected,
                                    connectorRow.available)
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 1
                            anchors.bottom: parent.bottom
                            color: Theme.border
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Т-Банк использует встроенный C++ коннектор только для чтения. Для Альфа-Банка, Сбера и ВТБ нужен отдельный проверяемый локальный адаптер.")
            color: Theme.textMuted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
    }
}
