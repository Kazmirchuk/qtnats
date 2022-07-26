/* Copyright(c) 2021-2022 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/

#pragma once

#include <QObject>
#include <QByteArray>
#include <QFuture>
#include <QUrl>
#include <QMultiHash>


// consider moving to PIMPL?
// https://wiki.qt.io/D-Pointer
// https://stackoverflow.com/questions/25250171/how-to-use-the-qts-pimpl-idiom
#include <nats.h>

// https://cmake.org/cmake/help/v3.0/module/GenerateExportHeader.html
namespace QtNats {

    Q_NAMESPACE

    using MessageHeaders = QMultiHash<QString, QString>;

    enum class ConnectionStatus
    {
        Disconnected = NATS_CONN_STATUS_DISCONNECTED,
        Connecting = NATS_CONN_STATUS_CONNECTING,
        Connected = NATS_CONN_STATUS_CONNECTED,
        Closed = NATS_CONN_STATUS_CLOSED,
        Reconnecting = NATS_CONN_STATUS_RECONNECTING,
        DrainingSubs = NATS_CONN_STATUS_DRAINING_SUBS,
        DrainingPubs = NATS_CONN_STATUS_DRAINING_PUBS
    };

    Q_ENUM_NS(ConnectionStatus)

    // need to throw it from QFuture; otherwise it would be derived from std::runtime_error
    // in fact, QException inherits from std::exception, although it's not documented
    class Exception : public QException
    {
    public:
        Exception(natsStatus s) : errorCode(s) {}

        void raise() const override { throw* this; }
        Exception* clone() const override { return new Exception(*this); }
        const char* what() const noexcept override { return natsStatus_GetText(errorCode); }

        const natsStatus errorCode = NATS_OK;
    };

    class JetStreamException : public Exception
    {
    public:
        JetStreamException(natsStatus s, jsErrCode js) : Exception(s), jsError(js) {}
        void raise() const override { throw* this; }
        JetStreamException* clone() const override { return new JetStreamException(*this); }

        const jsErrCode jsError = jsErrCode(0);
    };

    struct Options
    {
        QList<QUrl> servers;
        QString user;
        QString password;
        QString token;
        bool randomize = true; //NB! reverted option
        qint64 timeout;
        QString name;
        bool secure = false;
        bool verbose = false;
        bool pedantic = false;
        qint64 pingInterval;
        int maxPingsOut;
        int ioBufferSize;
        bool allowReconnect = true;
        int maxReconnect;
        qint64 reconnectWait;
        int reconnectBufferSize;
        int maxPendingMessages;
        bool echo = true; //NB! reverted option

        Options();
        ~Options();

    private:
        mutable natsOptions* o = nullptr;
        natsOptions* build() const;
        friend class Connection;
    };

    // this could be a simple struct, but in future I may need to optimize it to minimize conversions const char* -> QString etc
    // so keep getters and setters
    class Message
    {
    public:
        Message() {}
        Message(const QString& subject, const QByteArray& data) : m_subject(subject), m_data(data) {}
        QString subject() const { return m_subject; }
        QString reply() const { return m_reply; }
        QByteArray data() const { return m_data; }
        MessageHeaders headers() const { return m_headers; }

        void setSubject(const QString& s) { m_subject = s; }
        void setReply(const QString& r) { m_reply = r; }
        void setData(const QByteArray& d) { m_data = d; }
        void setHeaders(const MessageHeaders& h) { m_headers = h; }

    private:
        QString m_subject;
        QString m_reply;
        QByteArray m_data;
        // NB! 1. headers are case-sensitive
        // 2. cnats does NOT preserve the order of headers
        MessageHeaders m_headers;
    };

    class Subscription;
    class JetStream;
    
    struct JsOptions
    {
        // QString prefix = "$JS.API"; don't think it's a good idea to change this?
        QString domain;
        qint64 timeout = 5000;
    };


    class Connection : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY(Connection)
        
    public:
        explicit Connection(QObject* parent = nullptr);
        ~Connection() noexcept override;
        Connection(Connection&&) = delete;
        Connection& operator=(Connection&&) = delete;
        
        void connectToServer(const Options& opts);
        void connectToServer(const QUrl& address);
        void close() noexcept;
        
        void publish(const Message& msg);

        Message request(const Message& msg, qint64 timeout = 2000);
        QFuture<Message> asyncRequest(const Message& msg, qint64 timeout = 2000);

        Subscription* subscribe(const QString& subject);
        Subscription* subscribe(const QString& subject, const QString& queueGroup);

        bool ping(qint64 timeout = 10000); //ms
        
        QString currentServer() const;
        ConnectionStatus status() const;
        QString errorString() const;

        static QString newInbox();

        JetStream* jetStream(const JsOptions& options = JsOptions());

    signals:
        void errorOccurred(natsStatus error, const QString& text);
        void statusChanged(ConnectionStatus status);

    private:
        natsConnection* m_conn = nullptr;
    };
    
    class Subscription : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY(Subscription)

    public:
        ~Subscription() noexcept override;
        Subscription(Subscription&&) = delete;
        Subscription& operator=(Subscription&&) = delete;

    signals:
        void received(const Message& message);

    private:
        Subscription(QObject* parent) : QObject(parent) {}

        natsSubscription* m_sub = nullptr;
        friend class Connection;
    };

    // ---------------------------- JET STREAM -------------------------------

    struct JsPublishOptions
    {
        qint64 timeout = -1;
        QString msgID;
        QString expectStream;
        QString expectLastMessageID;
        quint64 expectLastSequence = 0;
        quint64 expectLastSubjectSequence = 0;
        bool expectNoMessage = false;
    };

    struct JsPublishAck
    {
        QString stream;
        quint64 sequence;
        QString domain;
        bool duplicate;
    };

    class JetStream : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY(JetStream)

    public:
        ~JetStream() noexcept override;
        JetStream(JetStream&&) = delete;
        JetStream& operator=(JetStream&&) = delete;

        JsPublishAck publish(const Message& msg, const JsPublishOptions& opts);
        JsPublishAck publish(const Message& msg, qint64 timeout = -1);

        void asyncPublish(const Message& msg, const JsPublishOptions& opts);
        void asyncPublish(const Message& msg, qint64 timeout = -1);
        void waitForPublishCompleted(qint64 timeout = -1);
        
    signals:
        void errorOccurred(natsStatus error, jsErrCode jsErr, const QString& text, const Message& msg);

    private:
        JetStream(QObject* parent) : QObject(parent) {}

        jsCtx* m_jsCtx = nullptr;
        
        JsPublishAck doPublish(const Message& msg, jsPubOptions* opts);
        void doAsyncPublish(const Message& msg, jsPubOptions* opts);

        friend class Connection;
    };

    
}

Q_DECLARE_METATYPE(QtNats::Message)
