import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtTest
import WealthTracker

TestCase {
    id: test
    name: "DatePicker"
    width: 400
    height: 800
    visible: true
    when: windowShown

    Component { id: fieldComponent; DatePickerField { value: "15.11.2026" } }
    SignalSpy { id: selected; signalName: "dateSelected" }

    function field(properties) {
        const item = createTemporaryObject(fieldComponent, test, properties || {x: 20, y: 20})
        verify(item !== null)
        selected.target = item
        selected.clear()
        return item
    }

    function popup(item) {
        const result = findChild(item, "calendarPopup")
        verify(result !== null)
        return result
    }

    function cleanup() {
        selected.target = null
        width = 400
        height = 800
        test.Window.window.width = width
        test.Window.window.height = height
    }

    function test_monthNavigationAndSelection() {
        const item = field()
        item.value = "15.12.2026"
        mouseClick(item)
        tryCompare(popup(item), "opened", true)
        compare(item.visibleMonth, 11)
        compare(item.visibleYear, 2026)
        mouseClick(findChild(item, "nextMonthButton"))
        compare(item.visibleMonth, 0)
        compare(item.visibleYear, 2027)
        mouseClick(findChild(item, "previousMonthButton"))
        compare(item.visibleMonth, 11)
        compare(item.visibleYear, 2026)
        compare(selected.count, 0)
        tryVerify(() => findChild(popup(item).contentItem, "calendarDay_22.12.2026") !== null)
        const day = findChild(popup(item).contentItem, "calendarDay_22.12.2026")
        verify(day !== null)
        verify(waitForRendering(day))
        mouseClick(day)
        tryCompare(popup(item), "opened", false)
        compare(selected.count, 1)
        compare(selected.signalArguments[0][0], "22.12.2026")
        // The owner controls value; reopening must restore that value.
        mouseClick(item)
        tryCompare(popup(item), "opened", true)
        compare(item.selectedDate.getDate(), 15)
        item.enabled = false
        tryCompare(popup(item), "opened", false)
        compare(selected.count, 1)
    }

    function test_valueChangesAndInvalidDates() {
        const item = field()
        item.value = "29.02.2024"
        compare(item.visibleMonth, 1)
        compare(item.selectedDate.getDate(), 29)
        compare(item.parsedDate("29.02.2026"), null)
        compare(item.parsedDate("31.04.2026"), null)
        compare(item.parsedDate("not a date"), null)
        item.value = "01.01.2027"
        compare(item.visibleYear, 2027)
        compare(item.visibleMonth, 0)
        compare(selected.count, 0)
    }

    function test_popupBounds_data() {
        return [
            {tag: "phone-left", w: 320, h: 700, x: 12, y: 20},
            {tag: "phone-bottom-right", w: 400, h: 800, x: 240, y: 730},
            {tag: "desktop-bottom-right", w: 1100, h: 800, x: 940, y: 730}
        ]
    }

    function test_popupBounds(data) {
        width = data.w
        height = data.h
        test.Window.window.width = width
        test.Window.window.height = height
        const item = field({x: data.x, y: data.y})
        mouseClick(item)
        const calendar = popup(item)
        tryCompare(calendar, "opened", true)
        compare(calendar.popupType, Popup.Item)
        verify(waitForRendering(calendar.contentItem))
        const rendered = calendar.contentItem.parent
        const origin = rendered.mapToItem(test.Window.window.contentItem, 0, 0)
        verify(origin.x >= 0 && origin.y >= 0)
        verify(origin.x + rendered.width <= width + 1)
        verify(origin.y + rendered.height <= height + 1)
        keyClick(Qt.Key_Escape)
        tryCompare(calendar, "opened", false)
        compare(selected.count, 0)
    }
}
