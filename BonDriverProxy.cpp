/*
$ g++ -O2 -Wall -pthread -rdynamic -o BonDriverProxy BonDriverProxy.cpp -ldl
*/
#include "BonDriverProxy.h"

namespace BonDriverProxy {

#define STRICT_LOCK

static std::list<cProxyServer *> InstanceList;
static cCriticalSection Lock_Instance;

static int Init(int ac, char *av[])
{
	if (ac < 3)
		return -1;
	::strncpy(g_Host, av[1], sizeof(g_Host) - 1);
	g_Host[sizeof(g_Host) - 1] = '\0';
	g_Port = ::atoi(av[2]);
	if (ac > 3)
	{
		g_PacketFifoSize = ::atoi(av[3]);
		if (ac > 4)
			g_TsPacketBufSize = ::atoi(av[4]);
	}
	return 0;
}

cProxyServer::cProxyServer() : m_Error(m_c, m_m), m_fifoSend(m_c, m_m), m_fifoRecv(m_c, m_m)
{
	m_s = INVALID_SOCKET;
	m_hModule = NULL;
	m_pIBon = m_pIBon2 = m_pIBon3 = NULL;
	m_strBonDriver[0] = '\0';
	m_bTunerOpen = FALSE;
	m_hTsRead = 0;
	m_pTsReceiversList = NULL;
	m_pStopTsRead = NULL;
	m_pTsLock = NULL;
	m_ppos = NULL;

	pthread_mutexattr_t attr;
	::pthread_mutexattr_init(&attr);
	::pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	::pthread_mutex_init(&m_m, &attr);
	::pthread_cond_init(&m_c, NULL);
}

cProxyServer::~cProxyServer()
{
	LOCK(Lock_Instance);
	BOOL bRelease = TRUE;
	std::list<cProxyServer *>::iterator it = InstanceList.begin();
	while (it != InstanceList.end())
	{
		if (*it == this)
			InstanceList.erase(it++);
		else
		{
			if ((m_hModule != NULL) && (m_hModule == (*it)->m_hModule))
				bRelease = FALSE;
			++it;
		}
	}
	if (bRelease)
	{
		if (m_hTsRead)
		{
			*m_pStopTsRead = TRUE;
			::pthread_join(m_hTsRead, NULL);
			delete m_pTsReceiversList;
			delete m_pStopTsRead;
			delete m_pTsLock;
			delete m_ppos;
		}
		if (m_pIBon)
			m_pIBon->Release();
		if (m_hModule)
			::dlclose(m_hModule);
	}
	else
	{
		if (m_hTsRead)
		{
			LOCK(*m_pTsLock);
			it = m_pTsReceiversList->begin();
			while (it != m_pTsReceiversList->end())
			{
				if (*it == this)
				{
					m_pTsReceiversList->erase(it);
					break;
				}
				++it;
			}
			// 可能性は低いがゼロではない…
			if (m_pTsReceiversList->empty())
			{
				*m_pStopTsRead = TRUE;
				::pthread_join(m_hTsRead, NULL);
				delete m_pTsReceiversList;
				delete m_pStopTsRead;
				delete m_pTsLock;
				delete m_ppos;
			}
		}
	}

	::pthread_cond_destroy(&m_c);
	::pthread_mutex_destroy(&m_m);

	if (m_s != INVALID_SOCKET)
		::close(m_s);
}

void *cProxyServer::Reception(LPVOID pv)
{
	cProxyServer *pProxy = static_cast<cProxyServer *>(pv);
	pProxy->Process();
	delete pProxy;
	return NULL;
}

DWORD cProxyServer::Process()
{
	pthread_t hThread[2];
	if (::pthread_create(&hThread[0], NULL, cProxyServer::Sender, this))
		return 1;

	if (::pthread_create(&hThread[1], NULL, cProxyServer::Receiver, this))
	{
		m_Error.Set();
		::pthread_join(hThread[0], NULL);
		return 2;
	}

	cEvent *h[2] = { &m_Error, m_fifoRecv.GetEventHandle() };
	while (1)
	{
		DWORD dwRet = cEvent::MultipleWait(2, h);
		switch (dwRet)
		{
		case WAIT_OBJECT_0:
			goto end;

		case WAIT_OBJECT_0 + 1:
		{
#ifdef STRICT_LOCK
			LOCK(Lock_Instance);
#endif
			cPacketHolder *pPh = NULL;
			m_fifoRecv.Pop(&pPh);
			switch (pPh->GetCommand())
			{
			case eSelectBonDriver:
			{
				if (pPh->GetBodyLength() <= sizeof(char))
					makePacket(eSelectBonDriver, FALSE);
				else
				{
					BOOL bFind = FALSE;
#ifndef STRICT_LOCK
					LOCK(Lock_Instance);
#endif
					for (std::list<cProxyServer *>::iterator it = InstanceList.begin(); it != InstanceList.end(); ++it)
					{
						if (::strcmp((LPCSTR)(pPh->m_pPacket->payload), (*it)->m_strBonDriver) == 0)
						{
							bFind = TRUE;
							m_hModule = (*it)->m_hModule;
							::strcpy(m_strBonDriver, (*it)->m_strBonDriver);
							m_pIBon = (*it)->m_pIBon;	// (*it)->m_pIBonがNULLの可能性はゼロではない
							m_pIBon2 = (*it)->m_pIBon2;
							m_pIBon3 = (*it)->m_pIBon3;
							break;
						}
					}
					BOOL bSuccess;
					if (!bFind)
					{
						bSuccess = SelectBonDriver((LPCSTR)(pPh->m_pPacket->payload));
						if (bSuccess)
						{
							InstanceList.push_back(this);
							::strncpy(m_strBonDriver, (LPCSTR)(pPh->m_pPacket->payload), sizeof(m_strBonDriver) - 1);
							m_strBonDriver[sizeof(m_strBonDriver) - 1] = '\0';
						}
					}
					else
					{
						InstanceList.push_back(this);
						bSuccess = TRUE;
					}
					makePacket(eSelectBonDriver, bSuccess);
				}
				break;
			}

			case eCreateBonDriver:
			{
				if (m_pIBon == NULL)
				{
					BOOL bFind = FALSE;
					{
#ifndef STRICT_LOCK
						LOCK(Lock_Instance);
#endif
						for (std::list<cProxyServer *>::iterator it = InstanceList.begin(); it != InstanceList.end(); ++it)
						{
							if (*it == this)
								continue;
							if (m_hModule == (*it)->m_hModule)
							{
								if ((*it)->m_pIBon != NULL)
								{
									bFind = TRUE;	// ここに来るのはかなりのレアケースのハズ
									m_pIBon = (*it)->m_pIBon;
									m_pIBon2 = (*it)->m_pIBon2;
									m_pIBon3 = (*it)->m_pIBon3;
									break;
								}
								// ここに来るのは上より更にレアケース、あるいはクライアントが
								// BonDriver_Proxyを要求し、サーバ側のBonDriver_Proxyも
								// 同じサーバに対して自分自身を要求する無限ループ状態だけのハズ
								// なお、STRICT_LOCKが定義してある場合は、そもそもデッドロックを
								// 起こすので、後者の状況は発生しない
								// 無限ループ状態に関しては放置
								// 無限ループ状態以外の場合は一応リストの最後まで検索してみて、
								// それでも見つからなかったらCreateBonDriver()をやらせてみる
							}
						}
					}
					if (!bFind)
					{
						if ((CreateBonDriver() != NULL) && (m_pIBon2 != NULL))
							makePacket(eCreateBonDriver, TRUE);
						else
						{
							makePacket(eCreateBonDriver, FALSE);
							m_Error.Set();
						}
					}
					else
						makePacket(eCreateBonDriver, TRUE);
				}
				else
					makePacket(eCreateBonDriver, TRUE);
				break;
			}

			case eOpenTuner:
			{
				BOOL bFind = FALSE;
				{
#ifndef STRICT_LOCK
					LOCK(Lock_Instance);
#endif
					for (std::list<cProxyServer *>::iterator it = InstanceList.begin(); it != InstanceList.end(); ++it)
					{
						if (*it == this)
							continue;
						if ((m_pIBon != NULL) && (m_pIBon == (*it)->m_pIBon))
						{
							if ((*it)->m_bTunerOpen)
							{
								bFind = TRUE;
								m_bTunerOpen = TRUE;
								break;
							}
						}
					}
				}
				if (!bFind)
					m_bTunerOpen = OpenTuner();
				makePacket(eOpenTuner, m_bTunerOpen);
				break;
			}

			case eCloseTuner:
			{
				BOOL bFind = FALSE;
				{
#ifndef STRICT_LOCK
					LOCK(Lock_Instance);
#endif
					for (std::list<cProxyServer *>::iterator it = InstanceList.begin(); it != InstanceList.end(); ++it)
					{
						if (*it == this)
							continue;
						if ((m_pIBon != NULL) && (m_pIBon == (*it)->m_pIBon))
						{
							if ((*it)->m_bTunerOpen)
							{
								bFind = TRUE;
								break;
							}
						}
					}
				}
				if (!bFind)
				{
					if (m_hTsRead)
					{
						LOCK(*m_pTsLock);
						CloseTuner();
						*m_ppos = 0;
						m_bTunerOpen = FALSE;
					}
				}
				break;
			}

			case ePurgeTsStream:
			{
				if (m_hTsRead && m_bChannelLock)
				{
					LOCK(*m_pTsLock);
					PurgeTsStream();
					*m_ppos = 0;
					makePacket(ePurgeTsStream, TRUE);
				}
				else
					makePacket(ePurgeTsStream, FALSE);
				break;
			}

			case eRelease:
				m_Error.Set();
				break;

			case eEnumTuningSpace:
			{
				if (pPh->GetBodyLength() != sizeof(DWORD))
				{
					TCHAR c = 0;
					makePacket(eEnumTuningSpace, &c);
				}
				else
				{
					DWORD *pdw = (DWORD *)(pPh->m_pPacket->payload);
					LPCTSTR p = EnumTuningSpace(ntohl(*pdw));
					if (p)
						makePacket(eEnumTuningSpace, p);
					else
					{
						TCHAR c = 0;
						makePacket(eEnumTuningSpace, &c);
					}
				}
				break;
			}

			case eEnumChannelName:
			{
				if (pPh->GetBodyLength() != (sizeof(DWORD) * 2))
				{
					TCHAR c = 0;
					makePacket(eEnumChannelName, &c);
				}
				else
				{
					DWORD *pdw1 = (DWORD *)(pPh->m_pPacket->payload);
					DWORD *pdw2 = (DWORD *)(&(pPh->m_pPacket->payload[sizeof(DWORD)]));
					LPCTSTR p = EnumChannelName(ntohl(*pdw1), ntohl(*pdw2));
					if (p)
						makePacket(eEnumChannelName, p);
					else
					{
						TCHAR c = 0;
						makePacket(eEnumChannelName, &c);
					}
				}
				break;
			}

			case eSetChannel2:
			{
				if (pPh->GetBodyLength() != ((sizeof(DWORD) * 2) + sizeof(BYTE)))
					makePacket(eSetChannel2, (DWORD)0xff);
				else
				{
					m_bChannelLock = pPh->m_pPacket->payload[sizeof(DWORD) * 2];
					BOOL bLocked = FALSE;
					{
#ifndef STRICT_LOCK
						LOCK(Lock_Instance);
#endif
						for (std::list<cProxyServer *>::iterator it = InstanceList.begin(); it != InstanceList.end(); ++it)
						{
							if (*it == this)
								continue;
							if ((m_pIBon != NULL) && (m_pIBon == (*it)->m_pIBon))
							{
								if ((*it)->m_bChannelLock)
									bLocked = TRUE;
								if ((m_hTsRead == 0) && ((*it)->m_hTsRead != 0))
								{
									m_hTsRead = (*it)->m_hTsRead;
									m_pTsReceiversList = (*it)->m_pTsReceiversList;
									m_pStopTsRead = (*it)->m_pStopTsRead;
									m_pTsLock = (*it)->m_pTsLock;
									m_ppos = (*it)->m_ppos;
									m_pTsLock->Enter();
									m_pTsReceiversList->push_back(this);
									m_pTsLock->Leave();
								}
							}
						}
					}
					if (bLocked && !m_bChannelLock)
						makePacket(eSetChannel2, (DWORD)0x01);
					else
					{
						DWORD *pdw1 = (DWORD *)(pPh->m_pPacket->payload);
						DWORD *pdw2 = (DWORD *)(&(pPh->m_pPacket->payload[sizeof(DWORD)]));
						if (m_hTsRead)
							m_pTsLock->Enter();
						BOOL b = SetChannel(ntohl(*pdw1), ntohl(*pdw2));
						if (m_hTsRead)
							m_pTsLock->Leave();
						if (b)
						{
							makePacket(eSetChannel2, (DWORD)0x00);
							if (m_hTsRead == 0)
							{
#ifndef STRICT_LOCK
								// すぐ上で検索してるのになぜ再度検索するのかと言うと、同じBonDriverを要求している複数の
								// クライアントから、ほぼ同時のタイミングで最初のeSetChannel2をリクエストされた場合の為
								// eSetChannel2全体をまとめてロックすれば必要無くなるが、BonDriver_Proxyがロードされ、
								// それが自分自身に接続してきた場合デッドロックする事になる
								// なお、同様の理由でeCreateBonDriver, eOpenTuner, eCloseTunerのロックは実は不完全
								// しかし、自分自身への再帰接続を行わないならば完全なロックも可能
								// 実際の所、テスト用途以外で自分自身への再接続が必要になる状況と言うのはまず無いと
								// 思うので、STRICT_LOCKが定義してある場合は完全なロックを行う事にする
								// ただしそのかわりに、BonDriver_Proxyをロードし、そこからのプロキシチェーンのどこかで
								// 自分自身に再帰接続した場合はデッドロックとなるので注意
								BOOL bFind = FALSE;
								LOCK(Lock_Instance);
								for (std::list<cProxyServer *>::iterator it = InstanceList.begin(); it != InstanceList.end(); ++it)
								{
									if (*it == this)
										continue;
									if (m_pIBon == (*it)->m_pIBon)
									{
										if ((*it)->m_hTsRead != 0)
										{
											bFind = TRUE;
											m_hTsRead = (*it)->m_hTsRead;
											m_pTsReceiversList = (*it)->m_pTsReceiversList;
											m_pStopTsRead = (*it)->m_pStopTsRead;
											m_pTsLock = (*it)->m_pTsLock;
											m_ppos = (*it)->m_ppos;
											m_pTsLock->Enter();
											m_pTsReceiversList->push_back(this);
											m_pTsLock->Leave();
											break;
										}
									}
								}
								if (!bFind)
								{
#endif
									m_pTsReceiversList = new std::list<cProxyServer *>();
									m_pTsReceiversList->push_back(this);
									m_pStopTsRead = new BOOL(FALSE);
									m_pTsLock = new cCriticalSection();
									m_ppos = new DWORD(0);
									LPVOID *ppv = new LPVOID[5];
									ppv[0] = m_pIBon;
									ppv[1] = m_pTsReceiversList;
									ppv[2] = m_pStopTsRead;
									ppv[3] = m_pTsLock;
									ppv[4] = m_ppos;
									if (::pthread_create(&m_hTsRead, NULL, cProxyServer::TsReader, ppv))
									{
										m_hTsRead = 0;
										delete[] ppv;
										delete m_pTsReceiversList;
										m_pTsReceiversList = NULL;
										delete m_pStopTsRead;
										m_pStopTsRead = NULL;
										delete m_pTsLock;
										m_pTsLock = NULL;
										delete m_ppos;
										m_ppos = NULL;
										m_Error.Set();
									}
#ifndef STRICT_LOCK
								}
#endif
							}
							else
							{
								LOCK(*m_pTsLock);
								*m_ppos = 0;
							}
						}
						else
							makePacket(eSetChannel2, (DWORD)0xff);
					}
				}
				break;
			}

			case eGetTotalDeviceNum:
				makePacket(eGetTotalDeviceNum, GetTotalDeviceNum());
				break;

			case eGetActiveDeviceNum:
				makePacket(eGetActiveDeviceNum, GetActiveDeviceNum());
				break;

			case eSetLnbPower:
			{
				if (pPh->GetBodyLength() != sizeof(BYTE))
					makePacket(eSetLnbPower, FALSE);
				else
					makePacket(eSetLnbPower, SetLnbPower((BOOL)(pPh->m_pPacket->payload[0])));
				break;
			}

			default:
				break;
			}
			delete pPh;
			break;
		}

		default:
			// 何かのエラー
			m_Error.Set();
			goto end;
		}
	}
end:
	::pthread_join(hThread[0], NULL);
	::pthread_join(hThread[1], NULL);
	return 0;
}

int cProxyServer::ReceiverHelper(char *pDst, int left)
{
	int len, ret;
	fd_set rd;
	timeval tv;

	while (left > 0)
	{
		if (m_Error.IsSet())
			return -1;

		FD_ZERO(&rd);
		FD_SET(m_s, &rd);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		if ((len = ::select((int)(m_s + 1), &rd, NULL, NULL, &tv)) == SOCKET_ERROR)
		{
			ret = -2;
			goto err;
		}

		if (len == 0)
			continue;

		if ((len = ::recv(m_s, pDst, left, 0)) == SOCKET_ERROR)
		{
			ret = -3;
			goto err;
		}
		else if (len == 0)
		{
			ret = -4;
			goto err;
		}
		left -= len;
		pDst += len;
	}
	return 0;
err:
	m_Error.Set();
	return ret;
}

void *cProxyServer::Receiver(LPVOID pv)
{
	cProxyServer *pProxy = static_cast<cProxyServer *>(pv);
	DWORD &ret = pProxy->m_tRet;
	int left;
	char *p;
	cPacketHolder *pPh = NULL;

	while (1)
	{
		pPh = new cPacketHolder(16);
		left = sizeof(stPacketHead);
		p = (char *)&(pPh->m_pPacket->head);
		if (pProxy->ReceiverHelper(p, left) != 0)
		{
			ret = 201;
			goto end;
		}

		if (!pPh->IsValid())
		{
			pProxy->m_Error.Set();
			ret = 202;
			goto end;
		}

		left = (int)pPh->GetBodyLength();
		if (left == 0)
		{
			pProxy->m_fifoRecv.Push(pPh);
			continue;
		}

		if (left > 512 || left < 0)
		{
			pProxy->m_Error.Set();
			ret = 203;
			goto end;
		}

		if (left >= 16)
		{
			cPacketHolder *pTmp = new cPacketHolder(left);
			pTmp->m_pPacket->head = pPh->m_pPacket->head;
			delete pPh;
			pPh = pTmp;
		}

		p = (char *)(pPh->m_pPacket->payload);
		if (pProxy->ReceiverHelper(p, left) != 0)
		{
			ret = 204;
			goto end;
		}

		pProxy->m_fifoRecv.Push(pPh);
	}
end:
	if (pPh)
		delete pPh;
	return &ret;
}

void cProxyServer::makePacket(enumCommand eCmd, BOOL b)
{
	cPacketHolder *p = new cPacketHolder(eCmd, sizeof(BYTE));
	p->m_pPacket->payload[0] = (BYTE)b;
	m_fifoSend.Push(p);
}

void cProxyServer::makePacket(enumCommand eCmd, DWORD dw)
{
	cPacketHolder *p = new cPacketHolder(eCmd, sizeof(DWORD));
	DWORD *pos = (DWORD *)(p->m_pPacket->payload);
	*pos = htonl(dw);
	m_fifoSend.Push(p);
}

void cProxyServer::makePacket(enumCommand eCmd, LPCTSTR str)
{
	int i;
	for (i = 0; str[i]; i++);
	register size_t size = (i + 1) * sizeof(TCHAR);
	cPacketHolder *p = new cPacketHolder(eCmd, size);
	::memcpy(p->m_pPacket->payload, str, size);
	m_fifoSend.Push(p);
}

void cProxyServer::makePacket(enumCommand eCmd, BYTE *pSrc, DWORD dwSize, float fSignalLevel)
{
	register size_t size = (sizeof(DWORD) * 2) + dwSize;
	cPacketHolder *p = new cPacketHolder(eCmd, size);
	union {
		DWORD dw;
		float f;
	} u;
	u.f = fSignalLevel;
	DWORD *pos = (DWORD *)(p->m_pPacket->payload);
	*pos++ = htonl(dwSize);
	*pos++ = htonl(u.dw);
	if (dwSize > 0)
		::memcpy(pos, pSrc, dwSize);
	m_fifoSend.Push(p);
}

void *cProxyServer::Sender(LPVOID pv)
{
	cProxyServer *pProxy = static_cast<cProxyServer *>(pv);
	DWORD &ret = pProxy->m_tRet;
	cEvent *h[2] = { &(pProxy->m_Error), pProxy->m_fifoSend.GetEventHandle() };
	while (1)
	{
		DWORD dwRet = cEvent::MultipleWait(2, h);
		switch (dwRet)
		{
		case WAIT_OBJECT_0:
			ret = 101;
			goto end;

		case WAIT_OBJECT_0 + 1:
		{
			cPacketHolder *pPh = NULL;
			pProxy->m_fifoSend.Pop(&pPh);
			int left = (int)pPh->m_Size;
			char *p = (char *)(pPh->m_pPacket);
			while (left > 0)
			{
				int len = ::send(pProxy->m_s, p, left, 0);
				if (len == SOCKET_ERROR)
				{
					pProxy->m_Error.Set();
					break;
				}
				left -= len;
				p += len;
			}
			delete pPh;
			break;
		}

		default:
			// 何かのエラー
			pProxy->m_Error.Set();
			ret = 102;
			goto end;
		}
	}
end:
	return &ret;
}

void *cProxyServer::TsReader(LPVOID pv)
{
	LPVOID *ppv = static_cast<LPVOID *>(pv);
	IBonDriver *pIBon = static_cast<IBonDriver *>(ppv[0]);
	std::list<cProxyServer *> &TsReceiversList = *(static_cast<std::list<cProxyServer *> *>(ppv[1]));
	volatile BOOL &StopTsRead = *(static_cast<BOOL *>(ppv[2]));
	cCriticalSection &TsLock = *(static_cast<cCriticalSection *>(ppv[3]));
	DWORD &pos = *(static_cast<DWORD *>(ppv[4]));
	delete[] ppv;
	DWORD dwSize, dwRemain, now, before = 0;
	float fSignalLevel = 0;
	const DWORD TsPacketBufSize = g_TsPacketBufSize;
	BYTE *pBuf, *pTsBuf = new BYTE[TsPacketBufSize];
	timeval tv;
	timespec ts;

	ts.tv_sec = 0;
	ts.tv_nsec = WAIT_TIME * 1000 * 1000;

	// TS読み込みループ
	while (!StopTsRead)
	{
		::gettimeofday(&tv, NULL);
		now = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
		if ((now - before) >= 1000)
		{
			fSignalLevel = pIBon->GetSignalLevel();
			before = now;
		}
		dwSize = dwRemain = 0;
		{
			LOCK(TsLock);
			if (pIBon->GetTsStream(&pBuf, &dwSize, &dwRemain) && (dwSize != 0))
			{
				if ((pos + dwSize) < TsPacketBufSize)
				{
					::memcpy(&pTsBuf[pos], pBuf, dwSize);
					pos += dwSize;
					if (dwRemain == 0)
					{
						for (std::list<cProxyServer *>::iterator it = TsReceiversList.begin(); it != TsReceiversList.end(); ++it)
							(*it)->makePacket(eGetTsStream, pTsBuf, pos, fSignalLevel);
						pos = 0;
					}
				}
				else
				{
					DWORD left, dwLen = TsPacketBufSize - pos;
					::memcpy(&pTsBuf[pos], pBuf, dwLen);
					for (std::list<cProxyServer *>::iterator it = TsReceiversList.begin(); it != TsReceiversList.end(); ++it)
						(*it)->makePacket(eGetTsStream, pTsBuf, TsPacketBufSize, fSignalLevel);
					left = dwSize - dwLen;
					pBuf += dwLen;
					while (left > TsPacketBufSize)
					{
						for (std::list<cProxyServer *>::iterator it = TsReceiversList.begin(); it != TsReceiversList.end(); ++it)
							(*it)->makePacket(eGetTsStream, pBuf, TsPacketBufSize, fSignalLevel);
						left -= TsPacketBufSize;
						pBuf += TsPacketBufSize;
					}
					if (left != 0)
					{
						if (dwRemain == 0)
						{
							for (std::list<cProxyServer *>::iterator it = TsReceiversList.begin(); it != TsReceiversList.end(); ++it)
								(*it)->makePacket(eGetTsStream, pBuf, left, fSignalLevel);
							left = 0;
						}
						else
							::memcpy(pTsBuf, pBuf, left);
					}
					pos = left;
				}
			}
		}
		if (dwRemain == 0)
			::nanosleep(&ts, NULL);
	}
	delete[] pTsBuf;
	return NULL;
}

BOOL cProxyServer::SelectBonDriver(LPCSTR p)
{
	m_hModule = ::dlopen(p, RTLD_LAZY);
	return (m_hModule != NULL);
}

IBonDriver *cProxyServer::CreateBonDriver()
{
	if (m_hModule)
	{
		char *err;
		::dlerror();
		IBonDriver *(*f)() = (IBonDriver *(*)())::dlsym(m_hModule, "CreateBonDriver");
		if ((err = dlerror()) == NULL)
		{
			try { m_pIBon = f(); }
			catch (...) {}
			if (m_pIBon)
			{
				m_pIBon2 = dynamic_cast<IBonDriver2 *>(m_pIBon);
				m_pIBon3 = dynamic_cast<IBonDriver3 *>(m_pIBon);
			}
		}
		else
			::fprintf(stderr, "CreateBonDriver(): %s\n", err);
	}
	return m_pIBon;
}

const BOOL cProxyServer::OpenTuner(void)
{
	BOOL b = FALSE;
	if (m_pIBon)
		b = m_pIBon->OpenTuner();
	return b;
}

void cProxyServer::CloseTuner(void)
{
	if (m_pIBon)
		m_pIBon->CloseTuner();
}

void cProxyServer::PurgeTsStream(void)
{
	if (m_pIBon)
		m_pIBon->PurgeTsStream();
}

LPCTSTR cProxyServer::EnumTuningSpace(const DWORD dwSpace)
{
	LPCTSTR pStr = NULL;
	if (m_pIBon2)
		pStr = m_pIBon2->EnumTuningSpace(dwSpace);
	return pStr;
}

LPCTSTR cProxyServer::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	LPCTSTR pStr = NULL;
	if (m_pIBon2)
		pStr = m_pIBon2->EnumChannelName(dwSpace, dwChannel);
	return pStr;
}

