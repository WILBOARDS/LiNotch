// Spike 0b: the opened panel at the size the design doc promises (420x340),
// with the sections it promises: header, now-playing, four meters, a five-tile
// control grid, and a scrolling notification list. Throwaway.
//
// This exists to answer one question - what does a real Qt Quick tree cost on
// a layer surface - so it is deliberately as close to the shipped layout as a
// spike gets: layouts, nine text items, a ListView with twelve rows, and one
// animation that runs under --animate.
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#ff0a0a0c"
    radius: 18

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: "Tuesday, 3 November"
                color: "#b3ffffff"
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                width: 22; height: 22; radius: 11
                color: "#1affffff"
                Text { anchors.centerIn: parent; text: "×"; color: "#cccccc"; font.pixelSize: 13 }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                width: 44; height: 44; radius: 8
                color: "#ff2b3a55"
                Text { anchors.centerIn: parent; text: "♪"; color: "#ffffff"; font.pixelSize: 18 }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: "Anemosyne"
                    color: "#f2ffffff"
                    font.pixelSize: 14
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: "Julianna Barwick — Nepenthe"
                    color: "#99ffffff"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 3
                    radius: 1
                    color: "#33ffffff"
                    Rectangle {
                        width: parent.width * 0.42
                        height: parent.height
                        radius: 1
                        color: "#ff4ad9a0"
                    }
                }
            }
            Row {
                spacing: 10
                Repeater {
                    model: 3
                    Text {
                        text: index === 1 ? "▶" : (index === 0 ? "⏮" : "⏭")
                        color: "#e6ffffff"
                        font.pixelSize: 15
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: ["BAT 87%", "CPU", "RAM", "NET"]
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text { text: modelData; color: "#8cffffff"; font.pixelSize: 10 }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 4
                        radius: 2
                        color: "#26ffffff"
                        Rectangle {
                            height: parent.height
                            radius: 2
                            color: index === 1 ? "#ffffc466" : "#ff5b9dff"
                            width: parent.width * (0.2 + 0.15 * (index + 1))
                            NumberAnimation on width {
                                running: spikeAnimate === 1
                                from: parent.width * 0.15
                                to: parent.width * 0.85
                                duration: 600 + index * 200
                                loops: Animation.Infinite
                                alwaysRunToEnd: true
                            }
                        }
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            columns: 5
            rowSpacing: 8
            columnSpacing: 8
            Repeater {
                model: ["🔊 42%", "☀ 70%", "☾ DND", "⏞ wi", "✦ bt"]
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 10
                    color: "#17ffffff"
                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        color: "#e6ffffff"
                        font.pixelSize: 11
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: "#0dffffff"
            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                spacing: 6
                model: 12
                delegate: RowLayout {
                    width: list.width
                    spacing: 6
                    Rectangle { width: 16; height: 16; radius: 4; color: "#33ffffff" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: "Build finished"
                            color: "#e6ffffff"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "spike0/layer-shell · 41 s elapsed · notification body text " + index
                            color: "#8cffffff"
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                    }
                }
            }
        }
    }
}
