/* Copyright(c) 2021-2022 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/

#include <qtnats.h>

#include <iostream>
#include <memory>

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

class BasicTestCase : public QObject
{
    Q_OBJECT

    QProcess natsServer;

private slots:
    void initTestCase();
    void cleanupTestCase();

    void asyncRequest();
    void jsPublish();
};

void BasicTestCase::initTestCase()
{
    QDir::setCurrent("../test"); //default pwd is "build"

    connect(&natsServer, &QProcess::stateChanged, [](QProcess::ProcessState newState) {
        cout << "nats-server: " << qPrintable(enumToString(newState)) << endl;
    });

    natsServer.start("nats-server", QStringList() << "-js");
    natsServer.waitForStarted();
    QTest::qWait(1000);
}
void BasicTestCase::cleanupTestCase()
{
    natsServer.close();
    natsServer.waitForFinished();
}

void BasicTestCase::asyncRequest()
{
    try {
        QProcess natsCli; 
        natsCli.start("nats", QStringList() << "reply" << "foo" << "bla"); // can't use --count=1 because sometimes NATS CLI exits before flushing its reply (?!)
        natsCli.waitForStarted();
        QTest::qWait(1000);
        Connection c;
        c.connectToServer(QUrl("nats://localhost:4222"));
        auto future = c.asyncRequest(Message("foo", "bar"));
        future.waitForFinished();
        natsCli.close();
        natsCli.waitForFinished();
        QCOMPARE(future.result().data(), QByteArray("bla"));
    }
    catch (const QException& e) {
        QFAIL(e.what());
    }
}

void BasicTestCase::jsPublish()
{
    QProcess natsCli;
    bool streamCreated = false;
    try {
        
        natsCli.start("nats", QStringList() << "stream" << "add" << "--config=stream_config.json");
        natsCli.waitForFinished();

        streamCreated = true;

        // I need a consumer too to avoid the "no responders" error
        // trying to use a json config leads to: Consumer creation failed: consumer in pull mode requires ack policy (10084)
        natsCli.start("nats", QStringList() << "consumer" << "add"
            << "--ack=explicit"
            << "--deliver=all"
            << "--max-pending=100"
            << "--max-deliver=-1"
            << "--replay=instant"
            << "--pull"
            << "--wait=1s"
            << "MY_STREAM" << "MY_CONSUMER");
        natsCli.waitForFinished();

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
            js->asyncPublish(Message("test.2", "HI"), 1000);
        }
        js->waitForPublishCompleted();
        QTest::qWait(1000);
    }
    catch (const QException& e) {
        QFAIL(e.what());
    }

    if (streamCreated) {
        natsCli.start("nats", QStringList() << "stream" << "rm" << "-f" << "MY_STREAM");
        natsCli.waitForFinished();
    }

    
}

QTEST_GUILESS_MAIN(BasicTestCase)
#include "qtnats_test.moc"
