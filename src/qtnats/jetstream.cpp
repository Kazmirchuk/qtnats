/*
 * Copyright(c) 2021-2022 Petro Kazmirchuk https://github.com/Kazmirchuk
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "qtnats/qtnats.h"
#include "qtnats/qtnats_p.h"

using namespace QtNats;

static void checkJsError(const natsStatus& s, const jsErrCode& js) {
    if (s == NATS_OK) {
        return;
    }
    throw JetStreamException(s, js);
}

static void jsPubErrHandler(jsCtx*, jsPubAckErr* pae, void* closure) {
    auto* const js = static_cast<JetStream*>(closure);
    const Message msg = fromC(NatsMsgPtr(pae->Msg));
    Q_EMIT js->errorOccurred(pae->Err, pae->ErrCode, QString(pae->ErrText), msg);
}

JetStream* Client::jetStream(const JsOptions& options) {
    // If something throws, it will be cleaned up by the unique_ptr's destructor instead of being leaked.
    // We can't use make_unique because we're relying on the friend declaration.
    auto js = std::unique_ptr<JetStream>(new JetStream(this));
    convertAndHandle(options, [&](jsOptions& jsOpts) {
        jsOpts.PublishAsync.ErrHandler = &jsPubErrHandler;
        jsOpts.PublishAsync.ErrHandlerClosure = js.get();
        checkError(natsConnection_JetStream(&js->m_jsCtx, m_conn, &jsOpts));
    });
    return js.release();
}

JsStreamInfo JetStream::addStream(const JsStreamConfig& config) const {
    return convertAndHandle(config, [&](jsStreamConfig& jsConfig) {
        jsStreamInfo* si;
        jsErrCode jsErr = {};
        const natsStatus s = js_AddStream(&si, m_jsCtx, &jsConfig, nullptr, &jsErr);
        checkJsError(s, jsErr);
        return fromC(JsStreamInfoPtr(si));
    });
}

JsStreamInfo JetStream::updateStream(const JsStreamConfig& config) const {
    return convertAndHandle(config, [&](jsStreamConfig& jsConfig) {
        jsStreamInfo* si;
        jsErrCode jsErr = {};
        const natsStatus s = js_UpdateStream(&si, m_jsCtx, &jsConfig, nullptr, &jsErr);
        checkJsError(s, jsErr);
        return fromC(JsStreamInfoPtr(si));
    });
}

void JetStream::purgeStream(const QString& stream) const {
    jsErrCode jsErr = {};
    const natsStatus s = js_PurgeStream(m_jsCtx, stream.toUtf8().constData(), nullptr, &jsErr);
    checkJsError(s, jsErr);
}

void JetStream::deleteStream(const QString& stream) const {
    jsErrCode jsErr = {};
    const natsStatus s = js_DeleteStream(m_jsCtx, stream.toUtf8().constData(), nullptr, &jsErr);
    checkJsError(s, jsErr);
}

JsStreamInfo JetStream::getStreamInfo(const QString& stream) const {
    jsStreamInfo* si;
    jsErrCode jsErr = {};
    const natsStatus s = js_GetStreamInfo(&si, m_jsCtx, stream.toUtf8().constData(), nullptr, &jsErr);
    checkJsError(s, jsErr);
    return fromC(JsStreamInfoPtr(si));
}

JsConsumerInfo JetStream::addConsumer(const QString& stream, const JsConsumerConfig& config) const {
    return convertAndHandle(config, [&](jsConsumerConfig& jsConfig) {
        jsConsumerInfo* ci;
        jsErrCode jsErr = {};
        const natsStatus s = js_AddConsumer(&ci, m_jsCtx, stream.toUtf8().constData(), &jsConfig, nullptr, &jsErr);
        checkJsError(s, jsErr);
        return fromC(JsConsumerInfoPtr(ci));
    });
}

JsConsumerInfo JetStream::updateConsumer(const QString& stream, const JsConsumerConfig& config) const {
    return convertAndHandle(config, [&](jsConsumerConfig& jsConfig) {
        jsConsumerInfo* ci;
        jsErrCode jsErr = {};
        const natsStatus s = js_UpdateConsumer(&ci, m_jsCtx, stream.toUtf8().constData(), &jsConfig, nullptr, &jsErr);
        checkJsError(s, jsErr);
        return fromC(JsConsumerInfoPtr(ci));
    });
}

JsConsumerInfo JetStream::getConsumerInfo(const QString& stream, const QString& consumer) const {
    jsConsumerInfo* ci;
    jsErrCode jsErr = {};
    const natsStatus s =
        js_GetConsumerInfo(&ci, m_jsCtx, stream.toUtf8().constData(), consumer.toUtf8().constData(), nullptr, &jsErr);
    checkJsError(s, jsErr);
    return fromC(JsConsumerInfoPtr(ci));
}

void JetStream::deleteConsumer(const QString& stream, const QString& consumer) const {
    jsErrCode jsErr = {};
    const natsStatus s =
        js_DeleteConsumer(m_jsCtx, stream.toUtf8().constData(), consumer.toUtf8().constData(), nullptr, &jsErr);
    checkJsError(s, jsErr);
}

JsConsumerPauseResponse JetStream::pauseConsumer(
    const QString& stream,
    const QString& consumer,
    const NatsTimePoint pauseUntil
) const {
    jsConsumerPauseResponse* resp;
    jsErrCode jsErr = {};
    const natsStatus s = js_PauseConsumer(
        &resp,
        m_jsCtx,
        stream.toUtf8().constData(),
        consumer.toUtf8().constData(),
        pauseUntil.time_since_epoch().count(),
        nullptr,
        &jsErr
    );
    checkJsError(s, jsErr);
    return fromC(JsConsumerPauseResponsePtr(resp));
}

ObjectStore* JetStream::createObjectStore(const ObjStoreConfig& config) {
    // If something throws, it will be cleaned up by the unique_ptr's destructor instead of being leaked.
    // We can't use make_unique because we're relying on the friend declaration.
    auto store = std::unique_ptr<ObjectStore>(new ObjectStore(this));
    convertAndHandle(config, [&](objStoreConfig& jsConfig) {
        checkError(js_CreateObjectStore(&store->m_objStore, m_jsCtx, &jsConfig));
    });
    return store.release();
}

ObjectStore* JetStream::updateObjectStore(const ObjStoreConfig& config) {
    // If something throws, it will be cleaned up by the unique_ptr's destructor instead of being leaked.
    // We can't use make_unique because we're relying on the friend declaration.
    auto store = std::unique_ptr<ObjectStore>(new ObjectStore(this));    convertAndHandle(config, [&](objStoreConfig& jsConfig) {
        checkError(js_UpdateObjectStore(&store->m_objStore, m_jsCtx, &jsConfig));
    });
    return store.release();
}

ObjectStore* JetStream::getObjectStore(const QString& bucket) {
    // If something throws, it will be cleaned up by the unique_ptr's destructor instead of being leaked.
    // We can't use make_unique because we're relying on the friend declaration.
    auto store = std::unique_ptr<ObjectStore>(new ObjectStore(this));
    checkError(js_ObjectStore(&store->m_objStore, m_jsCtx, bucket.toUtf8().constData()));
    return store.release();
}

void JetStream::deleteObjectStore(const QString& bucket) const {
    checkError(js_DeleteObjectStore(m_jsCtx, bucket.toUtf8().constData()));
}

void Message::ack() const {
    jsErrCode jsErr = {};
    const natsStatus s = natsMsg_AckSync(m_natsMsg.get(), nullptr, &jsErr);
    checkJsError(s, jsErr);
}

void Message::nack(std::optional<int64_t> delay) const {
    natsStatus s;
    if (delay.has_value()) {
        s = natsMsg_NakWithDelay(m_natsMsg.get(), delay.value(), nullptr);
    } else {
        s = natsMsg_Nak(m_natsMsg.get(), nullptr);
    }
    checkError(s);
}

void Message::inProgress() const { checkError(natsMsg_InProgress(m_natsMsg.get(), nullptr)); }

void Message::terminate() const { checkError(natsMsg_Term(m_natsMsg.get(), nullptr)); }

PullSubscription::~PullSubscription() noexcept { natsSubscription_Destroy(m_sub); }

QList<Message> PullSubscription::fetch(const int batch, const NatsTimeout timeout) const {
    // see also https://github.com/nats-io/nats.c/issues/545
    natsMsgList list{nullptr, 0};
    jsErrCode jsErr = {};
    const natsStatus s = natsSubscription_Fetch(&list, m_sub, batch, timeout.count(), &jsErr);
    checkJsError(s, jsErr);
    QList<Message> result;
    for (int i = 0; i < list.Count; i++) {
        result += fromC(NatsMsgPtr(list.Msgs[i]));
        list.Msgs[i] = nullptr; // natsMsgList_Destroy should destroy only the list, and keep the messages
    }
    natsMsgList_Destroy(&list);
    return result;
}

JetStream::~JetStream() noexcept { jsCtx_Destroy(m_jsCtx); }


JsPublishAck JetStream::publish(const Message& msg, const JsPublishOptions& opts) {
    return convertAndHandle(opts, [&](jsPubOptions& jsOpts) { return doPublish(msg, &jsOpts); });
}

void JetStream::asyncPublish(const Message& msg, const JsPublishOptions& opts) const {
    convertAndHandle(opts, [&](jsPubOptions& jsOpts) { doAsyncPublish(msg, &jsOpts); });
}

void JetStream::waitForPublishCompleted(const std::optional<NatsTimeout> timeout) const {
    natsStatus s = NATS_OK;

    if (timeout.has_value()) {
        convertAndHandle(JsPublishOptions{.timeout = timeout}, [&](jsPubOptions& jsOpts) {
            s = js_PublishAsyncComplete(m_jsCtx, &jsOpts);
        });
    } else {
        s = js_PublishAsyncComplete(m_jsCtx, nullptr);
    }

    if (s == NATS_TIMEOUT) {
        // optionally we can delete the messages, but they might be ACK'ed later
        // natsMsgList list;
        // js_PublishAsyncGetPendingList(&list, m_jsCtx);
        // natsMsgList_Destroy(&list);
    }
    checkError(s);
}

Subscription* JetStream::subscribe(const QString& subject, const QString& stream, const QString& consumer) {
    // manualAck=true: avoid _autoAckCB in cnats internals, because it takes over ownership of delivered messages
    // If something throws, it will be cleaned up by the unique_ptr's destructor instead of being leaked.
    // We can't use make_unique because we're relying on the friend declaration.
    auto sub = std::unique_ptr<Subscription>(new Subscription(nullptr));
    jsErrCode jsErr = {};
    convertAndHandle(JsSubOptions{stream, consumer}, /*manualAck=*/true, [&](jsSubOptions& subOpts) {
        const natsStatus s = js_Subscribe(
            &sub->m_sub,
            m_jsCtx,
            subject.toUtf8().constData(),
            &subscriptionCallback,
            sub.get(),
            nullptr,
            &subOpts,
            &jsErr
        );
        checkJsError(s, jsErr);
    });
    sub->setParent(this);
    return sub.release();
}

