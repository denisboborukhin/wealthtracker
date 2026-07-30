import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 18
        RowLayout {
            Text {
                text: qsTr("Категории и правила")
                color: Theme.text
                font.pixelSize: 30
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Новое правило") }
        }
        SectionCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                anchors.fill: parent
                anchors.margins: 20
                model: App.categories
                delegate: RowLayout {
                    width: ListView.view.width
                    height: 54
                    Rectangle {
                        implicitWidth: 12
                        implicitHeight: 12
                        radius: 6
                        color: model.color
                    }
                    Text { Layout.fillWidth: true; text: model.name; color: Theme.text; font.pixelSize: 15 }
                    Text { text: model.amount; color: Theme.textMuted }
                    Text { text: Math.round(model.share * 100) + "%"; color: Theme.primary }
                }
            }
        }
    }
}
