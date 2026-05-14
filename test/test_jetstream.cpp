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

#include <qtnats/qtnats.h>

#include <iostream>

#include <QCoreApplication>
#include <QMetaEnum>
#include <QDir>
#include <QProcess>

#include <QtTest>

using namespace QtNats;

template<typename T>
QString enumToString(T value) {
    int castValue = static_cast<int>(value);
    return QMetaEnum::fromType<T>().valueToKey(castValue);
}

class JetStreamTestCase : public QObject {
    Q_OBJECT

    QProcess natsServer;
    QProcess natsCli;

    std::unique_ptr<Client> client;
    JetStream* js = nullptr;

private Q_SLOTS:
    void initTestCase();
    void cleanup() const;
    void cleanupTestCase();

    void streamManagement() const;
    void maxMsgsRetention() const;
    void discardPolicies() const;
    void publish();
    void pullSubscribe();
    void pushSubscribe();
    void readWriteObject();
};

void JetStreamTestCase::initTestCase() {
    connect(&natsServer, &QProcess::stateChanged, [](const QProcess::ProcessState newState) {
        std::cout << "nats-server: " << qPrintable(enumToString(newState)) << std::endl;
    });

    natsServer.start("nats-server", QStringList() << "-js");
    natsServer.waitForStarted();
    QTest::qWait(1000);

    client = std::make_unique<Client>();
    client->connectToServer(QUrl("nats://localhost:4222"));
    js = client->jetStream();

    connect(js, &JetStream::errorOccurred, [](natsStatus, jsErrCode, const QString &text, const Message&) {
        std::cout << "JS error: " << qPrintable(text) << std::endl;
    });

    JsStreamConfig config;
    config.name = "MY_STREAM";
    config.subjects = {"test.*"};
    config.retention = JsRetentionPolicy::Limits;
    config.discard = JsDiscardPolicy::Old;
    config.storage = JsStorageType::Memory;
    config.maxConsumers = std::nullopt;
    config.maxMsgs = std::nullopt;
    config.maxBytes = std::nullopt;
    config.maxAge = std::nullopt;
    config.maxMsgsPerSubject = std::nullopt;
    config.maxMsgSize = std::nullopt;

    const JsStreamInfo jsStreamInfo = js->addStream(config);
    QVERIFY(jsStreamInfo.config.name == config.name);

    natsCli.setProcessChannelMode(QProcess::ForwardedChannels);
    natsCli.start("nats", QStringList() << "stream" << "info" << config.name);
    QVERIFY2(natsCli.waitForFinished(), qPrintable(natsCli.errorString()));
    QVERIFY2(natsCli.exitCode() == 0, "nats CLI failed (see output above)");
}

void JetStreamTestCase::cleanup() const {
    // Ensure that we don't accidentally retain messages between tests.
    js->purgeStream("MY_STREAM");
}

void JetStreamTestCase::cleanupTestCase() {
    natsServer.close();
    natsServer.waitForFinished();
}

/// Verifies stream CRUD: create a stream and inspect its config, update maxMsgs, delete it, and
/// confirm that deleting a non-existent stream returns false.
void JetStreamTestCase::streamManagement() const {
    try {
        // Create
        JsStreamConfig config;
        config.name = "MGMT_STREAM";
        config.subjects = {"mgmt.>"};
        config.storage = JsStorageType::Memory;

        const auto info = js->addStream(config);
        QCOMPARE(info.config.name, config.name);
        QCOMPARE(info.config.subjects.size(), config.subjects.size());
        QCOMPARE(info.config.subjects[0], config.subjects[0]);
        QCOMPARE(info.config.storage, config.storage);

        // Update — change max messages
        config.maxMsgs = 1000;
        const auto updatedInfo = js->updateStream(config);
        QCOMPARE(updatedInfo.config.maxMsgs, config.maxMsgs);

        // Delete
        js->deleteStream("MGMT_STREAM");

        // Deleting a non-existent stream should throw
        QVERIFY_THROWS_EXCEPTION(QtNats::JetStreamException, js->deleteStream("MGMT_STREAM"));
    } catch (const QException& e) {
        QFAIL(e.what());
    }
}