PullSubscription* JetStream::pullSubscribe(const QString& subject, const QString& stream, const QString& consumer) {
    // If something throws, it will be cleaned up by the unique_ptr's destructor instead of being leaked.
    // We can't use make_unique because we're relying on the friend declaration.
    auto sub = std::unique_ptr<PullSubscription>(new PullSubscription(nullptr));
    jsErrCode jsErr = {};
    convertAndHandle(JsSubOptions{stream, consumer}, /*manualAck=*/false, [&](jsSubOptions& subOpts) {
        const natsStatus s = js_PullSubscribe(
            &sub->m_sub, m_jsCtx, subject.toUtf8().constData(), consumer.toUtf8().constData(), nullptr, &subOpts, &jsErr
        );
        checkJsError(s, jsErr);
    });
    sub->setParent(this);
    return sub.release();
}

JsPublishAck JetStream::doPublish(const Message& msg, jsPubOptions* opts) {
    return convertAndHandle(msg, nullptr, [&](const auto& p) {
        auto jsErr = static_cast<jsErrCode>(0);
        jsPubAck* ack = nullptr;

        const natsStatus s = js_PublishMsg(&ack, m_jsCtx, p.get(), opts, &jsErr);
        checkJsError(s, jsErr);

        return fromC(JsPubAckPtr(ack));
    });
}

