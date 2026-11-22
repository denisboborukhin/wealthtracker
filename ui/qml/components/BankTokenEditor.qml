pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: editor
    required property string connectorId
    required property bool connected
    property alias token: tokenField.text
    readonly property bool canSave: !connected && token.trim().length > 0

    visible: !connected
    spacing: 7

    function clear() { tokenField.clear() }

    Text {
        text: qsTr("Токен только для чтения")
        color: Theme.text
        font.pixelSize: 12
        font.weight: Font.Medium
    }
    TextField {
        id: tokenField
        objectName: "bankTokenField"
        Layout.fillWidth: true
        Layout.preferredHeight: 40
        echoMode: TextInput.Password
        placeholderText: editor.connectorId === "alfa-api"
                         ? qsTr("Access token Альфа-Банка")
                         : editor.connectorId === "tbank-business"
                           ? qsTr("T-API токен")
                           : editor.connectorId === "modulbank"
                             ? qsTr("API-токен или sandboxtoken")
                             : qsTr("Read-only токен Т-Инвест")
        color: Theme.text
        selectByMouse: true
        background: Rectangle {
            color: Theme.surface
            border.color: tokenField.activeFocus ? Theme.primary : Theme.borderStrong
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
