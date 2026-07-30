import QtQuick
import QtQuick.Layouts
import "../components"

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pageMargin
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 4
                Text { text: qsTr("Категории"); color: Theme.text; font.pixelSize: 26; font.weight: Font.DemiBold }
                Text { text: qsTr("Расходы и правила автоматической категоризации"); color: Theme.textMuted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            ActionButton { text: qsTr("Новое правило"); primary: true }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    Layout.preferredHeight: 42
                    spacing: 10
                    Item { Layout.minimumWidth: 8; Layout.preferredWidth: 8; Layout.maximumWidth: 8 }
                    Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: qsTr("Категория"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text { Layout.minimumWidth: 140; Layout.preferredWidth: 140; Layout.maximumWidth: 140; horizontalAlignment: Text.AlignRight; text: qsTr("Расходы"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text { Layout.minimumWidth: 80; Layout.preferredWidth: 80; Layout.maximumWidth: 80; horizontalAlignment: Text.AlignRight; text: qsTr("Доля"); color: Theme.textMuted; font.pixelSize: 11 }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

                ListView {
                    id: categoryList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: count > 0
                    model: App.categories
                    clip: true
                    delegate: Item {
                        width: ListView.view.width
                        height: 54
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 18
                            anchors.rightMargin: 18
                            spacing: 10
                            Rectangle { Layout.minimumWidth: 8; Layout.preferredWidth: 8; Layout.maximumWidth: 8; Layout.preferredHeight: 8; radius: 4; color: model.color }
                            Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: model.name; color: Theme.text; font.pixelSize: 13 }
                            Text { Layout.minimumWidth: 140; Layout.preferredWidth: 140; Layout.maximumWidth: 140; horizontalAlignment: Text.AlignRight; text: model.amount; color: Theme.text; font.pixelSize: 12 }
                            Text { Layout.minimumWidth: 80; Layout.preferredWidth: 80; Layout.maximumWidth: 80; horizontalAlignment: Text.AlignRight; text: Math.round(model.share * 100) + "%"; color: Theme.textMuted; font.pixelSize: 12 }
                        }
                        Rectangle { width: parent.width; height: 1; anchors.bottom: parent.bottom; color: Theme.border }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: categoryList.count === 0
                    text: qsTr("Категории расходов появятся после загрузки операций")
                    color: Theme.textMuted
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
