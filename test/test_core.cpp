/* Copyright(c) 2021-2022 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/

#include <qtnats.h>

#include <iostream>

#include <QCoreApplication>
#include <QMetaEnum>
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

class CoreTestCase : public QObject
{
    Q_OBJECT

    QProcess natsServer;

private slots:
    void initTestCase();
    void cleanupTestCase();

    void subscribe();
    void request();
    void asyncRequest();
};

void CoreTestCase::initTestCase()
{
    connect(&natsServer, &QProcess::stateChanged, [](QProcess::ProcessState newState) {
        cout << "nats-server: " << qPrintable(enumToString(newState)) << endl;
    });

    natsServer.start("nats-server", QStringList());
    natsServer.waitForStarted();
    QTest::qWait(1000);
}
void CoreTestCase::cleanupTestCase()
{
    natsServer.close();
    natsServer.waitForFinished();
}

void CoreTestCase::subscribe()
{
    try {
        Connection c;
        c.connectToServer(QUrl("nats://localhost:4222"));
        auto sub = c.subscribe("test_subject");
        
        QList<Message> msgList;
        connect(sub, &Subscription::received, [&msgList](const Message& message) {
            msgList += message;
        });

        c.ping(); //ensure the server received SUB

        QProcess natsCli;
        natsCli.start("nats", QStringList() << "publish" << "--count=100" << "test_subject" << "hello");
        natsCli.waitForFinished();
        
        QTest::qWait(1000);
        QCOMPARE(msgList.size(), 100);
        for (Message m : msgList) {
            QCOMPARE(m.subject, "test_subject");
            QCOMPARE(m.data, "hello");
        }
    }
    catch (const QException& e) {
        QFAIL(e.what());
    }
}

void CoreTestCase::request()
{
    QProcess responder;
    try {
        Connection c;
        c.connectToServer(QUrl("nats://localhost:4222"));
        
        responder.start("nats", QStringList() << "reply" << "service" << "bla");
        responder.waitForStarted();
        QTest::qWait(1000);

        for (int i = 0; i < 100; i++) {
            Message response = c.request(Message("service", "foo"), 1000);
            QCOMPARE(response.data, "bla");
        }
    }
    catch (const QException& e) {
        QFAIL(e.what());
    }
    responder.close();
    responder.waitForFinished();
}

void CoreTestCase::asyncRequest()
{
    QProcess responder;
    try {
        responder.start("nats", QStringList() << "reply" << "service" << "bla"); // can't use --count because sometimes NATS CLI exits before flushing its reply (?!)
        responder.waitForStarted();
        QTest::qWait(1000);
        
        Connection c;
        c.connectToServer(QUrl("nats://localhost:4222"));
        QList<QFuture<Message>> futuresList;
        for (int i = 0; i < 100; i++) {
            futuresList += c.asyncRequest(Message("service", "bar"));
        }
        QTest::qWait(2000);
        
        c.close();
        QCOMPARE(futuresList.size(), 100);

        for (QFuture<Message> f : futuresList) {
            QCOMPARE(f.isFinished(), true);
            QCOMPARE(f.result().data, "bla");
        }
    }
    catch (const QException& e) {
        QFAIL(e.what());
    }
    responder.close();
    responder.waitForFinished();
}

QTEST_GUILESS_MAIN(CoreTestCase)
#include "test_core.moc"
