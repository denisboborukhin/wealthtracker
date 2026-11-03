pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    required property var app
    required property string connectorId
    property bool fallbackConnected: false

    readonly property var bank: [
        {id: "tbank-retail", propertyPrefix: "tbank", methodPrefix: "TBank"},
        {id: "alfa-retail", propertyPrefix: "alfa", methodPrefix: "Alfa"},
        {id: "vtb-retail", propertyPrefix: "vtb", methodPrefix: "Vtb"},
        {id: "ozon-retail", propertyPrefix: "ozon", methodPrefix: "Ozon"}
    ].find(item => item.id === connectorId)
    readonly property string methodPrefix: bank ? bank.methodPrefix : ""
    readonly property bool supported: bank !== undefined
    readonly property bool connected: bank ? app[bank.propertyPrefix + "Connected"] : fallbackConnected
    readonly property bool loginActive: bank ? app[bank.propertyPrefix + "LoginActive"] : false
    readonly property bool syncRunning: bank ? app[bank.propertyPrefix + "SyncRunning"] : false
    readonly property bool autoUpdate: bank ? app[bank.propertyPrefix + "AutoUpdate"] : false
    readonly property string lastSync: bank ? app[bank.propertyPrefix + "LastSync"] : ""
    readonly property string nextSync: bank ? app[bank.propertyPrefix + "NextSync"] : ""
    readonly property string loginStatus: bank ? app[bank.propertyPrefix + "LoginStatus"] : ""
    readonly property var webView: bank ? app[bank.propertyPrefix + "WebView"] : null

    function prepare() { if (supported) app["prepare" + methodPrefix + "Connection"]() }
    function beginLogin() { if (supported) app["begin" + methodPrefix + "Login"]() }
    function reloadLogin() { if (supported) app["reload" + methodPrefix + "LoginPage"]() }
    function cancelLogin() { if (supported) app["cancel" + methodPrefix + "Login"]() }
    function synchronize() { if (supported) app["synchronize" + methodPrefix]() }
    function setAutoUpdate(enabled) { if (supported) app["set" + methodPrefix + "AutoUpdate"](enabled) }
    function disconnectBank() { if (supported) app["disconnect" + methodPrefix]() }
}
