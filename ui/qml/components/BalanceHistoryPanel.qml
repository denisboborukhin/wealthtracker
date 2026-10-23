pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SectionCard {
    id: panel
    required property var app
    property bool allowAccountSelection: true
    property bool showControls: true
    property bool showValue: true
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
    readonly property bool composition: selectedAccountId.length === 0
                                        && chartData.compositionAvailable === true && series.length > 0
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
        plot.hoveredSeriesIndex = -1
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
        Text {
            Layout.fillWidth: true
            visible: panel.showValue && panel.points.length > 0
            text: panel.chartData.lastValue || ""
            color: Theme.text
            font.pixelSize: panel.compact ? 18 : 22
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            visible: !panel.compact
            text: panel.composition
                  ? qsTr("Цвет — банк или EVM-кошелёк; линия — общий остаток")
                  : panel.selectedAccountId.length === 0
                    ? qsTr("Сумма последних известных остатков · без пересчёта валют")
                    : qsTr("Остаток на момент успешной синхронизации")
            color: Theme.textMuted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            visible: panel.selectedAccountId.length === 0 && (panel.chartData.compositionNote || "").length > 0
            text: panel.chartData.compositionNote || ""
            color: Theme.textMuted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
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
                    Layout.preferredWidth: Math.min(110, Math.max(minimumLabel.implicitWidth,
                                                               maximumLabel.implicitWidth))
                    Layout.fillHeight: true
                    Text {
                        id: maximumLabel
                        width: parent.width
                        y: !panel.composition && panel.chartData.minimumLabel === panel.chartData.maximumLabel
                           ? (parent.height - height) / 2 : 0
                        text: panel.composition ? panel.chartData.compositionMaximumLabel
                                                : panel.chartData.maximumLabel || ""
                        color: Theme.textMuted
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                    Text {
                        id: minimumLabel
                        width: parent.width
                        anchors.bottom: parent.bottom
                        visible: panel.composition || panel.chartData.minimumLabel !== panel.chartData.maximumLabel
                        text: panel.composition ? panel.chartData.compositionMinimumLabel
                                                : panel.chartData.minimumLabel || ""
                        color: Theme.textMuted
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        visible: panel.composition && panel.chartData.compositionZeroY > 0
                                 && panel.chartData.compositionZeroY < 1
                                 && panel.chartData.compositionMinimumLabel !== panel.chartData.compositionMaximumLabel
                        y: visible ? Math.max(0, Math.min(parent.height - height,
                            parent.height - plot.padding - panel.chartData.compositionZeroY * (parent.height - plot.padding * 2) - height / 2)) : 0
                        text: "0.00 " + panel.selectedCurrency
                        color: Theme.textMuted
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }

                Canvas {
                    id: plot
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    property int hoveredIndex: -1
                    property int hoveredSeriesIndex: -1
                    readonly property var values: panel.points
                    readonly property var bands: panel.series
                    readonly property var hoveredSeries: hoveredSeriesIndex >= 0 ? bands[hoveredSeriesIndex] : null
                    readonly property var hoveredTotal: hoveredIndex < 0 || hoveredIndex >= values.length ? null : values[hoveredIndex]
                    readonly property var hoveredPoint: hoveredTotal
                        ? hoveredSeries ? bandPoint(hoveredSeries, hoveredTotal.x) : hoveredTotal : null
                    readonly property var hoveredAccounts: {
                        let rows = []
                        if (panel.composition && hoveredTotal) {
                            for (let index = 0; index < bands.length; ++index) {
                                const point = bandPoint(bands[index], hoveredTotal.x)
                                if (point) rows.push({label: bands[index].label, colorIndex: bands[index].colorIndex,
                                                      value: point.label, share: point.shareLabel})
                            }
                        }
                        return rows
                    }
                    readonly property real padding: 5

                    function pointX(point) { return padding + point.x * (width - padding * 2) }
                    function ordinate(value) { return height - padding - value * (height - padding * 2) }
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
                        return ordinate(panel.composition
                                        ? point.upper !== undefined ? (point.lower + point.upper) / 2 : totalHeight(point)
                                        : point.y)
                    }

                    onValuesChanged: requestPaint()
                    onBandsChanged: requestPaint()
                    onHoveredIndexChanged: requestPaint()
                    onHoveredSeriesIndexChanged: requestPaint()
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()

                    onPaint: {
                        const context = getContext("2d")
                        context.reset()
                        if (values.length === 0) return
                        context.strokeStyle = Theme.border
                        context.lineWidth = 1
                        for (let row = 0; row < 3; ++row) {
                            const y = padding + row * (height - padding * 2) / 2
                            context.beginPath()
                            context.moveTo(padding, y)
                            context.lineTo(width - padding, y)
                            context.stroke()
                        }
                        if (panel.composition) {
                            for (let index = 0; index < bands.length; ++index) {
                                const band = bands[index]
                                const samples = band.points
                                if (samples.length === 0) continue
                                context.fillStyle = panel.seriesColor(band.colorIndex)
                                context.globalAlpha = index === hoveredSeriesIndex ? 1 : 0.82
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
                            if (panel.chartData.compositionZeroY !== undefined) {
                                const zero = ordinate(panel.chartData.compositionZeroY)
                                context.beginPath()
                                context.moveTo(padding, zero)
                                context.lineTo(width - padding, zero)
                                context.lineWidth = 1
                                context.strokeStyle = Theme.borderStrong
                                context.stroke()
                            }
                        }
                        context.beginPath()
                        for (let index = 0; index < values.length; ++index) {
                            const x = pointX(values[index])
                            const y = panel.composition ? ordinate(totalHeight(values[index])) : pointY(values[index])
                            if (index === 0) context.moveTo(x, y)
                            else {
                                if (panel.composition)
                                    context.lineTo(x, ordinate(totalHeight(values[index - 1])))
                                context.lineTo(x, y)
                            }
                        }
                        context.lineWidth = panel.composition ? 1.5 : 2
                        context.strokeStyle = panel.composition ? Theme.text : Theme.primary
                        context.stroke()
                        if (!panel.composition) {
                            context.fillStyle = Theme.primary
                            for (let index = 0; index < values.length; ++index) {
                                context.beginPath()
                                context.arc(pointX(values[index]), pointY(values[index]), index === hoveredIndex ? 4 : 2.5, 0, Math.PI * 2)
                                context.fill()
                            }
                        }
                        if (hoveredPoint) {
                            context.beginPath()
                            context.moveTo(pointX(hoveredPoint), padding)
                            context.lineTo(pointX(hoveredPoint), height - padding)
                            context.strokeStyle = Theme.textMuted
                            context.lineWidth = 1
                            context.stroke()
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                        onPositionChanged: function(mouse) {
                            let low = 0
                            let high = plot.values.length
                            while (low < high) {
                                const middle = Math.floor((low + high) / 2)
                                if (plot.pointX(plot.values[middle]) < mouse.x) low = middle + 1
                                else high = middle
                            }
                            let nearest = low < plot.values.length ? low : plot.values.length - 1
                            if (low > 0 && (nearest < 0 || Math.abs(mouse.x - plot.pointX(plot.values[low - 1]))
                                < Math.abs(mouse.x - plot.pointX(plot.values[nearest])))) nearest = low - 1
                            plot.hoveredIndex = nearest
                            plot.hoveredSeriesIndex = -1
                            if (!panel.composition || nearest < 0) return
                            const share = (plot.height - plot.padding - mouse.y) / (plot.height - plot.padding * 2)
                            for (let index = 0; index < plot.bands.length; ++index) {
                                const point = plot.bandPoint(plot.bands[index], plot.values[nearest].x)
                                if (point && point.upper > point.lower && share >= point.lower && share <= point.upper) {
                                    plot.hoveredSeriesIndex = index
                                    break
                                }
                            }
                        }
                        onExited: { plot.hoveredIndex = -1; plot.hoveredSeriesIndex = -1 }
                    }

                    Rectangle {
                        visible: plot.hoveredPoint !== null
                        width: Math.min(plot.width, panel.composition ? 440 : 280)
                        height: Math.min(plot.height, hoverLabels.implicitHeight + 18)
                        x: plot.hoveredPoint ? Math.max(0, Math.min(plot.width - width,
                            plot.pointX(plot.hoveredPoint) - width / 2)) : 0
                        y: plot.hoveredPoint ? Math.max(0, Math.min(plot.height - height,
                            plot.pointY(plot.hoveredPoint) - height - 10)) : 0
                        radius: 5
                        color: Theme.surface
                        border.color: Theme.borderStrong
                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 8
                            contentWidth: availableWidth
                            clip: true
                            ColumnLayout {
                                id: hoverLabels
                                width: parent.width
                                spacing: 5
                                Text {
                                    Layout.fillWidth: true
                                    text: plot.hoveredTotal ? plot.hoveredTotal.dateLabel || plot.hoveredTotal.timestamp : ""
                                    color: Theme.text
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: plot.hoveredTotal ? (panel.composition ? qsTr("Итого: ") : "") + plot.hoveredTotal.label : ""
                                    color: Theme.text
                                    font.pixelSize: 12
                                    wrapMode: Text.WrapAnywhere
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: panel.chartData.shareBasis === "absolute"
                                    text: qsTr("Доли от суммы модулей остатков")
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                }
                                Repeater {
                                    model: plot.hoveredAccounts
                                    delegate: RowLayout {
                                        id: accountShare
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 6
                                        Rectangle {
                                            Layout.preferredWidth: 8
                                            Layout.preferredHeight: 8
                                            radius: 2
                                            color: panel.seriesColor(accountShare.modelData.colorIndex)
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            text: accountShare.modelData.label
                                            textFormat: Text.PlainText
                                            color: Theme.textMuted
                                            font.pixelSize: 10
                                            wrapMode: Text.WordWrap
                                        }
                                        Text {
                                            Layout.maximumWidth: 180
                                            text: accountShare.modelData.value + " · " + accountShare.modelData.share
                                            color: Theme.text
                                            font.pixelSize: 10
                                            wrapMode: Text.WrapAnywhere
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: plot.hoveredTotal ? (plot.hoveredTotal.carried
                                          ? qsTr("Без обновлений; сохранены предыдущие остатки. Последняя синхронизация: ")
                                          : qsTr("Последняя синхронизация: ")) + plot.hoveredTotal.timestamp : ""
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
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
            Flow {
                id: legend
                width: legendViewport.width
                spacing: 12
                Repeater {
                    model: panel.series
                    delegate: Row {
                        id: legendEntry
                        required property var modelData
                        spacing: 5
                        Rectangle {
                            width: 8; height: 8; radius: 2
                            y: 3
                            color: panel.seriesColor(legendEntry.modelData.colorIndex)
                        }
                        Text {
                            width: Math.min(implicitWidth, 230)
                            text: legendEntry.modelData.label
                            textFormat: Text.PlainText
                            color: Theme.textMuted
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            ToolTip.visible: legendHover.hovered
                            ToolTip.text: legendEntry.modelData.label + " · " + legendEntry.modelData.lastValue
                            HoverHandler { id: legendHover }
                        }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: panel.points.length > 0
            text: panel.chartData.baselineCarried && panel.chartData.observationCount === 0
                  ? qsTr("В этом периоде обновлений нет — показаны последние известные остатки")
                  : panel.points.length === 1
                    ? qsTr("Один снимок — динамика появится после следующего обновления")
                    : panel.selectedAccountId.length === 0
                      ? qsTr("Снимки: %1 · Подключения: %2").arg(panel.chartData.observationCount).arg(panel.chartData.connectionCount || 0)
                      : qsTr("Снимки: %1").arg(panel.chartData.observationCount)
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
