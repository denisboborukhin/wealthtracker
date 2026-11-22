pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SectionCard {
    id: panel
    required property var app
    property bool allowAccountSelection: true
    property bool showControls: true
    property var connectionIds: []
    property bool restrictConnections: false
    property string fromDate: ""
    property string toDate: ""
    property string selectedAccountId: ""
    property string selectedCurrency: "RUB"
    property int selectedAccountIndex: 0
    property var accountChoices: []
    property var accountLabels: []
    property var currencyChoices: []
    property var chartData: ({})
    property bool ready: false
    readonly property var points: chartData.points || []
    readonly property var series: chartData.series || []
    readonly property bool hasComposition: selectedAccountId.length === 0
                                           && chartData.compositionAvailable === true && series.length > 0
    readonly property bool composition: hasComposition && scaleSelector.currentIndex === 0
    readonly property var ticks: (composition ? chartData.compositionTicks : chartData.valueTicks) || []
    readonly property var displayedPoint: plot.activePoint || (points.length ? points[points.length - 1] : null)
    readonly property bool compact: height < 360
    readonly property string errorText: chartData.error || app.balanceHistoryError || ""
    readonly property bool hasFilters: restrictConnections || fromDate.length > 0 || toDate.length > 0
    readonly property var colors: ["#3157D5", "#1C8C75", "#A76BCE", "#DCA034",
                                   "#D96577", "#3B9DC4", "#7C8956", "#9F6A49"]

    implicitHeight: showControls && allowAccountSelection ? 440 : 380

    function seriesColor(index) {
        return index < colors.length ? colors[index] : Qt.hsla((index * 0.61803398875) % 1, 0.55, 0.46, 1)
    }

    function reload() {
        const accounts = app.balanceHistoryAccounts || []
        let filteredAccounts = []
        for (let index = 0; index < accounts.length; ++index) {
            if (!restrictConnections || connectionIds.indexOf(accounts[index].connectionId) >= 0)
                filteredAccounts.push(accounts[index])
        }
        accountChoices = filteredAccounts
        let labels = [qsTr("Итого по подключениям")]
        let accountIndex = 0
        for (let index = 0; index < accountChoices.length; ++index) {
            const account = accountChoices[index]
            labels.push(account.label + " · " + account.currency)
            if (allowAccountSelection && account.id === selectedAccountId)
                accountIndex = index + 1
        }
        accountLabels = labels
        selectedAccountIndex = accountIndex
        accountSelector.currentIndex = accountIndex
        selectedAccountId = accountIndex > 0 ? accountChoices[accountIndex - 1].id : ""

        let currencies = []
        for (let index = 0; index < accountChoices.length; ++index) {
            const currency = accountChoices[index].currency
            if (currencies.indexOf(currency) < 0)
                currencies.push(currency)
        }
        currencyChoices = currencies.length > 0 ? currencies : ["RUB"]
        if (accountIndex > 0)
            selectedCurrency = accountChoices[accountIndex - 1].currency
        if (currencyChoices.indexOf(selectedCurrency) < 0)
            selectedCurrency = currencyChoices.indexOf("RUB") >= 0 ? "RUB" : currencyChoices[0]
        currencySelector.currentIndex = currencyChoices.indexOf(selectedCurrency)
        chartData = app.demoMode ? {}
                  : hasFilters || selectedAccountId.length === 0
                    ? app.filteredBalanceHistoryChart(selectedAccountId, selectedCurrency,
                                                       connectionIds, restrictConnections,
                                                       fromDate, toDate, selectedAccountId.length === 0)
                               : app.balanceHistoryChart(selectedAccountId, selectedCurrency)
        plot.hoveredIndex = -1
        plot.selectedIndex = -1
    }

    Component.onCompleted: {
        ready = true
        reload()
    }
    onAppChanged: if (ready) reload()
    onConnectionIdsChanged: if (ready) Qt.callLater(reload)
    onRestrictConnectionsChanged: if (ready) Qt.callLater(reload)
    onFromDateChanged: if (ready) Qt.callLater(reload)
    onToDateChanged: if (ready) Qt.callLater(reload)

    Connections {
        target: panel.app
        function onBalanceHistoryChanged() { panel.reload() }
        function onDemoModeChanged() { panel.reload() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: panel.composition ? qsTr("Остатки по подключениям") : qsTr("История остатков")
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            MinimalComboBox {
                id: currencySelector
                Layout.preferredWidth: 88
                implicitHeight: 34
                visible: panel.showControls
                model: panel.currencyChoices
                enabled: !panel.app.demoMode && panel.selectedAccountId.length === 0 && count > 1
                Accessible.name: qsTr("Валюта истории остатков")
                onActivated: function(index) {
                    panel.selectedCurrency = panel.currencyChoices[index]
                    panel.reload()
                }
            }
        }

        MinimalComboBox {
            id: accountSelector
            Layout.fillWidth: true
            visible: panel.showControls && panel.allowAccountSelection
            enabled: !panel.app.demoMode && count > 1
            implicitHeight: 34
            model: panel.accountLabels
            Accessible.name: qsTr("Счёт для истории остатков")
            onActivated: function(index) {
                panel.selectedAccountId = index > 0 ? panel.accountChoices[index - 1].id : ""
                panel.reload()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: panel.displayedPoint !== null
            spacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    objectName: "chartSelectedDate"
                    text: panel.displayedPoint ? panel.displayedPoint.dateLabel : ""
                    color: Theme.textMuted
                    font.pixelSize: 11
                }
                Text {
                    Layout.fillWidth: true
                    objectName: "chartSelectedBalance"
                    text: panel.displayedPoint ? panel.displayedPoint.label : ""
                    color: Theme.text
                    font.pixelSize: panel.compact ? 18 : 22
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }
            MinimalComboBox {
                id: scaleSelector
                objectName: "chartScale"
                visible: panel.hasComposition
                Layout.preferredWidth: 102
                implicitHeight: 34
                model: [qsTr("Состав"), qsTr("Итого")]
                Accessible.name: qsTr("Шкала графика")
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: panel.compact ? 60 : 120

            Text {
                anchors.fill: parent
                visible: panel.points.length === 0
                text: panel.app.demoMode
                      ? qsTr("Демо-данные не сохраняются в историю")
                      : panel.restrictConnections && panel.connectionIds.length === 0
                        ? qsTr("Выберите хотя бы один банк или кошелёк")
                      : panel.hasFilters
                        ? qsTr("Нет сохранённых остатков для выбранных источников и периода")
                      : qsTr("История появится после успешной синхронизации счетов")
                color: Theme.textMuted
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap
            }

            RowLayout {
                anchors.fill: parent
                visible: panel.points.length > 0
                spacing: 10
                Item {
                    id: scaleLabels
                    Layout.preferredWidth: Math.min(72, Math.max(46, panel.width * 0.18))
                    Layout.fillHeight: true
                    Repeater {
                        model: plot.labelTicks
                        delegate: Text {
                            required property var modelData
                            width: scaleLabels.width
                            y: plot.ordinate(modelData.y) - height / 2
                            text: modelData.label
                            color: modelData.zero ? Theme.text : Theme.textMuted
                            font.pixelSize: 10
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideLeft
                        }
                    }
                }

                Canvas {
                    id: plot
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    objectName: "balanceHistoryPlot"
                    activeFocusOnTab: true
                    property int hoveredIndex: -1
                    property int selectedIndex: -1
                    readonly property var values: panel.points
                    readonly property var bands: panel.series
                    readonly property int activeIndex: hoveredIndex >= 0 ? hoveredIndex : selectedIndex
                    readonly property var activePoint: values[activeIndex] || null
                    readonly property real padding: 8
                    readonly property var labelTicks: {
                        let placed = []
                        const ticks = panel.ticks
                        // Keep zero and the range bounds before adding intermediate labels.
                        for (const tick of [...ticks.filter(t => t.zero), ...ticks.filter(t => t.edge && !t.zero),
                                            ...ticks.filter(t => !t.edge && !t.zero)]) {
                            if (placed.every(t => Math.abs(tick.y - t.y) * (height - padding * 2) >= 15))
                                placed.push(tick)
                        }
                        return placed
                    }

                    function pointX(point) { return padding + point.x * (width - padding * 2) }
                    function ordinate(value) { return height - padding - value * (height - padding * 2) }
                    function nearestIndex(x) {
                        if (!values.length) return -1
                        if (panel.chartData.openingUnknown && values[0].x > 0
                                && x < pointX(values[0]) - padding) return -1
                        let low = 0
                        let high = values.length
                        while (low < high) {
                            const middle = Math.floor((low + high) / 2)
                            if (pointX(values[middle]) < x) low = middle + 1
                            else high = middle
                        }
                        if (low >= values.length) return values.length - 1
                        if (low > 0 && x - pointX(values[low - 1]) <= pointX(values[low]) - x) return low - 1
                        return low
                    }
                    function selectAt(x) {
                        hoveredIndex = -1
                        selectedIndex = nearestIndex(x)
                    }
                    function selectAdjacent(offset) {
                        if (!values.length) return
                        const index = activeIndex < 0 ? values.length - 1 : activeIndex
                        hoveredIndex = -1
                        selectedIndex = Math.max(0, Math.min(values.length - 1, index + offset))
                    }
                    Keys.onLeftPressed: selectAdjacent(-1)
                    Keys.onRightPressed: selectAdjacent(1)

                    function bandPoint(band, x) {
                        let low = 0
                        let high = band.points.length
                        while (low < high) {
                            const middle = Math.floor((low + high) / 2)
                            if (band.points[middle].x <= x + 0.00000001) low = middle + 1
                            else high = middle
                        }
                        return low > 0 ? band.points[low - 1] : null
                    }
                    function totalHeight(point) {
                        if (point.compositionY !== undefined) return point.compositionY
                        let top = 0
                        for (let index = 0; index < bands.length; ++index) {
                            const part = bandPoint(bands[index], point.x)
                            if (part) top = Math.max(top, part.upper)
                        }
                        return top
                    }
                    function pointY(point) {
                        return ordinate(panel.composition ? totalHeight(point) : point.y)
                    }

                    onValuesChanged: requestPaint()
                    onBandsChanged: requestPaint()
                    onLabelTicksChanged: requestPaint()
                    Connections {
                        target: panel
                        function onCompositionChanged() { plot.requestPaint() }
                    }
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()

                    onPaint: {
                        const context = getContext("2d")
                        context.reset()
                        if (values.length === 0) return
                        if (panel.composition) {
                            for (let index = 0; index < bands.length; ++index) {
                                const band = bands[index]
                                const samples = band.points
                                if (samples.length === 0) continue
                                context.fillStyle = panel.seriesColor(band.colorIndex)
                                context.globalAlpha = 0.78
                                if (samples.length === 1) {
                                    const barWidth = values.length === 1 ? Math.min(72, width / 3) : 3
                                    context.fillRect(pointX(samples[0]) - barWidth / 2, ordinate(samples[0].upper),
                                                     barWidth, ordinate(samples[0].lower) - ordinate(samples[0].upper))
                                    continue
                                }
                                context.beginPath()
                                context.moveTo(pointX(samples[0]), ordinate(samples[0].upper))
                                for (let sample = 1; sample < samples.length; ++sample) {
                                    context.lineTo(pointX(samples[sample]), ordinate(samples[sample - 1].upper))
                                    context.lineTo(pointX(samples[sample]), ordinate(samples[sample].upper))
                                }
                                context.lineTo(pointX(samples[samples.length - 1]), ordinate(samples[samples.length - 1].lower))
                                for (let sample = samples.length - 2; sample >= 0; --sample) {
                                    context.lineTo(pointX(samples[sample + 1]), ordinate(samples[sample].lower))
                                    context.lineTo(pointX(samples[sample]), ordinate(samples[sample].lower))
                                }
                                context.closePath()
                                context.fill()
                            }
                            context.globalAlpha = 1
                        }
                        context.lineWidth = 1
                        for (const tick of labelTicks) {
                            const y = ordinate(tick.y)
                            context.strokeStyle = tick.zero ? Theme.borderStrong : Theme.border
                            context.globalAlpha = panel.composition && !tick.zero ? 0.55 : 1
                            context.beginPath()
                            context.moveTo(padding, y)
                            context.lineTo(width - padding, y)
                            context.stroke()
                        }
                        context.globalAlpha = 1
                        context.beginPath()
                        for (let index = 0; index < values.length; ++index) {
                            const x = pointX(values[index])
                            const y = pointY(values[index])
                            if (index === 0) context.moveTo(x, y)
                            else {
                                if (panel.selectedAccountId.length === 0)
                                    context.lineTo(x, pointY(values[index - 1]))
                                context.lineTo(x, y)
                            }
                        }
                        context.lineWidth = panel.composition ? 1.5 : 2
                        context.strokeStyle = panel.composition ? Theme.text : Theme.primary
                        context.stroke()
                        if (values.length === 1) {
                            context.fillStyle = Theme.primary
                            context.beginPath()
                            context.arc(pointX(values[0]), pointY(values[0]), 3, 0, Math.PI * 2)
                            context.fill()
                        }
                    }

                    Rectangle {
                        visible: plot.activePoint !== null
                        x: plot.activePoint ? plot.pointX(plot.activePoint) - width / 2 : 0
                        y: plot.padding
                        width: 1
                        height: Math.max(0, plot.height - plot.padding * 2)
                        color: Theme.textMuted
                    }
                    Rectangle {
                        visible: plot.activePoint !== null
                        x: plot.activePoint ? plot.pointX(plot.activePoint) - width / 2 : 0
                        y: plot.activePoint ? plot.pointY(plot.activePoint) - height / 2 : 0
                        width: 9
                        height: width
                        radius: width / 2
                        color: Theme.primary
                        border.color: Theme.surface
                        border.width: 2
                    }

                    HoverHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                        cursorShape: Qt.CrossCursor
                        onPointChanged: if (hovered && !scrub.active) plot.hoveredIndex = plot.nearestIndex(point.position.x)
                        onHoveredChanged: if (!hovered) plot.hoveredIndex = -1
                    }
                    TapHandler {
                        onTapped: event => {
                            plot.forceActiveFocus()
                            plot.selectAt(event.position.x)
                        }
                    }
                    DragHandler {
                        id: scrub
                        target: null
                        yAxis.enabled: false
                        onActiveChanged: {
                            if (!active) return
                            plot.forceActiveFocus()
                            plot.selectAt(centroid.position.x)
                        }
                        onActiveTranslationChanged: {
                            if (active) plot.selectAt(centroid.position.x)
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: scaleLabels.width + 10 + plot.padding
            Layout.rightMargin: plot.padding
            visible: panel.points.length > 0
            Text {
                Layout.fillWidth: true
                text: panel.chartData.firstDate || ""
                color: Theme.textMuted
                font.pixelSize: 10
            }
            Text {
                visible: panel.points.length > 1
                text: panel.chartData.lastDate || ""
                color: Theme.textMuted
                font.pixelSize: 10
            }
        }
        Flickable {
            id: legendViewport
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(legend.implicitHeight, panel.compact ? 36 : 72)
            visible: panel.composition
            contentWidth: width
            contentHeight: legend.implicitHeight
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ScrollBar {}
            Grid {
                id: legend
                width: legendViewport.width
                columns: width >= 420 ? 2 : 1
                columnSpacing: 12
                rowSpacing: 4
                Repeater {
                    model: panel.series
                    delegate: Row {
                        id: legendEntry
                        required property var modelData
                        readonly property var sample: panel.displayedPoint
                            ? plot.bandPoint(modelData, panel.displayedPoint.x) : null
                        spacing: 5
                        Rectangle {
                            width: 8; height: 8; radius: 2
                            y: 3
                            color: panel.seriesColor(legendEntry.modelData.colorIndex)
                        }
                        Text {
                            width: (legend.width - (legend.columns - 1) * legend.columnSpacing) / legend.columns - 13
                            text: legendEntry.modelData.label + (legendEntry.sample
                                  ? " · " + legendEntry.sample.label + " · " + legendEntry.sample.shareLabel : "")
                            textFormat: Text.PlainText
                            color: Theme.textMuted
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            ToolTip.visible: legendHover.hovered
                            ToolTip.text: text
                            HoverHandler { id: legendHover }
                        }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: panel.points.length > 0
            text: !panel.displayedPoint ? ""
                  : panel.displayedPoint.carried ? qsTr("Без обновлений · %1").arg(panel.displayedPoint.timestamp)
                  : qsTr("Последняя синхронизация · %1").arg(panel.displayedPoint.timestamp)
            color: Theme.textMuted
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            visible: panel.errorText.length > 0
            text: panel.errorText
            color: Theme.negative
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
    }
}
