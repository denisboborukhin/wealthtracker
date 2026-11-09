import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtTest
import WealthTracker

TestCase {
    id: test
    name: "PrivacyScreen"
    width: 1100
    height: 850
    visible: true
    when: windowShown

    Component {
        id: screenFixture
        Item {
            id: fixture
            property bool locked: false
            property int unlocks: 0
            property int backgroundClicks: 0
            property int backgroundKeys: 0
            focus: true
            Keys.onPressed: backgroundKeys++
            Rectangle { anchors.fill: parent; color: "magenta" }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
                onClicked: fixture.backgroundClicks++
            }
            PrivacyScreen {
                objectName: "privacyScreen"
                anchors.fill: parent
                visible: fixture.locked
                onUnlockRequested: { fixture.unlocks++; fixture.locked = false }
            }
        }
    }
    Component {
        id: settingsFixture
        Item {
            id: fixture
            property int locks: 0
            property int unlocks: 0
            QtObject {
                id: mock
                property bool demoMode: false
                property bool locked: false
                function lock() { fixture.locks++; locked = true }
                function unlock() { fixture.unlocks++; locked = false }
            }
            SettingsPage { anchors.fill: parent; app: mock }
            PrivacyScreen {
                objectName: "privacyScreen"
                anchors.fill: parent
                visible: mock.locked
                onUnlockRequested: mock.unlock()
            }
        }
    }

    function sizes() {
        return [{tag: "phone", w: 320, h: 800}, {tag: "desktop", w: 1100, h: 850}]
    }

    function createFixture(component, data) {
        width = data.w
        height = data.h
        test.Window.window.width = width
        test.Window.window.height = height
        const fixture = createTemporaryObject(component, test, {width: width, height: height})
        verify(fixture !== null)
        return fixture
    }

    function test_inputAndKeyboardReturn_data() { return sizes() }
    function test_inputAndKeyboardReturn(data) {
        const fixture = createFixture(screenFixture, data)
        fixture.locked = true
        const screen = findChild(fixture, "privacyScreen")
        const button = findChild(screen, "returnToDataButton")
        tryCompare(button, "activeFocus", true)
        mouseClick(fixture, 5, 5)
        mouseClick(fixture, 5, 5, Qt.RightButton)
        mouseClick(fixture, 5, 5, Qt.MiddleButton)
        compare(fixture.backgroundClicks, 0)
        keyClick(Qt.Key_A)
        keyClick(Qt.Key_Escape)
        compare(fixture.backgroundKeys, 0)
        verify(fixture.locked)
        keyClick(Qt.Key_Space)
        compare(fixture.unlocks, 1)
        verify(!screen.visible)
        mouseClick(fixture, 5, 5)
        compare(fixture.backgroundClicks, 1)
        fixture.locked = true
        tryCompare(button, "activeFocus", true)
    }

    function test_cardBounds_data() { return sizes() }
    function test_cardBounds(data) {
        const fixture = createFixture(screenFixture, data)
        fixture.locked = true
        const screen = findChild(fixture, "privacyScreen")
        const card = findChild(screen, "privacyCard")
        verify(waitForRendering(card))
        compare(screen.width, fixture.width)
        compare(screen.height, fixture.height)
        verify(card.x >= 0 && card.y >= 0)
        verify(card.x + card.width <= screen.width)
        verify(card.y + card.height <= screen.height)
        const button = findChild(screen, "returnToDataButton")
        const position = button.mapToItem(card, 0, 0)
        verify(position.x >= 0 && position.y >= 0)
        verify(position.x + button.width <= card.width)
        verify(position.y + button.height <= card.height)
    }

    function test_settingsLockAndPointerReturn_data() { return sizes() }
    function test_settingsLockAndPointerReturn(data) {
        const fixture = createFixture(settingsFixture, data)
        const screen = findChild(fixture, "privacyScreen")
        verify(!screen.visible)
        const lock = findChild(fixture, "hidePrivateData")
        verify(lock !== null)
        verify(waitForRendering(lock))
        mouseClick(lock)
        compare(fixture.locks, 1)
        verify(screen.visible)
        mouseClick(findChild(screen, "returnToDataButton"))
        compare(fixture.unlocks, 1)
        verify(!screen.visible)
    }
}
