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
            Text {
                text: qsTr("Счета")
                color: Theme.text
                font.pixelSize: 26
                font.weight: Font.DemiBold
            }
            Text {
                text: qsTr("Банковские, накопительные и инвестиционные счета")
                color: Theme.textMuted
                font.pixelSize: 12
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
                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        leftPadding: 22
                        text: qsTr("Счёт")
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }
                    Text {
                        Layout.minimumWidth: 200
                        Layout.preferredWidth: 200
                        Layout.maximumWidth: 200
                        text: qsTr("Тип")
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }
                    Text {
                        Layout.minimumWidth: 145
                        Layout.preferredWidth: 145
                        Layout.maximumWidth: 145
                        horizontalAlignment: Text.AlignRight
                        text: qsTr("Остаток")
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.border
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Text {
                        anchors.centerIn: parent
                        visible: accountList.count === 0
                        text: qsTr("Счетов пока нет. Добавьте подключение или импортируйте выписку.")
                        color: Theme.textMuted
                        font.pixelSize: 12
                    }

                    ListView {
                        id: accountList
                        anchors.fill: parent
                        clip: true
                        model: page.app.accounts

                        delegate: Item {
                            id: accountRow
                            required property string accent
                            required property string name
                            required property string institution
                            required property string type
                            required property string balance
                            required property string currency

                            width: ListView.view.width
                            height: 62

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 18
                                anchors.rightMargin: 18
                                spacing: 14

                                Rectangle {
                                    Layout.minimumWidth: 8
                                    Layout.preferredWidth: 8
                                    Layout.maximumWidth: 8
                                    Layout.preferredHeight: 34
                                    radius: 4
                                    color: accountRow.accent
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: accountRow.name
                                        color: Theme.text
                                        font.pixelSize: 13
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: accountRow.institution
                                        color: Theme.textMuted
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }
                                Text {
                                    Layout.minimumWidth: 200
                                    Layout.preferredWidth: 200
                                    Layout.maximumWidth: 200
                                    text: accountRow.type
                                    color: Theme.textMuted
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.minimumWidth: 145
                                    Layout.preferredWidth: 145
                                    Layout.maximumWidth: 145
                                    horizontalAlignment: Text.AlignRight
                                    text: accountRow.balance + " " + accountRow.currency
                                    color: Theme.text
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
    }
}
