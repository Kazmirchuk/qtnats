/* Copyright(c) 2022 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/


#include "qmlnatsplugin.h"

using namespace QtNats;

QmlNatsClient::QmlNatsClient(QObject* parent) : QObject(parent)
{

}

bool QmlNatsClient::connectToServer()
{
	m_conn = new Client(this);
	m_conn->connectToServer(QUrl(m_serverUrl));
	emit statusChanged("Connected");
	return true;
}

void QmlNatsClient::disconnectFromServer()
{
	delete m_conn;
	emit statusChanged("Disconnected");
}

QString QmlNatsClient::status() const
{
	if (m_conn)
		return "Connected";
	else
		return "Disconnected";
}

QmlNatsSubscription* QmlNatsClient::subscribe(const QString& subject)
{
	if (!m_conn)
		return nullptr;

	Subscription* sub = m_conn->subscribe(subject.toLatin1());
	return new QmlNatsSubscription(sub, this);
}

void QmlNatsClient::publish(const QString& subject, const QString& message)
{
	if (!m_conn)
		return;

	m_conn->publish(Message(subject.toLatin1(), message.toLatin1()));
}

QString QmlNatsClient::request(const QString& subject, const QString& message)
{
	if (!m_conn)
		return "";

	Message out(subject.toLatin1(), message.toLatin1());
	Message response = m_conn->request(out);
	return QString::fromLatin1(response.data);
}

QmlNatsSubscription::QmlNatsSubscription(QtNats::Subscription* s, QObject* parent):
	QObject(parent),
	m_sub(s)
{
	connect(m_sub, &Subscription::received, this, [this](const Message& message) {
		emit received(QString::fromLatin1(message.data));
	});
	m_sub->setParent(this);
}

//TODO: error logging ? https://stackoverflow.com/questions/32118682/how-to-report-errors-from-custom-qml-components
