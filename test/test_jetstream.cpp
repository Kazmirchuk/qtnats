/* Copyright(c) 2022 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/

#include <qtnats.h>

#include <iostream>

#include <QCoreApplication>
#include <QMetaEnum>
#include <QDir>
#include <QProcess>

#include <QtTest>

using namespace std;
using namespace QtNats;

template<typename T>
QString enumToString(T value)
{
    int castValue = static_cast<int>(value);
    return QMetaEnum::fromType<T>().valueToKey(castValue);
}

class JetStreamTestCase : public QObject
{
    Q_OBJECT
    
    QProcess natsServer;
    QProcess natsCli;

private slots:
    void initTestCase();
    void cleanupTestCase();

    void publish();
    void pullSubscribe();
    void pushSubscribe();
};

void JetStreamTestCase::initTestCase()
{
    QDir::setCurrent("../test"); //default pwd is "build"

    connect(&natsServer, &QProcess::stateChanged, [](QProcess::ProcessState newState) {
        cout << "nats-server: " << qPrintable(enumToString(newState)) << endl;
        });

    natsServer.start("nats-server", QStringList() << "-js");
    natsServer.waitForStarted();
    QTest::qWait(1000);
    
    natsCli.start("nats", QStringList() << "stream" << "add" << "--config=stream_config.json");
    natsCli.waitForFinished();

}

void JetStreamTestCase::cleanupTestCase()
{
    natsServer.close();
    natsServer.waitForFinished();
}

void JetStreamTestCase::publish()
{
    try {
        Connection c;
        c.connectToServer(QUrl("nats://localhost:4222"));
        
        auto js = c.jetStream();

        connect(js, &JetStream::errorOccurred, [](natsStatus error, jsErrCode jsErr, const QString& text, const Message& msg) {
            cout << "JS error: " << qPrintable(text) << endl;
        });

        auto ack = js->publish(Message("test.1", "HI"));

        QCOMPARE(ack.stream, QString("MY_STREAM"));
        QCOMPARE(ack.sequence, 1);

        for (int i = 0; i < 5; i++) {
            js->asyncPublish(Message("test.1", "HI"), 1000);
        }
        js->waitForPublishCompleted();
    }
    catch (const QException& e) {
        QFAIL(e.what());
    }
}

void JetStreamTestCase::pullSubscribe() {
    
    try {
        Connection c;
        c.connectToServer(QUrl("nats://localhost:4222"));

        auto js = c.jetStream();
        
        natsCli.start("nats", QStringList() << "consumer" << "add" << "MY_STREAM" << "PULL_CONSUMER" << "--config=pull_consumer_config.json");
        natsCli.waitForFinished();

        natsCli.start("nats", QStringList() << "publish" << "-H" << "hdr1:val1" << "test.pull" << "hello JS");
        natsCli.waitForFinished();

        jsSubOptions subOpts;
        jsSubOptions_Init(&subOpts);
        subOpts.Stream = "MY_STREAM";
        subOpts.Consumer = "PULL_CONSUMER";

        auto sub = js->pullSubscribe("test.pull", "PULL_CONSUMER", &subOpts);

        auto msgList = sub->fetch();
        QCOMPARE(msgList.size(), 1);
        Message m = msgList[0];
        QCOMPARE(m.data, QByteArray("hello JS"));
        QCOMPARE(m.subject, QString("test.pull"));
    }
    catch (const QException& e) {
        QFAIL(e.what());
    }
}

void JetStreamTestCase::pushSubscribe() {
    
    Connection c;
    try {
        c.connectToServer(QUrl("nats://localhost:4222"));

        auto js = c.jetStream();

        natsCli.start("nats", QStringList() << "consumer" << "add" << "MY_STREAM" << "PUSH_CONSUMER" << "--config=push_consumer_config.json");
        natsCli.waitForFinished();

        natsCli.start("nats", QStringList() << "publish" << "-H" << "hdr1:val1" << "test.push" << "hello JS again");
        natsCli.waitForFinished();

        jsSubOptions subOpts;
        jsSubOptions_Init(&subOpts);
        subOpts.Stream = "MY_STREAM";
        subOpts.Consumer = "PUSH_CONSUMER";

        auto sub = js->subscribe("test.push", &subOpts);
        // can we miss a message if "connect" is not fast enough?
        // apparently, consumer's deliver_subject does not matter here
        Message msg;
        connect(sub, &Subscription::received, [&msg](const Message& message) {
            msg = message;
        });

        QTRY_COMPARE_WITH_TIMEOUT(msg.data, QByteArray("hello JS again"), 1000);
        QCOMPARE(msg.subject, QString("test.push"));
    }
    catch (const QException& e) {
        QFAIL(e.what());
    }
}

QTEST_GUILESS_MAIN(JetStreamTestCase)

#include "test_jetstream.moc"
