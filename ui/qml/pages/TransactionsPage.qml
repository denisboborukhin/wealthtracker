pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: page
    required property var app

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pageMargin
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 4
                Text {
                    text: qsTr("Операции")
                    color: Theme.text
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("Все счета · сначала новые")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
            }
            Item { Layout.fillWidth: true }
            TextField {
                id: searchField
                Layout.preferredWidth: 240
                Layout.preferredHeight: 38
                placeholderText: qsTr("Поиск операций")
                color: Theme.text
                selectByMouse: true
                background: Rectangle {
                    color: Theme.surface
                    border.color: searchField.activeFocus
                                  ? Theme.primary : Theme.borderStrong
                    radius: 7
                }
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
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    Layout.preferredHeight: 42
                    spacing: 14
                    Text { Layout.minimumWidth: 95; Layout.preferredWidth: 95; Layout.maximumWidth: 95; text: qsTr("Дата"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: qsTr("Описание"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text { Layout.minimumWidth: 170; Layout.preferredWidth: 170; Layout.maximumWidth: 170; text: qsTr("Категория"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text {
                        Layout.minimumWidth: 115
                        Layout.preferredWidth: 115
                        Layout.maximumWidth: 115
                        horizontalAlignment: Text.AlignRight
                        text: qsTr("Сумма")
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.border
                }

                ListView {
                    id: transactionList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: count > 0
                    clip: true
                    model: page.app.transactions

                    delegate: Item {
                        id: transactionRow
                        required property string date
                        required property string title
                        required property string subtitle
                        required property string category
                        required property string amount
                        required property bool income

                        width: ListView.view.width
                        height: 58

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 18
                            anchors.rightMargin: 18
                            spacing: 14
                            Text {
                                Layout.minimumWidth: 95
                                Layout.preferredWidth: 95
                                Layout.maximumWidth: 95
                                text: transactionRow.date
                                color: Theme.textMuted
                                font.pixelSize: 11
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: transactionRow.title
                                    color: Theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: transactionRow.subtitle
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }
                            Text {
                                Layout.minimumWidth: 170
                                Layout.preferredWidth: 170
                                Layout.maximumWidth: 170
                                text: transactionRow.category
                                color: Theme.textMuted
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.minimumWidth: 115
                                Layout.preferredWidth: 115
                                Layout.maximumWidth: 115
                                horizontalAlignment: Text.AlignRight
                                text: transactionRow.amount
                                color: transactionRow.income ? Theme.positive : Theme.text
                                font.pixelSize: 12
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
                Text {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: transactionList.count === 0
                    text: qsTr("Операций пока нет. Подключите банк или импортируйте выписку.")
                    color: Theme.textMuted
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
