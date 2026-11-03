// Spike 0b: the idle pill as Qt Quick renders it.
// Throwaway. Mirrors the design doc's Idle mode: 260x36, black, clock +
// hairline + equaliser + unread dot + battery. Text is real text, because the
// font and glyph caches are part of the footprint being measured.
//
// SHAPE CAVEAT: the design wants square top corners and ~18 px radius on the
// bottom two. Qt 6.4's QtQuick Rectangle has no per-corner radius properties
// (verified against /usr/lib/x86_64-linux-gnu/qt6/qml/QtQuick/plugins.qmltypes:
// no *Radius member besides `radius`), so that shape needs QtQuick.Shapes - a
// further runtime module, and a hand-written path, for a corner. Uniform
// radius here.
import QtQuick

Rectangle {
    id: root
    color: "#ff000000"
    radius: 18

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 9

        Text {
            text: "14:32"
            color: "#f2ffffff"
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Rectangle { width: 1; height: 14; color: "#2affffff" }

        // Four equaliser bars. With --animate they move, which is the only
        // thing in the idle design that costs a frame.
        Row {
            id: eq
            spacing: 2
            Repeater {
                model: 4
                Rectangle {
                    width: 3
                    height: 4 + (index * 2)
                    radius: 1
                    color: "#ff4ad9a0"
                    NumberAnimation on height {
                        running: spikeAnimate === 1
                        from: 4
                        to: 14
                        duration: 180 + index * 60
                        loops: Animation.Infinite
                        alwaysRunToEnd: true
                    }
                }
            }
        }

        Rectangle { width: 6; height: 6; radius: 3; color: "#ff5b9dff" }

        Text {
            text: "87%"
            color: "#d9ffffff"
            font.pixelSize: 13
        }
    }
}
