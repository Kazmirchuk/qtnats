/* Copyright(c) 2021 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/

#include "qtnats.h"

#include <nats.h>

#include <QThread>

// need to pass it through queued signal-slot connections
static const int messageTypeId = qRegisterMetaType<QtNats::Message>();

QtNats::Message::Message(natsMsg* msg)
{
    m_data = QByteArray (natsMsg_GetData(msg), natsMsg_GetDataLength(msg));
    m_subject = QString::fromLatin1(natsMsg_GetSubject(msg));
    m_reply = QString::fromLatin1(natsMsg_GetReply(msg));
};


static QString getErrorText(natsStatus status) {
    if (status == NATS_OK)
        return QString();

    return QString::fromLatin1(natsStatus_GetText(status));
}

static void subscriptionCallback(natsConnection* /*nc*/, natsSubscription* /*sub*/, natsMsg* msg, void* closure) {
    QtNats::Subscription* sub = reinterpret_cast<QtNats::Subscription*>(closure);
    
    QtNats::Message m(msg);
    natsMsg_Destroy(msg);
    emit sub->received(m);
}

static void errorHandler(natsConnection* /*nc*/, natsSubscription* /*subscription*/, natsStatus err, void* closure) {
    QtNats::Connection* c = reinterpret_cast<QtNats::Connection*>(closure);
    emit c->errorOccurred(err, getErrorText(err));
}

static void closedConnectionHandler(natsConnection* /*nc*/, void *closure) {
    QtNats::Connection* c = reinterpret_cast<QtNats::Connection*>(closure);
    emit c->statusChanged(QtNats::Status::Closed);
}

static void reconnectedHandler(natsConnection* /*nc*/, void *closure) {
    QtNats::Connection* c = reinterpret_cast<QtNats::Connection*>(closure);
    emit c->statusChanged(QtNats::Status::Connected);
}

static void disconnectedHandler(natsConnection* /*nc*/, void *closure) {
    QtNats::Connection* c = reinterpret_cast<QtNats::Connection*>(closure);
    emit c->statusChanged(QtNats::Status::Reconnecting);
}

QtNats::Connection::Connection(QObject* parent):
    QObject(parent)
{
    int cpuCoresCount = QThread::idealThreadCount(); //this function may fail, thus the check
    if (cpuCoresCount >= 2) {
        nats_SetMessageDeliveryPoolSize(cpuCoresCount);
    }
}

QtNats::Connection::~Connection() {
    natsConnection_Destroy(nats_conn);
}

bool QtNats::Connection::connectToServer(const QString& address)
{
    natsOptions* options = nullptr;
    natsStatus s = NATS_OK;
    //natsOptions* functions almost don't validate their arguments, and mostly handle only NATS_NO_MEMORY
    //so no point in checking natsStatus everywhere
    natsOptions_Create(&options);

    natsOptions_SetURL(options, qUtf8Printable(address));
    natsOptions_SetName(options, "QtNats");
    natsOptions_SetErrorHandler(options, &errorHandler, this);
    natsOptions_SetClosedCB(options, &closedConnectionHandler, this);
    natsOptions_SetDisconnectedCB(options, &disconnectedHandler, this);
    natsOptions_SetReconnectedCB(options, &reconnectedHandler, this);
    //don't create a thread for each subscription, since we may have a lot of subscriptions
    //number of threads in the pool is set by nats_SetMessageDeliveryPoolSize above
    natsOptions_UseGlobalMessageDelivery(options, true);

    s = natsConnection_Connect(&nats_conn, options);
    natsOptions_Destroy(options);
    if (s == NATS_OK)
        return true;
    else {
        m_lastError = getErrorText(s);
        return false;
    }
}

bool QtNats::Connection::publish(const QString& subject, const QByteArray& message) {
    natsStatus s = natsConnection_Publish(nats_conn, qPrintable(subject), message.constData(), message.size());
    return (s == NATS_OK);
}

bool QtNats::Connection::ping() {
    natsStatus s = natsConnection_FlushTimeout(nats_conn, 10000);
    return (s == NATS_OK);
}

QString QtNats::Connection::currentServer() const {
    char buffer[500];
    natsStatus s = natsConnection_GetConnectedUrl(nats_conn, buffer, sizeof(buffer));
    if (s != NATS_OK) {
        return QString();
    }
    return QString::fromLatin1(buffer);
}

QtNats::Status QtNats::Connection::status() const {
    return Status::Closed;
}

QString QtNats::Connection::lastError() const {
    if (nats_conn) {
        const char** buffer = nullptr;
        natsConnection_GetLastError(nats_conn, buffer);
        return QString::fromLatin1(*buffer);
    }
    else {
        return m_lastError;
    }
}

QtNats::Subscription::Subscription(Connection* connection, const QString& subject,  QObject* parent): QObject(parent)
{
    natsConnection_Subscribe(&nats_sub, connection->nats_conn, qUtf8Printable(subject), &subscriptionCallback, reinterpret_cast<void*>(this));
}

QtNats::Subscription::Subscription(Connection* connection, const QString& subject, const QString& queueGroup,  QObject* parent): QObject(parent)
{
    natsConnection_QueueSubscribe(&nats_sub, connection->nats_conn, qUtf8Printable(subject), qUtf8Printable(queueGroup), &subscriptionCallback, reinterpret_cast<void*>(this));
}

QtNats::Subscription::~Subscription()
{
    natsSubscription_Destroy(nats_sub);
}

bool QtNats::Subscription::isValid() const 
{
    return natsSubscription_IsValid(nats_sub);
}

