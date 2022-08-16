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
        c.ping(); //ensure the server received SUB
        Message recv;
        connect(sub, &Subscription::received, [&recv](const Message& message) {
            recv = message;
        });
        QProcess natsCli;
        natsCli.start("nats", QStringList() << "publish" << "test_subject" << "hello");
        natsCli.waitForFinished();
        
        QTRY_COMPARE_WITH_TIMEOUT(recv.subject, "test_subject", 500);
        QCOMPARE(recv.data, QByteArray("hello"));
    }
    catch (const QException& e) {
        QFAIL(e.what());
    }
}

void CoreTestCase::asyncRequest()
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
        QCOMPARE(future.result().data, QByteArray("bla"));
    }
    catch (const QException& e) {
        QFAIL(e.what());
    }
}

QTEST_GUILESS_MAIN(CoreTestCase)
#include "test_core.moc"
