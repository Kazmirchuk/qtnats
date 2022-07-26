/* Copyright(c) 2021-2022 Petro Kazmirchuk https://github.com/Kazmirchuk

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
    Message msg = fromNatsMsg(pae->Msg);
    emit js->errorOccurred(pae->Err, pae->ErrCode, QString(pae->ErrText), msg);
}

JetStream* Connection::jetStream(const JsOptions& options)
{
    JetStream* js = new JetStream(this);
    jsOptions jsOpts;
    jsOptions_Init(&jsOpts);
    jsOpts.Domain = qPrintable(options.domain);
    jsOpts.Wait = options.timeout;

    jsOpts.PublishAsync.ErrHandler = &jsPubErrHandler;
    jsOpts.PublishAsync.ErrHandlerClosure = js;

    checkError(natsConnection_JetStream(&js->m_jsCtx, m_conn, &jsOpts));
    return js;
}

JetStream::~JetStream() noexcept
{
	jsCtx_Destroy(m_jsCtx);
}

static JsPublishAck fromJsPubAck(jsPubAck* ack)
{
    JsPublishAck result;

    result.stream = QString::fromLatin1(ack->Stream);
    result.domain = QString::fromLatin1(ack->Domain);
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
        msgID = opts.msgID.toLatin1();
        out->MsgId = msgID.constData();
    }

    QByteArray expectStream;
    if (opts.expectStream.size()) {
        expectStream = opts.expectStream.toLatin1();
        out->ExpectStream = expectStream.constData();
    }

    QByteArray expectLastMessageID;
    if (opts.expectLastMessageID.size()) {
        expectLastMessageID = opts.expectLastMessageID.toLatin1();
        out->ExpectLastMsgId = expectLastMessageID.constData();
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
        // this doesn't compile https://github.com/nats-io/nats.c/issues/545
        //natsMsgList list;
        
        //js_PublishAsyncGetPendingList(&list, m_jsCtx);
        //natsMsgList_Destroy(&list);
    }
    checkError(s);
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
    const QByteArray data = msg.data();
    //js_PublishMsgAsync is tricky to manage lifetime of natsMsg, so let's go the safe way
    checkError(js_PublishAsync(m_jsCtx, qPrintable(msg.subject()), data.constData(), data.size(), opts));
}
