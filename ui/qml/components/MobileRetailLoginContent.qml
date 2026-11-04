pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: content
    required property string institution
    required property string status
    required property bool mobileLogin
    required property var webView
    signal closeRequested()
    signal reloadRequested()
    spacing: 10
    RowLayout {
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: content.institution
            color: Theme.text
            font.pixelSize: 18
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        ActionButton {
            objectName: "retailLoginClose"
            text: qsTr("Закрыть")
            onClicked: content.closeRequested()
        }
    }
    Text {
        Layout.fillWidth: true
        objectName: "retailLoginStatus"
        text: content.status
        color: Theme.textMuted
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        maximumLineCount: 3
        elide: Text.ElideRight
    }
    WindowContainer {
        objectName: "mobileBankWindow"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 0
        window: content.mobileLogin ? content.webView : null
    }
    ActionButton {
        Layout.fillWidth: true
        objectName: "retailLoginReload"
        text: qsTr("Перезагрузить страницу")
        onClicked: content.reloadRequested()
    }
}
