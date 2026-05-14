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

#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <QList>
#include <QMap>

#include <QByteArray>
#include <QFuture>
#include <QMultiHash>
#include <QObject>
#include <QSemaphore>
#include <QUrl>

#include <nats/nats.h>

#include <qtnats/qtnats_export.h>

#include <filesystem>

namespace QtNats {
QTNATS_EXPORT Q_NAMESPACE // we need the "export" directive due to https://bugreports.qt.io/browse/QTBUG-68014

    using MessageHeaders = QMultiHash<QString, QByteArray>;
    using NatsMetadata = QMap<QString, QByteArray>;
    using NatsTimePoint = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>;
    using NatsDuration = std::chrono::nanoseconds;
    using NatsTimeout = std::chrono::milliseconds;

// need to throw it from QFuture; otherwise it would be derived from std::runtime_error
// in fact, QException inherits from std::exception, although it's not documented
class Exception : public QException {
public:
    explicit Exception(const natsStatus s) : errorCode(s) {}

    void raise() const override { throw *this; }
    [[nodiscard]] Exception* clone() const override { return new Exception(*this); }
    [[nodiscard]] const char* what() const noexcept override { return natsStatus_GetText(errorCode); }

    const natsStatus errorCode;
};

class JetStreamException : public Exception {
public:
    JetStreamException(natsStatus s, jsErrCode js) : Exception(s), jsError(js), errorText(initText(js)) {}

    void raise() const override { throw *this; }
    [[nodiscard]] JetStreamException* clone() const override { return new JetStreamException(*this); }
    [[nodiscard]] const char* what() const noexcept override { return errorText.constData(); }

    const jsErrCode jsError;

private:
    [[nodiscard]] QByteArray initText(const jsErrCode js) const {
        return QString("%1: %2").arg(Exception::what()).arg(js).toLatin1();
    }

    const QByteArray errorText;
};

// =====================================================================================================================
// Data types  (analogous to nats.h types)
// =====================================================================================================================
#pragma region Data types

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

enum class JsAckPolicy {
    Explicit = js_AckExplicit,
    None = js_AckNone,
    All = js_AckAll,
};

Q_ENUM_NS(JsAckPolicy)

enum class JsDeliverPolicy {
    All = js_DeliverAll,
    Last = js_DeliverLast,
    New = js_DeliverNew,
    ByStartSequence = js_DeliverByStartSequence,
    ByStartTime = js_DeliverByStartTime,
    LastPerSubject = js_DeliverLastPerSubject,
};

Q_ENUM_NS(JsDeliverPolicy)

enum class JsReplayPolicy {
    Instant = js_ReplayInstant,
    Original = js_ReplayOriginal,
};

Q_ENUM_NS(JsReplayPolicy)

enum class JsRetentionPolicy {
    Limits = js_LimitsPolicy,       ///< Retain messages until any limit (MaxMsgs/MaxBytes/MaxAge) is reached (default)
    Interest = js_InterestPolicy,   ///< Remove message when all known consumers have acknowledged it
    WorkQueue = js_WorkQueuePolicy, ///< Remove message when the first consumer acknowledges it
};

Q_ENUM_NS(JsRetentionPolicy)

enum class JsDiscardPolicy {
    Old = js_DiscardOld, ///< Remove older messages to stay within limits (default)
    New = js_DiscardNew, ///< Reject new messages when limits are reached
};

Q_ENUM_NS(JsDiscardPolicy)

enum class JsStorageType {
    File = js_FileStorage,     ///< Persist messages to disk (default)
    Memory = js_MemoryStorage, ///< Store messages in memory only
};

Q_ENUM_NS(JsStorageType)

enum class JsStorageCompression {
    None = js_StorageCompressionNone, ///< No compression (default)
    S2 = js_StorageCompressionS2,     ///< S2 compression
};

Q_ENUM_NS(JsStorageCompression)

struct JsConsumerConfig {
    std::optional<QString> name;
    std::optional<QString> durable;
    std::optional<QString> description;

    // nullopt = leave at existing consumer's value (for jsSubOptions bind); 0 = DeliverAll / AckExplicit /
    // ReplayInstant
    std::optional<JsDeliverPolicy> deliverPolicy;
    std::optional<JsAckPolicy> ackPolicy;
    std::optional<JsReplayPolicy> replayPolicy;

