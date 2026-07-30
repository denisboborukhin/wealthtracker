import QtQuick
import QtQuick.Layouts
import "../components"

Flickable {
    contentHeight: content.implicitHeight + 48
    clip: true
    ColumnLayout {
        id: content
        width: parent.width - 48
        x: 24
        y: 24
        spacing: 18
        Text {
            text: qsTr("Счета")
            color: Theme.text
            font.pixelSize: 30
            font.weight: Font.DemiBold
        }
        Repeater {
            model: App.accounts
            delegate: SectionCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 92
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    Rectangle {
                        implicitWidth: 44
                        implicitHeight: 44
                        radius: 12
                        color: model.accent
                        Text {
                            anchors.centerIn: parent
                            text: model.institution.charAt(0)
                            color: "#0B1020"
                            font.pixelSize: 18
                            font.weight: Font.Bold
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text { text: model.name; color: Theme.text; font.pixelSize: 16 }
                        Text { text: model.institution + " · " + model.type; color: Theme.textMuted; font.pixelSize: 12 }
                    }
                    Text {
                        text: model.balance + " " + model.currency
                        color: Theme.text
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
    }
}
