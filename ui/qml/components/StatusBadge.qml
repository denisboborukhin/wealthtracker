import QtQuick

Rectangle {
    id: root
    required property string text
    property string tone: "neutral"

    implicitWidth: label.implicitWidth + 16
    implicitHeight: 26
    radius: 6
    color: tone === "positive" ? Theme.positiveMuted
         : tone === "warning" ? Theme.warningMuted
         : tone === "negative" ? Theme.negativeMuted
         : Theme.surfaceRaised

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.tone === "positive" ? Theme.positive
             : root.tone === "warning" ? Theme.warning
             : root.tone === "negative" ? Theme.negative
             : Theme.textMuted
        font.pixelSize: 11
        font.weight: Font.Medium
    }
}