    uint64_t optStartSeq = 0;                  ///< Sequence to start from when DeliverPolicy = ByStartSequence
    std::optional<NatsTimePoint> optStartTime; ///< Timestamp to start from when DeliverPolicy = ByStartTime

    NatsDuration ackWait{};      ///< How long to wait for ack before redelivery; 0 = server default
    int64_t maxDeliver = 0;      ///< Maximum number of delivery attempts
    QList<NatsDuration> backOff; ///< Redelivery intervals

    std::optional<QString> filterSubject;   ///< Subject filter for this consumer
    std::optional<uint64_t> rateLimit;      ///< Rate limit in bits per second; nullopt = unlimited (0 in cnats)
    std::optional<QString> sampleFrequency; ///< Percentage of messages to sample for observability

    int64_t maxWaiting = 0;    ///< Maximum number of outstanding pull requests
    int64_t maxAckPending = 0; ///< Maximum number of unacknowledged messages

    bool flowControl = false;
    NatsDuration heartbeat{}; ///< Heartbeat interval; 0 = disabled
    bool headersOnly = false;

    // Pull-based options
    int64_t maxRequestBatch = 0;      ///< Maximum pull request batch size
    NatsDuration maxRequestExpires{}; ///< Maximum pull request expiration; 0 = server default
    int64_t maxRequestMaxBytes = 0;   ///< Maximum pull request byte limit

    // Push-based options
    std::optional<QString> deliverSubject;
    std::optional<QString> deliverGroup;

    NatsDuration inactiveThreshold{}; ///< Ephemeral consumer inactivity threshold; 0 = server default
    int64_t replicas = 0;
    bool memoryStorage = false;

    // Added in NATS 2.10
    QList<QString> filterSubjects; ///< Multiple subject filters
    NatsMetadata metadata;

    // Added in NATS 2.11
    std::optional<NatsTimePoint> pauseUntil; ///< Suspend consumer until this time; nullopt = not paused
};

struct JsExternalStream {
    QString apiPrefix;                    ///< API prefix for accessing the stream in another account
    std::optional<QString> deliverPrefix; ///< Delivery prefix for push consumers
};

struct JsStreamSource {
    QString name;                              ///< Name of the stream to source from
    uint64_t optStartSeq = 0;                  ///< Start sourcing from this sequence number
    std::optional<NatsTimePoint> optStartTime; ///< Start sourcing from this timestamp
    std::optional<QString> filterSubject;      ///< Only source messages matching this subject
    std::optional<JsExternalStream> external;  ///< Cross-account stream access; mutually exclusive with domain
    std::optional<QString> domain;             ///< Domain for cross-account access; mutually exclusive with external
};

struct JsPlacement {
    QString cluster;     ///< Cluster name to place the stream on
    QList<QString> tags; ///< Server tags used to select placement candidates
};

struct JsRePublish {
    QString source;           ///< Subject pattern to match for republishing (default: all)
    QString destination;      ///< Destination subject for republished messages
    bool headersOnly = false; ///< Republish only the headers, not the payload
};

struct JsSubjectTransformConfig {
    std::optional<QString> source; ///< Subject filter to match incoming messages
    QString destination;           ///< Subject transform destination
};

struct JsStreamConsumerLimits {
    NatsDuration inactiveThreshold{}; ///< Default inactivity threshold for ephemeral consumers
    int maxAckPending = 0;            ///< Maximum number of unacknowledged messages across all consumers
};

struct JsStreamConfig {
    QString name; ///< Stream name; must be unique and not contain spaces, tabs, period, greater-than, or asterisk
    std::optional<QString> description; ///< Human-readable description

    QList<QString> subjects; ///< Subjects the stream will consume; defaults to the stream name if empty

    JsRetentionPolicy retention = JsRetentionPolicy::Limits;
    JsDiscardPolicy discard = JsDiscardPolicy::Old;
    JsStorageType storage = JsStorageType::File;
    JsStorageCompression compression = JsStorageCompression::None; ///< Added in NATS 2.10