const BOOL cProxyServer::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	BOOL b = FALSE;
	if (m_pIBon2)
		b = m_pIBon2->SetChannel(dwSpace, dwChannel);
	return b;
}

const DWORD cProxyServer::GetTotalDeviceNum(void)
{
	DWORD d = 0;
	if (m_pIBon3)
		d = m_pIBon3->GetTotalDeviceNum();
	return d;
}

const DWORD cProxyServer::GetActiveDeviceNum(void)
{
	DWORD d = 0;
	if (m_pIBon3)
		d = m_pIBon3->GetActiveDeviceNum();
	return d;
}

const BOOL cProxyServer::SetLnbPower(const BOOL bEnable)
{
	BOOL b = FALSE;
	if (m_pIBon3)
		b = m_pIBon3->SetLnbPower(bEnable);
	return b;
}

static int Listen(char *host, unsigned short port)
{
	sockaddr_in address;
	hostent *he;
	SOCKET lsock, csock;
	socklen_t len;

	lsock = ::socket(AF_INET, SOCK_STREAM, 0);
	if (lsock == INVALID_SOCKET)
		return 1;

	BOOL reuse = TRUE;
	::setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
	::memset((char *)&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = ::inet_addr(host);
	if (address.sin_addr.s_addr == INADDR_NONE)
	{
		he = ::gethostbyname(host);
		if (he == NULL)
		{
			::close(lsock);
			return 2;
		}
		::memcpy(&(address.sin_addr), *(he->h_addr_list), he->h_length);
	}
	address.sin_port = htons(port);
	if (::bind(lsock, (sockaddr *)&address, sizeof(address)) == SOCKET_ERROR)
	{
		::close(lsock);
		return 3;
	}
	if (::listen(lsock, 4) == SOCKET_ERROR)
	{
		::close(lsock);
		return 4;
	}

	while (1)
	{
		len = sizeof(address);
		csock = ::accept(lsock, (sockaddr *)&address, &len);
		if (csock == INVALID_SOCKET)
			continue;

		cProxyServer *pProxy = new cProxyServer();
		pProxy->setSocket(csock);

		pthread_t ht;
		pthread_attr_t attr;
		if (::pthread_attr_init(&attr))
			goto retry;
		if (::pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
			goto retry;
		if (::pthread_create(&ht, &attr, cProxyServer::Reception, pProxy) == 0)
			continue;
	retry:
		delete pProxy;
	}

	return 0;	// ここには来ない
}

}

int main(int argc, char *argv[])
{
	if (BonDriverProxy::Init(argc, argv) != 0)
	{
		fprintf(stderr, "usage: %s address port (packet_fifo_size tspacket_bufsize)\ne.g. $ %s 192.168.0.100 1192\n", argv[0],argv[0]);
		return 0;
	}

	if (daemon(1, 1))
	{
		perror("daemon");
		return -1;
	}

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGPIPE, &sa, NULL))
	{
		perror("sigaction");
		return -2;
	}

	int ret = BonDriverProxy::Listen(BonDriverProxy::g_Host, BonDriverProxy::g_Port);

	return ret;
}
