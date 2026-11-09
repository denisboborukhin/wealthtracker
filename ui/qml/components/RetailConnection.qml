pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    id: adapter
    required property var app
    required property string connectorId
    property bool fallbackConnected: false
    property var snapshot: ({})

    function update() {
        snapshot = app ? app.retailConnectionState(connectorId) : ({})
    }
    onAppChanged: update()
    onConnectorIdChanged: update()
    Component.onCompleted: update()
    readonly property Connections updates: Connections {
        target: adapter.app
        function onRetailConnectionChanged(connectorId) {
            if (connectorId === adapter.connectorId) adapter.update()
        }
    }

    readonly property bool supported: snapshot.supported === true
    readonly property bool connected: supported ? snapshot.connected : fallbackConnected
    readonly property bool loginActive: supported ? snapshot.loginActive : false
    readonly property bool syncRunning: supported ? snapshot.syncRunning : false
    readonly property bool autoUpdate: supported ? snapshot.autoUpdate : false
    readonly property string lastSync: supported ? snapshot.lastSync : ""
    readonly property string nextSync: supported ? snapshot.nextSync : ""
    readonly property string loginStatus: supported ? snapshot.loginStatus : ""
    readonly property var webView: supported ? snapshot.webView : null
    readonly property bool certificateHelpRequired: supported && snapshot.certificateHelpRequired

    function prepare() { if (supported) app.prepareRetailConnection(connectorId) }
    function beginLogin() { if (supported) app.beginRetailLogin(connectorId) }
    function reloadLogin() { if (supported) app.reloadRetailLogin(connectorId) }
    function cancelLogin() { if (supported) app.cancelRetailLogin(connectorId) }
    function synchronize() { if (supported) app.synchronizeRetailConnection(connectorId) }
    function setAutoUpdate(enabled) { if (supported) app.setRetailAutoUpdate(connectorId, enabled) }
    function disconnectBank() { if (supported) app.disconnectRetailConnection(connectorId) }
    function openCertificateInstructions() {
        if (supported) return app.openRetailCertificateInstructions(connectorId)
        return false
    }
}
