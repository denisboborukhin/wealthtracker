pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: field

    required property string value
    property string accessibleName: qsTr("Выбрать дату")
    property date selectedDate: new Date()
    property int visibleMonth: selectedDate.getMonth()
    property int visibleYear: selectedDate.getFullYear()

    signal dateSelected(string selectedValue)

    implicitWidth: 142
    implicitHeight: 36
    leftPadding: 11
    rightPadding: 10
    hoverEnabled: true
    Accessible.name: accessibleName
    Accessible.description: qsTr("Открывает календарь")

    function parsedDate(text) {
        const parts = String(text).split(".")
        if (parts.length !== 3)
            return null
        const day = Number(parts[0])
        const month = Number(parts[1])
        const year = Number(parts[2])
        const candidate = new Date(year, month - 1, day, 12, 0, 0, 0)
        if (candidate.getFullYear() !== year
                || candidate.getMonth() !== month - 1
                || candidate.getDate() !== day)
            return null
        return candidate
    }

    function sameDay(left, right) {
        return left instanceof Date && right instanceof Date
                && left.getFullYear() === right.getFullYear()
                && left.getMonth() === right.getMonth()
                && left.getDate() === right.getDate()
    }

    function syncCalendar() {
        const parsed = parsedDate(value)
        if (parsed === null)
            return
        selectedDate = parsed
        visibleMonth = parsed.getMonth()
        visibleYear = parsed.getFullYear()
    }

    function moveMonth(offset) {
        const target = new Date(visibleYear, visibleMonth + offset, 1, 12, 0, 0, 0)
        visibleMonth = target.getMonth()
        visibleYear = target.getFullYear()
    }

    function chooseDate(candidate) {
        selectedDate = new Date(candidate.getFullYear(), candidate.getMonth(),
                                candidate.getDate(), 12, 0, 0, 0)
        dateSelected(Qt.formatDate(selectedDate, "dd.MM.yyyy"))
        calendarPopup.close()
    }

    onClicked: calendarPopup.open()
    onValueChanged: syncCalendar()
    onEnabledChanged: {
        if (!enabled)
            calendarPopup.close()
    }
    Component.onCompleted: syncCalendar()

    contentItem: RowLayout {
        spacing: 8

        Text {
            Layout.fillWidth: true
            text: field.value
            color: field.enabled ? Theme.text : Theme.textMuted
            font.pixelSize: 12
            elide: Text.ElideRight
        }

        Item {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16

            Rectangle {
                x: 2
                y: 3
                width: 12
                height: 11
                radius: 2
                color: "transparent"
                border.width: 1
                border.color: field.enabled ? Theme.textMuted : Theme.borderStrong
            }
            Rectangle {
                x: 2
                y: 6
                width: 12
                height: 1
                color: field.enabled ? Theme.textMuted : Theme.borderStrong
            }
            Rectangle {
                x: 5
                y: 1
                width: 1
                height: 4
                color: field.enabled ? Theme.textMuted : Theme.borderStrong
            }
            Rectangle {
                x: 10
                y: 1
                width: 1
                height: 4
                color: field.enabled ? Theme.textMuted : Theme.borderStrong
            }
        }
    }

    background: Rectangle {
        color: field.down ? Theme.surfaceRaised : Theme.surface
        border.width: 1
        border.color: field.activeFocus || calendarPopup.opened
                      ? Theme.primary : Theme.borderStrong
        radius: 7
    }

    Popup {
        id: calendarPopup
        parent: Overlay.overlay
        width: 306
        padding: 12
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        x: {
            const point = field.mapToItem(parent, 0, field.height + 6)
            return Math.max(12, Math.min(point.x, parent.width - width - 12))
        }
        y: {
            const below = field.mapToItem(parent, 0, field.height + 6).y
            if (below + height <= parent.height - 12)
                return below
            const above = field.mapToItem(parent, 0, -height - 6).y
            return Math.max(12, above)
        }

        onAboutToShow: field.syncCalendar()

        background: Rectangle {
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.borderStrong
            radius: 10
        }

        contentItem: ColumnLayout {
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Button {
                    id: previousMonthButton
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    flat: true
                    text: "‹"
                    Accessible.name: qsTr("Предыдущий месяц")
                    onClicked: field.moveMonth(-1)
                    contentItem: Text {
                        text: previousMonthButton.text
                        color: Theme.text
                        font.pixelSize: 22
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: monthGrid.title
                    color: Theme.text
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                }

                Button {
                    id: nextMonthButton
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    flat: true
                    text: "›"
                    Accessible.name: qsTr("Следующий месяц")
                    onClicked: field.moveMonth(1)
                    contentItem: Text {
                        text: nextMonthButton.text
                        color: Theme.text
                        font.pixelSize: 22
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            DayOfWeekRow {
                id: weekDays
                Layout.fillWidth: true
                locale: Qt.locale("ru_RU")
                spacing: 4
                delegate: Text {
                    required property string shortName
                    width: (weekDays.availableWidth - weekDays.spacing * 6) / 7
                    height: 24
                    text: shortName
                    color: Theme.textMuted
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            MonthGrid {
                id: monthGrid
                Layout.fillWidth: true
                Layout.preferredHeight: 218
                month: field.visibleMonth
                year: field.visibleYear
                locale: Qt.locale("ru_RU")
                spacing: 4

                delegate: Rectangle {
                    required property var model
                    width: (monthGrid.availableWidth - monthGrid.spacing * 6) / 7
                    height: 32
                    radius: 6
                    opacity: model.month === monthGrid.month ? 1 : 0
                    color: field.sameDay(model.date, field.selectedDate)
                           ? Theme.primary : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: parent.model.day
                        color: field.sameDay(parent.model.date, field.selectedDate)
                               ? Theme.surface : Theme.text
                        font.pixelSize: 11
                    }
                }

                onReleased: function(date) {
                    if (date.getMonth() === field.visibleMonth)
                        field.chooseDate(date)
                }
            }
        }
    }
}
