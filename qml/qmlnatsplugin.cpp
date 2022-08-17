/* Copyright(c) 2022 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/


#include "qmlnatsplugin.h"

QmlNatsConnection::QmlNatsConnection(QObject* parent) : QObject(parent)
{

}

bool QmlNatsConnection::connectToServer()
{

}

void QmlNatsConnection::disconnectFromServer()
{

}

QmlNatsSubscription* QmlNatsConnection::subscribe(const QString& subject)
{
	return nullptr;
}

void QmlNatsConnection::publish(const QString& subject, const QString& message)
{

}

QString QmlNatsConnection::request(const QString& subject, const QString& message)
{
	return QString();
}

QmlNatsSubscription::QmlNatsSubscription(QtNats::Subscription* s)
{

}

QmlNatsSubscription::~QmlNatsSubscription()
{

}
//TODO: error logging ? https://stackoverflow.com/questions/32118682/how-to-report-errors-from-custom-qml-components
