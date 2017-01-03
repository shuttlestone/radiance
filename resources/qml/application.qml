import QtQuick 2.3
import QtQuick.Layouts 1.2
import QtQuick.Controls 1.4
import radiance 1.0

ApplicationWindow {
    id: window;
    visible: true;

    menuBar: MenuBar {
        Menu {
            title: "File";
        }
    }

    statusBar: StatusBar {
        RowLayout {
            anchors.fill: parent;
            Label { text: "Live" }
        }
    }
    
    ColumnLayout {
        anchors.fill: parent;

        RowLayout {
            Layout.fillHeight: true;
            Rectangle {
                width: 300;
                Layout.fillHeight: true;
                color: "white";
                Label { text: "TODO: Waveform" }
            }
            Rectangle {
                width: 300;
                Layout.fillHeight: true;
                color: "grey";
                Label { text: "TODO: Spectrum" }
            }
            GroupBox {
                title: "Outputs";
                Layout.fillHeight: true;
                Layout.fillWidth: true;

                OutputManager {
                    id: outputManager
                    Component.onCompleted: { this.createBus("udp://127.0.0.1:1365"); }
                }
                ColumnLayout {
                    anchors.fill: parent;
                    ScrollView {
                        Layout.fillHeight: true;
                        Layout.fillWidth: true;
                        ListView {
                            model: outputManager.buses
                            delegate: RowLayout {
                                property var bus : outputManager.buses[index];
                                TextField {
                                    id: bus_textbox;
                                    text: bus.uri;
                                    onAccepted: { bus.uri = text }
                                }
                                Label { text: "Bus URI: " + bus.uri + "; State: " + bus.state}
                            }
                        }
                    }
                    Button {
                        text: "Add Output";
                        onClicked: { outputManager.createBus(""); }
                    }
                }
            }
        }

        UIEffectPanel {}
    }
}