/* Copyright(c) 2022 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/

#include "qtnats.h"
#include "qtnats_p.h"

using namespace QtNats;

static void checkJsError(natsStatus s, jsErrCode js)
{
    if (s == NATS_OK) return;
    throw JetStreamException(s, js);
}

static void jsPubErrHandler(jsCtx*, jsPubAckErr* pae, void* closure)
{
    JetStream* js = reinterpret_cast<JetStream*>(closure);
    Message msg (pae->Msg);
    emit js->errorOccurred(pae->Err, pae->ErrCode, QString(pae->ErrText), msg);
}

JetStream* Client::jetStream(const JsOptions& options)
{
    JetStream* js = new JetStream(this);
    jsOptions jsOpts;
    jsOptions_Init(&jsOpts);
    jsOpts.Domain = options.domain.constData();
    jsOpts.Wait = options.timeout;

    jsOpts.PublishAsync.ErrHandler = &jsPubErrHandler;
    jsOpts.PublishAsync.ErrHandlerClosure = js;

    checkError(natsConnection_JetStream(&js->m_jsCtx, m_conn, &jsOpts));
    return js;
}

void Message::ack()
{
    jsErrCode jsErr;
    natsStatus s = natsMsg_AckSync(m_natsMsg.get(), nullptr, &jsErr);
    checkJsError(s, jsErr);
}

void Message::nack(qint64 delay)
{
    natsStatus s;
    if (delay == -1) {
        s = natsMsg_Nak(m_natsMsg.get(), nullptr);
    }
    else {
        s = natsMsg_NakWithDelay(m_natsMsg.get(), delay, nullptr);
    }
    checkError(s);
}

void Message::inProgress()
{
    checkError(natsMsg_InProgress(m_natsMsg.get(), nullptr));
}

void Message::terminate()
{
    checkError(natsMsg_Term(m_natsMsg.get(), nullptr));
}

PullSubscription::~PullSubscription() noexcept
{
    natsSubscription_Destroy(m_sub);
}

QList<Message> PullSubscription::fetch(int batch, qint64 timeout)
{
    // see also https://github.com/nats-io/nats.c/issues/545
    natsMsgList list {nullptr, 0};
    jsErrCode jsErr;
    natsStatus s = natsSubscription_Fetch(&list, m_sub, batch, timeout, &jsErr);
    checkJsError(s, jsErr);
    QList<Message> result;
    for (int i = 0; i < list.Count; i++) {
        result += Message(list.Msgs[i]);
        list.Msgs[i] = nullptr; //natsMsgList_Destroy should destroy only the list, and keep the messages
    }
    natsMsgList_Destroy(&list);
    return result;
}

JetStream::~JetStream() noexcept
{
	jsCtx_Destroy(m_jsCtx);
}

static JsPublishAck fromJsPubAck(jsPubAck* ack)
{
    JsPublishAck result;

    result.stream = QByteArray(ack->Stream);
    result.domain = QByteArray(ack->Domain);
    result.sequence = ack->Sequence;
    result.duplicate = ack->Duplicate;
    jsPubAck_Destroy(ack);
    
    return result;
}

static void jsPublishOptionsToC(const JsPublishOptions& opts, jsPubOptions* out)
{
    jsPubOptions_Init(out);
    out->MaxWait = opts.timeout;

    QByteArray msgID;
    if (opts.msgID.size()) {
        out->MsgId = opts.msgID.constData();
    }

    QByteArray expectStream;
    if (opts.expectStream.size()) {
        out->ExpectStream = opts.expectStream.constData();
    }

    QByteArray expectLastMessageID;
    if (opts.expectLastMessageID.size()) {
        out->ExpectLastMsgId = opts.expectLastMessageID.constData();
    }

    out->ExpectLastSeq = opts.expectLastSequence;
    out->ExpectLastSubjectSeq = opts.expectLastSubjectSequence;
    out->ExpectNoMessage = opts.expectNoMessage;
}

JsPublishAck JetStream::publish(const Message& msg, const JsPublishOptions& opts)
{
    jsPubOptions jsOpts;
    jsPublishOptionsToC(opts, &jsOpts);
    return doPublish(msg, &jsOpts);
}

JsPublishAck JetStream::publish(const Message& msg, qint64 timeout)
{
    jsPubOptions jsOpts;
    jsPubOptions_Init(&jsOpts);
    if (timeout != -1) {
        jsOpts.MaxWait = timeout;
    }
    return doPublish(msg, &jsOpts);
}

void JetStream::asyncPublish(const Message& msg, const JsPublishOptions& opts)
{
    jsPubOptions jsOpts;
    jsPublishOptionsToC(opts, &jsOpts);
    doAsyncPublish(msg, &jsOpts);
}

void JetStream::asyncPublish(const Message& msg, qint64 timeout)
{
    jsPubOptions jsOpts;
    jsPubOptions_Init(&jsOpts);
    if (timeout != -1) {
        jsOpts.MaxWait = timeout;
    }
    doAsyncPublish(msg, &jsOpts);
}

void JetStream::waitForPublishCompleted(qint64 timeout)
{
    // TODO use QtConcurrent::run and return QFuture?
    natsStatus s = NATS_OK;
    if (timeout != -1) {
        jsPubOptions jsOpts;
        jsPubOptions_Init(&jsOpts);
        jsOpts.MaxWait = timeout;
        s = js_PublishAsyncComplete(m_jsCtx, &jsOpts);
    }
    else {
        s = js_PublishAsyncComplete(m_jsCtx, nullptr);
    }
    if (s == NATS_TIMEOUT) {
        // optionally we can delete the messages, but they might be ACK'ed later
        //natsMsgList list;
        //js_PublishAsyncGetPendingList(&list, m_jsCtx);
        //natsMsgList_Destroy(&list);
    }
    checkError(s);
}

Subscription* JetStream::subscribe(const QByteArray& subject, const QByteArray& stream, const QByteArray& consumer)
{
    jsSubOptions subOpts;
    jsSubOptions_Init(&subOpts);
    subOpts.Stream = stream.constData();
    subOpts.Consumer = consumer.constData();
    subOpts.ManualAck = true; // avoid _autoAckCB in cnats internals, because it takes over ownership of delivered messages
    auto sub = std::unique_ptr<Subscription>(new Subscription(nullptr));
    jsErrCode jsErr;
    natsStatus s = js_Subscribe(&sub->m_sub, m_jsCtx, subject.constData(), &subscriptionCallback, sub.get(), nullptr, &subOpts, &jsErr);
    checkJsError(s, jsErr);
    sub->setParent(this);
    return sub.release();
}

PullSubscription* JetStream::pullSubscribe(const QByteArray& subject, const QByteArray& stream, const QByteArray& consumer)
{
    auto sub = std::unique_ptr<PullSubscription>(new PullSubscription(nullptr));
    jsErrCode jsErr;
 
    jsSubOptions subOpts;
    jsSubOptions_Init(&subOpts);
    subOpts.Stream = stream.constData();
    subOpts.Consumer = consumer.constData();

    natsStatus s = js_PullSubscribe(&sub->m_sub, m_jsCtx, subject.constData(), consumer.constData(), nullptr, &subOpts, &jsErr);
    checkJsError(s, jsErr);
    sub->setParent(this);
    return sub.release();
}

JsPublishAck JetStream::doPublish(const Message& msg, jsPubOptions* opts)
{
    jsErrCode jsErr = jsErrCode(0);
    jsPubAck* ack = nullptr;
    NatsMsgPtr cnatsMsg = toNatsMsg(msg);

    natsStatus s = js_PublishMsg(&ack, m_jsCtx, cnatsMsg.get(), opts, &jsErr);
    checkJsError(s, jsErr);

    return fromJsPubAck(ack);
}

void JetStream::doAsyncPublish(const Message& msg, jsPubOptions* opts)
{
    //js_PublishMsgAsync is tricky to manage lifetime of natsMsg, so let's go the safe way
    //TODO headers will require js_PublishMsgAsync
    checkError(js_PublishAsync(m_jsCtx, msg.subject.constData(), msg.data.constData(), msg.data.size(), opts));
}
