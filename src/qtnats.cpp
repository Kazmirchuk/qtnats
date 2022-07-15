/* Copyright(c) 2021-2022 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/

#include "qtnats.h"

#include <QThread>
#include <QFutureInterface>

using namespace QtNats;

static QString getNatsErrorText(natsStatus status) {
    if (status == NATS_OK)
        return QString();

    return QString::fromLatin1(natsStatus_GetText(status));
}

static void checkError(natsStatus s)
{
    if (s == NATS_OK) return;
    throw Exception(s);
 }

Options::Options()
{
    natsOptions_Create(&o);
}
Options::~Options()
{
    natsOptions_Destroy(o);
}
Options::Options(natsOptions* opts) : o(opts)
{
}
natsOptions* Options::build()
{
    natsOptions* res = o;
    o = nullptr;
    return res;
}
Options& Options::servers(const QUrl& url)
{
    QList<QUrl> l({ url });
    return servers(l);
}
Options& Options::servers(const QList<QUrl>& urls)
{
    QList<QByteArray> l;
    QVector<const char*> ptrs;

    for (auto url : urls) {
        // TODO check for invalid URL
        l.append(url.toEncoded());
        ptrs.append(l.last().constData());
    }
    checkError(natsOptions_SetServers(o, ptrs.data(), static_cast<int>(ptrs.size())));
    return *this;
}
Options& Options::userInfo(const QString& user, const QString& password)
{
    checkError(natsOptions_SetUserInfo(o, qUtf8Printable(user), qUtf8Printable(password)));
    return *this;
}
Options& Options::token(const QString& token)
{
    //s = nats_setError(NATS_ILLEGAL_STATE, "%s", "Cannot set a token if a token handler has already been set");
    checkError(natsOptions_SetToken(o, qPrintable(token)));
    return *this;
}
Options& Options::randomize(bool on)
{
    natsOptions_SetNoRandomize(o, !on); //NB! reverted flag
    return *this;
}
Options& Options::timeout(qint64 ms)
{
    natsOptions_SetTimeout(o, ms);
    return *this;
}
Options& Options::name(const QString& name)
{
    natsOptions_SetName(o, qUtf8Printable(name));
    return *this;
}
/*
* postpone until I have SSL
Options& Options::secure(bool on)
{
    natsOptions_SetSecure(o, on);
    return *this;
}
*/
Options& Options::verbose(bool on)
{
    natsOptions_SetVerbose(o, on);
    return *this;
}
Options& Options::pedantic(bool on)
{
    natsOptions_SetPedantic(o, on);
    return *this;
}
Options& Options::pingInterval(qint64 ms)
{
    natsOptions_SetPingInterval(o, ms);
    return *this;
}
Options& Options::maxPingsOut(int count)
{
    natsOptions_SetMaxPingsOut(o, count);
    return *this;
}
Options& Options::allowReconnect(bool on)
{
    natsOptions_SetAllowReconnect(o, on);
    return *this;
}
Options& Options::maxReconnect(int count)
{
    natsOptions_SetMaxReconnect(o, count);
    return *this;
}
Options& Options::reconnectWait(qint64 ms)
{
    checkError(natsOptions_SetReconnectWait(o, ms));
    return *this;
}
Options& Options::echo(bool on)
{
    natsOptions_SetNoEcho(o, !on);  //NB! reverted flag
    return *this;
}

// need to pass it through queued signal-slot connections
static const int messageTypeId = qRegisterMetaType<Message>();

Message::Message(natsMsg* msg)
{
    m_data = QByteArray (natsMsg_GetData(msg), natsMsg_GetDataLength(msg));
    m_subject = QString::fromLatin1(natsMsg_GetSubject(msg));
    m_reply = QString::fromLatin1(natsMsg_GetReply(msg));
};

static void subscriptionCallback(natsConnection* /*nc*/, natsSubscription* /*sub*/, natsMsg* msg, void* closure) {
    Subscription* sub = reinterpret_cast<Subscription*>(closure);
    
    Message m(msg);
    natsMsg_Destroy(msg);
    emit sub->received(m);
}

static void asyncRequestCallback(natsConnection* /*nc*/, natsSubscription* natsSub, natsMsg* msg, void* closure) {
    auto future_iface = reinterpret_cast<QFutureInterface<Message>*>(closure);

    if (msg) {
        if (natsMsg_IsNoResponders(msg)) {
            future_iface->reportException(Exception(NATS_NO_RESPONDERS));
        }
        else {
            Message m(msg);
            natsMsg_Destroy(msg);
            future_iface->reportResult(m);
        }
    }
    else {
        future_iface->reportException(Exception(NATS_TIMEOUT));
    }
    future_iface->reportFinished();
    delete future_iface;
    natsSubscription_Destroy(natsSub);
}

static void errorHandler(natsConnection* /*nc*/, natsSubscription* /*subscription*/, natsStatus err, void* closure) {
    Connection* c = reinterpret_cast<Connection*>(closure);
    emit c->errorOccurred(err, getNatsErrorText(err));
}

