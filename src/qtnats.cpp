/* Copyright(c) 2021-2022 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/

#include "qtnats.h"

#include <opts.h>

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

    // don't want to include opts.h in qtnats.h
    timeout = NATS_OPTS_DEFAULT_TIMEOUT;
    pingInterval = NATS_OPTS_DEFAULT_PING_INTERVAL;
    maxPingsOut = NATS_OPTS_DEFAULT_MAX_PING_OUT;
    ioBufferSize = NATS_OPTS_DEFAULT_IO_BUF_SIZE;
    maxReconnect = NATS_OPTS_DEFAULT_MAX_RECONNECT;
    reconnectWait = NATS_OPTS_DEFAULT_RECONNECT_WAIT;
    reconnectBufferSize = NATS_OPTS_DEFAULT_RECONNECT_BUF_SIZE;
    maxPendingMessages = NATS_OPTS_DEFAULT_MAX_PENDING_MSGS;
}
Options::~Options()
{
    natsOptions_Destroy(o);
}
natsOptions* Options::build() const
{
    if (servers.size()) {
        QList<QByteArray> l;
        QVector<const char*> ptrs;
        for (auto url : servers) {
            // TODO check for invalid URL
            l.append(url.toEncoded());
            ptrs.append(l.last().constData());
        }
        checkError(natsOptions_SetServers(o, ptrs.data(), static_cast<int>(ptrs.size())));
    }
    checkError(natsOptions_SetUserInfo(o, qUtf8Printable(user), qUtf8Printable(password)));
    checkError(natsOptions_SetToken(o, qPrintable(token)));
    checkError(natsOptions_SetNoRandomize(o, !randomize)); //NB! reverted flag
    checkError(natsOptions_SetTimeout(o, timeout));
    checkError(natsOptions_SetName(o, qUtf8Printable(name)));
    //postpone until I have SSL
    // natsOptions_SetSecure(o, secure);
    checkError(natsOptions_SetVerbose(o, verbose));
    checkError(natsOptions_SetPedantic(o, pedantic));
    checkError(natsOptions_SetPingInterval(o, pingInterval));
    checkError(natsOptions_SetMaxPingsOut(o, maxPingsOut));
    checkError(natsOptions_SetAllowReconnect(o, allowReconnect));
    checkError(natsOptions_SetMaxReconnect(o, maxReconnect));
    checkError(natsOptions_SetReconnectWait(o, reconnectWait));
    checkError(natsOptions_SetReconnectBufSize(o, reconnectBufferSize));
    checkError(natsOptions_SetMaxPendingMsgs(o, maxPendingMessages));
    checkError(natsOptions_SetNoEcho(o, !echo));  //NB! reverted flag

    return o;
}

// need to pass it through queued signal-slot connections
static const int messageTypeId = qRegisterMetaType<Message>();

static Message fromNatsMsg(natsMsg* msg) noexcept
{
    QString subj = QString::fromLatin1(natsMsg_GetSubject(msg));
    QByteArray data = QByteArray(natsMsg_GetData(msg), natsMsg_GetDataLength(msg));
    Message m (subj, data);
    m.setReply(QString::fromLatin1(natsMsg_GetReply(msg)));

    const char** keys = nullptr;
    int keyCount = 0;

    natsStatus s = natsMsgHeader_Keys(msg, &keys, &keyCount);
    if (s != NATS_OK || keyCount == 0)
        return m;
    
    // handle message headers
    MessageHeaders hdrs;
    const char** values = nullptr;
    int valueCount = 0;
    for (int i = 0; i < keyCount; i++) {
        s = natsMsgHeader_Values(msg, keys[i], &values, &valueCount);
        if (s != NATS_OK)
            continue;
        QString key = QString::fromLatin1(keys[i]);
        // I guess, values can be UTF-8?
        for (int j = 0; j < valueCount; j++) {
            QString value = QString::fromUtf8(values[j]);
            hdrs.insert(key, value);
        }
        free(values);
    }

    m.setHeaders(hdrs);
    free(keys);

    return m;
};

using NatsMsgPtr = std::unique_ptr<natsMsg, decltype(&natsMsg_Destroy)>;

static NatsMsgPtr toNatsMsg(const Message& msg)
{
    natsMsg* cnatsMsg;
    QByteArray data = msg.data();
    checkError(natsMsg_Create(&cnatsMsg,
        qPrintable(msg.subject()),
        qPrintable(msg.reply()),
        data.constData(),
        data.size()
    ));
    
    auto msgPtr = NatsMsgPtr(cnatsMsg, natsMsg_Destroy);

    MessageHeaders h = msg.headers();
    auto i = h.constBegin();
    while (i != h.constEnd()) {
        checkError(natsMsgHeader_Add(cnatsMsg, qPrintable(i.key()), qPrintable(i.value())));
    }
    return msgPtr;
}

static void subscriptionCallback(natsConnection* /*nc*/, natsSubscription* /*sub*/, natsMsg* msg, void* closure) {
    Subscription* sub = reinterpret_cast<Subscription*>(closure);
    
    Message m(fromNatsMsg(msg));
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
            Message m(fromNatsMsg(msg));
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
    natsOptions* nats_opts = opts.build();

    natsOptions_UseGlobalMessageDelivery(nats_opts, true);

    natsOptions_SetErrorHandler(nats_opts, &errorHandler, this);
    natsOptions_SetClosedCB(nats_opts, &closedConnectionHandler, this);
    natsOptions_SetDisconnectedCB(nats_opts, &disconnectedHandler, this);
    natsOptions_SetReconnectedCB(nats_opts, &reconnectedHandler, this);

    checkError(natsConnection_Connect(&m_conn, nats_opts));
}

