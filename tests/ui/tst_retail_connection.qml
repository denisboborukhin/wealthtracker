import QtQuick
import QtTest
import WealthTracker

TestCase {
    id: test
    name: "RetailAdapter"
    QtObject {
        id: mock
        signal retailConnectionChanged(string connectorId)
        property var states: ({})
        property var calls: []
        property bool lastEnabled: false
        function retailConnectionState(id) { return states[id] ? Object.assign({}, states[id]) : ({}) }
        function prepareRetailConnection(id) { calls.push(["prepare", id]) }
        function beginRetailLogin(id) { calls.push(["begin", id]) }
        function reloadRetailLogin(id) { calls.push(["reload", id]) }
        function cancelRetailLogin(id) { calls.push(["cancel", id]) }
        function synchronizeRetailConnection(id) { calls.push(["sync", id]) }
        function disconnectRetailConnection(id) { calls.push(["disconnect", id]) }
        function setRetailAutoUpdate(id, enabled) { calls.push(["auto", id]); lastEnabled = enabled }
        function openRetailCertificateInstructions(id) { calls.push(["certificates", id]); return true }
    }
    RetailConnection { id: adapter; app: mock; connectorId: "" }

    function init() {
        adapter.connectorId = ""
        adapter.fallbackConnected = false
        mock.states = ({})
        mock.calls = []
        mock.lastEnabled = false
    }
    function test_dispatch_data() {
        return ["tbank-retail", "alfa-retail", "vtb-retail", "ozon-retail"].map(id => ({tag: id, id: id}))
    }
    function test_dispatch(data) {
        mock.states[data.id] = {supported: true, connected: true, loginActive: true,
            syncRunning: true, autoUpdate: true, lastSync: "last", nextSync: "next",
            loginStatus: "selected bank", webView: null, certificateHelpRequired: false}
        adapter.connectorId = data.id
        verify(adapter.supported && adapter.connected && adapter.loginActive)
        verify(adapter.syncRunning && adapter.autoUpdate)
        compare(adapter.lastSync, "last")
        compare(adapter.nextSync, "next")
        compare(adapter.loginStatus, "selected bank")
        compare(adapter.webView, null)
        mock.states[data.id].loginStatus = "updated"
        mock.retailConnectionChanged("other-retail")
        compare(adapter.loginStatus, "selected bank")
        mock.retailConnectionChanged(data.id)
        compare(adapter.loginStatus, "updated")
        adapter.prepare(); adapter.beginLogin(); adapter.reloadLogin(); adapter.cancelLogin()
        adapter.synchronize(); adapter.setAutoUpdate(true); adapter.disconnectBank()
        verify(adapter.openCertificateInstructions())
        compare(mock.calls, ["prepare", "begin", "reload", "cancel", "sync", "auto", "disconnect", "certificates"].map(action => [action, data.id]))
        verify(mock.lastEnabled)
    }
    function test_unknown_data() {
        return ["", "t-invest", "other-retail", "toString"].map(id => ({tag: id || "empty", id: id}))
    }
    function test_unknown(data) {
        adapter.connectorId = data.id
        adapter.fallbackConnected = true
        verify(!adapter.supported && adapter.connected)
        verify(!adapter.loginActive && !adapter.syncRunning && !adapter.autoUpdate)
        compare(adapter.lastSync, ""); compare(adapter.nextSync, "")
        compare(adapter.loginStatus, ""); compare(adapter.webView, null)
        adapter.prepare(); adapter.beginLogin(); adapter.reloadLogin(); adapter.cancelLogin()
        adapter.synchronize(); adapter.setAutoUpdate(true); adapter.disconnectBank()
        verify(!adapter.openCertificateInstructions())
        compare(mock.calls.length, 0)
    }
}
