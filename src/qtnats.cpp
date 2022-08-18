/* Copyright(c) 2021-2022 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/

#include "qtnats.h"
#include "qtnats_p.h"

#include <opts.h>

#include <QThread>
#include <QFutureInterface>

using namespace QtNats;

static QString getNatsErrorText(natsStatus status) {
    if (status == NATS_OK)
        return QString();

    return QString::fromLatin1(natsStatus_GetText(status));
}

void QtNats::checkError(natsStatus s)
{
    if (s == NATS_OK) return;
    throw Exception(s);
}

Options::Options()
{
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

static natsOptions* buildNatsOptions(const Options& opts)
{
    natsOptions* o;
    natsOptions_Create(&o);

    if (opts.servers.size()) {
        QList<QByteArray> l;
        QVector<const char*> ptrs;
        for (auto url : opts.servers) {
            // TODO check for invalid URL
            l.append(url.toEncoded());
            ptrs.append(l.last().constData());
        }
        checkError(natsOptions_SetServers(o, ptrs.data(), static_cast<int>(ptrs.size())));
    }
    checkError(natsOptions_SetUserInfo(o, opts.user.constData(), opts.password.constData()));
    checkError(natsOptions_SetToken(o, opts.token.constData()));
    checkError(natsOptions_SetNoRandomize(o, !opts.randomize)); //NB! reverted flag
    checkError(natsOptions_SetTimeout(o, opts.timeout));
    checkError(natsOptions_SetName(o, opts.name.constData()));
    //postpone until I have SSL
    // natsOptions_SetSecure(o, secure);
    checkError(natsOptions_SetVerbose(o, opts.verbose));
    checkError(natsOptions_SetPedantic(o, opts.pedantic));
    checkError(natsOptions_SetPingInterval(o, opts.pingInterval));
    checkError(natsOptions_SetMaxPingsOut(o, opts.maxPingsOut));
    checkError(natsOptions_SetAllowReconnect(o, opts.allowReconnect));
    checkError(natsOptions_SetMaxReconnect(o, opts.maxReconnect));
    checkError(natsOptions_SetReconnectWait(o, opts.reconnectWait));
    checkError(natsOptions_SetReconnectBufSize(o, opts.reconnectBufferSize));
    checkError(natsOptions_SetMaxPendingMsgs(o, opts.maxPendingMessages));
    checkError(natsOptions_SetNoEcho(o, !opts.echo));  //NB! reverted flag

    return o;
}

// need to pass it through queued signal-slot connections
static const int messageTypeId = qRegisterMetaType<Message>();

Message::Message(natsMsg* msg) noexcept:
    m_natsMsg(msg, &natsMsg_Destroy)
{
    subject = QByteArray(natsMsg_GetSubject(msg));
    data = QByteArray(natsMsg_GetData(msg), natsMsg_GetDataLength(msg));
    reply = QByteArray(natsMsg_GetReply(msg));

    const char** keys = nullptr;
    int keyCount = 0;

    natsStatus s = natsMsgHeader_Keys(msg, &keys, &keyCount);
    if (s != NATS_OK || keyCount == 0)
        return;
    
    // handle message headers
    for (int i = 0; i < keyCount; i++) {
        const char** values = nullptr;
        int valueCount = 0;
        s = natsMsgHeader_Values(msg, keys[i], &values, &valueCount);
        if (s != NATS_OK)
            continue;
        QByteArray key (keys[i]);
        
        for (int j = 0; j < valueCount; j++) {
            QByteArray value (values[j]);
            headers.insert(key, value);
        }
        free(values);
    }

    free(keys);
};

NatsMsgPtr QtNats::toNatsMsg(const Message& msg, const char* reply)
{
    natsMsg* cnatsMsg;
    
    const char* realReply = nullptr; //in asyncRequest I need to provide my own reply
    if (reply) {
        realReply = reply;
    }
    else if (msg.reply.size()) {
        realReply = msg.reply.constData();
    }

    checkError(natsMsg_Create(&cnatsMsg,
        msg.subject.constData(),
        realReply,
        msg.data.constData(),
        msg.data.size()
    ));
    
    NatsMsgPtr msgPtr(cnatsMsg, &natsMsg_Destroy);

    auto i = msg.headers.constBegin();
    while (i != msg.headers.constEnd()) {
        checkError(natsMsgHeader_Add(cnatsMsg, i.key().constData(), i.value().constData()));
    }
    return msgPtr;
}

void QtNats::subscriptionCallback(natsConnection* /*nc*/, natsSubscription* /*sub*/, natsMsg* msg, void* closure) {
    Subscription* sub = reinterpret_cast<Subscription*>(closure);
    
    Message m(msg);
    emit sub->received(m);
}