    std::optional<uint64_t> maxConsumers;      ///< Maximum number of consumers; nullopt = unlimited (-1 in cnats)
    std::optional<uint64_t> maxMsgs;           ///< Maximum number of messages; nullopt = unlimited (-1 in cnats)
    std::optional<uint64_t> maxBytes;          ///< Maximum total size in bytes; nullopt = unlimited (-1 in cnats)
    std::optional<NatsDuration> maxAge;        ///< Maximum message age, nanoseconds; nullopt = unlimited (0 in cnats)
    std::optional<uint64_t> maxMsgsPerSubject; ///< Maximum messages per subject; nullopt = unlimited (0 in cnats)
    std::optional<uint32_t> maxMsgSize;        ///< Maximum individual message size in bytes; nullopt = unlimited (-1 in cnats)

    int64_t replicas = 1;      ///< Number of replicas (cluster only)
    bool noAck = false;        ///< Disable acknowledgement for the stream
    NatsDuration duplicates{}; ///< Duplicate message window; 0 = disabled

    std::optional<QString> templateOwner; ///< Name of the JetStream template that manages this stream

    std::optional<JsPlacement> placement; ///< Placement constraints for the stream in a cluster
    std::optional<JsStreamSource> mirror; ///< Mirror configuration (stream mirrors another stream)
    QList<JsStreamSource> sources;        ///< Sources from other streams

    bool sealed = false;               ///< Seal the stream; no new messages accepted or deleted
    bool denyDelete = false;           ///< Prevent message deletion
    bool denyPurge = false;            ///< Prevent stream purge
    bool allowRollup = false;          ///< Allow purging messages via the Rollup header
    bool allowDirect = false;          ///< Enable high-performance direct message access
    bool mirrorDirect = false;         ///< Enable direct access for mirror streams
    bool discardNewPerSubject = false; ///< Apply DiscardNew per subject rather than the whole stream

    std::optional<JsRePublish> rePublish;      ///< Republish matched messages to another subject
    JsSubjectTransformConfig subjectTransform; ///< Transform subject on ingress (NATS 2.10)
    JsStreamConsumerLimits consumerLimits;     ///< Default limits applied to all consumers

    uint64_t firstSeq = 0; ///< Starting sequence number for the stream (NATS 2.10)

