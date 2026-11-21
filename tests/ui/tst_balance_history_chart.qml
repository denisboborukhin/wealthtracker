import QtQuick
import QtTest
import WealthTracker

TestCase {
    id: test
    name: "BalanceHistoryChart"
    width: 360
    height: 500
    visible: true
    when: windowShown

    QtObject {
        id: historyMock
        property bool demoMode: false
        property string balanceHistoryError: ""
        property var balanceHistoryAccounts: []
        signal balanceHistoryChanged()
        function filteredBalanceHistoryChart() {
            const points = Array.from({length: 5}, (_, i) => ({
                x: i / 4, y: i / 4, compositionY: 0.2 + i / 8,
                label: (10000 + i * 5000) + ".00 RUB", dateLabel: "0" + (i + 1) + ".11.2026",
                timestamp: "01.11.2026 09:00:00", carried: i > 0
            }))
            return {
                points: points, firstDate: "01.11.2026", lastDate: "05.11.2026",
                compositionAvailable: true,
                series: [{label: "Bank", colorIndex: 0, points: points.map(p => ({
                    x: p.x, lower: 0, upper: p.compositionY, label: p.label, shareLabel: "100 %"
                }))}],
                compositionTicks: [{y: 0, label: "0", zero: true, edge: true},
                                   {y: 0.5, label: "20000"}, {y: 1, label: "40000", edge: true}],
                valueTicks: [{y: 0, label: "10000", edge: true},
                             {y: 0.5, label: "20000"}, {y: 1, label: "30000", edge: true}]
            }
        }
    }

    Component {
        id: fixture
        Flickable {
            width: test.width
            height: test.height
            contentHeight: 800
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            BalanceHistoryPanel {
                objectName: "chart"
                width: test.width
                height: 380
                app: historyMock
                showControls: false
            }
        }
    }

    function createChart() {
        const scroller = createTemporaryObject(fixture, test)
        verify(scroller !== null)
        const panel = findChild(scroller, "chart")
        verify(waitForRendering(panel))
        return {scroller: scroller, panel: panel, plot: findChild(panel, "balanceHistoryPlot"),
            date: findChild(panel, "chartSelectedDate"), balance: findChild(panel, "chartSelectedBalance")}
    }

    function test_mouseDragAndScale() {
        const chart = createChart()
        compare(chart.date.text, "05.11.2026")
        const y = chart.plot.height / 2
        mouseDrag(chart.plot, chart.plot.width / 4, y, chart.plot.width / 4, 0, Qt.LeftButton)
        mouseMove(chart.scroller, chart.scroller.width - 2, chart.scroller.height - 2)
        compare(chart.date.text, "03.11.2026")
        compare(chart.balance.text, "20000.00 RUB")

        const scale = findChild(chart.panel, "chartScale")
        compare(chart.plot.labelTicks[0].label, "0")
        scale.forceActiveFocus()
        keyClick(Qt.Key_Down)
        tryCompare(chart.panel, "composition", false)
        compare(chart.plot.labelTicks[0].label, "10000")
        compare(chart.date.text, "03.11.2026")
        compare(chart.balance.text, "20000.00 RUB")
    }

    function test_touchScrubAndVerticalScroll() {
        const chart = createChart()
        const x = chart.plot.width / 4
        const y = chart.plot.height / 2
        const touch = touchEvent(chart.plot)
        touch.press(0, chart.plot, x, y).commit()
        touch.move(0, chart.plot, x + 20, y).commit()
        touch.move(0, chart.plot, chart.plot.width * 0.75, y).commit()
        tryCompare(chart.date, "text", "04.11.2026")
        touch.release(0, chart.plot, chart.plot.width * 0.75, y).commit()
        compare(chart.balance.text, "25000.00 RUB")
        compare(chart.scroller.contentY, 0)

        touch.press(0, chart.plot, x, y).commit()
        touch.move(0, chart.plot, x, y - 20).commit()
        touch.move(0, chart.plot, x, y - 62).commit()
        touch.release(0, chart.plot, x, y - 62).commit()
        tryVerify(() => chart.scroller.contentY > 0)
        compare(chart.date.text, "04.11.2026")
    }
}
