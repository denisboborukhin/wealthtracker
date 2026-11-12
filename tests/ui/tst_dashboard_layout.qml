import QtQuick
import QtQuick.Window
import QtTest
import WealthTracker

TestCase {
    id: test
    name: "DashboardLayout"
    width: 1100
    height: 850
    visible: true
    when: windowShown

    Component { id: dashboardComponent; DashboardPage {} }
    ListModel { id: categoryRows }
    ListModel { id: transactionRows }
    QtObject {
        id: mock
        signal balanceHistoryChanged()
        property bool demoMode: false
        property bool hasFinancialData: true
        property string reportingFrom: "01.11.2026"
        property string reportingTo: "20.11.2026"
        property string reportingPeriodLabel: "Synthetic reporting period"
        property string reportingPeriodError: ""
        property string netWorth: "123 456 ₽"
        property string netWorthUpdatedAt: "01.11.2026 12:00"
        property string reportIncome: "20 000 ₽"
        property string reportSpending: "12 000 ₽"
        property string savings: "8 000 ₽"
        property var categories: categoryRows
        property var transactions: transactionRows
        property var balanceHistoryAccounts: []
        property string balanceHistoryError: ""
        function filteredBalanceHistoryChart() { return {} }
    }

    function init() {
        mock.demoMode = false
        mock.hasFinancialData = true
        mock.netWorthUpdatedAt = "01.11.2026 12:00"
        mock.savings = "8 000 ₽"
        categoryRows.clear()
        transactionRows.clear()
        for (let index = 0; index < 24; ++index) {
            categoryRows.append({name: "Very long category name with additional detail " + index,
                amount: "123 456 789 ₽", share: index === 0 ? -0.25 : index === 1 ? 0.5 : 1.4,
                color: "#3157D5"})
            transactionRows.append({date: "01.11.2026", title: "A long transaction description " + index,
                category: "A long category description", amount: "-1 234 567 ₽", income: false, transfer: false})
        }
    }

    function page(w) {
        width = w
        test.Window.window.width = width
        test.Window.window.height = height
        const result = createTemporaryObject(dashboardComponent, test,
            {width: width, height: height, app: mock})
        verify(result !== null)
        verify(waitForRendering(result))
        return result
    }

    function inside(item, container) {
        const position = item.mapToItem(container, 0, 0)
        verify(position.x >= -1 && position.y >= -1)
        verify(position.x + item.width <= container.width + 1)
        verify(position.y + item.height <= container.height + 1)
    }

    function sizes() {
        return [{tag: "phone", w: 320, columns: 1},
                {tag: "medium", w: 640, columns: 2},
                {tag: "desktop", w: 1100, columns: 4}]
    }

    function test_responsiveCards_data() { return sizes() }
    function test_responsiveCards(data) {
        const dashboard = page(data.w)
        const content = findChild(dashboard, "dashboard_content")
        const metrics = findChild(dashboard, "dashboard_metricGrid")
        const cards = ["dashboardNetWorth", "dashboardIncome", "dashboardSpending", "dashboardSavings"]
            .map(name => findChild(dashboard, name))
        for (let index = 0; index < cards.length; ++index) {
            inside(cards[index], metrics)
            if (index > 0 && index % data.columns !== 0) {
                verify(cards[index].x >= cards[index - 1].x + cards[index - 1].width - 1)
                compare(cards[index].y, cards[index - 1].y)
            } else if (index > 0) {
                verify(cards[index].y >= cards[index - data.columns].y + cards[index - data.columns].height - 1)
            }
        }
        const history = findChild(dashboard, "dashboardHistory")
        const categories = findChild(dashboard, "dashboardCategories")
        if (data.w > 860)
            verify(categories.x >= history.x + history.width - 1)
        else
            verify(categories.y >= history.y + history.height - 1)
        for (let name of ["dashboard_reportingPeriodCard", "dashboard_metricGrid", "dashboard_summaryGrid", "dashboard_recentOperationsCard"])
            inside(findChild(dashboard, name), content)
        verify(findChild(dashboard, "dashboard_recentOperationsCard").height >= 182)
        verify(dashboard.contentHeight >= dashboard.height)
        if (data.w < 600)
            verify(dashboard.contentHeight > dashboard.height)
    }

    function test_populatedListsAndBoundedShares_data() { return sizes() }
    function test_populatedListsAndBoundedShares(data) {
        const dashboard = page(data.w)
        const categories = findChild(dashboard, "dashboard_categoryList")
        const transactions = findChild(dashboard, "dashboard_transactionList")
        compare(categories.count, 24)
        compare(transactions.count, 24)
        verify(categories.clip && transactions.clip)
        verify(categories.contentHeight > categories.height)
        verify(transactions.contentHeight > transactions.height)
        for (let index = 0; index < 3; ++index) {
            const row = categories.itemAtIndex(index)
            verify(row !== null)
            const track = findChild(row, "categoryShareTrack")
            const fill = findChild(row, "categoryShareFill")
            inside(track, row)
            inside(fill, track)
            const fraction = index === 0 ? 0 : index === 1 ? 0.5 : 1
            verify(Math.abs(fill.width - track.width * fraction) < 0.01)
            inside(findChild(row, "categoryName"), row)
            inside(findChild(row, "categoryAmount"), row)
        }
        categories.positionViewAtEnd()
        transactions.positionViewAtEnd()
        tryVerify(() => categories.atYEnd && transactions.atYEnd)
        verify(categories.contentY > 0 && transactions.contentY > 0)
        tryVerify(() => transactions.itemAtIndex(23) !== null)
        const transaction = transactions.itemAtIndex(23)
        for (let name of ["date", "title", "amount"])
            inside(findChild(transaction, "transaction_" + name), transaction)
    }

    function test_savingsTimestampAndEmptyCopy() {
        const dashboard = page(400)
        const savings = findChild(dashboard, "dashboardSavings")
        const worth = findChild(dashboard, "dashboardNetWorth")
        compare(savings.value, "8 000 ₽")
        mock.savings = "-5 000 ₽"
        compare(savings.value, "-5 000 ₽")
        compare(worth.value, "123 456 ₽")
        compare(worth.hint, "Последние данные · 01.11.2026 12:00")
        mock.netWorthUpdatedAt = ""
        compare(worth.hint, "Текущий остаток")
        mock.hasFinancialData = false
        compare(findChild(dashboard, "dashboardSubtitle").text, "Подключите банк, чтобы увидеть данные")
    }
    function test_externalLabelsAreLiteral() {
        const markup = '<b>Merchant</b><img src="http://127.0.0.1:9/not-loaded">'
        categoryRows.setProperty(0, "name", markup)
        transactionRows.setProperty(0, "title", markup)
        transactionRows.setProperty(0, "category", markup)
        const dashboard = page(1100)
        const categories = findChild(dashboard, "dashboard_categoryList")
        const transactions = findChild(dashboard, "dashboard_transactionList")
        tryVerify(() => categories.itemAtIndex(0) !== null && transactions.itemAtIndex(0) !== null)
        const name = findChild(categories.itemAtIndex(0), "categoryName")
        const title = findChild(transactions.itemAtIndex(0), "transaction_title")
        const category = findChild(transactions.itemAtIndex(0), "transaction_category")
        for (let label of [name, title, category]) {
            compare(label.textFormat, Text.PlainText)
            compare(label.text, markup)
        }
    }

}
