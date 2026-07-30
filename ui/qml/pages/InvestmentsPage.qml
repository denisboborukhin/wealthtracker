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
            ColumnLayout {
                Text {
                    text: qsTr("Инвестиции")
                    color: Theme.text
                    font.pixelSize: 30
                    font.weight: Font.DemiBold
                }
                Text { text: App.syncStatus; color: Theme.textMuted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Синхронизировать")
                onClicked: App.synchronizeInvestments()
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Стоимость портфеля")
                value: App.portfolioValue
                hint: qsTr("+14,8% за всё время")
            }
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Дивиденды и купоны")
                value: "86 420 ₽"
                hint: qsTr("За 12 месяцев")
                accent: Theme.positive
            }
            MetricCard {
                Layout.fillWidth: true
                title: "XIRR"
                value: "17,6%"
                hint: qsTr("Не является налоговым расчётом")
                accent: Theme.warning
            }
        }
        SectionCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                Text {
                    text: qsTr("Активы")
                    color: Theme.text
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: App.holdings
                    clip: true
                    delegate: Item {
                        width: ListView.view.width
                        height: 58
                        RowLayout {
                            anchors.fill: parent
                            Text {
                                Layout.preferredWidth: 92
                                text: model.ticker
                                color: Theme.primary
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                text: model.name
                                color: Theme.text
                            }
                            Text { text: model.allocation; color: Theme.textMuted }
                            Text {
                                Layout.preferredWidth: 92
                                horizontalAlignment: Text.AlignRight
                                text: model.performance
                                color: model.positive ? Theme.positive : Theme.negative
                            }
                            Text {
                                Layout.preferredWidth: 125
                                horizontalAlignment: Text.AlignRight
                                text: model.value
                                color: Theme.text
                                font.weight: Font.Medium
                            }
                        }
                    }
                }
            }
        }
    }
}
