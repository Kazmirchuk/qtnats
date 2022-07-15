/* Copyright(c) 2021-2022 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/

#pragma once

#include <QObject>
#include <QByteArray>
#include <QFuture>
#include <QUrl>


// consider moving to PIMPL?
// https://wiki.qt.io/D-Pointer
// https://stackoverflow.com/questions/25250171/how-to-use-the-qts-pimpl-idiom
#include <nats.h>

// https://cmake.org/cmake/help/v3.0/module/GenerateExportHeader.html
namespace QtNats {

    Q_NAMESPACE

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

    class Options {
        natsOptions* o { nullptr };
        friend class Connection;

    public:
        Options();
        ~Options();
        Options(const Options& other) = delete;//remember to call build()!
        Options(natsOptions* opts);

        Options& servers(const QUrl& url);
        Options& servers(const QList<QUrl>& urls);
        Options& userInfo(const QString& user, const QString& password);
        Options& token(const QString& token);
        Options& randomize(bool on);
        Options& timeout(qint64 ms);
        Options& name(const QString& name);
        Options& secure(bool on);
        Options& verbose(bool on);
        Options& pedantic(bool on);
        Options& pingInterval(qint64 ms);
        Options& maxPingsOut(int count);
        Options& allowReconnect(bool on);
        Options& maxReconnect(int count);
        Options& reconnectWait(qint64 ms);
        Options& echo(bool on);

        natsOptions* build();
            
    };

    class Message {

    public:
        Message() {}
        explicit Message(natsMsg* msg);
        QString subject() const { return m_subject; }
        QString reply() const { return m_reply; }
        QByteArray data() const { return m_data; }

    private:
        QString m_subject;
        QString m_reply;
        QByteArray m_data;
        //todo headers
    };

    class Connection : public QObject {
        Q_OBJECT
        Q_DISABLE_COPY(Connection)
        
    public:
        explicit Connection(QObject* parent = nullptr);
        ~Connection() noexcept override;
        Connection(Connection&&) = delete;
        Connection& operator=(Connection&&) = delete;
        
        void connectToServer(const Options& address);
        void close() noexcept;
        
        void publish(const QString& subject, const QByteArray& message);

        Message request(const QString& subject, const QByteArray& message, qint64 timeout = 2000);
        QFuture<Message> asyncRequest(const QString& subject, const QByteArray& message, qint64 timeout = 2000);

        bool ping(qint64 timeout = 10000); //ms
        
        QString currentServer() const;
        ConnectionStatus status() const;
        QString errorString() const;

    signals:
        void errorOccurred(natsStatus error, const QString& text);
        void statusChanged(ConnectionStatus status);

    private:
        natsConnection* m_conn { nullptr };

        friend class Subscription;
    };
    
    class Subscription : public QObject {
        Q_OBJECT
        Q_DISABLE_COPY(Subscription)

    public:
        Subscription(Connection* connection, const QString& subject, QObject* parent = nullptr);
        Subscription(Connection* connection, const QString& subject, const QString& queueGroup, QObject* parent = nullptr);
        ~Subscription() override;
    signals:
        void received(const Message& message);

    private:
        natsSubscription* m_sub { nullptr };
    };
}

Q_DECLARE_METATYPE(QtNats::Message)
