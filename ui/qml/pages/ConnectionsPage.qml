import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 18
        Text {
            text: qsTr("Подключения")
            color: Theme.text
            font.pixelSize: 30
            font.weight: Font.DemiBold
        }
        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 215
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                RowLayout {
                    Layout.fillWidth: true
                    Rectangle {
                        implicitWidth: 52
                        implicitHeight: 52
                        radius: 14
                        color: "#F9C74F"
                        Text { anchors.centerIn: parent; text: "T"; color: "#111827"; font.pixelSize: 22; font.bold: true }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("Т-Инвестиции"); color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                        Text {
                            text: qsTr("Официальный токен только для чтения. Торговые операции недоступны.")
                            color: Theme.textMuted
                            wrapMode: Text.WordWrap
                        }
                    }
                    Rectangle {
                        implicitWidth: stateText.implicitWidth + 20
                        implicitHeight: 30
                        radius: 15
                        color: App.investmentConnected ? "#173F36" : Theme.surfaceRaised
                        Text {
                            id: stateText
                            anchors.centerIn: parent
                            text: App.investmentConnected ? qsTr("Подключено") : qsTr("Не подключено")
                            color: App.investmentConnected ? Theme.positive : Theme.textMuted
                            font.pixelSize: 11
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: tokenField
                        Layout.fillWidth: true
                        visible: !App.investmentConnected
                        echoMode: TextInput.Password
                        placeholderText: qsTr("Read-only токен")
                        color: Theme.text
                        background: Rectangle {
                            color: Theme.surfaceRaised
                            border.color: Theme.border
                            radius: 9
                        }
                    }
                    Button {
                        text: App.investmentConnected ? qsTr("Отключить") : qsTr("Сохранить токен")
                        onClicked: {
                            if (App.investmentConnected) {
                                App.removeInvestmentToken()
                            } else if (App.saveInvestmentToken(tokenField.text)) {
                                tokenField.clear()
                            }
                        }
                    }
                }
                Text { text: App.syncStatus; color: Theme.textMuted; font.pixelSize: 12 }
            }
        }
        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                Text { text: qsTr("Повседневные банковские счета"); color: Theme.text; font.pixelSize: 17 }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Т-Банк, Альфа-Банк, Сбер и ВТБ подключаются через локальный импорт выписок, пока официальные потребительские API недоступны.")
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                }
            }
        }
        Item { Layout.fillHeight: true }
    }
}
