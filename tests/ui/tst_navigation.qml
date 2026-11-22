import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtTest
import WealthTracker

TestCase {
    id: test
    name: "AppNavigation"
    width: 1100
    height: 850
    visible: true
    when: windowShown

    Component { id: navigationComponent; AppNavigation { locked: false } }

    function navigation(w) {
        width = w
        test.Window.window.width = width
        const item = createTemporaryObject(navigationComponent, test,
            {width: width, height: height})
        verify(item !== null)
        compare(item.navigationItems.length, 7)
        return item
    }

    function test_desktopPointerAndKeyboard() {
        const item = navigation(1100)
        verify(!item.compact)
        compare(item.contentLeft, 204)
        compare(item.contentTop, 0)
        verify(waitForRendering(item))
        for (let index = 0; index < 7; ++index) {
            const button = findChild(item, "navigationItem_" + index)
            verify(button !== null && button.visible)
            mouseClick(button)
            compare(item.currentIndex, index)
        }
        const history = findChild(item, "navigationItem_1")
        history.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(item.currentIndex, 1)
        item.currentIndex = 3
        compare(findChild(item, "navigationSelector").currentIndex, 3)
    }

    function test_compactPointerKeyboardAndLock() {
        const item = navigation(400)
        verify(item.compact)
        compare(item.contentLeft, 0)
        compare(item.contentTop, 56)
        const selector = findChild(item, "navigationSelector")
        verify(selector !== null && selector.visible)
        compare(selector.count, 7)
        mouseClick(selector)
        tryCompare(selector.popup, "opened", true)
        const list = selector.popup.contentItem
        tryVerify(() => list.itemAtIndex(4) !== null)
        const connections = list.itemAtIndex(4)
        verify(waitForRendering(connections))
        mouseClick(connections)
        tryCompare(selector.popup, "opened", false)
        compare(item.currentIndex, 4)
        selector.forceActiveFocus()
        keyClick(Qt.Key_Down)
        compare(item.currentIndex, 5)
        item.locked = true
        verify(!selector.enabled)
        mouseClick(selector)
        verify(!selector.popup.opened)
        compare(item.currentIndex, 5)
    }

    function test_resizePreservesProgrammaticSelection() {
        const item = navigation(1100)
        item.currentIndex = 6
        item.width = 400
        tryCompare(item, "compact", true)
        compare(item.currentIndex, 6)
        compare(findChild(item, "navigationSelector").currentIndex, 6)
        verify(!findChild(item, "navigationRail").visible)
        verify(findChild(item, "compactNavigation").visible)
        compare(item.contentLeft, 0)
        compare(item.contentTop, 56)
        item.width = 1100
        tryCompare(item, "compact", false)
        compare(item.currentIndex, 6)
        verify(findChild(item, "navigationRail").visible)
        verify(!findChild(item, "compactNavigation").visible)
        compare(item.contentLeft, 204)
        compare(item.contentTop, 0)
    }
}
