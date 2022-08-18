/* Copyright(c) 2022 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/

#pragma once

#include <qtnats.h>

#include <QQmlEngine>

class QmlNatsSubscription;

class QmlNatsClient : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NatsClient)

    Q_DISABLE_COPY(QmlNatsClient)
    Q_PROPERTY(QString serverUrl MEMBER m_serverUrl)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    QmlNatsClient(QObject* parent = nullptr);
    QString status() const;

public slots:
    bool connectToServer();
    void disconnectFromServer();

    QmlNatsSubscription* subscribe(const QString& subject);
    void publish(const QString& subject, const QString& message);
    QString request(const QString& subject, const QString& message);

signals:
    void statusChanged(QString status);

private:
    QtNats::Client* m_conn = nullptr;
    QString m_serverUrl;
};

class QmlNatsSubscription : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(QmlNatsSubscription)
    QML_UNCREATABLE("Use NatsClient to create subscriptions")

public:
    QmlNatsSubscription(QtNats::Subscription* s, QObject* parent = nullptr);

signals:
    void received(const QString& message); //only payload

private:
    QtNats::Subscription* m_sub;
};
