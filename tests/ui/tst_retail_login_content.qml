import QtQuick
import QtTest
import WealthTracker

TestCase {
    id: test
    name: "RetailLoginContent"
    width: 800
    height: 800
    visible: true
    when: windowShown

    Component {
        id: desktopComponent
        RetailLoginContent {
            width: 460
            loginActive: false
            mobileLogin: false
            tbank: false
            status: ""
            webView: null
        }
    }
    Component {
        id: mobileComponent
        MobileRetailLoginContent {
            width: 304
            height: 600
            institution: "Synthetic bank"
            status: "Synthetic status"
            mobileLogin: true
            webView: null
        }
    }
    Component {
        id: bankWindowComponent
        Window { width: 200; height: 200; visible: false }
    }
    SignalSpy { id: closeSpy; signalName: "closeRequested" }
    SignalSpy { id: reloadSpy; signalName: "reloadRequested" }

    function test_desktopVisibilityAndStatus() {
        const content = createTemporaryObject(desktopComponent, test)
        verify(content !== null)
        const nativeWindow = findChild(content, "desktopBankWindow")
        verify(nativeWindow !== null)
        verify(!nativeWindow.visible)
        compare(nativeWindow.window, null)
        const status = findChild(content, "retailLoginStatus")
        verify(status.text.indexOf("Нажмите «Войти»") >= 0)
        content.status = "Synthetic updated status"
        compare(status.text, "Synthetic updated status")
        const bankWindow = createTemporaryObject(bankWindowComponent, null)
        verify(bankWindow !== null)
        content.webView = bankWindow
        compare(nativeWindow.window, null)
        content.loginActive = true
        verify(nativeWindow.visible)
        compare(nativeWindow.window, bankWindow)
        content.mobileLogin = true
        verify(!nativeWindow.visible)
        compare(nativeWindow.window, null)
        content.mobileLogin = false
        verify(nativeWindow.visible)
        compare(nativeWindow.window, bankWindow)
        content.loginActive = false
        verify(!nativeWindow.visible)
        compare(nativeWindow.window, null)
    }

    function test_mobileActionsAndStatus() {
        const content = createTemporaryObject(mobileComponent, test)
        verify(content !== null)
        closeSpy.target = content
        reloadSpy.target = content
        closeSpy.clear()
        reloadSpy.clear()
        const status = findChild(content, "retailLoginStatus")
        compare(status.text, "Synthetic status")
        compare(status.maximumLineCount, 3)
        content.status = "Updated status"
        compare(status.text, "Updated status")
        const nativeWindow = findChild(content, "mobileBankWindow")
        verify(nativeWindow !== null)
        compare(nativeWindow.window, null)
        const bankWindow = createTemporaryObject(bankWindowComponent, null)
        verify(bankWindow !== null)
        content.webView = bankWindow
        compare(nativeWindow.window, bankWindow)
        content.mobileLogin = false
        compare(nativeWindow.window, null)
        content.mobileLogin = true
        compare(nativeWindow.window, bankWindow)
        content.mobileLogin = false
        compare(nativeWindow.window, null)
        mouseClick(findChild(content, "retailLoginClose"))
        compare(closeSpy.count, 1)
        mouseClick(findChild(content, "retailLoginReload"))
        compare(reloadSpy.count, 1)
        closeSpy.target = null
        reloadSpy.target = null
    }
}
