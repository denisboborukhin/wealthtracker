import QtQuick

Canvas {
    id: chart
    required property var categoryModel

    onCategoryModelChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    Connections {
        target: chart.categoryModel
        function onModelReset() { chart.requestPaint() }
    }

    onPaint: {
        const context = getContext("2d")
        context.reset()
        const size = Math.min(width, height)
        const centerX = width / 2
        const centerY = height / 2
        const radius = size * 0.38
        const thickness = size * 0.13
        let start = -Math.PI / 2

        for (let row = 0; row < categoryModel.rowCount(); ++row) {
            const index = categoryModel.index(row, 0)
            const share = categoryModel.data(index, 259)
            const color = categoryModel.data(index, 260)
            const end = start + Math.PI * 2 * share
            context.beginPath()
            context.arc(centerX, centerY, radius, start, end)
            context.lineWidth = thickness
            context.strokeStyle = color
            context.stroke()
            start = end
        }
    }
}