void JetStream::doAsyncPublish(const Message& msg, jsPubOptions* opts) const {
    // js_PublishMsgAsync is tricky to manage lifetime of natsMsg, so let's go the safe way
    // TODO headers will require js_PublishMsgAsync
    checkError(js_PublishAsync(m_jsCtx, msg.subject.toUtf8().constData(), msg.data.constData(), msg.data.size(), opts));
}

ObjectStore::~ObjectStore() noexcept { objStore_Destroy(m_objStore); }

ObjStoreInfo ObjectStore::putString(const QString& name, const QString& data) const {
    objStoreInfo* info;
    checkError(objStore_PutString(&info, m_objStore, name.toUtf8().constData(), data.toUtf8().constData()));
    return fromC(ObjStoreInfoPtr(info));
}

ObjStoreInfo ObjectStore::putBytes(const QString& name, const QByteArray& data) const {
    objStoreInfo* info;
    checkError(objStore_PutBytes(&info, m_objStore, name.toUtf8().constData(), data.constData(), data.size()));
    return fromC(ObjStoreInfoPtr(info));
}

ObjStoreInfo ObjectStore::putFile(const std::filesystem::path& path) const {
    objStoreInfo* info;
    checkError(objStore_PutFile(&info, m_objStore, path.string().c_str()));
    return fromC(ObjStoreInfoPtr(info));
}

