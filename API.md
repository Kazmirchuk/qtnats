# C++ API

```
#include <qtnats.h>
```
All definitions are contained in the `QtNats` namespace.
All subjects, queue groups, stream and consumer names can use only Latin-1, so `QByteArray` is used for arguments instead of `QString`.
## Client Class
Represents a connection to a NATS server/cluster. All timeout values are in milliseconds.

Inherits: `QObject`

### Public Functions
```cpp
explicit Client(QObject* parent = nullptr);
void connectToServer(const Options& opts);
void connectToServer(const QUrl& address);
void close() noexcept;
void publish(const Message& msg);
Message request(const Message& msg, qint64 timeout = 2000);
QFuture<Message> asyncRequest(const Message& msg, qint64 timeout = 2000);
Subscription* subscribe(const QByteArray& subject);
Subscription* subscribe(const QByteArray& subject, const QByteArray& queueGroup);
bool ping(qint64 timeout = 10000) noexcept;
QUrl currentServer() const;
ConnectionStatus status() const;
QString errorString() const;
static QByteArray newInbox();
JetStream* jetStream(const JsOptions& options = JsOptions());
natsConnection* getNatsConnection() const;
```

### Signals
```cpp
void errorOccurred(natsStatus error, const QString& text);
void statusChanged(ConnectionStatus status);
```

## Subscription Class
Represents a NATS subscription. Do not create the object yourself - use the Client's factory function `subscribe`.

Inherits: `QObject`

### Signals
```cpp
void received(const Message& message);
```
## Options Struct
A simple autocompletion-friendly wrapper over [cnats](http://nats-io.github.io/nats.c/group__opts_group.html) connection options.
## Message Struct
Represents a NATS message.
### Public Functions
```cpp
Message() {}
Message(const QByteArray& in_subject, const QByteArray& in_data);
explicit Message(natsMsg* cmsg) noexcept;
bool isIncoming() const;
void ack();
void nack(qint64 delay = -1);
void inProgress();
void terminate();
```
### Public Members
```cpp
QByteArray subject;
QByteArray reply;
QByteArray data;
MessageHeaders headers; //QMultiHash<QByteArray, QByteArray>
```

## JetStream Class
Represents a JetStream context. Created by `Client`.
### Public Functions
```cpp
JsPublishAck publish(const Message& msg, const JsPublishOptions& opts);
JsPublishAck publish(const Message& msg, qint64 timeout = -1);
void asyncPublish(const Message& msg, const JsPublishOptions& opts);
void asyncPublish(const Message& msg, qint64 timeout = -1);
void waitForPublishCompleted(qint64 timeout = -1);
Subscription* subscribe(const QByteArray& subject, const QByteArray& stream, const QByteArray& push_consumer);
PullSubscription* pullSubscribe(const QByteArray& subject, const QByteArray& stream, const QByteArray& pull_consumer);
jsCtx* getJsContext() const;
```
### Signals
```cpp
void errorOccurred(natsStatus error, jsErrCode jsErr, const QString& text, const Message& msg);
```
## PullSubscription class
```cpp
QList<Message> fetch(int batch = 1, qint64 timeout = 5000);
```
## JsPublishOptions Struct
Options to publish a message to JetStream.
### Public Members
```cpp
qint64 timeout
QByteArray msgID
QByteArray expectStream
QByteArray expectLastMessageID
quint64 expectLastSequence
quint64 expectLastSubjectSequence
bool expectNoMessage
```
## JsPublishAck Struct
JetStream acknowledgment.
### Public Members
```cpp
QByteArray stream
quint64 sequence
QByteArray domain
bool duplicate
```

# Error reporting
All synchronous errors are reported with exceptions.
Asynchronous errors are reported with signals.
## Exception Class
Inherits: `QException`

Thrown by core NATS functions.
```cpp
const natsStatus errorCode;
```
The error code reported by [cnats](http://nats-io.github.io/nats.c/status_8h.html).

```cpp
const char* what() const noexcept override
```
Returns human-readable description of the error.
## JetStreamException Class
Inherits: `Exception`

Thrown in case of JetStream-specific failures. Extends the Exception class with `const jsErrCode jsError` field.


# QML API
## NatsClient QML Type
### Properties
```qml
serverUrl: string
status: string (read-only)
```
### Methods
```qml
connectToServer()
disconnectFromServer()
subscription subscribe(string subject)
publish(string subject, string message)
string request(string subject, string message)
```
The `subscription` object has only `received(string payload)` signal.
