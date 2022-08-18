// based on https://doc.qt.io/qt-6/qtmqtt-quicksubscription-main-qml.html

import QtQuick 2.8
import QtQuick.Window 2.2
import QtQuick.Controls 2.1
import QtQuick.Layouts 1.1
import NATS 1.0

Window {
    visible: true
    width: 640
    height: 480
    title: qsTr("NATS QML Example")
    id: root

    property var natsSub: 0

    NatsClient {
        id: client
        serverUrl: urlField.text
    }

    ListModel {
        id: messageModel
    }

    function addMessage(payload)
    {
        messageModel.insert(0, {"payload" : payload})

        if (messageModel.count >= 100)
            messageModel.remove(99)
    }

    GridLayout {
        anchors.fill: parent
        anchors.margins: 10
        columns: 2

        Label {
            text: "URL:"
            enabled: client.status === "Disconnected"
        }

        TextField {
            id: urlField
            Layout.fillWidth: true
            text: "nats://localhost:4222"
            enabled: client.status === "Disconnected"
        }

        Button {
            id: connectButton
            Layout.columnSpan: 2
            Layout.fillWidth: true
            text: client.status === "Connected" ? "Disconnect" : "Connect"
            onClicked: {
                if (client.status === "Connected") {
                    client.disconnectFromServer()
                    messageModel.clear()
                    natsSub.destroy()
                    natsSub = 0
                } else {
                    client.connectToServer()
                }
            }
        }

        RowLayout {
            enabled: client.status === "Connected"
            Layout.columnSpan: 2
            Layout.fillWidth: true

            Label {
                text: "Topic:"
            }

            TextField {
                id: subField
                text: "foo"
                placeholderText: "<Subscription topic>"
                Layout.fillWidth: true
                enabled: natsSub === 0
            }

            Button {
                id: subButton
                text: "Subscribe"
                visible: natsSub === 0
                onClicked: {
                    if (subField.text.length === 0) {
                        console.log("No topic specified to subscribe to.")
                        return
                    }
                    natsSub = client.subscribe(subField.text)
                    natsSub.received.connect(addMessage)
                }
            }
        }

        ListView {
            id: messageView
            model: messageModel
            height: 300
            width: 200
            Layout.columnSpan: 2
            Layout.fillHeight: true
            Layout.fillWidth: true
            clip: true
            delegate: Rectangle {
                width: messageView.width
                height: 30
                color: index % 2 ? "#DDDDDD" : "#888888"
                radius: 5
                Text {
                    text: payload
                    anchors.centerIn: parent
                }
            }
        }

        Label {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            color: "#333333"
            text: "Status: " + client.status
        }
    }
}
