import QtQuick
import QtTest
import WealthTracker

TestCase {
    id: test
    name: "DashboardPeriod"
    width: 400
    height: 850
    visible: true
    when: windowShown

    Component { id: dashboardComponent; DashboardPage {} }
    QtObject {
        id: mock
        signal balanceHistoryChanged()
        signal periodRequested(string from, string to)
        property bool demoMode: false
        property bool hasFinancialData: false
        property string reportingFrom: "01.11.2026"
        property string reportingTo: "20.11.2026"
        property string reportingPeriodLabel: "Synthetic period"
        property string reportingPeriodError: ""
        property string netWorth: "0 ₽"
        property string netWorthUpdatedAt: ""
        property string reportIncome: "0 ₽"
        property string reportSpending: "0 ₽"
        property string savings: "0 ₽"
        property var categories: []
        property var transactions: []
        property var balanceHistoryAccounts: []
        property string balanceHistoryError: ""
        property int resets: 0
        function filteredBalanceHistoryChart() { return {} }
        function setReportingPeriod(from, to) {
            periodRequested(from, to)
            reportingFrom = from
            reportingTo = to
        }
        function resetReportingPeriod() {
            resets++
            reportingFrom = "01.11.2026"
            reportingTo = "20.11.2026"
        }
    }
    SignalSpy { id: requested; signalName: "periodRequested" }

    function dashboard() {
        mock.demoMode = false
        mock.reportingFrom = "01.11.2026"
        mock.reportingTo = "20.11.2026"
        mock.resets = 0
        requested.target = mock
        requested.clear()
        const page = createTemporaryObject(dashboardComponent, test,
            {width: width, height: height, app: mock})
        verify(page !== null)
        return page
    }

    function choose(field, date) {
        mouseClick(field)
        const calendar = findChild(field, "calendarPopup")
        tryCompare(calendar, "opened", true)
        const day = findChild(calendar.contentItem, "calendarDay_" + date)
        verify(day !== null)
        verify(waitForRendering(day))
        mouseClick(day)
        tryCompare(calendar, "opened", false)
    }

    function cleanup() { requested.target = null }

    function test_crossedPeriodBoundsAdjustTogether() {
        const page = dashboard()
        const from = findChild(page, "reportingFromPicker")
        const to = findChild(page, "reportingToPicker")
        choose(from, "05.11.2026")
        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], "05.11.2026")
        compare(requested.signalArguments[0][1], "20.11.2026")
        choose(from, "25.11.2026")
        compare(requested.count, 2)
        compare(requested.signalArguments[1][0], "25.11.2026")
        compare(requested.signalArguments[1][1], "25.11.2026")
        compare(from.value, "25.11.2026")
        compare(to.value, "25.11.2026")
        choose(to, "10.11.2026")
        compare(requested.count, 3)
        compare(requested.signalArguments[2][0], "10.11.2026")
        compare(requested.signalArguments[2][1], "10.11.2026")
        compare(from.value, "10.11.2026")
        compare(to.value, "10.11.2026")
    }

    function test_resetAndDemoGuards() {
        const page = dashboard()
        const from = findChild(page, "reportingFromPicker")
        const to = findChild(page, "reportingToPicker")
        const reset = findChild(page, "resetReportingPeriod")
        page.app.reportingFrom = "05.11.2026"
        compare(from.value, "05.11.2026")
        mouseClick(reset)
        compare(page.app.resets, 1)
        compare(from.value, "01.11.2026")
        page.app.demoMode = true
        verify(!from.enabled && !to.enabled && !reset.enabled)
        mouseClick(reset)
        mouseClick(from)
        compare(page.app.resets, 1)
        compare(requested.count, 0)
        verify(!findChild(from, "calendarPopup").opened)
    }
}
