/* Copyright(c) 2021 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/

#pragma once

#include <memory>
#include <atomic>
// #include <optional>

#include <QObject>
#include <QByteArray>
#include <QMutex>
// #include <QRegularExpression>
#include <QHash>
#include <QFuture>

typedef struct __natsConnection natsConnection;
typedef struct __natsSubscription natsSubscription;
typedef struct __natsMsg natsMsg;

namespace QtNats {

    enum class Status {
        Connected,
        Closed,
        Connecting,
        Reconnecting
    };

    class Message {

    public:
        Message() {}
        explicit Message(natsMsg* msg);
        Message(const Message& other) {
            m_subject = other.m_subject;
            m_reply = other.m_reply;
            m_data = other.m_data;
        }
        ~Message() {}
        QString subject() const { return m_subject; }
        QString reply() const { return m_reply; }
        QByteArray data() const { return m_data; }

    private:
        QString m_subject;
        QString m_reply;
        QByteArray m_data;
    };

    class Connection : public QObject {
        Q_OBJECT
        Q_DISABLE_COPY(Connection)
        
    public:
        Connection(QObject* parent = nullptr);
        ~Connection() override;
        bool connectToServer(const QString& address);
        bool publish(const QString& subject, const QByteArray& message);
        //QFuture request(const QString& subject, const QByteArray& message, qint64 timeout = 2000);
        bool ping();
        
        QString currentServer() const;
        Status status() const;
        QString lastError() const;

    signals:
        void errorOccurred(int error, const QString& text);
        void statusChanged(Status status);

    private:
        natsConnection* nats_conn { nullptr };
        QString m_lastError;

        friend class Subscription;
    };

    class Subscription : public QObject {
        Q_OBJECT
        Q_DISABLE_COPY(Subscription)

    public:
        Subscription(Connection* connection, const QString& subject, QObject* parent = nullptr);
        Subscription(Connection* connection, const QString& subject, const QString& queueGroup, QObject* parent = nullptr);
        ~Subscription() override;
        bool isValid() const;
    signals:
        void received(const Message& message);

    private:
        QString m_subject;
        natsSubscription* nats_sub { nullptr };
    };
}

Q_DECLARE_METATYPE(QtNats::Message)