/// Verifies the discard-old retention policy: a stream with maxMsgs=5 evicts the oldest messages
/// when more than 5 are published, leaving only the most recent 5 retrievable.
void JetStreamTestCase::maxMsgsRetention() const {
    try {
        // Create a stream that retains at most 5 messages
        JsStreamConfig streamConfig;
        streamConfig.name = "RETENTION_STREAM";
        streamConfig.subjects = {"retain.>"};
        streamConfig.storage = JsStorageType::Memory;
        streamConfig.maxMsgs = 5;

        const auto info = js->addStream(streamConfig);
        QCOMPARE(info.config.maxMsgs, 5);

        // Create a consumer so we can inspect what's in the stream
        JsConsumerConfig consumerConfig;
        consumerConfig.durable = "RETENTION_CONSUMER";
        consumerConfig.ackPolicy = JsAckPolicy::Explicit;
        consumerConfig.filterSubject = "retain.data";
        js->addConsumer("RETENTION_STREAM", consumerConfig);

        // Publish 10 messages — the first 5 should be evicted
        for (int i = 0; i < 10; i++) {
            js->publish(Message("retain.data", QByteArray::number(i)), {});
        }

        // Fetch all available messages — should be exactly the last 5
        const auto sub = js->pullSubscribe("retain.data", streamConfig.name, consumerConfig.durable.value());
        auto msgList = sub->fetch(10, NatsTimeout{2000});

        QCOMPARE(msgList.size(), 5);
        for (int i = 0; i < 5; i++) {
            QCOMPARE(msgList[i].data, QByteArray::number(i + 5));
            msgList[i].ack();
        }

        // Cleanup
        js->deleteConsumer(streamConfig.name, consumerConfig.durable.value());
        js->deleteStream(streamConfig.name);
    } catch (const QException& e) {
        QFAIL(e.what());
    }
}

/// Verifies both discard policies: DiscardOld evicts the oldest messages when the stream is full
/// (keeping the newest), while DiscardNew rejects new publishes once the limit is reached (keeping
/// the oldest). Both use maxMsgs=3 and verify which messages survive after publishing 5.
void JetStreamTestCase::discardPolicies() const {
    try {
        // --- DiscardOld: oldest messages are evicted ---
        {
            JsStreamConfig streamConfig;
            streamConfig.name = "DISCARD_OLD_STREAM";
            streamConfig.subjects = {"dold.>"};
            streamConfig.storage = JsStorageType::Memory;
            streamConfig.discard = JsDiscardPolicy::Old;
            streamConfig.maxMsgs = 3;

            JsConsumerConfig consumerConfig;
            consumerConfig.durable = "DOLD_CONSUMER";
            consumerConfig.ackPolicy = JsAckPolicy::None;

            js->addStream(streamConfig);
            js->addConsumer("DISCARD_OLD_STREAM", consumerConfig);

            for (int i = 1; i <= 5; i++) {
                js->publish(Message("dold.x", QByteArray::number(i)), {});
            }

            const auto oldSub = js->pullSubscribe("dold.>", streamConfig.name, consumerConfig.durable.value());
            auto oldMsgs = oldSub->fetch(10, NatsTimeout{2000});

            // Only the last 3 messages (3, 4, 5) should remain
            QCOMPARE(oldMsgs.size(), 3);
            QCOMPARE(oldMsgs[0].data, QByteArray("3"));
            QCOMPARE(oldMsgs[1].data, QByteArray("4"));
            QCOMPARE(oldMsgs[2].data, QByteArray("5"));

            js->deleteStream(streamConfig.name);
        }

        // --- DiscardNew: new publishes are rejected once full ---
        {
            JsStreamConfig streamConfig;
            streamConfig.name = "DISCARD_NEW_STREAM";
            streamConfig.subjects = {"dnew.>"};
            streamConfig.storage = JsStorageType::Memory;
            streamConfig.discard = JsDiscardPolicy::New;
            streamConfig.maxMsgs = 3;

            JsConsumerConfig consumerConfig;
            consumerConfig.durable = "DNEW_CONSUMER";
            consumerConfig.ackPolicy = JsAckPolicy::None;

            js->addStream(streamConfig);
            js->addConsumer("DISCARD_NEW_STREAM", consumerConfig);

            int published = 0;
            int rejected = 0;
            for (int i = 1; i <= 5; i++) {
                try {
                    js->publish(Message("dnew.x", QByteArray::number(i)), {});
                    published++;
                } catch (const JetStreamException&) {
                    rejected++;
                }
            }
            QCOMPARE(published, 3);
            QCOMPARE(rejected, 2);

            auto newSub = js->pullSubscribe("dnew.>", streamConfig.name, consumerConfig.durable.value());
            auto newMsgs = newSub->fetch(10, NatsTimeout{2000});

            // Only the first 3 messages (1, 2, 3) should remain
            QCOMPARE(newMsgs.size(), 3);
            QCOMPARE(newMsgs[0].data, QByteArray("1"));
            QCOMPARE(newMsgs[1].data, QByteArray("2"));
            QCOMPARE(newMsgs[2].data, QByteArray("3"));

            js->deleteStream(streamConfig.name);
        }
    } catch (const QException& e) {
        QFAIL(e.what());
    }
}

