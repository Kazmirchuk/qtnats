/* Copyright(c) 2021-2022 Petro Kazmirchuk https://github.com/Kazmirchuk

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.You may obtain a copy of the License at http ://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.See the License for the specific language governing permissions and  limitations under the License.
*/

#include "qtnats.h"

using namespace QtNats;

//potential to move to a private header?
static void checkError(natsStatus s)
{
    if (s == NATS_OK) return;
    throw Exception(s);
}

JetStream::JetStream(Connection* natsConn) : QObject(natsConn)
{
    jsCtx* js = nullptr;
    checkError(natsConnection_JetStream(&js, natsConn->m_conn, nullptr));
    
}

JetStream::~JetStream() noexcept
{
	jsCtx_Destroy(m_jsCtx);
}
