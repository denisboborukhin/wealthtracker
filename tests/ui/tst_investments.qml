import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtTest
import WealthTracker

TestCase {
    id: test
    name: "Investments"
    width: 1100
    height: 900
    visible: true
    when: windowShown

    Component { id: pageComponent; InvestmentsPage {} }
    ListModel {
        id: holdingRows
        property var sortRequests: []
        function setSort(field, descending) {
            sortRequests.push({field: field, descending: descending})
        }
    }
    QtObject {
        id: mock
        property bool demoMode: false
        property string portfolioValue: "123 456 ₽"
        property string syncStatus: "A long synthetic synchronization status that should stay within the header"
        property var holdings: holdingRows
        property int synchronizations: 0
        function synchronizeInvestments() { synchronizations++ }
    }

    function init() {
        holdingRows.sortRequests = []
        holdingRows.clear()
        holdingRows.append({ticker: "TMOS", name: "<b>A very long instrument name with additional details</b>",
            allocation: "25%", performance: "+1,5%", value: "123 456 789 ₽", positive: true})
        mock.demoMode = false
        mock.synchronizations = 0
    }

    function page(w) {
        width = w
        test.Window.window.width = width
        test.Window.window.height = height
        const result = createTemporaryObject(pageComponent, test,
            {width: width, height: height, app: mock})
        verify(result !== null)
        verify(waitForRendering(result))
        return result
    }

    function latestSort() { return holdingRows.sortRequests[holdingRows.sortRequests.length - 1] }
    function inside(item, row) {
        const position = item.mapToItem(row, 0, 0)
        verify(position.x >= -1 && position.y >= -1)
        verify(position.x + item.width <= row.width + 1)
        verify(position.y + item.height <= row.height + 1)
    }

    function test_sortAndSynchronization() {
        const investments = page(1100)
        compare(latestSort().field, "ticker")
        compare(latestSort().descending, false)
        const selector = findChild(investments, "investmentSort")
        mouseClick(selector)
        tryCompare(selector.popup, "opened", true)
        const list = selector.popup.contentItem
        tryVerify(() => list.itemAtIndex(1) !== null)
        const nameChoice = list.itemAtIndex(1)
        verify(waitForRendering(nameChoice))
        mouseClick(nameChoice)
        tryCompare(selector.popup, "opened", false)
        compare(latestSort().field, "name")
        selector.forceActiveFocus()
        keyClick(Qt.Key_Down)
        compare(latestSort().field, "value")
        const direction = findChild(investments, "investmentSortDirection")
        mouseClick(direction)
        compare(latestSort().field, "value")
        compare(latestSort().descending, true)
        mouseClick(direction)
        compare(latestSort().descending, false)
        mouseClick(findChild(investments, "investmentSync"))
        compare(mock.synchronizations, 1)
    }

    function test_longInstrumentRows_data() {
        return [{tag: "phone", w: 320}, {tag: "medium", w: 640}, {tag: "desktop", w: 1100}]
    }
    function test_longInstrumentRows(data) {
        const investments = page(data.w)
        const list = findChild(investments, "holdingList")
        compare(list.count, 1)
        tryVerify(() => list.itemAtIndex(0) !== null)
        const row = list.itemAtIndex(0)
        const name = findChild(row, "holding_name")
        compare(name.text, "<b>A very long instrument name with additional details</b>")
        compare(name.textFormat, Text.PlainText)
        for (let field of ["ticker", "name", "value"])
            inside(findChild(row, "holding_" + field), row)
        compare(findChild(row, "holding_value").text, "123 456 789 ₽")
        compare(findChild(row, "holding_allocation").visible, data.w >= 680)
        compare(findChild(row, "holding_performance").visible, data.w >= 680)
        verify(list.clip)
    }
    function test_demoOnlyMetrics() {
        const investments = page(1100)
        const dividends = findChild(investments, "investmentDividends")
        const xirr = findChild(investments, "investmentXirr")
        verify(!dividends.visible && !xirr.visible)
        mock.demoMode = true
        verify(dividends.visible && xirr.visible)
        verify(dividends.title.indexOf("демо") >= 0)
        verify(xirr.title.indexOf("демо") >= 0)
        compare(dividends.value, "86 420 ₽")
        compare(xirr.value, "17,6%")
        mock.demoMode = false
        verify(!dividends.visible && !xirr.visible)
    }

}
