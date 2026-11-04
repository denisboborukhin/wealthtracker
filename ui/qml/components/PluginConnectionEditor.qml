pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: editor
    required property var app
    property alias institution: pluginInstitutionField.text
    readonly property bool canStart: institution.trim().length > 0
    readonly property bool canAnswer: pluginPromptField.text.length > 0
    property var _preferences: ({})
    signal chooseBundle()
    signal resetSensitiveFields()

    function setPreference(key, value) {
        let values = Object.assign({}, _preferences)
        values[key] = value
        _preferences = values
    }

    function resetPreferences() { _preferences = ({}) }

    function start() {
        if (app.startZenPlugin(institution, _preferences))
            resetSensitiveFields()
    }

    function answerPrompt() {
        if (app.answerZenPluginPrompt(pluginPromptField.text))
            pluginPromptField.clear()
    }

    function clear() {
        pluginPromptField.clear()
        resetSensitiveFields()
    }
    spacing: 12

    RowLayout {
        Layout.fillWidth: true
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                text: editor.app.zenPluginId.length > 0
                      ? editor.app.zenPluginId
                      : qsTr("Bundle не выбран")
                color: Theme.text
                font.pixelSize: 12
                font.weight: Font.Medium
            }
            Text {
                Layout.fillWidth: true
                text: editor.app.zenPluginBundlePath.length > 0
                      ? editor.app.zenPluginBundlePath
                      : qsTr("Сначала соберите plugin через yarn build <plugin-id>")
                color: Theme.textMuted
                font.pixelSize: 10
                elide: Text.ElideMiddle
            }
        }
        ActionButton {
            text: qsTr("Выбрать bundle")
            enabled: !editor.app.zenPluginRunning
            onClicked: editor.chooseBundle()
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: editor.app.zenPluginId.length > 0
                 && !editor.app.zenPluginRunning
        spacing: 7

        Text {
            text: qsTr("Название банка")
            color: Theme.text
            font.pixelSize: 12
            font.weight: Font.Medium
        }
        TextField {
            id: pluginInstitutionField
            objectName: "pluginInstitutionField"
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            text: editor.app.zenPluginInstitution
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
            model: editor.app.zenPluginFields

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
                    editor.setPreference(
                        preferenceField.fieldKey,
                        preferenceField.fieldValue)

                Connections {
                    target: editor
                    function onResetSensitiveFields() {
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
                    objectName: "pluginPreference_" + preferenceField.fieldKey
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
                        editor.setPreference(
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
                    objectName: "pluginCheckbox_" + preferenceField.fieldKey
                    accessibleName: preferenceField.modelData.label
                    visible: preferenceField.modelData.kind === "checkbox"
                    checked: preferenceField.modelData.defaultValue === true
                             || preferenceField.modelData.defaultValue === "true"
                    onCheckedChanged:
                        editor.setPreference(
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
        visible: editor.app.zenPluginId.length > 0
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
                accessibleName: qsTr("Автообновление локального плагина")
                checked: editor.app.zenPluginAutoUpdate
                enabled: !editor.app.zenPluginRunning
                onToggled: editor.app.setZenPluginAutoUpdate(checked)
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Последнее: ") + editor.app.zenPluginLastSync
                  + qsTr(" · Следующее: ") + editor.app.zenPluginNextSync
            color: Theme.textMuted
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: editor.app.zenPluginRunning
        spacing: 7

        StatusBadge {
            text: editor.app.zenPluginPrompt.length > 0
                  ? qsTr("Требуется ответ")
                  : qsTr("Синхронизация")
            tone: editor.app.zenPluginPrompt.length > 0
                  ? "warning" : "neutral"
        }
        Text {
            Layout.fillWidth: true
            visible: editor.app.zenPluginPrompt.length > 0
            text: editor.app.zenPluginPrompt
            color: Theme.text
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
        TextField {
            id: pluginPromptField
            objectName: "pluginPromptField"
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            visible: editor.app.zenPluginPrompt.length > 0
            echoMode: editor.app.zenPluginPromptSecret
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
