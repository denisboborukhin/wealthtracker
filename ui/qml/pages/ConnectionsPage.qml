import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: page

    Dialog {
        id: connectionDialog
        parent: Overlay.overlay
        modal: true
        focus: true
        width: 520
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 0

        property string connectorId: ""
        property string institution: ""
        property string method: ""
        property string status: ""
        property bool connected: false
        property bool available: false

        background: Rectangle {
            color: Theme.surface
            radius: 12
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 24
                spacing: 8

                Text {
                    text: connectionDialog.institution
                    color: Theme.text
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: connectionDialog.method
                    color: Theme.textMuted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: 20
                Layout.bottomMargin: 24
                spacing: 14

                Text {
                    Layout.fillWidth: true
                    visible: connectionDialog.connectorId !== "t-invest"
                    text: App.connectionMessage
                    color: Theme.textMuted
                    font.pixelSize: 13
                    lineHeight: 1.35
                    wrapMode: Text.WordWrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: connectionDialog.connectorId === "t-invest"
                               && !connectionDialog.connected
                    spacing: 7

                    Text {
                        text: qsTr("Read-only токен")
                        color: Theme.text
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                    TextField {
                        id: tokenField
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        echoMode: TextInput.Password
                        placeholderText: qsTr("Вставьте токен Т-Инвест")
                        color: Theme.text
                        selectByMouse: true
                        background: Rectangle {
                            color: Theme.surface
                            border.color: tokenField.activeFocus
                                          ? Theme.primary : Theme.borderStrong
                            radius: 7
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Токен хранится в системном хранилище ключей и используется только для чтения.")
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }

                StatusBadge {
                    visible: connectionDialog.connectorId === "t-invest"
                             && connectionDialog.connected
                    text: qsTr("Подключено")
                    tone: "positive"
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        text: qsTr("Закрыть")
                        onClicked: connectionDialog.close()
                    }
                    ActionButton {
                        visible: connectionDialog.connectorId !== "t-invest"
                        text: qsTr("Плагин не установлен")
                        enabled: false
                    }
                    ActionButton {
                        visible: connectionDialog.connectorId === "t-invest"
                                 && !connectionDialog.connected
                        text: qsTr("Сохранить токен")
                        primary: true
                        enabled: tokenField.text.trim().length > 0
                        onClicked: {
                            if (App.saveInvestmentToken(tokenField.text)) {
                                tokenField.clear()
                                connectionDialog.close()
                            }
                        }
                    }
                    ActionButton {
                        visible: connectionDialog.connectorId === "t-invest"
                                 && connectionDialog.connected
                        text: qsTr("Отключить")
                        destructive: true
                        onClicked: {
                            App.removeInvestmentToken()
                            connectionDialog.close()
                        }
                    }
                }
            }
        }

        onClosed: tokenField.clear()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pageMargin
        spacing: 20

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 5
            Text {
                text: qsTr("Подключения")
                color: Theme.text
                font.pixelSize: 26
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Банки подключаются через локальные коннекторы. Пароли и одноразовые коды не сохраняются.")
                color: Theme.textMuted
                font.pixelSize: 13
                wrapMode: Text.WordWrap
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
                        text: qsTr("Данные")
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }
                    Text {
                        Layout.preferredWidth: 132
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
                    model: App.bankConnections

                    delegate: Item {
                        id: connectorRow

                        required property string connectorId
                        required property string institution
                        required property string scope
                        required property string method
                        required property string status
                        required property bool connected
                        required property bool available

                        width: ListView.view.width
                        height: 76

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 20
                            anchors.rightMargin: 20
                            spacing: 18

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 230
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
                                text: connectorRow.scope
                                color: Theme.textMuted
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }

                            StatusBadge {
                                Layout.preferredWidth: 132
                                text: connectorRow.status
                                tone: connectorRow.connected ? "positive"
                                      : connectorRow.available ? "neutral"
                                      : "warning"
                            }

                            ActionButton {
                                Layout.preferredWidth: 116
                                text: connectorRow.connected ? qsTr("Управление")
                                      : connectorRow.available ? qsTr("Подключить")
                                                               : qsTr("Подробнее")
                                onClicked: {
                                    App.describeBankConnection(connectorRow.connectorId)
                                    connectionDialog.connectorId = connectorRow.connectorId
                                    connectionDialog.institution = connectorRow.institution
                                    connectionDialog.method = connectorRow.method
                                    connectionDialog.status = connectorRow.status
                                    connectionDialog.connected = connectorRow.connected
                                    connectionDialog.available = connectorRow.available
                                    connectionDialog.open()
                                }
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

        Text {
            Layout.fillWidth: true
            text: qsTr("Официальный API Т-Банка для счетов и выписок сейчас предназначен для бизнеса. Розничные подключения требуют отдельного сопровождаемого плагина.")
            color: Theme.textMuted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
    }
}