static void asyncRequestCallback(natsConnection* /*nc*/, natsSubscription* natsSub, natsMsg* msg, void* closure) {
    auto future_iface = reinterpret_cast<QFutureInterface<Message>*>(closure);

    if (msg) {
        if (natsMsg_IsNoResponders(msg)) {
            future_iface->reportException(Exception(NATS_NO_RESPONDERS));
            natsMsg_Destroy(msg);
        }
        else {
            Message m(msg);
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
    Client* c = reinterpret_cast<Client*>(closure);
    emit c->errorOccurred(err, getNatsErrorText(err));
}

static void closedConnectionHandler(natsConnection* /*nc*/, void *closure) {
    Client* c = reinterpret_cast<Client*>(closure);
    //can ask for last error here
    emit c->statusChanged(ConnectionStatus::Closed);
}

static void reconnectedHandler(natsConnection* /*nc*/, void *closure) {
    Client* c = reinterpret_cast<Client*>(closure);
    emit c->statusChanged(ConnectionStatus::Connected);
}

static void disconnectedHandler(natsConnection* /*nc*/, void *closure) {
    Client* c = reinterpret_cast<Client*>(closure);
    emit c->statusChanged(ConnectionStatus::Disconnected);
}

Client::Client(QObject* parent):
    QObject(parent)
{
    int cpuCoresCount = QThread::idealThreadCount(); //this function may fail, thus the check
    if (cpuCoresCount >= 2) {
        nats_SetMessageDeliveryPoolSize(cpuCoresCount);
    }
}

Client::~Client()
{
    close();
}

void Client::connectToServer(const Options& opts)
{
    natsOptions* nats_opts = buildNatsOptions(opts);
    //don't create a thread for each subscription, since we may have a lot of subscriptions
    //number of threads in the pool is set by nats_SetMessageDeliveryPoolSize above
    natsOptions_UseGlobalMessageDelivery(nats_opts, true);

    natsOptions_SetErrorHandler(nats_opts, &errorHandler, this);
    natsOptions_SetClosedCB(nats_opts, &closedConnectionHandler, this);
    natsOptions_SetDisconnectedCB(nats_opts, &disconnectedHandler, this);
    natsOptions_SetReconnectedCB(nats_opts, &reconnectedHandler, this);

    emit statusChanged(ConnectionStatus::Connecting);
    checkError(natsConnection_Connect(&m_conn, nats_opts));
    emit statusChanged(ConnectionStatus::Connected);
    //TODO handle reopening
}

void Client::connectToServer(const QUrl& address)
{
    Options connOpts;
    connOpts.servers += address;
    connectToServer(connOpts);
}

void Client::close() noexcept
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

void Client::publish(const Message& msg) {
    NatsMsgPtr p = toNatsMsg(msg);
    checkError(natsConnection_PublishMsg(m_conn, p.get()));
}

Message Client::request(const Message& msg, qint64 timeout)
{
    natsMsg* replyMsg;
    NatsMsgPtr p = toNatsMsg(msg);
    checkError(natsConnection_RequestMsg(&replyMsg, m_conn, p.get(), timeout));
    return Message(replyMsg);
}

QFuture<Message> Client::asyncRequest(const Message& msg, qint64 timeout)
{
    // QFutureInterface is undocumented; Qt6 provides QPromise instead
    // based on https://stackoverflow.com/questions/59197694/qt-how-to-create-a-qfuture-from-a-thread
    auto future_iface = std::make_unique<QFutureInterface<Message>>();
    QByteArray inbox = Client::newInbox();

    natsSubscription* subscription = nullptr;
    
    checkError(natsConnection_SubscribeTimeout(&subscription, m_conn, inbox.constData(), timeout, &asyncRequestCallback, future_iface.get()));
    checkError(natsSubscription_AutoUnsubscribe(subscription, 1));
    // can't do msg.reply = inbox; publish(msg); because "msg" is constant
    NatsMsgPtr p = toNatsMsg(msg, inbox.constData());
    checkError(natsConnection_PublishMsg(m_conn, p.get()));

    future_iface->reportStarted();
    auto f = future_iface->future();
    future_iface.release(); //will be deleted in asyncRequestCallback
    return f;
}

Subscription* Client::subscribe(const QByteArray& subject)
{
    // avoid a memory leak if checkError throws
    // can't use make_unique because Subscription's constructor is private
    auto sub = std::unique_ptr<Subscription>(new Subscription(nullptr));
    checkError(natsConnection_Subscribe(&sub->m_sub, m_conn, subject.constData(), &subscriptionCallback, sub.get()));
    sub->setParent(this);
    return sub.release();
}

Subscription* Client::subscribe(const QByteArray& subject, const QByteArray& queueGroup)
{
    auto sub = std::unique_ptr<Subscription>(new Subscription(nullptr));
    checkError(natsConnection_QueueSubscribe(&sub->m_sub, m_conn, subject.constData(), queueGroup.constData(), &subscriptionCallback, sub.get()));
    sub->setParent(this);
    return sub.release();
}

bool Client::ping(qint64 timeout) noexcept
{
    natsStatus s = natsConnection_FlushTimeout(m_conn, timeout);
    return (s == NATS_OK);
}

QUrl Client::currentServer() const
{
    char buffer[500];
    natsStatus s = natsConnection_GetConnectedUrl(m_conn, buffer, sizeof(buffer));
    if (s != NATS_OK) {
        return QUrl();
    }
    return QUrl(QString::fromLatin1(buffer));
}

ConnectionStatus Client::status() const
{
    return ConnectionStatus(natsConnection_Status(m_conn));
}

QString Client::errorString() const
{
    // TODO handle when m_conn==nullptr ?
    const char* buffer = nullptr;
    natsConnection_GetLastError(m_conn, &buffer);
    return QString::fromLatin1(buffer);
}

QByteArray Client::newInbox()
{
    natsInbox* inbox = nullptr;
    natsInbox_Create(&inbox);
    QByteArray result (inbox);
    natsInbox_Destroy(inbox);
    return result;
}

Subscription::~Subscription()
{
    natsSubscription_Destroy(m_sub);
}