    NatsMetadata metadata; ///< User-defined key/value metadata (NATS 2.10)
};

struct JsLostStreamData {
    QList<uint64_t> msgs; ///< Sequence numbers of lost messages
    uint64_t bytes = 0;   ///< Total bytes of lost messages
};

struct JsStreamStateSubject {
    QString subject;
    uint64_t msgs = 0; ///< Number of messages for this subject
};

struct JsStreamState {
    uint64_t msgs = 0;         ///< Number of messages in the stream
    uint64_t bytes = 0;        ///< Total size of messages in bytes
    uint64_t firstSeq = 0;     ///< Sequence number of the first message
    NatsTimePoint firstTime{}; ///< Timestamp of the first message
    uint64_t lastSeq = 0;      ///< Sequence number of the last message
    NatsTimePoint lastTime{};  ///< Timestamp of the last message
    int64_t numSubjects = 0;   ///< Not sure why this signed, but it's like that in nats.c
    std::optional<QList<JsStreamStateSubject>>
        subjects; ///< Per-subject message counts; only populated if SubjectsFilter was set in jsOptions
    uint64_t numDeleted = 0;
    QList<uint64_t>
        deleted; ///< Sequence numbers of deleted messages; only populated if deletedDetails was set in jsOptions
    std::optional<JsLostStreamData> lost;
    int64_t consumers =
        0; ///< Number of consumers on this stream. Not sure why this signed, but it's like that in nats.c
};

struct JsPeerInfo {
    QString name;
    bool current = false; ///< Whether this peer is up to date
    bool offline = false;
    NatsDuration active{}; ///< Time since this peer was last seen
    uint64_t lag = 0;      ///< Number of uncommitted operations this peer is behind
};

struct JsClusterInfo {
    std::optional<QString> name;
    std::optional<QString> leader;
    QList<JsPeerInfo> replicas;
};

struct JsStreamSourceInfo {
    QString name;
    std::optional<JsExternalStream> external;
    uint64_t lag = 0;      ///< Number of messages this source is behind
    NatsDuration active{}; ///< Time since this source was last seen
    std::optional<QString> filterSubject;
    QList<JsSubjectTransformConfig> subjectTransforms;
};

struct JsStreamAlternate {
    QString name;
    QString domain;
    QString cluster;
};

struct JsStreamInfo {
    JsStreamConfig config;
    NatsTimePoint created{}; ///< When the stream was created
    JsStreamState state;
    std::optional<JsClusterInfo> cluster;
    std::optional<JsStreamSourceInfo> mirror;
    QList<JsStreamSourceInfo> sources;
    QList<JsStreamAlternate> alternates;
};

struct JsConsumerPauseResponse {
    bool paused = false;
    std::optional<NatsTimePoint> pauseUntil; ///< When the pause ends; nullopt if not paused
    NatsDuration pauseRemaining{};           ///< Remaining pause duration
};

struct JsSequencePair {
    uint64_t consumer = 0; ///< Consumer sequence number
    uint64_t stream = 0;   ///< Stream sequence number
};

struct JsSequenceInfo {
    uint64_t consumer = 0; ///< Consumer sequence number
    uint64_t stream = 0;   ///< Stream sequence number
    NatsTimePoint last{};  ///< Timestamp of the last activity
};

struct JsConsumerInfo {
    QString stream;          ///< Name of the stream this consumer belongs to
    QString name;            ///< Name of the consumer
    NatsTimePoint created{}; ///< When the consumer was created
    JsConsumerConfig config;
    JsSequenceInfo delivered;   ///< Last sequence delivered and acknowledged
    JsSequenceInfo ackFloor;    ///< Highest contiguous acknowledged sequence
    int64_t numAckPending = 0;  ///< Number of messages waiting for acknowledgement
    int64_t numRedelivered = 0; ///< Number of messages redelivered
    int64_t numWaiting = 0;     ///< Number of waiting pull requests
    uint64_t numPending = 0;    ///< Number of messages remaining to be delivered
    std::optional<JsClusterInfo> cluster;
    bool pushBound = false;        ///< Whether a push consumer is bound to a subscription
    bool paused = false;           ///< Whether the consumer is paused
    NatsDuration pauseRemaining{}; ///< Remaining pause duration
};

struct JsOptionsPublishAsync {
    int64_t maxPending = 0;     ///< Maximum outstanding async publishes inflight at one time (0 = no limit)
    NatsTimeout stallWait{200}; ///< Time to wait in PublishAsync when MaxPending is reached
    // Note: AckHandler/ErrHandler are not exposed here — connect to JetStream::errorOccurred instead
};

struct JsOptionsPullSubscribeAsync {
    NatsTimeout timeout{}; ///< Auto-unsubscribe timeout
    int maxMessages = 0;   ///< Auto-unsubscribe after this many messages
    int64_t maxBytes = 0;  ///< Auto-unsubscribe after this many bytes
    bool noWait = false;   ///< Receive only messages already on server; don't wait for more
    int64_t heartbeat = 0; ///< Server heartbeat interval (ms) to detect communication failures
    int fetchSize = 0;     ///< Messages per automatic fetch request (default flow control)
    int keepAhead = 0;     ///< Pre-fetch this many messages before current request is fulfilled
    // Note: CompleteHandler is not exposed here — use a signal on PullSubscription instead
    // Note: NextHandler is not exposed here — it overrides cnats's internal fetch flow control
};

struct JsOptionsStreamInfo {
    bool deletedDetails = false;           ///< Include list of deleted message sequences
    std::optional<QString> subjectsFilter; ///< Filter subjects returned in stream state
};

struct JsOptionsStreamPurge {
    std::optional<QString> subject; ///< Subject to match against messages for the purge command
    uint64_t sequence = 0;          ///< Purge up to but not including this sequence
    uint64_t keep = 0;              ///< Number of messages to keep
};

struct JsOptionsStream {
    JsOptionsStreamPurge purge;
    JsOptionsStreamInfo info;
};

struct JsOptions {
    QString prefix = "$JS.API";                     ///< JetStream API prefix
    std::optional<QString> domain;                  ///< Changes the domain part of the JetStream API prefix
    NatsTimeout timeout{5000};                      ///< Timeout for JetStream API requests
    JsOptionsPublishAsync publishAsync;             ///< extra options for #js_PublishAsync
    JsOptionsPullSubscribeAsync pullSubscribeAsync; ///< extra options for #js_PullSubscribeAsync
    JsOptionsStream stream;                         ///< Optional stream options
};

struct JsPublishAck {
    QString stream;
    uint64_t sequence = 0;
    QString domain;
    bool duplicate = false;
};

struct JsPublishOptions {
    std::optional<NatsTimeout> timeout;         ///< Timeout for publish response; default uses context's Wait value
    std::optional<QString> msgID;               ///< Message ID used for de-duplication
    std::optional<QString> expectStream;        ///< Expected stream to respond from the publish call
    std::optional<QString> expectLastMessageID; ///< Expected last message ID in the stream
    uint64_t expectLastSequence = 0;            ///< Expected last message sequence in the stream
    uint64_t expectLastSubjectSequence = 0;     ///< Expected last message sequence for the subject in the stream
    bool expectNoMessage = false;               ///< Expected no message (sequence == 0) for the subject in the stream
};

struct JsSubOptions {
    std::optional<QString> stream;   ///< Bind subscription to this stream name
    std::optional<QString> consumer; ///< Bind to this existing consumer (must be pre-created)
    std::optional<QString> queue;    ///< Queue group name for queue subscriptions
    bool ordered = false;            ///< If true, creates an ordered ephemeral consumer
    JsConsumerConfig config;
    // Note: ManualAck is not exposed here — managed internally by the subscription type
};

struct QTNATS_EXPORT Message {
    Message() = default;
    Message(QString subject, QByteArray data) : subject{std::move(subject)}, data{std::move(data)} {}
    Message(QString subject, QByteArray data, MessageHeaders headers)
        : subject{std::move(subject)}
        , data{std::move(data)}
        , headers{std::move(headers)} {}

