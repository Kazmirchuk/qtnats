/* Copyright(c) 2022 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/

#pragma once

#include <qtnats.h>

#include <QObject>

class QmlNatsSubscription;

class QmlNatsConnection : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(QmlNatsConnection)
    Q_PROPERTY(QString serverUrl MEMBER m_serverUrl)

public:
    QmlNatsConnection(QObject* parent = nullptr);

public slots:
    bool connectToServer();
    void disconnectFromServer();

    QmlNatsSubscription* subscribe(const QString& subject);
    void publish(const QString& subject, const QString& message);
    QString request(const QString& subject, const QString& message);

private:
    QtNats::Connection m_conn;
    QString m_serverUrl;
};

class QmlNatsSubscription : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(QmlNatsSubscription)

public:
    QmlNatsSubscription(QtNats::Subscription* s);
    ~QmlNatsSubscription();

signals:
    void received(const QString& subject, const QString& reply, const QByteArray& data);

private:
    QtNats::Subscription* m_sub;
};