// Verifies JetStream publish: a synchronous publish returns an ack with the correct stream name
// and a positive sequence number, and five async publishes complete without error.
void JetStreamTestCase::publish() {
    try {
        auto [stream, sequence, domain, duplicate] = js->publish(Message("test.1", "HI"), {});

        QCOMPARE(stream, "MY_STREAM");

        for (int i = 0; i < 5; i++) {
            js->asyncPublish(Message("test.1", "HI"), {.timeout = NatsTimeout{1000}});
        }
        js->waitForPublishCompleted();
    } catch (const QException &e) {
        QFAIL(e.what());
    }
}

void JetStreamTestCase::pullSubscribe() {
    try {
        JsConsumerConfig config;
        config.name = "PULL_CONSUMER";
        config.deliverPolicy = JsDeliverPolicy::All;
        config.ackPolicy = JsAckPolicy::Explicit;
        config.replayPolicy = JsReplayPolicy::Instant;
        config.maxDeliver = 5;
        config.filterSubject = "test.pull";

        constexpr auto streamName = "MY_STREAM";
        const auto consumerInfo = js->addConsumer(streamName, config);
        QVERIFY(consumerInfo.name == config.name);

        natsCli.setProcessChannelMode(QProcess::ForwardedChannels);
        natsCli.start("nats", QStringList() << "consumer" << "info" << streamName << config.name.value());
        QVERIFY2(natsCli.waitForFinished(), qPrintable(natsCli.errorString()));
        QVERIFY2(natsCli.exitCode() == 0, "nats CLI failed (see output above)");

        // Publish messages with headers
        const Message pubMessage{"test.pull", "hello JS", {{"hdr1", "val1"}}};
        for (auto i = 0; i < 10; i++) {
            client->publish(pubMessage);
        }

        // Pull subscribe and fetch
        const auto sub = js->pullSubscribe("test.pull", streamName, config.name.value());
        auto msgList = sub->fetch(10);

        QCOMPARE(msgList.size(), 10);
        for (const Message m: msgList) {
            QCOMPARE(m.data, "hello JS");
            QCOMPARE(m.subject, "test.pull");
            auto val = m.headers.values("hdr1");
            QCOMPARE(val.size(), 1);
            QCOMPARE(val[0], "val1");
            m.ack();
        }

        // Cleanup consumer
        js->deleteConsumer(streamName, config.name.value());
    } catch (const QException &e) {
        QFAIL(e.what());
    }
}

/// Verifies push-based consumption: programmatically creates a durable push consumer with a deliver
/// subject, subscribes via Qt signal, publishes 10 messages, and confirms all are delivered.
void JetStreamTestCase::pushSubscribe() {
    try {
        JsConsumerConfig config;
        config.name = "PUSH_CONSUMER";
        config.deliverPolicy = JsDeliverPolicy::Last;
        config.ackPolicy = JsAckPolicy::None;
        config.replayPolicy = JsReplayPolicy::Instant;
        config.maxDeliver = 5;
        config.filterSubject = "test.push";
        config.deliverSubject = "delivery";

        constexpr auto streamName = "MY_STREAM";
        const auto consumerInfo = js->addConsumer(streamName, config);
        QVERIFY(consumerInfo.name == config.name);

        natsCli.setProcessChannelMode(QProcess::ForwardedChannels);
        natsCli.start("nats", QStringList() << "consumer" << "info" << streamName << config.name.value());
        QVERIFY2(natsCli.waitForFinished(), qPrintable(natsCli.errorString()));
        QVERIFY2(natsCli.exitCode() == 0, "nats CLI failed (see output above)");

        const auto sub = js->subscribe("test.push", streamName, config.name.value());
        // can we miss a message if "connect" is not fast enough?
        // apparently, consumer's deliver_subject does not matter here
        QList<Message> msgList;
        connect(sub, &Subscription::received, [&msgList](Message message) {
            msgList += message;
        });

        // Publish messages
        const Message pubMessage{"test.push", "hello JS again"};
        for (auto i = 0; i < 10; i++) {
            client->publish(pubMessage);
        }

        QTest::qWait(1000);

        QCOMPARE(msgList.size(), 10);
        for (const Message m: msgList) {
            QCOMPARE(m.data, "hello JS again");
            QCOMPARE(m.subject, "test.push");
        }

        // Cleanup consumer
        js->deleteConsumer(streamName, config.name.value());
    } catch (const QException &e) {
        QFAIL(e.what());
    }
}

