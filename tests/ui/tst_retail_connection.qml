import QtQuick
import QtTest
import WealthTracker
TestCase {
 id:test; name:"RetailAdapter"
 QtObject {id:mock;property var calls:[];property bool lastEnabled:false;
 property bool tbankConnected: false
 property bool tbankLoginActive: false
 property bool tbankSyncRunning: false
 property bool tbankAutoUpdate: false
 property string tbankLastSync: "tbankLastSync"
 property string tbankNextSync: "tbankNextSync"
 property string tbankLoginStatus: "tbankLoginStatus"
 property var tbankWebView: null
 property bool alfaConnected: false
 property bool alfaLoginActive: false
 property bool alfaSyncRunning: false
 property bool alfaAutoUpdate: false
 property string alfaLastSync: "alfaLastSync"
 property string alfaNextSync: "alfaNextSync"
 property string alfaLoginStatus: "alfaLoginStatus"
 property var alfaWebView: null
 property bool vtbConnected: false
 property bool vtbLoginActive: false
 property bool vtbSyncRunning: false
 property bool vtbAutoUpdate: false
 property string vtbLastSync: "vtbLastSync"
 property string vtbNextSync: "vtbNextSync"
 property string vtbLoginStatus: "vtbLoginStatus"
 property var vtbWebView: null
 property bool ozonConnected: false
 property bool ozonLoginActive: false
 property bool ozonSyncRunning: false
 property bool ozonAutoUpdate: false
 property string ozonLastSync: "ozonLastSync"
 property string ozonNextSync: "ozonNextSync"
 property string ozonLoginStatus: "ozonLoginStatus"
 property var ozonWebView: null
 function prepareTBankConnection() {calls.push("prepareTBankConnection")}
 function beginTBankLogin() {calls.push("beginTBankLogin")}
 function reloadTBankLoginPage() {calls.push("reloadTBankLoginPage")}
 function cancelTBankLogin() {calls.push("cancelTBankLogin")}
 function synchronizeTBank() {calls.push("synchronizeTBank")}
 function disconnectTBank() {calls.push("disconnectTBank")}
 function setTBankAutoUpdate(enabled) {calls.push("setTBankAutoUpdate");lastEnabled=enabled}
 function prepareAlfaConnection() {calls.push("prepareAlfaConnection")}
 function beginAlfaLogin() {calls.push("beginAlfaLogin")}
 function reloadAlfaLoginPage() {calls.push("reloadAlfaLoginPage")}
 function cancelAlfaLogin() {calls.push("cancelAlfaLogin")}
 function synchronizeAlfa() {calls.push("synchronizeAlfa")}
 function disconnectAlfa() {calls.push("disconnectAlfa")}
 function setAlfaAutoUpdate(enabled) {calls.push("setAlfaAutoUpdate");lastEnabled=enabled}
 function prepareVtbConnection() {calls.push("prepareVtbConnection")}
 function beginVtbLogin() {calls.push("beginVtbLogin")}
 function reloadVtbLoginPage() {calls.push("reloadVtbLoginPage")}
 function cancelVtbLogin() {calls.push("cancelVtbLogin")}
 function synchronizeVtb() {calls.push("synchronizeVtb")}
 function disconnectVtb() {calls.push("disconnectVtb")}
 function setVtbAutoUpdate(enabled) {calls.push("setVtbAutoUpdate");lastEnabled=enabled}
 function prepareOzonConnection() {calls.push("prepareOzonConnection")}
 function beginOzonLogin() {calls.push("beginOzonLogin")}
 function reloadOzonLoginPage() {calls.push("reloadOzonLoginPage")}
 function cancelOzonLogin() {calls.push("cancelOzonLogin")}
 function synchronizeOzon() {calls.push("synchronizeOzon")}
 function disconnectOzon() {calls.push("disconnectOzon")}
 function setOzonAutoUpdate(enabled) {calls.push("setOzonAutoUpdate");lastEnabled=enabled}
 }
 RetailConnection {id:adapter;app:mock;connectorId:""}
 function test_dispatch_data(){return [
 {tag:"tbank",id:"tbank-retail",stem:"TBank",prefix:"tbank"},
 {tag:"alfa",id:"alfa-retail",stem:"Alfa",prefix:"alfa"},
 {tag:"vtb",id:"vtb-retail",stem:"Vtb",prefix:"vtb"},
 {tag:"ozon",id:"ozon-retail",stem:"Ozon",prefix:"ozon"}]}
 function test_dispatch(data){
  adapter.connectorId=data.id;mock.calls=[]
  verify(adapter.supported);compare(adapter.connected,false)
  mock[data.prefix+"Connected"]=true;compare(adapter.connected,true)
  mock[data.prefix+"LoginActive"]=true;compare(adapter.loginActive,true)
  mock[data.prefix+"SyncRunning"]=true;compare(adapter.syncRunning,true)
  mock[data.prefix+"AutoUpdate"]=true;compare(adapter.autoUpdate,true)
  compare(adapter.lastSync,data.prefix+"LastSync");compare(adapter.nextSync,data.prefix+"NextSync")
  compare(adapter.loginStatus,data.prefix+"LoginStatus");compare(adapter.webView,null)
  mock[data.prefix+"LoginStatus"]="updated";compare(adapter.loginStatus,"updated")
  adapter.prepare();adapter.beginLogin();adapter.reloadLogin();adapter.cancelLogin()
  adapter.synchronize();adapter.setAutoUpdate(true);adapter.disconnectBank()
  compare(mock.calls,["prepare"+data.stem+"Connection","begin"+data.stem+"Login",
   "reload"+data.stem+"LoginPage","cancel"+data.stem+"Login","synchronize"+data.stem,
   "set"+data.stem+"AutoUpdate","disconnect"+data.stem]);compare(mock.lastEnabled,true)
 }
 function test_unknown_data(){return [{tag:"empty",id:""},{tag:"token",id:"t-invest"},{tag:"unknown",id:"other-retail"},{tag:"prototype",id:"toString"}]}
 function test_unknown(data){
  adapter.connectorId=data.id;mock.calls=[];adapter.fallbackConnected=true
  verify(!adapter.supported);verify(adapter.connected);verify(!adapter.loginActive)
  verify(!adapter.syncRunning);verify(!adapter.autoUpdate);compare(adapter.lastSync,"")
  compare(adapter.nextSync,"");compare(adapter.loginStatus,"");compare(adapter.webView,null)
  adapter.prepare();adapter.beginLogin();adapter.reloadLogin();adapter.cancelLogin()
  adapter.synchronize();adapter.setAutoUpdate(true);adapter.disconnectBank();compare(mock.calls.length,0)
 }
}
