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
};

void BasicTestCase::initTestCase()
{
    connect(&natsServer, &QProcess::stateChanged, [](QProcess::ProcessState newState) {
        cout << "nats-server: " << qPrintable(enumToString(newState)) << endl;
    });

    natsServer.start("nats-server", QStringList());
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
        auto o = Options().servers(QUrl("nats://localhost:4222")).build();
        c.connectToServer(o);
        auto future = c.asyncRequest("foo", "bar");
        future.waitForFinished();
        natsCli.close();
        natsCli.waitForFinished();
        QCOMPARE(future.result().data(), QByteArray("bla"));
    }
    catch (const QException& e) {
        QFAIL(e.what());
    }
}

QTEST_GUILESS_MAIN(BasicTestCase)
#include "qtnats_test.moc"
