/* Copyright(c) 2021 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/

#include "qtnats.h"

#include <nats.h>
#include <vector>

#include <QThread>

using namespace QtNats;

Options::Options()
{
    natsOptions_Create(&o);
}
Options::~Options()
{
    natsOptions_Destroy(o);
}
Options& Options::servers(const QUrl& url)
{
    QByteArray ba = url.toEncoded();
    const char* servers[] = { ba.constData(), nullptr };
    natsOptions_SetServers(o, servers, 1);
}
Options& Options::servers(const QList<QUrl>& urls)
{
    QList<QByteArray> l;
    std::vector<const char*> v;
    for (auto url : urls) {
        l.append(url.toEncoded());
        v.push_back(l.last().constData());
    }
    natsOptions_SetServers(o, v.data(), v.size());
}
Options& Options::userInfo(const QString& user, const QString& password)
{
    natsOptions_SetUserInfo(o, qUtf8Printable(user), qUtf8Printable(password));
}
Options& Options::token(const QString& token)
{
    natsOptions_SetToken(o, qUtf8Printable(token));
}
Options& Options::randomize(bool on)
{
    natsOptions_SetNoRandomize(o, !on); //NB! reverted flag
}
Options& Options::timeout(qint64 ms)
{
    natsOptions_SetTimeout(o, ms);
}
Options& Options::name(const QString& name)
{
    natsOptions_SetName(o, qUtf8Printable(name));
}
Options& Options::secure(bool on)
{
    natsOptions_SetSecure(o, on);
}
Options& Options::verbose(bool on)
{
    natsOptions_SetVerbose(o, on);
}
Options& Options::pedantic(bool on)
{
    natsOptions_SetPedantic(o, on);
}
Options& Options::pingInterval(qint64 ms)
{
    natsOptions_SetPingInterval(o, ms);
}
Options& Options::maxPingsOut(int count)
{
    natsOptions_SetMaxPingsOut(o, count);
}
Options& Options::allowReconnect(bool on)
{
    natsOptions_SetAllowReconnect(o, on);
}
Options& Options::maxReconnect(int count)
{
    natsOptions_SetMaxReconnect(o, count);
}
Options& Options::reconnectWait(qint64 ms)
{
    natsOptions_SetReconnectWait(o, ms);
}
Options& Options::echo(bool on)
{
    natsOptions_SetNoEcho(o, !on);  //NB! reverted flag
}

// need to pass it through queued signal-slot connections
static const int messageTypeId = qRegisterMetaType<Message>();

Message::Message(natsMsg* msg)
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
    Subscription* sub = reinterpret_cast<Subscription*>(closure);
    
    Message m(msg);
    natsMsg_Destroy(msg);
    emit sub->received(m);
}

static void errorHandler(natsConnection* /*nc*/, natsSubscription* /*subscription*/, natsStatus err, void* closure) {
    Connection* c = reinterpret_cast<Connection*>(closure);
    emit c->errorOccurred(err, getErrorText(err));
}

static void closedConnectionHandler(natsConnection* /*nc*/, void *closure) {
    Connection* c = reinterpret_cast<Connection*>(closure);
    emit c->statusChanged(::Status::Closed);
}

static void reconnectedHandler(natsConnection* /*nc*/, void *closure) {
    Connection* c = reinterpret_cast<Connection*>(closure);
    emit c->statusChanged(Status::Connected);
}

static void disconnectedHandler(natsConnection* /*nc*/, void *closure) {
    ::Connection* c = reinterpret_cast<Connection*>(closure);
    emit c->statusChanged(Status::Reconnecting);
}

Connection::Connection(QObject* parent):
    QObject(parent)
{
    int cpuCoresCount = QThread::idealThreadCount(); //this function may fail, thus the check
    if (cpuCoresCount >= 2) {
        nats_SetMessageDeliveryPoolSize(cpuCoresCount);
    }
}

Connection::~Connection() {
    natsConnection_Destroy(nats_conn);
}

bool Connection::connectToServer(const QString& address)
{
    
    natsStatus s = NATS_OK;
    //natsOptions* functions almost don't validate their arguments, and mostly handle only NATS_NO_MEMORY
    //so no point in checking natsStatus everywhere
    

    
    
    natsOptions_SetErrorHandler(options, &errorHandler, this);
    natsOptions_SetClosedCB(options, &closedConnectionHandler, this);
    natsOptions_SetDisconnectedCB(options, &disconnectedHandler, this);
    natsOptions_SetReconnectedCB(options, &reconnectedHandler, this);
    //don't create a thread for each subscription, since we may have a lot of subscriptions
    //number of threads in the pool is set by nats_SetMessageDeliveryPoolSize above
    natsOptions_UseGlobalMessageDelivery(options, true);

    s = natsConnection_Connect(&nats_conn, options);
    
    if (s == NATS_OK)
        return true;
    else {
        m_lastError = getErrorText(s);
        return false;
    }
}

bool Connection::publish(const QString& subject, const QByteArray& message) {
    natsStatus s = natsConnection_Publish(nats_conn, qPrintable(subject), message.constData(), message.size());
    return (s == NATS_OK);
}

bool Connection::ping() {
    natsStatus s = natsConnection_FlushTimeout(nats_conn, 10000);
    return (s == NATS_OK);
}

QString Connection::currentServer() const {
    char buffer[500];
    natsStatus s = natsConnection_GetConnectedUrl(nats_conn, buffer, sizeof(buffer));
    if (s != NATS_OK) {
        return QString();
    }
    return QString::fromLatin1(buffer);
}

::Status Connection::status() const {
    return Status::Closed;
}

QString Connection::lastError() const {
    if (nats_conn) {
        const char** buffer = nullptr;
        natsConnection_GetLastError(nats_conn, buffer);
        return QString::fromLatin1(*buffer);
    }
    else {
        return m_lastError;
    }
}

Subscription::Subscription(Connection* connection, const QString& subject,  QObject* parent): QObject(parent)
{
    natsConnection_Subscribe(&nats_sub, connection->nats_conn, qUtf8Printable(subject), &subscriptionCallback, reinterpret_cast<void*>(this));
}

Subscription::Subscription(Connection* connection, const QString& subject, const QString& queueGroup,  QObject* parent): QObject(parent)
{
    natsConnection_QueueSubscribe(&nats_sub, connection->nats_conn, qUtf8Printable(subject), qUtf8Printable(queueGroup), &subscriptionCallback, reinterpret_cast<void*>(this));
}

Subscription::~Subscription()
{
    natsSubscription_Destroy(nats_sub);
}

bool Subscription::isValid() const 
{
    return natsSubscription_IsValid(nats_sub);
}