    explicit Message(natsMsg* msg);

    [[nodiscard]] bool isIncoming() const { return static_cast<bool>(m_natsMsg); }

    // JetStream acknowledgments
    void ack() const;

    void nack(std::optional<int64_t> delay) const; // ms
    void inProgress() const;

    void terminate() const;

private:
    // Message is copyable, but the underlying natsMsg is not. Thus, we use a shared pointer, and only delete the
    // natsMsg when the last Message referencing it is destroyed.
    // natsMsg needs to be before the headers, so if there's an issue parsing the headers, and it throws, the natsMsg
    // will still be deleted, and the natsMsg* passed in the constructor won't dangle.
    std::shared_ptr<natsMsg> m_natsMsg;

public:
    QString subject;
    QByteArray reply;
    QByteArray data;
    // NB! 1. headers are case-sensitive
    // 2. cnats does NOT preserve the order of headers
    MessageHeaders headers;

private:
    static MessageHeaders readHeaders(natsMsg* msg);
};

struct ObjStoreConfig {
    QString bucket;                              ///< Bucket name; must be unique, alphanumeric, dashes, underscores only
    std::optional<QString> description;          ///< Optional human-readable description
    std::optional<NatsDuration> ttl;             ///< Maximum age of objects (nullopt = no expiry)
    std::optional<uint64_t> maxBytes;            ///< Maximum size of the store in bytes (nullopt = unlimited)
    JsStorageType storage = JsStorageType::File; ///< Storage backend (file or memory)
    int replicas = 1;                            ///< Number of replicas in a clustered JetStream (1–5)
    std::optional<JsPlacement> placement;        ///< Cluster/tag placement constraints
    bool compression = false;                    ///< Enable stream-level compression (requires nats-server 2.10+)
    NatsMetadata metadata;                       ///< Bucket-level metadata (requires nats-server 2.10+)
};

struct ObjStoreMetaOptions {
    uint32_t chunkSize = 128 * 1024; ///< Maximum chunk size in bytes
    // Note: Link is intentionally omitted — use objStore_AddLink / objStore_AddBucketLink instead
};

struct ObjStoreMeta {
    QString name;                       ///< Object name; required and unique within the store
    std::optional<QString> description; ///< Optional human-readable description
    MessageHeaders headers;             ///< Optional user-defined headers
    NatsMetadata metadata;              ///< Optional user-supplied metadata
    ObjStoreMetaOptions opts;           ///< Additional options (e.g. chunk size)
};

struct ObjStoreInfo {
    ObjStoreMeta meta;             ///< High-level object metadata
    QString bucket;                ///< Name of the object store
    QString nuid;                  ///< Unique identifier assigned when the object was put
    uint64_t size;                 ///< Size of the object in bytes (excludes metadata)
    NatsTimePoint modTime;         ///< Last modification time
    uint32_t chunks;               ///< Number of chunks the object is split into
    std::optional<QString> digest; ///< SHA-256 digest for integrity verification (nullopt if not set)
    bool deleted;                  ///< True if the object is marked as deleted
};

struct ObjStoreOptions {
    bool showDeleted = false; ///< Include deleted objects in results
};

struct QTNATS_EXPORT Options {
    QList<QUrl> servers;
    QString user;
    QString password;
    QString token;
    QString name;
    bool secure = false;
    bool verbose = false;
    bool pedantic = false;
    bool allowReconnect = true;
    bool randomize = true; // NB! reverted option
    bool echo = true;      // NB! reverted option

