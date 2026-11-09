import QtQuick
import QtQuick.Window
import QtTest
import WealthTracker

TestCase {
    id: test
    name: "ConnectionDialog"
    width: 1100
    height: 850
    visible: true
    when: windowShown
    property var openedDialog: null

    QtObject {
        id: mock
        signal retailConnectionChanged(string connectorId)
        property var states: ({})
        property var calls: []
        property var bankConnections: []
        property var connectionStates: ({})
        property string connectionMessage: "Selected connection"
        property string syncStatus: "Global status"
        property string zenPluginId: ""
        property string zenPluginBundlePath: ""
        property string zenPluginInstitution: ""
        property var zenPluginFields: []
        property bool zenPluginRunning: false
        property string zenPluginPrompt: ""
        property bool zenPluginPromptSecret: true
        property bool zenPluginAutoUpdate: false
        property string zenPluginLastSync: ""
        property string zenPluginNextSync: ""
        function retailConnectionState(id) { return states[id] ? Object.assign({}, states[id]) : ({}) }
        function describeBankConnection(id) { calls.push(["describe", id]) }
        function prepareRetailConnection(id) { calls.push(["prepare", id]) }
        function cancelRetailLogin(id) {
            calls.push(["cancel", id])
            states[id].loginActive = false
            retailConnectionChanged(id)
        }
        function openRetailCertificateInstructions(id) { calls.push(["certificates", id]); return true }
    }
    Component { id: pageComponent; ConnectionsPage {} }

    function createPage(w) {
        width = w
        test.Window.window.width = w
        mock.states = ({})
        mock.calls = []
        mock.syncStatus = "Global status"
        for (const id of ["tbank-retail", "alfa-retail", "vtb-retail", "ozon-retail"])
            mock.states[id] = {supported: true, connected: true, loginActive: false,
                syncRunning: false, autoUpdate: false, lastSync: "last", nextSync: "next",
                loginStatus: id + " selected status", webView: null, certificateHelpRequired: false}
        const page = createTemporaryObject(pageComponent, test, {width: width, height: height, app: mock})
        verify(page !== null)
        return page
    }
    function open(page, id) {
        page.openConnection(id, "Synthetic bank", "Synthetic method", "Saved", true, true)
        openedDialog = findChild(page, "connectionDialog")
        verify(openedDialog !== null)
        tryCompare(openedDialog, "opened", true)
        return openedDialog
    }
    function cleanup() {
        if (openedDialog) {
            openedDialog.close()
            tryCompare(openedDialog, "opened", false)
            openedDialog = null
        }
    }
    function test_selectedBankStatus_data() {
        return [{tag: "phone", w: 320}, {tag: "desktop", w: 1100}]
    }
    function test_selectedBankStatus(data) {
        const page = createPage(data.w)
        const dialog = open(page, "vtb-retail")
        const status = findChild(dialog, "connectedRetailStatus")
        verify(status !== null && status.visible)
        compare(status.text, "vtb-retail selected status")
        compare(mock.calls, [["describe", "vtb-retail"], ["prepare", "vtb-retail"]])
        mock.syncStatus = "Another bank is updating"
        mock.states["alfa-retail"].loginStatus = "Other bank failure"
        mock.retailConnectionChanged("alfa-retail")
        compare(status.text, "vtb-retail selected status")
        mock.states["vtb-retail"].loginStatus = "Selected bank refreshed"
        mock.retailConnectionChanged("vtb-retail")
        compare(status.text, "Selected bank refreshed")
        verify(waitForRendering(dialog.contentItem))
        verify(dialog.x >= 0 && dialog.y >= 0)
        verify(dialog.x + dialog.width <= width)
        verify(dialog.y + dialog.height <= height)
        mouseClick(findChild(dialog, "closeConnectionDialog"))
        tryCompare(dialog, "opened", false)
    }
    function test_certificateActionAndCancel() {
        const page = createPage(1100)
        mock.states["alfa-retail"].loginActive = true
        mock.states["alfa-retail"].certificateHelpRequired = true
        const dialog = open(page, "alfa-retail")
        const help = findChild(dialog, "certificateInstructions")
        verify(help !== null && help.visible && help.enabled)
        verify(waitForRendering(help))
        mouseClick(help)
        compare(mock.calls[mock.calls.length - 1], ["certificates", "alfa-retail"])
        mouseClick(findChild(dialog, "closeConnectionDialog"))
        tryCompare(dialog, "opened", false)
        compare(mock.calls[mock.calls.length - 1], ["cancel", "alfa-retail"])
        verify(!mock.states["alfa-retail"].loginActive)
    }
}
