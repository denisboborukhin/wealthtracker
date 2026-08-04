import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as PlatformDialogs
import QtQuick.Layouts
import "../components"

Item {
    id: page

    function openConnection(connectorId, institution, method, status,
                            connected, available) {
        App.describeBankConnection(connectorId)
        connectionDialog.connectorId = connectorId
        connectionDialog.institution = institution
        connectionDialog.method = method
        connectionDialog.status = status
        connectionDialog.connected = connected
        connectionDialog.available = available
        connectionDialog.open()
        if (connectorId === "tbank-retail")
            App.prepareTBankConnection()
    }

    PlatformDialogs.FileDialog {
        id: pluginBundleDialog
        title: qsTr("Выберите собранный ZenPlugins bundle")
        nameFilters: [qsTr("JavaScript bundle (*.js *.cjs)")]
        onAccepted: App.inspectZenPluginBundle(selectedFile)
    }

    Dialog {
        id: connectionDialog
        parent: Overlay.overlay
        modal: true
        focus: true
        width: 520
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 0

        property string connectorId: ""
        property string institution: ""
        property string method: ""
        property string status: ""
        property bool connected: false
        property bool available: false
        readonly property bool builtinTBank:
            connectorId === "tbank-retail"
        readonly property bool localPlugin:
            connectorId === "zenplugins-local" || builtinTBank
        readonly property bool effectiveConnected:
            builtinTBank ? App.tbankConnected : connected

        function pluginPreferences() {
            let result = {}
            for (let index = 0; index < preferenceRepeater.count; ++index) {
                let field = preferenceRepeater.itemAt(index)
                result[field.fieldKey] = field.fieldValue
            }
            return result
        }

        function clearSensitivePluginFields() {
            for (let index = 0; index < preferenceRepeater.count; ++index) {
                let field = preferenceRepeater.itemAt(index)
                if (field.sensitive)
                    field.clearValue()
            }
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
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: 20
                Layout.bottomMargin: 24
                spacing: 14

                Text {
                    Layout.fillWidth: true
                    text: App.connectionMessage
                    color: Theme.textMuted
                    font.pixelSize: 13
                    lineHeight: 1.35
                    wrapMode: Text.WordWrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: connectionDialog.available
                               && !connectionDialog.connected
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
                             && !connectionDialog.localPlugin
                    text: qsTr("Подключено")
                    tone: "positive"
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: connectionDialog.builtinTBank
                             && !connectionDialog.effectiveConnected
                    spacing: 10

                    Text {
                        Layout.fillWidth: true
                        text: App.tbankLoginStatus.length > 0
                              ? App.tbankLoginStatus
                              : qsTr("Нажмите «Войти», затем завершите вход на официальной странице Т-Банка.")
                        color: Theme.text
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: browserStatusContent.implicitHeight + 32
                        visible: App.tbankLoginActive
                        color: Theme.surfaceRaised
                        radius: 8
                        border.color: Theme.border

                        RowLayout {
                            id: browserStatusContent
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12

                            BusyIndicator {
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                running: App.tbankLoginActive
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Вход выполняется в системном браузере")
                                    color: Theme.text
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Вернитесь сюда после подтверждения. Проверка выполняется автоматически.")
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Телефон, пароль, код и cookies остаются в браузере. WealthTracker хранит только отдельную API-сессию после проверки доступа к счетам.")
                        color: Theme.textMuted
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: connectionDialog.localPlugin
                             && (!connectionDialog.builtinTBank
                                 || connectionDialog.effectiveConnected)
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        visible: !connectionDialog.builtinTBank
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: App.zenPluginId.length > 0
                                      ? App.zenPluginId
                                      : qsTr("Bundle не выбран")
                                color: Theme.text
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }
                            Text {
                                Layout.fillWidth: true
                                text: App.zenPluginBundlePath.length > 0
                                      ? App.zenPluginBundlePath
                                      : qsTr("Сначала соберите plugin через yarn build <plugin-id>")
                                color: Theme.textMuted
                                font.pixelSize: 10
                                elide: Text.ElideMiddle
                            }
                        }
                        ActionButton {
                            text: qsTr("Выбрать bundle")
                            enabled: !App.zenPluginRunning
                            onClicked: pluginBundleDialog.open()
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: !connectionDialog.builtinTBank
                                 && App.zenPluginId.length > 0
                                 && !App.zenPluginRunning
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
                            text: App.zenPluginInstitution
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
                            model: App.zenPluginFields

                            delegate: ColumnLayout {
                                required property var modelData
                                readonly property string fieldKey: modelData.key
                                readonly property bool sensitive: modelData.sensitive
                                readonly property var fieldValue:
                                    modelData.kind === "checkbox"
                                    ? checkboxField.checked
                                    : valueField.text

                                function clearValue() {
                                    valueField.clear()
                                }

                                Layout.fillWidth: true
                                spacing: 5

                                Text {
                                    text: modelData.label
                                          + (modelData.required ? " *" : "")
                                    color: Theme.text
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                }
                                TextField {
                                    id: valueField
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 40
                                    visible: modelData.kind !== "checkbox"
                                    text: modelData.defaultValue
                                    echoMode: modelData.sensitive
                                              ? TextInput.Password
                                              : TextInput.Normal
                                    inputMethodHints:
                                        modelData.kind === "number"
                                        || modelData.kind === "phone"
                                        ? Qt.ImhDigitsOnly : Qt.ImhNone
                                    color: Theme.text
                                    selectByMouse: true
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
                                    visible: modelData.kind === "checkbox"
                                    checked: modelData.defaultValue === true
                                             || modelData.defaultValue === "true"
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
                        visible: App.zenPluginId.length > 0
                        spacing: 7

                        StatusBadge {
                            visible: connectionDialog.builtinTBank
                            text: App.zenPluginRunning
                                  ? qsTr("Синхронизация")
                                  : qsTr("Подключено")
                            tone: App.zenPluginRunning ? "neutral" : "positive"
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
                                checked: App.zenPluginAutoUpdate
                                enabled: !App.zenPluginRunning
                                onToggled: App.setZenPluginAutoUpdate(checked)
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Последнее: ") + App.zenPluginLastSync
                                  + qsTr(" · Следующее: ") + App.zenPluginNextSync
                            color: Theme.textMuted
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: App.zenPluginRunning
                        spacing: 7

                        StatusBadge {
                            text: App.zenPluginPrompt.length > 0
                                  ? qsTr("Требуется ответ")
                                  : qsTr("Синхронизация")
                            tone: App.zenPluginPrompt.length > 0
                                  ? "warning" : "neutral"
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: App.zenPluginPrompt.length > 0
                            text: App.zenPluginPrompt
                            color: Theme.text
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                        TextField {
                            id: pluginPromptField
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            visible: App.zenPluginPrompt.length > 0
                            echoMode: App.zenPluginPromptSecret
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
                                 && !connectionDialog.localPlugin
                        text: qsTr("Сохранить токен")
                        primary: true
                        enabled: tokenField.text.trim().length > 0
                        onClicked: {
                            if (App.saveBankToken(connectionDialog.connectorId,
                                                  tokenField.text)) {
                                tokenField.clear()
                                connectionDialog.close()
                            }
                        }
                    }
                    ActionButton {
                        visible: connectionDialog.available
                                 && connectionDialog.effectiveConnected
                                 && !connectionDialog.localPlugin
                        text: qsTr("Синхронизировать")
                        primary: true
                        onClicked: {
                            App.synchronizeBank(connectionDialog.connectorId)
                            connectionDialog.close()
                        }
                    }
                    ActionButton {
                        visible: connectionDialog.available
                                 && connectionDialog.effectiveConnected
                                 && !connectionDialog.localPlugin
                        text: qsTr("Отключить")
                        destructive: true
                        onClicked: {
                            App.removeBankToken(connectionDialog.connectorId)
                            connectionDialog.close()
                        }
                    }
                    ActionButton {
                        visible: connectionDialog.builtinTBank
                                 && !connectionDialog.effectiveConnected
                                 && !App.tbankLoginActive
                        text: qsTr("Открыть T‑Bank в браузере")
                        primary: true
                        onClicked: App.beginTBankLogin()
                    }
                    ActionButton {
                        visible: connectionDialog.builtinTBank
                                 && App.tbankLoginActive
                                 && !App.zenPluginRunning
                        text: qsTr("Открыть браузер снова")
                        onClicked: App.openTBankLoginPage()
                    }
                    ActionButton {
                        visible: connectionDialog.builtinTBank
                                 && App.tbankLoginActive
                                 && !App.zenPluginRunning
                        text: qsTr("Проверить сейчас")
                        primary: true
                        onClicked: App.checkTBankLogin()
                    }
                    ActionButton {
                        visible: connectionDialog.builtinTBank
                                 && App.tbankLoginActive
                        text: qsTr("Отмена")
                        destructive: true
                        onClicked: {
                            if (App.zenPluginRunning)
                                App.cancelZenPlugin()
                            App.cancelTBankLogin()
                        }
                    }
                    ActionButton {
                        visible: connectionDialog.localPlugin
                                 && !connectionDialog.builtinTBank
                                 && App.zenPluginRunning
                                 && App.zenPluginPrompt.length === 0
                        text: qsTr("Отменить")
                        destructive: true
                        onClicked: App.cancelZenPlugin()
                    }
                    ActionButton {
                        visible: connectionDialog.localPlugin
                                 && !connectionDialog.builtinTBank
                                 && App.zenPluginRunning
                                 && App.zenPluginPrompt.length > 0
                        text: qsTr("Передать")
                        primary: true
                        enabled: pluginPromptField.text.length > 0
                        onClicked: {
                            if (App.answerZenPluginPrompt(
                                        pluginPromptField.text))
                                pluginPromptField.clear()
                        }
                    }
                    ActionButton {
                        visible: connectionDialog.localPlugin
                                 && !App.zenPluginRunning
                                 && App.zenPluginId.length > 0
                                 && (!connectionDialog.builtinTBank
                                     || connectionDialog.effectiveConnected)
                        text: qsTr("Обновить")
                        onClicked: App.synchronizeZenPlugin()
                    }
                    ActionButton {
                        visible: connectionDialog.localPlugin
                                 && !connectionDialog.builtinTBank
                                 && !App.zenPluginRunning
                                 && App.zenPluginId.length > 0
                        text: qsTr("Запустить")
                        primary: true
                        enabled: pluginInstitutionField.text.trim().length > 0
                        onClicked: {
                            if (App.startZenPlugin(
                                        pluginInstitutionField.text,
                                        connectionDialog.pluginPreferences()))
                                connectionDialog.clearSensitivePluginFields()
                        }
                    }
                    ActionButton {
                        visible: connectionDialog.localPlugin
                                 && !App.zenPluginRunning
                                 && App.zenPluginId.length > 0
                                 && (!connectionDialog.builtinTBank
                                     || connectionDialog.effectiveConnected)
                        text: connectionDialog.builtinTBank
                              ? qsTr("Отключить") : qsTr("Удалить")
                        destructive: true
                        onClicked: {
                            if (connectionDialog.builtinTBank)
                                App.disconnectTBank()
                            else
                                App.removeZenPlugin()
                            connectionDialog.close()
                        }
                    }
                }
            }
        }

        onClosed: {
            if (connectionDialog.builtinTBank && App.tbankLoginActive) {
                if (App.zenPluginRunning)
                    App.cancelZenPlugin()
                App.cancelTBankLogin()
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
                    model: App.bankConnections

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
            text: qsTr("Т-Банк подключается через официальную страницу входа и встроенный read-only bundle. Для Альфа-Банка, Сбера и ВТБ по-прежнему нужен отдельный проверяемый локальный адаптер.")
            color: Theme.textMuted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
    }
}