    // Defaults mirror the NATS_OPTS_DEFAULT_* constants from cnats's private opts.h,
    // hardcoded here to avoid depending on that internal header.
    NatsTimeout timeout{2000};             // NATS_OPTS_DEFAULT_TIMEOUT
    int64_t pingInterval = 120000;             // NATS_OPTS_DEFAULT_PING_INTERVAL
    int maxPingsOut = 2;                       // NATS_OPTS_DEFAULT_MAX_PING_OUT
    int ioBufferSize = 32768;                  // NATS_OPTS_DEFAULT_IO_BUF_SIZE
    int maxReconnect = 60;                     // NATS_OPTS_DEFAULT_MAX_RECONNECT
    int64_t reconnectWait = 2000;              // NATS_OPTS_DEFAULT_RECONNECT_WAIT
    int reconnectBufferSize = 8 * 1024 * 1024; // NATS_OPTS_DEFAULT_RECONNECT_BUF_SIZE
    int maxPendingMessages = 65536;            // NATS_OPTS_DEFAULT_MAX_PENDING_MSGS

    // mTLS options
    QString certFile; // Client certificate file path
    QString keyFile;  // Client private key file path
    QString caFile;   // CA certificate for server verification
};

#pragma endregion

// =====================================================================================================================
// Classes.
// Owning classes are for when the lifetime of an underlying resource must be managed.
// Data structs are for simple data containers that don't manage any resources and can be freely copied.
// Both are returned from calls to the Client.
// =====================================================================================================================
#pragma region Classes

class Subscription;
class PullSubscription;
class JetStream;
class ObjectStore;

class QTNATS_EXPORT Client : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(Client)

public:
    explicit Client(QObject* parent = nullptr);

    ~Client() noexcept override;

    Client(Client&&) = delete;

    Client& operator=(Client&&) = delete;

    // General client manipulation functions.

    void connectToServer(const Options& opts);

    void connectToServer(const QUrl& address);

    void close() noexcept;

    QUrl currentServer() const;

    ConnectionStatus status() const;

    QString errorString() const;

    static QByteArray newInbox();

    natsConnection* getNatsConnection() const { return m_conn; }

    // Functions for creating durable objects that are related to the client.
    // The client owns these objects and will manage their lifetimes, so they are returned as raw pointers.

    Subscription* subscribe(const QString& subject);

    Subscription* subscribe(const QString& subject, const QString& queueGroup);

    JetStream* jetStream(const JsOptions& options = JsOptions());

    // General messaging functions.

    void publish(const Message& msg);

    Message request(const Message& msg, NatsTimeout timeout = NatsTimeout{2000});

    QFuture<Message> asyncRequest(const Message& msg, NatsTimeout timeout = NatsTimeout{2000});

    bool ping(NatsTimeout timeout = NatsTimeout{10000}) noexcept;

Q_SIGNALS:
    void errorOccurred(natsStatus error, const QString& text);

    void statusChanged(ConnectionStatus status);

private:
    natsConnection* m_conn = nullptr;
    QSemaphore semaphore;

    // Prevent QFutureInterface leak when close() destroys subscriptions
    // before asyncRequest callbacks fire.
    std::vector<std::shared_ptr<QFutureInterface<Message>>> m_pendingAsyncRequests;

    static void closedConnectionHandler(natsConnection* nc, void* closure);
};

class QTNATS_EXPORT Subscription : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(Subscription)

public:
    ~Subscription() noexcept override;

    Subscription(Subscription&&) = delete;

    Subscription& operator=(Subscription&&) = delete;

Q_SIGNALS:
    void received(Message message);

