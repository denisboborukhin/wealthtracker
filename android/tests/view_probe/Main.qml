import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ApplicationWindow {
    required property var bankWindow
    id: root
    visible: true
    color: "white"
    Dialog {
        anchors.centerIn: parent
        width: parent.width - 24
        height: parent.height - 100
        visible: true
        modal: true
        ColumnLayout {
            anchors.fill: parent
            Text { text: "Synthetic native WebView test" }
            WindowContainer {
                Layout.fillWidth: true
                Layout.fillHeight: true
                window: root.bankWindow
            }
        }
    }
}