// Writes an object to a stream and reads it back, verifying that the content is preserved.
void JetStreamTestCase::readWriteObject() {
    try {
        constexpr auto bucket = "test_bucket";

        // Initialize the object store
        const ObjectStore* objectStore = js->createObjectStore({
            .bucket = bucket,
            .description = "Test bucket for read/write object test",
        });

        // Initialize the underlying test data; we'll use the following test string for everything,
        // but we'll use all three put methods to verify them
        constexpr auto testString =
            "Oh! And it is lovely! Just beautiful. You know you are quite a decorator. It's amazing what you've done "
            "with such a modest budget. I like that boulder. That is a nice boulder. I guess you don't entertain much, "
            "do you?";

        constexpr auto asString = "as_string";
        constexpr auto asBytes = "as_bytes";
        const auto asFile = std::filesystem::temp_directory_path() / "as_file";

        // Put the object in the store using all three methods
        {
            const auto storeInfo = objectStore->putString(asString, testString);
            QVERIFY(storeInfo.size == strlen(testString));
        }
        {
            const auto storeInfo = objectStore->putBytes(asBytes, QByteArray(testString));
            QVERIFY(storeInfo.size == strlen(testString));
        }
        QString fileName;
        {
            // For putFile, we need to create a temporary file with the test content
            QFile tempFile{asFile};
            QVERIFY(tempFile.open(QIODevice::WriteOnly));
            tempFile.write(testString);
            tempFile.close();

            const auto storeInfo = objectStore->putFile(asFile);
            QVERIFY(storeInfo.size == strlen(testString));
            // putFile() doesn't guarantee a specific name.
            // In practice, it's the full path, but this is more robust.
            fileName = storeInfo.meta.name;

            // Cleanup the temporary file
            tempFile.remove();
        }

        // Validate that the object can be retrieved and matches the original content, using both the API and CLI
        natsCli.setProcessChannelMode(QProcess::ForwardedChannels);

        // We don't check the contents of the CLI output here, just that it exits successfully, since we already check
        // the contents using the API.
        {
            QVERIFY(objectStore->getString(asString, {}) == testString);

            natsCli.start("nats", QStringList() << "object" << "get" << bucket << asString << "--force");
            QVERIFY2(natsCli.waitForFinished(), qPrintable(natsCli.errorString()));
            QVERIFY2(natsCli.exitCode() == 0, "nats CLI failed (see output above)");
        }
        {
            QVERIFY(objectStore->getBytes(asBytes, {}) == testString);

            const auto storeInfo = objectStore->getInfo(asBytes, {});
            QCOMPARE(storeInfo.meta.name, QString{asBytes});
            QCOMPARE(storeInfo.bucket, QString{bucket});
            QCOMPARE(storeInfo.size, static_cast<uint64_t>(strlen(testString)));
            QVERIFY(!storeInfo.deleted);

            natsCli.start("nats", QStringList() << "object" << "get" << bucket << asBytes << "--force");
            QVERIFY2(natsCli.waitForFinished(), qPrintable(natsCli.errorString()));
            QVERIFY2(natsCli.exitCode() == 0, "nats CLI failed (see output above)");
        }
        {
            objectStore->getFile(fileName, asFile, {});
            QFile tempFile{asFile};
            QVERIFY(tempFile.open(QIODevice::ReadOnly));
            const QString fileContent = tempFile.readAll();
            tempFile.close();
            QCOMPARE(fileContent, QString{testString});

            natsCli.start("nats", QStringList() << "object" << "get" << bucket << asFile.c_str() << "--force");
            QVERIFY2(natsCli.waitForFinished(), qPrintable(natsCli.errorString()));
            QVERIFY2(natsCli.exitCode() == 0, "nats CLI failed (see output above)");
        }

        // Cleanup
        js->deleteObjectStore(bucket);
    } catch (const QException& e) {
        QFAIL(e.what());
    }
}

QTEST_GUILESS_MAIN(JetStreamTestCase)

#include "test_jetstream.moc"
