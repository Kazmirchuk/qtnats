# C++/Qt/QML client library for the NATS message broker

[![License Apache 2.0](https://img.shields.io/badge/License-Apache2-blue.svg)](https://www.apache.org/licenses/LICENSE-2.0)

Learn more about NATS [here](https://nats.io) and Qt [here](https://www.qt.io). This is a wrapper over the [official C NATS client - cnats](https://github.com/nats-io/cnats). The library provides Qt-friendly and idiomatic interface that greatly simplifies API, memory management and other aspects of use, compared to cnats. Error handling is streamlined with help of C++ exceptions.

Also it supports basic **JetStream** functionality: publishing messages with acknowledgment and subscribing to pull and push consumers.

In short, if your Qt application has more than one process, and they need to talk to each other, NATS is the simplest choice for a message broker.

Find the detailed API reference [here](API.md).

The library is under active development. Feedback is welcome.

# Building
Qt5 and Qt6 are supported. You will need [cmake](https://cmake.org) - at least version 3.16.

You can build the library as follows (assuming your build tree is somewhere under the root folder):
```
cmake -DBUILD_QMLNATS=ON -DCMAKE_BUILD_TYPE=Release -A x64 ..
# then build the library using your compiler as usual
```
The library is built as a shared DLL/SO. `cnats` is built as a static library.

Remember to specify an appropriate generator if it is not detected automatically. E.g. when using Visual Studio, running `vcvars64.bat` and `qtenv2.bat` beforehand should take care of this.

`cmake` options:
- BUILD_QMLNATS: build the QML plugin too; supported only for Qt6 (OFF by default)

cmake will automatically pull `cnats` from GitHub, before generating the project.

# Examples

## Core NATS
```cpp
#include <qtnats.h>
...
using namespace QtNats;

Client c;
// most of cnats connection options are supported: user/password/token, connection name, configuration of PING/PONG and reconnecting behaviour
c.connectToServer(QUrl("nats://localhost:4222"));
// to simplify memory management, the Client object is the subscription's parent
// you can also use smart pointers
Subscription* sub = c.subscribe("test_subject");
// messages are received through Qt signals. 
// The Message object can be sent via queued connections
connect(sub, &Subscription::received, [](const Message& message) {
    std::cout << "Received message from: " << message.subject.constData() << " Payload: " << message.data.constData() << std::endl;
    // you have access to message headers too
    // if it is a JetStream message, you can acknowledge it
});

// sync (blocking) request with a timeout = 1s:
Message response = c.request(Message("service_subject", "question"), 1000);
// async request returns a QFuture object
QFuture<Message> f = c.asyncRequest(Message("service_subject", "question"));
// the result can be obtained later with f.result()
```
## JetStream
```cpp
// start with creating a JetStream context. Client is set as the context's parent
JetStream* js = c.jetStream();
// publish a message synchronously:
JsPublishAck ack = js->publish(Message("test.1", "HI"));
// subscribe to a pull consumer and fetch a batch of 10 messages:
PullSubscription* sub = js->pullSubscribe("test.pull", "MY_STREAM", "PULL_CONSUMER");
QList<Message> msgList = sub->fetch(10);
// remember to acknowledge the messages, if required by the consumer's policy

// push-subscribe:
Subscription* sub = js->subscribe("test.push", "MY_STREAM", "PUSH_CONSUMER");
connect(sub, &Subscription::received, [](const Message& message) {
    // process the message
});
```
# QML

NatsClient component is available to QML and is used as a factory to create subscription objects:
```qml
import NATS 1.0

property var natsSub

NatsClient {
    id: client
    serverUrl: "nats://localhost:4222"
}

// when you're ready to subscribe:
natsSub = client.subscribe("test_subject")
// connect to a handler function
natsSub.received.connect(addMessage)

function addMessage(payload)
{
    //process the message
}
```

You can find a full example in `demo.qml`. You can run it as follows:

1. Build the QML plugin
2. Create a folder for the plugin called "NATS", and copy the plugin DLL/SO, generated qmldir and qmlnatsplugin.qmltypes files
3. Run it using the `qml` tool from Qt:
```
qml -I <folder one level above NATS folder> --verbose qml\demo.qml
```

# Running tests
The unit tests are written using the QtTest framework and expect [nats CLI](https://github.com/nats-io/natscli) and nats-server in your $PATH. You can run them with [ctest](https://cmake.org/cmake/help/latest/manual/ctest.1.html) as usual.