void Connection::connectToServer(const QUrl& address)
{
    Options connOpts;
    connOpts.servers += address;
    connectToServer(connOpts);
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

void Connection::publish(const Message& msg) {
    NatsMsgPtr p = toNatsMsg(msg);
    checkError(natsConnection_PublishMsg(m_conn, p.get()));
}

Message Connection::request(const Message& msg, qint64 timeout)
{
    natsMsg* replyMsg;
    NatsMsgPtr p = toNatsMsg(msg);
    checkError(natsConnection_RequestMsg(&replyMsg, m_conn, p.get(), timeout));
    return fromNatsMsg(replyMsg);
}

QFuture<Message> Connection::asyncRequest(const QString& subject, const QByteArray& message, qint64 timeout)
{
    // QFutureInterface is undocumented; Qt6 provides QPromise instead
    // based on https://stackoverflow.com/questions/59197694/qt-how-to-create-a-qfuture-from-a-thread
    auto future_iface = std::make_unique<QFutureInterface<Message>>();
    QByteArray inbox = Connection::newInbox().toLatin1();

    natsSubscription* subscription = nullptr;
    // cnats will copy natsInbox, so we can delete it in the end of this function
    checkError(natsConnection_SubscribeTimeout(&subscription, m_conn, inbox.constData(), timeout, &asyncRequestCallback, future_iface.get()));
    checkError(natsSubscription_AutoUnsubscribe(subscription, 1));
    checkError(natsConnection_PublishRequest(m_conn, qPrintable(subject), inbox.constData(), message.constData(), message.size()));
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

QString Connection::newInbox()
{
    natsInbox* inbox = nullptr;
    natsInbox_Create(&inbox);
    QString result = QString::fromLatin1(inbox);
    natsInbox_Destroy(inbox);
    return result;
}

Subscription::Subscription(Connection* connection, const QString& subject): QObject(connection)
{
    checkError(natsConnection_Subscribe(&m_sub, connection->m_conn, qPrintable(subject), &subscriptionCallback, this));
}

Subscription::Subscription(Connection* connection, const QString& subject, const QString& queueGroup): QObject(connection)
{
    checkError(natsConnection_QueueSubscribe(&m_sub, connection->m_conn, qPrintable(subject), qPrintable(queueGroup), &subscriptionCallback, this));
}

Subscription::~Subscription()
{
    natsSubscription_Destroy(m_sub);
}