static void closedConnectionHandler(natsConnection* /*nc*/, void *closure) {
    Connection* c = reinterpret_cast<Connection*>(closure);
    emit c->statusChanged(ConnectionStatus::Closed);
}

static void reconnectedHandler(natsConnection* /*nc*/, void *closure) {
    Connection* c = reinterpret_cast<Connection*>(closure);
    emit c->statusChanged(ConnectionStatus::Connected);
}

static void disconnectedHandler(natsConnection* /*nc*/, void *closure) {
    Connection* c = reinterpret_cast<Connection*>(closure);
    emit c->statusChanged(ConnectionStatus::Reconnecting);
}

Connection::Connection(QObject* parent):
    QObject(parent)
{
    int cpuCoresCount = QThread::idealThreadCount(); //this function may fail, thus the check
    if (cpuCoresCount >= 2) {
        nats_SetMessageDeliveryPoolSize(cpuCoresCount);
    }
}

Connection::~Connection()
{
    close();
}

void Connection::connectToServer(const Options& opts)
{
    //don't create a thread for each subscription, since we may have a lot of subscriptions
    //number of threads in the pool is set by nats_SetMessageDeliveryPoolSize above
    natsOptions* nats_opts = opts.o;
    natsOptions_UseGlobalMessageDelivery(nats_opts, true);

    natsOptions_SetErrorHandler(nats_opts, &errorHandler, this);
    natsOptions_SetClosedCB(nats_opts, &closedConnectionHandler, this);
    natsOptions_SetDisconnectedCB(nats_opts, &disconnectedHandler, this);
    natsOptions_SetReconnectedCB(nats_opts, &reconnectedHandler, this);

    checkError(natsConnection_Connect(&m_conn, nats_opts));
}

void Connection::close() noexcept
{
    if (!m_conn) {
        return;
    }
    natsConnection_Close(m_conn);
    //TODO sync thread with closedConnectionHandler otherwise I get a crash when trying to emit c->statusChanged(ConnectionStatus::Closed);
    QThread::msleep(200);
    natsConnection_Destroy(m_conn);
    m_conn = nullptr;
}

void Connection::publish(const QString& subject, const QByteArray& message) {
    checkError(natsConnection_Publish(m_conn, qPrintable(subject), message.constData(), message.size()));
}

Message Connection::request(const QString& subject, const QByteArray& message, qint64 timeout)
{
    natsMsg* replyMsg;
    checkError(natsConnection_Request(&replyMsg, m_conn, qPrintable(subject), message.constData(), message.size(), timeout));
    return Message(replyMsg);
}

QFuture<Message> Connection::asyncRequest(const QString& subject, const QByteArray& message, qint64 timeout)
{
    // QFutureInterface is undocumented; Qt6 provides QPromise instead
    // based on https://stackoverflow.com/questions/59197694/qt-how-to-create-a-qfuture-from-a-thread
    auto future_iface = std::make_unique<QFutureInterface<Message>>();
    natsInbox* inbox = nullptr;
    natsInbox_Create(&inbox);
    auto inboxPtr = std::unique_ptr<natsInbox, decltype(&natsInbox_Destroy)>(inbox, natsInbox_Destroy);

    natsSubscription* subscription = nullptr;
    // cnats will copy natsInbox, so we can delete it in the end of this function
    checkError(natsConnection_SubscribeTimeout(&subscription, m_conn, inbox, timeout, &asyncRequestCallback, future_iface.get()));
    checkError(natsSubscription_AutoUnsubscribe(subscription, 1));
    checkError(natsConnection_PublishRequest(m_conn, qPrintable(subject), inbox, message.constData(), message.size()));
    future_iface->reportStarted();
    auto f = future_iface->future();
    future_iface.release(); //will be deleted in asyncRequestCallback
    return f;
}

bool Connection::ping(qint64 timeout) {
    natsStatus s = natsConnection_FlushTimeout(m_conn, timeout);
    return (s == NATS_OK);
}

QString Connection::currentServer() const {
    char buffer[500];
    natsStatus s = natsConnection_GetConnectedUrl(m_conn, buffer, sizeof(buffer));
    if (s != NATS_OK) {
        return QString();
    }
    return QString::fromLatin1(buffer);
}

ConnectionStatus Connection::status() const {
    return ConnectionStatus(natsConnection_Status(m_conn));
}

QString Connection::errorString() const
{
    // TODO handle when m_conn==nullptr ?
    const char* buffer = nullptr;
    natsConnection_GetLastError(m_conn, &buffer);
    return QString::fromLatin1(buffer);
}

Subscription::Subscription(Connection* connection, const QString& subject,  QObject* parent): QObject(parent)
{
    checkError(natsConnection_Subscribe(&m_sub, connection->m_conn, qPrintable(subject), &subscriptionCallback, this));
}

Subscription::Subscription(Connection* connection, const QString& subject, const QString& queueGroup,  QObject* parent): QObject(parent)
{
    checkError(natsConnection_QueueSubscribe(&m_sub, connection->m_conn, qPrintable(subject), qPrintable(queueGroup), &subscriptionCallback, this));
}

Subscription::~Subscription()
{
    natsSubscription_Destroy(m_sub);
}
