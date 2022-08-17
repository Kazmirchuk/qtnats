# C++ API

```
#include <qtnats.h>
```
## Connection class

## Subscription class

## JetStream class
```cpp
JsPublishAck publish(const Message& msg, const JsPublishOptions& opts);
```

```cpp
JsPublishAck publish(const Message& msg, qint64 timeout = -1);
```
```cpp
void asyncPublish(const Message& msg, const JsPublishOptions& opts);
```
```cpp
void asyncPublish(const Message& msg, qint64 timeout = -1);
```
```cpp
void waitForPublishCompleted(qint64 timeout = -1);
```
```cpp
Subscription* subscribe(const QByteArray& subject, jsSubOptions* subOpts);
```
```cpp
PullSubscription* pullSubscribe(const QByteArray& subject, const QByteArray& durable, jsSubOptions* subOpts);
```
## PullSubscription class
```cpp
QList<Message> fetch(int batch = 1, qint64 timeout = 5000);
```
# QML API
