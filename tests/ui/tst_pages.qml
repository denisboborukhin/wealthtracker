import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtTest
import WealthTracker

TestCase {
    id: test
    name: "ConnectionPages"
    width: 400
    height: 850
    visible: true
    when: windowShown

    QtObject {
        id: mock
        signal balanceHistoryChanged()
        property bool demoMode: false
        property bool locked: false
        property string balanceHistoryError: ""
        property var balanceHistoryConnections: [
            {id: "tbank-retail", label: "Т-Банк"}, {id: "alfa-retail", label: "Альфа-Банк"}]
        property var balanceHistoryAccounts: []
        property var cashbackMonths: [{id: "2026-11-01", label: "Ноябрь 2026"},
                                      {id: "2026-12-01", label: "Декабрь 2026"}]
        property string cashbackError: ""
        property string refreshedBank: ""
        property var cashbackBanks: [{id: "tbank", name: "Т-Банк", connected: true,
            syncRunning: false, cashbackRunning: false, syncStatus: "", lastSync: "01.11.2026",
            cashbackStatus: "Категории обновлены", cashbackLastSync: "01.11.2026",
            current: [], next: [], currentFromBank: true, nextFromBank: false,
            currentSelectionRequired: false, nextSelectionRequired: false}]
        function balanceHistorySummary() { return {} }
        function filteredBalanceHistoryChart() { return {} }
        function refreshCashbackBank(bank) { refreshedBank = bank }
        function refreshCashbackBanks() { refreshedBank = "all" }
    }
    Component { id: history; HistoryPage {} }
    Component { id: cashback; CashbackPage {} }

    function test_historyMasterCheckbox() {
        const page = createTemporaryObject(history, test, {width: width, height: height, app: mock})
        verify(page !== null)
        mouseClick(findChild(page, "historyConfigure"))
        const all = findChild(test.Window.window.contentItem, "historyAllSources")
        const tbank = findChild(test.Window.window.contentItem, "historySource_tbank-retail")
        const alfa = findChild(test.Window.window.contentItem, "historySource_alfa-retail")
        verify(all.checked && tbank.checked && alfa.checked)
        mouseClick(all)
        verify(!all.checked && !tbank.checked && !alfa.checked)
        compare(page.selectedConnectionIds.length, 0)
        verify(!page.allConnections)
        mouseClick(all)
        verify(all.checked && tbank.checked && alfa.checked)
        mouseClick(tbank)
        verify(!all.checked && !tbank.checked && alfa.checked)
        mouseClick(all)
        verify(all.checked && tbank.checked && alfa.checked)
    }

    function test_singleBankCashbackButton() {
        mock.refreshedBank = ""
        const page = createTemporaryObject(cashback, test, {width: width, height: height, app: mock})
        verify(page !== null)
        const button = findChild(page, "cashbackRefresh_tbank")
        verify(button !== null && button.enabled)
        page.contentY = 170
        wait(50)
        mouseClick(button)
        compare(mock.refreshedBank, "tbank")
    }
}
