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
                text: qsTr("Операции")
                color: Theme.text
                font.pixelSize: 30
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            TextField {
                placeholderText: qsTr("Поиск")
                color: Theme.text
                background: Rectangle {
                    color: Theme.surface
                    border.color: Theme.border
                    radius: 9
                }
            }
        }
        SectionCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                anchors.fill: parent
                anchors.margins: 18
                clip: true
                model: App.transactions
                delegate: Item {
                    width: ListView.view.width
                    height: 62
                    RowLayout {
                        anchors.fill: parent
                        Text {
                            Layout.preferredWidth: 105
                            text: model.date
                            color: Theme.textMuted
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: model.title; color: Theme.text; font.pixelSize: 15 }
                            Text { text: model.subtitle; color: Theme.textMuted; font.pixelSize: 12 }
                        }
                        Rectangle {
                            implicitWidth: categoryLabel.implicitWidth + 18
                            implicitHeight: 28
                            radius: 14
                            color: Theme.surfaceRaised
                            Text {
                                id: categoryLabel
                                anchors.centerIn: parent
                                text: model.category
                                color: Theme.textMuted
                                font.pixelSize: 11
                            }
                        }
                        Text {
                            Layout.preferredWidth: 115
                            horizontalAlignment: Text.AlignRight
                            text: model.amount
                            color: model.income ? Theme.positive : Theme.text
                            font.pixelSize: 14
                            font.weight: Font.Medium
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
}