QString ObjectStore::getString(const QString& name, const ObjStoreOptions& options) const {
    return convertAndHandle(options, [&](objStoreOptions& opts) {
        char* data;
        checkError(objStore_GetString(&data, m_objStore, name.toUtf8().constData(), &opts));
        const QString result = QString::fromUtf8(data);
        free(data);
        return result;
    });
}

QByteArray ObjectStore::getBytes(const QString& name, const ObjStoreOptions& options) const {
    return convertAndHandle(options, [&](objStoreOptions& opts) {
        void* data;
        int dataLen;
        checkError(objStore_GetBytes(&data, &dataLen, m_objStore, name.toUtf8().constData(), &opts));
        const QByteArray result(static_cast<const char*>(data), dataLen);
        free(data);
        return result;
    });
}

ObjStoreInfo ObjectStore::getInfo(const QString& name, const ObjStoreOptions& options) const {
    return convertAndHandle(options, [&](objStoreOptions& opts) {
        objStoreInfo* info;
        checkError(objStore_GetInfo(&info, m_objStore, name.toUtf8().constData(), &opts));
        return fromC(ObjStoreInfoPtr(info));
    });
}

void ObjectStore::getFile(
    const QString& name,
    const std::filesystem::path& path,
    const ObjStoreOptions& options
) const {
    return convertAndHandle(options, [&](objStoreOptions& opts) {
        checkError(objStore_GetFile(m_objStore, name.toUtf8().constData(), path.string().c_str(), &opts));
    });
}