private:
    explicit Subscription(QObject* parent) : QObject(parent) {}

    natsSubscription* m_sub = nullptr;
    friend class Client;
    friend class JetStream;
};

class QTNATS_EXPORT PullSubscription : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(PullSubscription)

public:
    ~PullSubscription() noexcept override;

    PullSubscription(PullSubscription&&) = delete;

    PullSubscription& operator=(PullSubscription&&) = delete;

    QList<Message> fetch(int batch = 1, NatsTimeout timeout = NatsTimeout{5000}) const;

private:
    PullSubscription(QObject* parent) : QObject(parent) {}

    natsSubscription* m_sub = nullptr;
    friend class JetStream;
};

class QTNATS_EXPORT JetStream : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(JetStream)

public:
    ~JetStream() noexcept override;

    JetStream(JetStream&&) = delete;

    JetStream& operator=(JetStream&&) = delete;

    // General JetStream manipulation functions.

    JsPublishAck publish(const Message& msg, const JsPublishOptions& opts);

    void asyncPublish(const Message& msg, const JsPublishOptions& opts) const;

    void waitForPublishCompleted(std::optional<NatsTimeout> timeout = std::nullopt) const;

    Subscription* subscribe(const QString& subject, const QString& stream, const QString& consumer);

    PullSubscription* pullSubscribe(const QString& subject, const QString& stream, const QString& consumer);

    // General stream functions

    JsStreamInfo addStream(const JsStreamConfig& config) const;

    JsStreamInfo updateStream(const JsStreamConfig& config) const;

    void purgeStream(const QString& stream) const;

    void deleteStream(const QString& stream) const;

    JsStreamInfo getStreamInfo(const QString& stream) const;

    // General consumer functions

    JsConsumerInfo addConsumer(const QString& stream, const JsConsumerConfig& config) const;

    JsConsumerInfo updateConsumer(const QString& stream, const JsConsumerConfig& config) const;

    JsConsumerInfo getConsumerInfo(const QString& stream, const QString& consumer) const;

    void deleteConsumer(const QString& stream, const QString& consumer) const;

    JsConsumerPauseResponse pauseConsumer(
        const QString& stream,
        const QString& consumer,
        NatsTimePoint pauseUntil
    ) const;

    // Functions for creating durable objects that are related to the JetStream.
    // The JetStream owns these objects and will manage their lifetimes, so they are returned as raw pointers.

    ObjectStore* createObjectStore(const ObjStoreConfig& config);

    ObjectStore* updateObjectStore(const ObjStoreConfig& config);

    ObjectStore* getObjectStore(const QString& bucket);

    void deleteObjectStore(const QString& bucket) const;

Q_SIGNALS:
    void errorOccurred(natsStatus error, jsErrCode jsErr, const QString& text, Message msg);

private:
    explicit JetStream(QObject* parent) : QObject(parent) {}

    jsCtx* m_jsCtx = nullptr;

    JsPublishAck doPublish(const Message& msg, jsPubOptions* opts);

    void doAsyncPublish(const Message& msg, jsPubOptions* opts) const;

    friend class Client;
};

class QTNATS_EXPORT ObjectStore : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(ObjectStore)

public:
    ~ObjectStore() noexcept override;

    ObjectStore(ObjectStore&&) = delete;

    ObjectStore& operator=(ObjectStore&&) = delete;

    [[nodiscard]] ObjStoreInfo putString(const QString& name, const QString& data) const;

    [[nodiscard]] ObjStoreInfo putBytes(const QString& name, const QByteArray& data) const;

    [[nodiscard]] ObjStoreInfo putFile(const std::filesystem::path& path) const;

    [[nodiscard]] QString getString(const QString& name, const ObjStoreOptions& options) const;

    [[nodiscard]] QByteArray getBytes(const QString& name, const ObjStoreOptions& options) const;

    [[nodiscard]] ObjStoreInfo getInfo(const QString& name, const ObjStoreOptions& options) const;

    void getFile(const QString& name, const std::filesystem::path& path, const ObjStoreOptions& options) const;

private:
    explicit ObjectStore(QObject* parent) : QObject(parent) {}

    objStore* m_objStore = nullptr;

    friend class JetStream;
};

#pragma endregion
} // namespace QtNats

Q_DECLARE_METATYPE(QtNats::Message)
