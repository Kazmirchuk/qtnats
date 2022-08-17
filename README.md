# C++/Qt/QML client library for the NATS message broker

[![License Apache 2.0](https://img.shields.io/badge/License-Apache2-blue.svg)](https://www.apache.org/licenses/LICENSE-2.0)

Learn more about NATS [here](https://nats.io) and Qt [here](https://www.qt.io). This is a wrapper over the [official C NATS client](https://github.com/nats-io/cnats). The library provides Qt-friendly interface that greatly simplifies memory management and other aspects of use, compared to cnats.

Also it supports basic *JetStream* functionality: publishing messages with acknowledgment and subscribing to pull and push consumers.

# Building
Qt5 and Qt6 are supported. You will need [cmake](https://cmake.org) - at least version 3.14.

You can build the library as follows (assuming you build tree is somewhere under the root folder):
```
cmake -DUSE_QT6=ON -DBUILD_QMLNATS=ON -DCMAKE_BUILD_TYPE=Release -A x64 ..
```
Remember to specify an appropriate generator if it is not detected automatically. Running vcvars64.bat and qtenv2.bat beforehand should take care of this.

cmake options:
- USE_QT6 (Qt5 is the default choice)
- BUILD_QMLNATS: build the QML plugin too; supported only for Qt6 (OFF by default)

cmake will automatically pull cnats from GitHub, before generating project QtNats.

# Examples

## Core NATS
```cpp
#include <qtnats.h>
...
using namespace QtNats;

Connection c;
// most of cnats connections options are supported too: user/password/token, connection name, configuration of PING/PONG and reconnecting behaviour
c.connectToServer(QUrl("nats://localhost:4222"));
Subscription* sub = c.subscribe("test_subject");
// messages are received through Qt signals
connect(sub, &Subscription::received, [](const Message& message) {
    std::cout << "Received message from: " << message.subject.constData() << " Payload: " << message.data.constData() << std::endl;
    // you have access to message headers too
    // if it is a JetStream message, you can acknowledge it
});

// sync (blocking) request with a timeout = 1s:
Message response = c.request(Message("service", "question"), 1000);
// async request returns a QFuture object
QFuture<Message> f = c.asyncRequest(Message("service", "question"));
// the result can be obtained later with f.result()
```
## JetStream
```cpp
// start with creating a JetStream context:
JetStream* js = c.jetStream();
// publish a message synchronously:
JsPublishAck ack = js->publish(Message("test.1", "HI"));
// subscribe to a pull consumer and fetch a batch of 10 messages:
jsSubOptions subOpts;
jsSubOptions_Init(&subOpts);
subOpts.Stream = "MY_STREAM";
subOpts.Consumer = "PULL_CONSUMER";

PullSubscription* sub = js->pullSubscribe("test.pull", "PULL_CONSUMER", &subOpts);
QList<Message> msgList = sub->fetch(10);
```
# Running tests
The unit tests are written using QtTest framework and expect [nats CLI](https://github.com/nats-io/natscli) and nats-server in your $PATH. You can run them with [ctest](https://cmake.org/cmake/help/latest/manual/ctest.1.html) as usual.

Work in progress!

