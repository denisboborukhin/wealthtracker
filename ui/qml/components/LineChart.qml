import QtQuick

Canvas {
    id: chart
    required property var values
    property color lineColor: Theme.primary
    property color gridColor: Theme.border

    onValuesChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const context = getContext("2d")
        context.reset()
        if (!values || values.length < 2)
            return

        let minimum = values[0]
        let maximum = values[0]
        for (let i = 1; i < values.length; ++i) {
            minimum = Math.min(minimum, values[i])
            maximum = Math.max(maximum, values[i])
        }
        const range = Math.max(1, maximum - minimum)
        const padding = 8
        const step = (width - padding * 2) / (values.length - 1)

        context.beginPath()
        context.moveTo(padding, height - padding)
        context.lineTo(width - padding, height - padding)
        context.lineWidth = 1
        context.strokeStyle = gridColor
        context.stroke()

        context.beginPath()
        for (let i = 0; i < values.length; ++i) {
            const x = padding + i * step
            const y = height - padding - ((values[i] - minimum) / range) * (height - padding * 2)
            if (i === 0)
                context.moveTo(x, y)
            else
                context.lineTo(x, y)
        }
        context.lineWidth = 2
        context.strokeStyle = lineColor
        context.stroke()
    }
}
