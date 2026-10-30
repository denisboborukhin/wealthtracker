pragma Singleton
import QtQuick

QtObject {
    readonly property color background: "#F5F6F7"
    readonly property color surface: "#FFFFFF"
    readonly property color surfaceRaised: "#F0F2F4"
    readonly property color border: "#E1E4E8"
    readonly property color borderStrong: "#CED3D9"
    readonly property color text: "#171A1F"
    readonly property color textMuted: "#68707C"
    readonly property color primary: "#3157D5"
    readonly property color primaryMuted: "#E9EEFF"
    readonly property color positive: "#18794E"
    readonly property color positiveMuted: "#E9F6EF"
    readonly property color negative: "#C43D45"
    readonly property color negativeMuted: "#FBEDEE"
    readonly property color warning: "#946200"
    readonly property color warningMuted: "#FFF5D8"
    readonly property int radius: 9
    readonly property int pageMargin: Qt.platform.os === "android" ? 16 : 32
}
