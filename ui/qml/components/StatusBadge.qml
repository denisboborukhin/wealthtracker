import QtQuick

Rectangle {
    id: root
    required property string text
    property string tone: "neutral"

    implicitWidth: label.implicitWidth + 16
    implicitHeight: Math.max(26, label.implicitHeight + 10)
    radius: 6
    color: tone === "positive" ? Theme.positiveMuted
         : tone === "warning" ? Theme.warningMuted
         : tone === "negative" ? Theme.negativeMuted
         : Theme.surfaceRaised

    Text {
        id: label
        anchors.centerIn: parent
        width: Math.max(0, root.width - 16)
        text: root.text
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        color: root.tone === "positive" ? Theme.positive
             : root.tone === "warning" ? Theme.warning
             : root.tone === "negative" ? Theme.negative
             : Theme.textMuted
        font.pixelSize: 11
        font.weight: Font.Medium
    }
}
