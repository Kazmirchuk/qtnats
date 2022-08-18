/* Copyright(c) 2021-2022 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/

#pragma once

#include <memory>

#include <QObject>
// I've received the clarification that Latin-1 should be used everywhere for strings, so QByteArray is clearer API than QString
// https://github.com/nats-io/nats.c/issues/573
#include <QByteArray>
#include <QFuture>
#include <QUrl>
#include <QMultiHash>

#include <nats.h>

#include "qtnats_export.h"

namespace QtNats {

    QTNATS_EXPORT Q_NAMESPACE  //we need the "export" directive due to https://bugreports.qt.io/browse/QTBUG-68014

    using MessageHeaders = QMultiHash<QByteArray, QByteArray>;

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

        const natsStatus errorCode;
    };

    class JetStreamException : public Exception
    {
    public:
        JetStreamException(natsStatus s, jsErrCode js) : Exception(s), jsError(js), errorText(initText(js)) {}
        void raise() const override { throw* this; }
        JetStreamException* clone() const override { return new JetStreamException(*this); }
        const char* what() const noexcept override { return errorText.constData(); }

        const jsErrCode jsError;

    private:
        QByteArray initText(jsErrCode js) {
            return QString("%1: %2").arg(Exception::what()).arg(js).toLatin1();
        }
        const QByteArray errorText;
    };

    struct QTNATS_EXPORT Options
    {
        QList<QUrl> servers;
        QByteArray user;
        QByteArray password;
        QByteArray token;
        bool randomize = true; //NB! reverted option
        qint64 timeout;
        QByteArray name;
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
    };

    struct QTNATS_EXPORT Message
    {
        Message() {}
        Message(const QByteArray& in_subject, const QByteArray& in_data) : subject(in_subject), data(in_data) {}
        explicit Message(natsMsg* cmsg) noexcept;
        bool isIncoming() const { return bool(m_natsMsg); }

        // JetStream acknowledgments
        void ack();
        void nack(qint64 delay = -1); //ms
        void inProgress();
        void terminate();


        QByteArray subject;
        QByteArray reply;
        QByteArray data;
        // NB! 1. headers are case-sensitive
        // 2. cnats does NOT preserve the order of headers
        MessageHeaders headers;
        
    private:
        std::shared_ptr<natsMsg> m_natsMsg;
    };

    class Subscription;
    class JetStream;
    
    struct JsOptions
    {
        // QString prefix = "$JS.API"; don't think it's a good idea to change this?
        QByteArray domain;
        qint64 timeout = 5000;
    };


    class QTNATS_EXPORT Client : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY(Client)
        
    public:
        explicit Client(QObject* parent = nullptr);
        ~Client() noexcept override;
        Client(Client&&) = delete;
        Client& operator=(Client&&) = delete;
        
        void connectToServer(const Options& opts);
        void connectToServer(const QUrl& address);
        void close() noexcept;
        
        void publish(const Message& msg);

        Message request(const Message& msg, qint64 timeout = 2000);
        QFuture<Message> asyncRequest(const Message& msg, qint64 timeout = 2000);

        Subscription* subscribe(const QByteArray& subject);
        Subscription* subscribe(const QByteArray& subject, const QByteArray& queueGroup);

        bool ping(qint64 timeout = 10000) noexcept; //ms
        
        QUrl currentServer() const;
        ConnectionStatus status() const;
        QString errorString() const;

        static QByteArray newInbox();

        JetStream* jetStream(const JsOptions& options = JsOptions());

        natsConnection* getNatsConnection() const { return m_conn; }

    signals:
        void errorOccurred(natsStatus error, const QString& text);
        void statusChanged(ConnectionStatus status);

    private:
        natsConnection* m_conn = nullptr;
    };
    
    class QTNATS_EXPORT Subscription : public QObject
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
        friend class Client;
        friend class JetStream;
    };

    // ---------------------------- JET STREAM -------------------------------

    struct JsPublishOptions
    {
        qint64 timeout = -1;
        QByteArray msgID;
        QByteArray expectStream;
        QByteArray expectLastMessageID;
        quint64 expectLastSequence = 0;
        quint64 expectLastSubjectSequence = 0;
        bool expectNoMessage = false;
    };

    struct JsPublishAck
    {
        QByteArray stream;
        quint64 sequence;
        QByteArray domain;
        bool duplicate;
    };

    class QTNATS_EXPORT PullSubscription : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY(PullSubscription)

    public:
        ~PullSubscription() noexcept override;
        PullSubscription(PullSubscription&&) = delete;
        PullSubscription& operator=(PullSubscription&&) = delete;

        QList<Message> fetch(int batch = 1, qint64 timeout = 5000);

    private:
        PullSubscription(QObject* parent) : QObject(parent) {}

        natsSubscription* m_sub = nullptr;
        friend class JetStream;
    };

    class QTNATS_EXPORT JetStream : public QObject
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

        Subscription* subscribe(const QByteArray& subject, const QByteArray& stream, const QByteArray& consumer);
        PullSubscription* pullSubscribe(const QByteArray& subject, const QByteArray& stream, const QByteArray& consumer);

        jsCtx* getJsContext() const { return m_jsCtx; }
        
    signals:
        void errorOccurred(natsStatus error, jsErrCode jsErr, const QString& text, const Message& msg);

    private:
        JetStream(QObject* parent) : QObject(parent) {}

        jsCtx* m_jsCtx = nullptr;
        
        JsPublishAck doPublish(const Message& msg, jsPubOptions* opts);
        void doAsyncPublish(const Message& msg, jsPubOptions* opts);

        friend class Client;
    };

    
}

Q_DECLARE_METATYPE(QtNats::Message)
