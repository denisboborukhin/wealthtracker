pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import "../components"

Item {
    id: page
    required property var app

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pageMargin
        spacing: 20

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Text { text: qsTr("Категории"); color: Theme.text; font.pixelSize: 26; font.weight: Font.DemiBold }
            Text { text: qsTr("Распределение расходов по категориям банка"); color: Theme.textMuted; font.pixelSize: 12 }
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
                    model: page.app.categories
                    clip: true
                    delegate: Item {
                        id: categoryRow
                        required property string color
                        required property string name
                        required property string amount
                        required property real share

                        width: ListView.view.width
                        height: 54
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 18
                            anchors.rightMargin: 18
                            spacing: 10
                            Rectangle { Layout.minimumWidth: 8; Layout.preferredWidth: 8; Layout.maximumWidth: 8; Layout.preferredHeight: 8; radius: 4; color: categoryRow.color }
                            Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: categoryRow.name; color: Theme.text; font.pixelSize: 13 }
                            Text { Layout.minimumWidth: 140; Layout.preferredWidth: 140; Layout.maximumWidth: 140; horizontalAlignment: Text.AlignRight; text: categoryRow.amount; color: Theme.text; font.pixelSize: 12 }
                            Text { Layout.minimumWidth: 80; Layout.preferredWidth: 80; Layout.maximumWidth: 80; horizontalAlignment: Text.AlignRight; text: Math.round(categoryRow.share * 100) + "%"; color: Theme.textMuted; font.pixelSize: 12 }
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
