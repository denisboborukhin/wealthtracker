pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

SectionCard {
    id: list
    required property var model
    required property real pageWidth
    signal connectionRequested(string connectorId, string institution, string method,
                               string status, bool connected, bool available)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.preferredHeight: 48
            Text {
                Layout.fillWidth: true
                text: qsTr("Банк и способ")
                color: Theme.text
                font.pixelSize: 11
            }
            Text {
                Layout.preferredWidth: 190
                visible: list.pageWidth >= 900
                text: qsTr("Данные")
                color: Theme.textMuted
                font.pixelSize: 11
            }
            Text {
                Layout.preferredWidth: 220
                visible: list.pageWidth >= 900
                text: qsTr("Статус")
                color: Theme.textMuted
                font.pixelSize: 11
            }
            Item { Layout.preferredWidth: 116 }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: list.model

            delegate: Item {
                id: connectorRow
                objectName: "connectionRow_" + connectorId

                required property string connectorId
                required property string institution
                required property string scope
                required property string method
                required property string status
                required property bool connected
                required property bool available
                required property string connectionState

                width: ListView.view.width
                height: Math.max(76, connectorContent.implicitHeight + 32)

                GridLayout {
                    id: connectorContent
                    columns: list.pageWidth < 900 ? 1 : 4
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    columnSpacing: 18
                    rowSpacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 3
                        Text {
                            Layout.fillWidth: true
                            text: connectorRow.institution
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: connectorRow.method
                            color: Theme.textMuted
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    Text {
                        Layout.preferredWidth: 190
                        visible: list.pageWidth >= 900
                        text: connectorRow.scope
                        color: Theme.textMuted
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    StatusBadge {
                        objectName: "connectionStatus_" + connectorRow.connectorId
                        Layout.preferredWidth: list.pageWidth < 900 ? -1 : 220
                        Layout.fillWidth: list.pageWidth < 900
                        text: connectorRow.status
                        tone: connectorRow.connectionState === "ready" ? "positive"
                              : connectorRow.connectionState === "error" ? "warning" : "neutral"
                    }

                    ActionButton {
                        objectName: "connectionAction_" + connectorRow.connectorId
                        Layout.preferredWidth: 116
                        Layout.fillWidth: list.pageWidth < 900
                        text: connectorRow.connected ? qsTr("Управление")
                              : connectorRow.available ? qsTr("Подключить")
                                                       : qsTr("Подробнее")
                        onClicked: list.connectionRequested(
                            connectorRow.connectorId,
                            connectorRow.institution,
                            connectorRow.method,
                            connectorRow.status,
                            connectorRow.connected,
                            connectorRow.available)
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
