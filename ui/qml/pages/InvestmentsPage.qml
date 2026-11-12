pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Flickable {
    id: page
    required property var app
    property bool sortDescending: false

    function applySort() {
        app.holdings.setSort(["ticker", "name", "value"][sortField.currentIndex], sortDescending)
    }
    Component.onCompleted: applySort()

    contentWidth: width
    contentHeight: Math.max(height, content.implicitHeight + Theme.pageMargin * 2)
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        id: content
        x: Theme.pageMargin
        y: Theme.pageMargin
        width: page.width - Theme.pageMargin * 2
        spacing: 16

        GridLayout {
            id: pageHeader
            Layout.fillWidth: true
            columns: page.width < 600 ? 1 : 2
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 4
                Text { text: qsTr("Инвестиции"); color: Theme.text; font.pixelSize: 26; font.weight: Font.DemiBold }
                Text {
                    Layout.fillWidth: true
                    objectName: "investmentStatus"
                    text: page.app.demoMode ? qsTr("Демонстрационные данные")
                        : (page.app.connectionStates["t-invest"] || {}).text || ""
                    visible: text.length > 0
                    color: Theme.textMuted
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
            ActionButton {
                Layout.fillWidth: page.width < 600
                objectName: "investmentSync"
                text: qsTr("Синхронизировать")
                primary: true
                onClicked: page.app.synchronizeInvestments()
            }
        }

        GridLayout {
            id: metrics
            Layout.fillWidth: true
            columns: page.app.demoMode && page.width > 600 ? 3 : 1
            columnSpacing: 12
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Стоимость портфеля")
                value: page.app.portfolioValue
                hint: page.app.demoMode ? qsTr("+14,8% за всё время")
                                   : qsTr("Текущая оценка")
                accent: Theme.positive
            }
            MetricCard {
                Layout.fillWidth: true
                objectName: "investmentDividends"
                visible: page.app.demoMode
                title: qsTr("Дивиденды и купоны · демо")
                value: page.app.demoMode ? "86 420 ₽" : "—"
                hint: page.app.demoMode ? qsTr("За 12 месяцев")
                                   : qsTr("Нет данных")
                accent: Theme.positive
            }
            MetricCard {
                Layout.fillWidth: true
                objectName: "investmentXirr"
                visible: page.app.demoMode
                title: qsTr("XIRR · демо")
                value: page.app.demoMode ? "17,6%" : "—"
                hint: page.app.demoMode
                      ? qsTr("Не является налоговым расчётом")
                      : qsTr("Недостаточно операций")
                accent: Theme.textMuted
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: page.width < 600 ? 1 : 3
            Text { text: qsTr("Сортировка"); color: Theme.textMuted; font.pixelSize: 12 }
            MinimalComboBox {
                id: sortField
                objectName: "investmentSort"
                Layout.fillWidth: true
                model: [qsTr("Тикер"), qsTr("Название"), qsTr("Стоимость · по валютам")]
                Accessible.name: qsTr("Сортировка инвестиций")
                onActivated: page.applySort()
            }
            ActionButton {
                Layout.fillWidth: page.width < 600
                objectName: "investmentSortDirection"
                text: page.sortDescending ? qsTr("По убыванию ↓") : qsTr("По возрастанию ↑")
                onClicked: { page.sortDescending = !page.sortDescending; page.applySort() }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: page.width < 600
                ? Math.max(160, holdingList.count * 84 + 44)
                : Math.max(180, page.height - pageHeader.implicitHeight - metrics.implicitHeight - Theme.pageMargin * 2 - content.spacing * 2)

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    Layout.preferredHeight: 42
                    visible: page.width >= 600
                    Text { Layout.preferredWidth: 92; text: qsTr("Тикер"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text { Layout.fillWidth: true; text: qsTr("Актив"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text { Layout.preferredWidth: 90; visible: page.width >= 680; horizontalAlignment: Text.AlignRight; text: qsTr("Доля"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text { Layout.preferredWidth: 100; visible: page.width >= 680; horizontalAlignment: Text.AlignRight; text: qsTr("Доходность"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text { Layout.preferredWidth: 125; horizontalAlignment: Text.AlignRight; text: qsTr("Стоимость"); color: Theme.textMuted; font.pixelSize: 11 }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

                ListView {
                    id: holdingList
                    objectName: "holdingList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: count > 0
                    model: page.app.holdings
                    clip: true
                    delegate: Item {
                        id: holdingRow
                        required property string ticker
                        required property string name
                        required property string allocation
                        required property string performance
                        required property string value
                        required property bool positive

                        width: ListView.view.width
                        height: page.width < 600 ? 84 : 54
                        GridLayout {
                            columns: page.width < 600 ? 1 : 5
                            anchors.fill: parent
                            anchors.leftMargin: 18
                            anchors.rightMargin: 18
                            Text { Layout.fillWidth: page.width < 600; Layout.preferredWidth: 92; elide: Text.ElideRight; objectName: "holding_ticker"; text: holdingRow.ticker; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                            Text { Layout.fillWidth: true; Layout.minimumWidth: 0; objectName: "holding_name"; text: holdingRow.name; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                            Text { Layout.preferredWidth: 90; visible: page.width >= 680; horizontalAlignment: Text.AlignRight; objectName: "holding_allocation"; text: holdingRow.allocation; color: Theme.textMuted; font.pixelSize: 12 }
                            Text { Layout.preferredWidth: 100; visible: page.width >= 680; horizontalAlignment: Text.AlignRight; objectName: "holding_performance"; text: holdingRow.performance; color: holdingRow.positive ? Theme.positive : Theme.negative; font.pixelSize: 12 }
                            Text { Layout.fillWidth: page.width < 600; Layout.preferredWidth: 125; elide: Text.ElideRight; horizontalAlignment: page.width < 600 ? Text.AlignLeft : Text.AlignRight; objectName: "holding_value"; text: holdingRow.value; color: Theme.text; font.pixelSize: 12; font.weight: Font.Medium }
                        }
                        Rectangle { width: parent.width; height: 1; anchors.bottom: parent.bottom; color: Theme.border }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: holdingList.count === 0
                    wrapMode: Text.WordWrap
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
