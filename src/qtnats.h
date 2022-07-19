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

    enum class ConnectionStatus {
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

    struct Options {
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

    class Message {

    public:
        Message() {}
        Message(const QString& subject, const QByteArray& data): m_subject(subject), m_data(data) {}
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
        QMultiHash<QString, QString> m_headers;
    };

    class Connection : public QObject {
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
        QFuture<Message> asyncRequest(const QString& subject, const QByteArray& message, qint64 timeout = 2000);

        bool ping(qint64 timeout = 10000); //ms
        
        QString currentServer() const;
        ConnectionStatus status() const;
        QString errorString() const;

        static QString newInbox();

    signals:
        void errorOccurred(natsStatus error, const QString& text);
        void statusChanged(ConnectionStatus status);

    private:
        natsConnection* m_conn = nullptr;

        friend class Subscription;
        friend class JetStream;
    };
    
    class Subscription : public QObject {
        Q_OBJECT
        Q_DISABLE_COPY(Subscription)

    public:
        Subscription(Connection* connection, const QString& subject);
        Subscription(Connection* connection, const QString& subject, const QString& queueGroup);
        ~Subscription() override;
    signals:
        void received(const Message& message);

    private:
        natsSubscription* m_sub = nullptr;
    };

    // ---------------------------- JET STREAM -------------------------------

    class JetStream : public QObject {
        Q_OBJECT
        Q_DISABLE_COPY(JetStream)

    public:
        JetStream(Connection* natsConn);
        ~JetStream() noexcept override;
        JetStream(JetStream&&) = delete;
        JetStream& operator=(JetStream&&) = delete;

    private:
        jsCtx* m_jsCtx = nullptr;
    };
}

Q_DECLARE_METATYPE(QtNats::Message)
