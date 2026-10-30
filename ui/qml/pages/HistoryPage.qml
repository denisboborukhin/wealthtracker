pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Flickable {
    id: page
    required property var app
    property bool allConnections: true
    property var selectedConnectionIds: []
    property string fromDate: ""
    property string toDate: ""
    property var summary: ({})
    property bool ready: false
    readonly property var connectionOptions: app.balanceHistoryConnections || []
    readonly property var totals: (summary.totals || []).filter(function(total) {
        return total.currency === historyChart.selectedCurrency
    })
    readonly property var banks: (summary.banks || []).filter(function(bank) {
        return bank.currency === historyChart.selectedCurrency
    })
    readonly property string errorText: summary.error || app.balanceHistoryError || ""

    contentWidth: width
    contentHeight: Math.max(height, content.implicitHeight + Theme.pageMargin * 2)
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    ScrollBar.vertical: ScrollBar {}

    function refresh() {
        summary = app.demoMode ? {}
                  : app.balanceHistorySummary(selectedConnectionIds, !allConnections, fromDate, toDate)
    }
    function allConnectionIds() {
        return connectionOptions.map(function(connection) { return connection.id })
    }
    function selectConnection(id, selected) {
        let ids = allConnections ? allConnectionIds() : selectedConnectionIds.slice()
        const index = ids.indexOf(id)
        if (selected && index < 0) ids.push(id)
        else if (!selected && index >= 0) ids.splice(index, 1)
        selectedConnectionIds = ids
        allConnections = false
    }
    Component.onCompleted: { ready = true; refresh() }
    onAllConnectionsChanged: if (ready) Qt.callLater(refresh)
    onSelectedConnectionIdsChanged: if (ready) Qt.callLater(refresh)
    onFromDateChanged: if (ready) Qt.callLater(refresh)
    onToDateChanged: if (ready) Qt.callLater(refresh)
    Connections {
        target: page.app
        function onBalanceHistoryChanged() { page.refresh() }
        function onDemoModeChanged() { page.refresh() }
    }

    ColumnLayout {
        id: content
        width: page.width - Theme.pageMargin * 2
        x: Theme.pageMargin
        y: Theme.pageMargin
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text { text: qsTr("История"); color: Theme.text; font.pixelSize: 26; font.weight: Font.DemiBold }
                Text {
                    Layout.fillWidth: true
                    text: (page.allConnections ? qsTr("Все источники") : qsTr("Источников: %1").arg(page.selectedConnectionIds.length))
                          + " · " + historyChart.selectedCurrency + " · "
                          + (page.fromDate.length === 0 && page.toDate.length === 0 ? qsTr("За всё время")
                             : (page.fromDate || qsTr("С начала")) + " — " + (page.toDate || qsTr("Сегодня")))
                    color: Theme.textMuted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
            ActionButton {
                id: configurationButton
                text: qsTr("Настроить")
                enabled: !page.app.demoMode
                onClicked: configuration.open()
                Popup {
                    id: configuration
                    parent: configurationButton
                    popupType: Popup.Item
                    x: configurationButton.width - width
                    y: configurationButton.height + 8
                    width: Math.min(420, page.width - 64)
                    height: Math.min(540, page.height - 110)
                    padding: 16
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    background: Rectangle { color: Theme.surface; radius: 8; border.color: Theme.borderStrong }
                    contentItem: ScrollView {
                        clip: true
                        contentWidth: availableWidth
                        ColumnLayout {
                            width: parent.width
                            spacing: 10
                            Text { text: qsTr("Вид истории"); color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                            Text { text: qsTr("Валюта"); color: Theme.textMuted; font.pixelSize: 11 }
                            MinimalComboBox {
                                Layout.fillWidth: true
                                model: historyChart.currencyChoices
                                currentIndex: Math.max(0, historyChart.currencyChoices.indexOf(historyChart.selectedCurrency))
                                Accessible.name: qsTr("Валюта истории")
                                onActivated: function(index) {
                                    historyChart.selectedAccountId = ""
                                    historyChart.selectedCurrency = historyChart.currencyChoices[index]
                                    historyChart.reload()
                                }
                            }
                            Text { text: qsTr("Банки и кошельки"); color: Theme.textMuted; font.pixelSize: 11 }
                            MenuItem {
                                Layout.fillWidth: true
                                text: qsTr("Все источники")
                                checkable: true
                                checked: page.allConnections
                                onTriggered: {
                                    page.selectedConnectionIds = checked ? [] : page.allConnectionIds()
                                    page.allConnections = checked
                                }
                            }
                            Repeater {
                                model: page.connectionOptions
                                delegate: MenuItem {
                                    id: sourceChoice
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    checkable: true
                                    checked: page.allConnections || page.selectedConnectionIds.indexOf(modelData.id) >= 0
                                    onTriggered: page.selectConnection(sourceChoice.modelData.id, checked)
                                }
                            }
                            Text { text: qsTr("Период"); color: Theme.textMuted; font.pixelSize: 11 }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: page.width < 600 ? 1 : 2
                                DatePickerField {
                                    id: fromPicker
                                    Layout.fillWidth: true
                                    value: page.fromDate || qsTr("С начала")
                                    accessibleName: qsTr("Начало периода истории")
                                    onDateSelected: function(value) {
                                        page.fromDate = value
                                        if (fromPicker.parsedDate(value) > toPicker.parsedDate(page.toDate) && page.toDate.length > 0)
                                            page.toDate = value
                                    }
                                }
                                DatePickerField {
                                    id: toPicker
                                    Layout.fillWidth: true
                                    value: page.toDate || qsTr("Сегодня")
                                    accessibleName: qsTr("Конец периода истории")
                                    onDateSelected: function(value) {
                                        page.toDate = value
                                        if (toPicker.parsedDate(value) < fromPicker.parsedDate(page.fromDate) && page.fromDate.length > 0)
                                            page.fromDate = value
                                    }
                                }
                            }
                            ActionButton {
                                text: qsTr("За всё время")
                                onClicked: { page.fromDate = ""; page.toDate = "" }
                            }
                            ActionButton { Layout.fillWidth: true; text: qsTr("Готово"); primary: true; onClicked: configuration.close() }
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: page.errorText.length > 0
            text: page.errorText
            color: Theme.negative
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
        Repeater {
            model: page.totals
            delegate: SectionCard {
                id: totalCard
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: totalContent.implicitHeight + 32
                ColumnLayout {
                    id: totalContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 8
                    Text { text: qsTr("Общий остаток"); color: Theme.textMuted; font.pixelSize: 12 }
                    Text {
                        Layout.fillWidth: true
                        text: totalCard.modelData.lastValue || "—"
                        color: Theme.text
                        font.pixelSize: 28
                        font.weight: Font.DemiBold
                        wrapMode: Text.WrapAnywhere
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Изменение: %1").arg(totalCard.modelData.changeValue || "—")
                              + (totalCard.modelData.changePercent ? " (" + totalCard.modelData.changePercent + ")" : "")
                        color: Theme.textMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: Boolean(totalCard.modelData.baselineCarried || totalCard.modelData.openingUnknown)
                        text: totalCard.modelData.baselineCarried
                              ? qsTr("Начало периода: последний известный остаток до выбранной даты.")
                              : qsTr("На начало периода данных нет; изменение считается с первого снимка.")
                        color: Theme.textMuted
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
        BalanceHistoryPanel {
            id: historyChart
            Layout.fillWidth: true
            app: page.app
            showControls: false
            showValue: false
            allowAccountSelection: false
            connectionIds: page.selectedConnectionIds
            restrictConnections: !page.allConnections
            fromDate: page.fromDate
            toDate: page.toDate
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Цвета показывают банки и EVM-кошельки. Положительные остатки расположены выше нуля, отрицательные — ниже. Наведите курсор на дату, чтобы увидеть суммы и доли подключений. Изменение остатков не является доходностью.")
                  + (historyChart.selectedCurrency === "USDT" ? qsTr(" Оценка в USDT включает только активы с доступными котировками.") : "")
            color: Theme.textMuted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
    }
}
