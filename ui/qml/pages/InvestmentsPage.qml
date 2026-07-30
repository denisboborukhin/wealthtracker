import QtQuick
import QtQuick.Layouts
import "../components"

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pageMargin
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 4
                Text { text: qsTr("Инвестиции"); color: Theme.text; font.pixelSize: 26; font.weight: Font.DemiBold }
                Text { text: App.syncStatus; color: Theme.textMuted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Синхронизировать")
                primary: true
                onClicked: App.synchronizeInvestments()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 12
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Стоимость портфеля")
                value: App.portfolioValue
                hint: App.demoMode ? qsTr("+14,8% за всё время")
                                   : qsTr("Текущая оценка")
                accent: Theme.positive
            }
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Дивиденды и купоны")
                value: App.demoMode ? "86 420 ₽" : "0 ₽"
                hint: App.demoMode ? qsTr("За 12 месяцев")
                                   : qsTr("Нет данных")
                accent: Theme.positive
            }
            MetricCard {
                Layout.fillWidth: true
                title: "XIRR"
                value: App.demoMode ? "17,6%" : "—"
                hint: App.demoMode
                      ? qsTr("Не является налоговым расчётом")
                      : qsTr("Недостаточно операций")
                accent: Theme.textMuted
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
                    Text { Layout.preferredWidth: 92; text: qsTr("Тикер"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text { Layout.fillWidth: true; text: qsTr("Актив"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text { Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight; text: qsTr("Доля"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text { Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight; text: qsTr("Доходность"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text { Layout.preferredWidth: 125; horizontalAlignment: Text.AlignRight; text: qsTr("Стоимость"); color: Theme.textMuted; font.pixelSize: 11 }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

                ListView {
                    id: holdingList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: count > 0
                    model: App.holdings
                    clip: true
                    delegate: Item {
                        width: ListView.view.width
                        height: 54
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 18
                            anchors.rightMargin: 18
                            Text { Layout.preferredWidth: 92; text: model.ticker; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                            Text { Layout.fillWidth: true; text: model.name; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                            Text { Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight; text: model.allocation; color: Theme.textMuted; font.pixelSize: 12 }
                            Text { Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight; text: model.performance; color: model.positive ? Theme.positive : Theme.negative; font.pixelSize: 12 }
                            Text { Layout.preferredWidth: 125; horizontalAlignment: Text.AlignRight; text: model.value; color: Theme.text; font.pixelSize: 12; font.weight: Font.Medium }
                        }
                        Rectangle { width: parent.width; height: 1; anchors.bottom: parent.bottom; color: Theme.border }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: holdingList.count === 0
                    text: qsTr("Активов пока нет. Подключите инвестиционный счёт.")
                    color: Theme.textMuted
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
