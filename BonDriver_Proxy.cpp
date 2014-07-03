/*
$ g++ -O2 -shared -fPIC -Wall -o BonDriver_Proxy.so BonDriver_Proxy.cpp -lpthread -ldl
*/
#include "BonDriver_Proxy.h"

static std::list<cProxyClient *> InstanceList;
static cCriticalSection Lock_Global;

static int Init()
{
	FILE *fp;
	char *p, buf[512];

	Dl_info info;
	if (dladdr((void *)Init, &info) != 0)
	{
		strncpy(buf, info.dli_fname, sizeof(buf) - 8);
		buf[sizeof(buf) - 8] = '\0';
		strcat(buf, ".conf");
	}
	else
		strcpy(buf, DEFAULT_CONF_NAME);

	fp = fopen(buf, "r");
	if (fp == NULL)
		return -1;

	BOOL bHost, bPort, bBonDriver, bChannelLock, bConnectTimeOut, bUseMagicPacket;
	BOOL bTargetHost, bTargetPort, bTargetMac;
	BOOL bPacketFifoSize, bTsFifoSize, bTsPacketBufSize;
	bHost = bPort = bBonDriver = bChannelLock = bConnectTimeOut = bUseMagicPacket = FALSE;
	bTargetHost = bTargetPort = bTargetMac = FALSE;
	bPacketFifoSize = bTsFifoSize = bTsPacketBufSize = FALSE;
	while (fgets(buf, sizeof(buf), fp))
	{
		if (buf[0] == ';')
			continue;
		p = buf + strlen(buf) - 1;
		while (*p == '\r' || *p == '\n')
			*p-- = '\0';
		if (!bHost && (strncmp(buf, "ADDRESS=", 8) == 0))
		{
			p = &buf[8];
			while (*p == ' ' || *p == '\t')
				p++;
			strncpy(g_Host, p, sizeof(g_Host) - 1);
			g_Host[sizeof(g_Host) - 1] = '\0';
			bHost = TRUE;
		}
		else if (!bPort && (strncmp(buf, "PORT=", 5) == 0))
		{
			p = &buf[5];
			while (*p == ' ' || *p == '\t')
				p++;
			g_Port = atoi(p);
			bPort = TRUE;
		}
		else if (!bBonDriver && (strncmp(buf, "BONDRIVER=", 10) == 0))
		{
			p = &buf[10];
			while (*p == ' ' || *p == '\t')
				p++;
			strncpy(g_BonDriver, p, sizeof(g_BonDriver) - 1);
			g_BonDriver[sizeof(g_BonDriver) - 1] = '\0';
			bBonDriver = TRUE;
		}
		else if (!bChannelLock && (strncmp(buf, "CHANNEL_LOCK=", 13) == 0))
		{
			p = &buf[13];
			while (*p == ' ' || *p == '\t')
				p++;
			g_ChannelLock = atoi(p);
			bChannelLock = TRUE;
		}
		else if (!bConnectTimeOut && (strncmp(buf, "CONNECT_TIMEOUT=", 16) == 0))
		{
			p = &buf[16];
			while (*p == ' ' || *p == '\t')
				p++;
			g_ConnectTimeOut = atoi(p);
			bConnectTimeOut = TRUE;
		}
		else if (!bUseMagicPacket && (strncmp(buf, "USE_MAGICPACKET=", 16) == 0))
		{
			p = &buf[16];
			while (*p == ' ' || *p == '\t')
				p++;
			g_UseMagicPacket = atoi(p);
			bUseMagicPacket = TRUE;
		}
		else if (!bTargetHost && (strncmp(buf, "TARGET_ADDRESS=", 15) == 0))
		{
			p = &buf[15];
			while (*p == ' ' || *p == '\t')
				p++;
			strncpy(g_TargetHost, p, sizeof(g_TargetHost) - 1);
			g_TargetHost[sizeof(g_TargetHost) - 1] = '\0';
			bTargetHost = TRUE;
		}
		else if (!bTargetPort && (strncmp(buf, "TARGET_PORT=", 12) == 0))
		{
			p = &buf[12];
			while (*p == ' ' || *p == '\t')
				p++;
			g_TargetPort = atoi(p);
			bTargetPort = TRUE;
		}
		else if (!bTargetMac && (strncmp(buf, "TARGET_MACADDRESS=", 18) == 0))
		{
			p = &buf[18];
			while (*p == ' ' || *p == '\t')
				p++;
			char mac[32];
			memset(mac, 0, sizeof(mac));
			strncpy(mac, p, sizeof(mac) - 1);
			BOOL bErr = FALSE;
			for (int i = 0; i < 6 && !bErr; i++)
			{
				BYTE b = 0;
				p = &mac[i * 3];
				for (int j = 0; j < 2 && !bErr; j++)
				{
					if ('0' <= *p && *p <= '9')
						b = b * 0x10 + (*p - '0');
					else if ('A' <= *p && *p <= 'F')
						b = b * 0x10 + (*p - 'A' + 10);
					else if ('a' <= *p && *p <= 'f')
						b = b * 0x10 + (*p - 'a' + 10);
					else
						bErr = TRUE;
					p++;
				}
				g_TargetMac[i] = b;
			}
			if (!bErr)
				bTargetMac = TRUE;
		}
		else if (!bPacketFifoSize && (strncmp(buf, "PACKET_FIFO_SIZE=", 17) == 0))
		{
			p = &buf[17];
			while (*p == ' ' || *p == '\t')
				p++;
			g_PacketFifoSize = atoi(p);
			bPacketFifoSize = TRUE;
		}
		else if (!bTsFifoSize && (strncmp(buf, "TS_FIFO_SIZE=", 13) == 0))
		{
			p = &buf[13];
			while (*p == ' ' || *p == '\t')
				p++;
			g_TsFifoSize = atoi(p);
			bTsFifoSize = TRUE;
		}
		else if (!bTsPacketBufSize && (strncmp(buf, "TSPACKET_BUFSIZE=", 17) == 0))
		{
			p = &buf[17];
			while (*p == ' ' || *p == '\t')
				p++;
			g_TsPacketBufSize = atoi(p);
			bTsPacketBufSize = TRUE;
		}
	}
	fclose(fp);

	if (!bHost || !bPort || !bBonDriver)
		return -2;

	if (g_UseMagicPacket)
	{
		if (!bTargetMac)
			return -3;
		if (!bTargetHost)
			strcpy(g_TargetHost, g_Host);
		if (!bTargetPort)
			g_TargetPort = g_Port;
	}

	return 0;
}

cProxyClient::cProxyClient() : m_Error(m_c, m_m), m_SingleShot(m_c, m_m), m_fifoSend(m_c, m_m), m_fifoRecv(m_c, m_m), m_fifoTS(m_c, m_m)
{
	m_s = INVALID_SOCKET;
	m_LastBuff = NULL;
	m_dwBufPos = 0;
	::memset(m_pBuf, 0, sizeof(m_pBuf));
	m_bBonDriver = m_bTuner = m_bRereased = FALSE;
	m_fSignalLevel = 0;
	m_dwSpace = m_dwChannel = 0xff;
	m_hThread = 0;
//	m_iEndCount = -1;
	size_t n = 0;
	char *p = (char *)TUNER_NAME;
	while (1)
	{
		m_TunerName[n++] = *p;
		m_TunerName[n++] = '\0';
		if ((*p++ == '\0') || (n > (sizeof(m_TunerName) - 2)))
			break;
	}
	m_TunerName[sizeof(m_TunerName) - 2] = '\0';
	m_TunerName[sizeof(m_TunerName) - 1] = '\0';

	pthread_mutexattr_t attr;
	::pthread_mutexattr_init(&attr);
	::pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	::pthread_mutex_init(&m_m, &attr);
	::pthread_cond_init(&m_c, NULL);

	m_SingleShot.SetAutoReset(TRUE);

	int i;
	for (i = 0; i < ebResNum; i++)
	{
		m_bResEvent[i] = new cEvent(m_c, m_m);
		m_bResEvent[i]->SetAutoReset(TRUE);
	}
	for (i = 0; i < edwResNum; i++)
	{
		m_dwResEvent[i] = new cEvent(m_c, m_m);
		m_dwResEvent[i]->SetAutoReset(TRUE);
	}
	for (i = 0; i < epResNum; i++)
	{
		m_pResEvent[i] = new cEvent(m_c, m_m);
		m_pResEvent[i]->SetAutoReset(TRUE);
	}
}

cProxyClient::~cProxyClient()
{
	if (!m_bRereased)
	{
		if (m_bTuner)
			CloseTuner();
		makePacket(eRelease);
	}

	m_Error.Set();

//	if (m_iEndCount != -1)
//		SleepLock(3);

	if (m_hThread != 0)
		::pthread_join(m_hThread, NULL);

	int i;
	{
		LOCK(m_writeLock);
		for (i = 0; i < 8; i++)
		{
			if (m_pBuf[i] != NULL)
				delete[] m_pBuf[i];
		}
		::memset(m_pBuf, 0, sizeof(m_pBuf));
		TsFlush();
		if (m_LastBuff != NULL)
		{
			delete m_LastBuff;
			m_LastBuff = NULL;
		}
	}

	for (i = 0; i < ebResNum; i++)
		delete m_bResEvent[i];
	for (i = 0; i < edwResNum; i++)
		delete m_dwResEvent[i];
	for (i = 0; i < epResNum; i++)
		delete m_pResEvent[i];

	::pthread_cond_destroy(&m_c);
	::pthread_mutex_destroy(&m_m);

	if (m_s != INVALID_SOCKET)
		::close(m_s);
}

void *cProxyClient::ProcessEntry(LPVOID pv)
{
	cProxyClient *pProxy = static_cast<cProxyClient *>(pv);
	DWORD &ret = pProxy->m_tRet;
	ret = pProxy->Process();
//	pProxy->m_iEndCount++;
	return &ret;
}

DWORD cProxyClient::Process()
{
	pthread_t hThread[2];
	if (::pthread_create(&hThread[0], NULL, cProxyClient::Sender, this))
	{
		m_Error.Set();
		m_SingleShot.Set();
		return 1;
	}

	if (::pthread_create(&hThread[1], NULL, cProxyClient::Receiver, this))
	{
		m_Error.Set();
		::pthread_join(hThread[0], NULL);
		m_SingleShot.Set();
		return 2;
	}

//	m_iEndCount = 0;
	m_SingleShot.Set();

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
			int idx;
			cPacketHolder *pPh = NULL;
			m_fifoRecv.Pop(&pPh);
			switch (pPh->GetCommand())
			{
			case eSelectBonDriver:
				idx = ebResSelectBonDriver;
				goto bres;
			case eCreateBonDriver:
				idx = ebResCreateBonDriver;
				goto bres;
			case eOpenTuner:
				idx = ebResOpenTuner;
				goto bres;
			case ePurgeTsStream:
				idx = ebResPurgeTsStream;
				goto bres;
			case eSetLnbPower:
				idx = ebResSetLnbPower;
			bres:
			{
				LOCK(m_readLock);
				if (pPh->GetBodyLength() != sizeof(BYTE))
					m_bRes[idx] = FALSE;
				else
					m_bRes[idx] = pPh->m_pPacket->payload[0];
				m_bResEvent[idx]->Set();
				break;
			}

			case eGetTsStream:
				if (pPh->GetBodyLength() >= (sizeof(DWORD) * 2))
				{
					DWORD *pdw = (DWORD *)(pPh->m_pPacket->payload);
					DWORD dwSize = ntohl(*pdw);
					// 変なパケットは廃棄(正規のサーバに繋いでいる場合は来る事はないハズ)
					if ((pPh->GetBodyLength() - (sizeof(DWORD) * 2)) == dwSize)
					{
						union {
							DWORD dw;
							float f;
						} u;
						pdw = (DWORD *)(&(pPh->m_pPacket->payload[sizeof(DWORD)]));
						u.dw = ntohl(*pdw);
						m_fSignalLevel = u.f;

						TS_DATA *pData = new TS_DATA();
						pData->dwSize = dwSize;
						pData->pbBuff = new BYTE[dwSize];
						::memcpy(pData->pbBuff, &(pPh->m_pPacket->payload[sizeof(DWORD) * 2]), dwSize);
						m_fifoTS.Push(pData);
					}
				}
				break;

			case eEnumTuningSpace:
				idx = epResEnumTuningSpace;
				goto pres;
			case eEnumChannelName:
				idx = epResEnumChannelName;
			pres:
			{
				LOCK(m_writeLock);
				if (m_dwBufPos >= 8)
					m_dwBufPos = 0;
				if (m_pBuf[m_dwBufPos])
					delete[] m_pBuf[m_dwBufPos];
				if (pPh->GetBodyLength() == sizeof(TCHAR))
					m_pBuf[m_dwBufPos] = NULL;
				else
				{
					DWORD dw = pPh->GetBodyLength();
					m_pBuf[m_dwBufPos] = (TCHAR *)(new BYTE[dw]);
					::memcpy(m_pBuf[m_dwBufPos], pPh->m_pPacket->payload, dw);
				}
				{
					LOCK(m_readLock);
					m_pRes[idx] = m_pBuf[m_dwBufPos++];
					m_pResEvent[idx]->Set();
				}
				break;
			}

			case eSetChannel2:
				idx = edwResSetChannel2;
				goto dwres;
			case eGetTotalDeviceNum:
				idx = edwResGetTotalDeviceNum;
				goto dwres;
			case eGetActiveDeviceNum:
				idx = edwResGetActiveDeviceNum;
			dwres:
			{
				LOCK(m_readLock);
				if (pPh->GetBodyLength() != sizeof(DWORD))
					m_dwRes[idx] = 0;
				else
				{
					DWORD *pdw = (DWORD *)(pPh->m_pPacket->payload);
					m_dwRes[idx] = ntohl(*pdw);
				}
				m_dwResEvent[idx]->Set();
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
//	SleepLock(2);
	::pthread_join(hThread[0], NULL);
	::pthread_join(hThread[1], NULL);
	return 0;
}

int cProxyClient::ReceiverHelper(char *pDst, int left)
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

void *cProxyClient::Receiver(LPVOID pv)
{
	cProxyClient *pProxy = static_cast<cProxyClient *>(pv);
	DWORD &ret = pProxy->m_tRet;
	int left;
	char *p;
	cPacketHolder *pPh = NULL;
	const DWORD TsPacketBufSize = g_TsPacketBufSize;

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

		if ((!pPh->IsTS() && (left > 512)) || left < 0)
		{
			pProxy->m_Error.Set();
			ret = 203;
			goto end;
		}

		if (left >= 16)
		{
			if ((DWORD)left > (TsPacketBufSize + (sizeof(DWORD) * 2)))
			{
				pProxy->m_Error.Set();
				ret = 204;
				goto end;
			}
			cPacketHolder *pTmp = new cPacketHolder(left);
			pTmp->m_pPacket->head = pPh->m_pPacket->head;
			delete pPh;
			pPh = pTmp;
		}

		p = (char *)(pPh->m_pPacket->payload);
		if (pProxy->ReceiverHelper(p, left) != 0)
		{
			ret = 205;
			goto end;
		}

		pProxy->m_fifoRecv.Push(pPh);
	}
end:
	if (pPh)
		delete pPh;
//	pProxy->m_iEndCount++;
	return &ret;
}

void cProxyClient::makePacket(enumCommand eCmd)
{
	cPacketHolder *p = new cPacketHolder(eCmd, 0);
	m_fifoSend.Push(p);
}

void cProxyClient::makePacket(enumCommand eCmd, LPCSTR str)
{
	register size_t size = (::strlen(str) + 1);
	cPacketHolder *p = new cPacketHolder(eCmd, size);
	::memcpy(p->m_pPacket->payload, str, size);
	m_fifoSend.Push(p);
}

void cProxyClient::makePacket(enumCommand eCmd, BOOL b)
{
	cPacketHolder *p = new cPacketHolder(eCmd, sizeof(BYTE));
	p->m_pPacket->payload[0] = (BYTE)b;
	m_fifoSend.Push(p);
}

void cProxyClient::makePacket(enumCommand eCmd, DWORD dw)
{
	cPacketHolder *p = new cPacketHolder(eCmd, sizeof(DWORD));
	DWORD *pos = (DWORD *)(p->m_pPacket->payload);
	*pos = htonl(dw);
	m_fifoSend.Push(p);
}

void cProxyClient::makePacket(enumCommand eCmd, DWORD dw1, DWORD dw2)
{
	cPacketHolder *p = new cPacketHolder(eCmd, sizeof(DWORD) * 2);
	DWORD *pos = (DWORD *)(p->m_pPacket->payload);
	*pos++ = htonl(dw1);
	*pos = htonl(dw2);
	m_fifoSend.Push(p);
}

void cProxyClient::makePacket(enumCommand eCmd, DWORD dw1, DWORD dw2, BOOL b)
{
	cPacketHolder *p = new cPacketHolder(eCmd, (sizeof(DWORD) * 2) + sizeof(BYTE));
	DWORD *pos = (DWORD *)(p->m_pPacket->payload);
	*pos++ = htonl(dw1);
	*pos++ = htonl(dw2);
	*(BYTE *)pos = (BYTE)b;
	m_fifoSend.Push(p);
}

void *cProxyClient::Sender(LPVOID pv)
{
	cProxyClient *pProxy = static_cast<cProxyClient *>(pv);
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
//	pProxy->m_iEndCount++;
	return &ret;
}

BOOL cProxyClient::SelectBonDriver()
{
	{
		LOCK(Lock_Global);
		makePacket(eSelectBonDriver, g_BonDriver);
	}
	m_bResEvent[ebResSelectBonDriver]->Wait();
	{
		LOCK(m_readLock);
		return m_bRes[ebResSelectBonDriver];
	}
}

BOOL cProxyClient::CreateBonDriver()
{
	makePacket(eCreateBonDriver);
	m_bResEvent[ebResCreateBonDriver]->Wait();
	{
		LOCK(m_readLock);
		if (m_bRes[ebResCreateBonDriver])
			m_bBonDriver = TRUE;
		return m_bRes[ebResCreateBonDriver];
	}
}

const BOOL cProxyClient::OpenTuner(void)
{
	if (!m_bBonDriver)
		return FALSE;
	makePacket(eOpenTuner);
	m_bResEvent[ebResOpenTuner]->Wait();
	{
		LOCK(m_readLock);
		if (m_bRes[ebResOpenTuner])
			m_bTuner = TRUE;
		return m_bRes[ebResOpenTuner];
	}
}

void cProxyClient::CloseTuner(void)
{
	if (!m_bTuner)
		return;

	makePacket(eCloseTuner);
	m_bTuner = FALSE;
	m_fSignalLevel = 0;
	m_dwSpace = m_dwChannel = 0xff;
	{
		LOCK(m_writeLock);
		m_dwBufPos = 0;
		for (int i = 0; i < 8; i++)
		{
			if (m_pBuf[i] != NULL)
				delete[] m_pBuf[i];
		}
		::memset(m_pBuf, 0, sizeof(m_pBuf));
	}
}

const BOOL cProxyClient::SetChannel(const BYTE bCh)
{
	return TRUE;
}

const float cProxyClient::GetSignalLevel(void)
{
	return m_fSignalLevel;
}

const DWORD cProxyClient::WaitTsStream(const DWORD dwTimeOut)
{
	if (!m_bTuner)
		return WAIT_ABANDONED;
	if (m_fifoTS.Size() != 0)
		return WAIT_OBJECT_0;
	else
		return WAIT_TIMEOUT;	// 手抜き
}

const DWORD cProxyClient::GetReadyCount(void)
{
	if (!m_bTuner)
		return 0;
	return (DWORD)m_fifoTS.Size();
}

const BOOL cProxyClient::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	if (!m_bTuner)
		return FALSE;
	BYTE *pSrc;
	if (GetTsStream(&pSrc, pdwSize, pdwRemain))
	{
		if (*pdwSize)
			::memcpy(pDst, pSrc, *pdwSize);
		return TRUE;
	}
	return FALSE;
}

const BOOL cProxyClient::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	if (!m_bTuner)
		return FALSE;

	BOOL b;
	{
		LOCK(m_writeLock);
		if (m_fifoTS.Size() != 0)
		{
			if (m_LastBuff != NULL)
			{
				delete m_LastBuff;
				m_LastBuff = NULL;
			}
			m_fifoTS.Pop(&m_LastBuff);
			*ppDst = m_LastBuff->pbBuff;
			*pdwSize = m_LastBuff->dwSize;
			*pdwRemain = (DWORD)m_fifoTS.Size();
			b = TRUE;
		}
		else
		{
			*pdwSize = 0;
			*pdwRemain = 0;
			b = FALSE;
		}
	}
	return b;
}

void cProxyClient::PurgeTsStream(void)
{
	if (!m_bTuner)
		return;
	makePacket(ePurgeTsStream);
	m_bResEvent[ebResPurgeTsStream]->Wait();
	BOOL b;
	{
		LOCK(m_readLock);
		b = m_bRes[ebResPurgeTsStream];
	}
	if (b)
	{
		LOCK(m_writeLock);
		TsFlush();
	}
}

void cProxyClient::Release(void)
{
	if (m_bTuner)
		CloseTuner();
	makePacket(eRelease);
	m_bRereased = TRUE;
	{
		LOCK(Lock_Global);
		InstanceList.remove(this);
	}
	delete this;
}

LPCTSTR cProxyClient::GetTunerName(void)
{
	return (LPCTSTR)m_TunerName;
}

const BOOL cProxyClient::IsTunerOpening(void)
{
	return FALSE;
}

LPCTSTR cProxyClient::EnumTuningSpace(const DWORD dwSpace)
{
	if (!m_bTuner)
		return NULL;
	makePacket(eEnumTuningSpace, dwSpace);
	m_pResEvent[epResEnumTuningSpace]->Wait();
	{
		LOCK(m_readLock);
		return m_pRes[epResEnumTuningSpace];
	}
}

LPCTSTR cProxyClient::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	if (!m_bTuner)
		return NULL;
	makePacket(eEnumChannelName, dwSpace, dwChannel);
	m_pResEvent[epResEnumChannelName]->Wait();
	{
		LOCK(m_readLock);
		return m_pRes[epResEnumChannelName];
	}
}

const BOOL cProxyClient::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	if (!m_bTuner)
		return FALSE;
	if ((m_dwSpace == dwSpace) && (m_dwChannel == dwChannel))
		return TRUE;
	makePacket(eSetChannel2, dwSpace, dwChannel, g_ChannelLock);
	m_dwResEvent[edwResSetChannel2]->Wait();
	DWORD dw;
	{
		LOCK(m_readLock);
		dw = m_dwRes[edwResSetChannel2];
	}
	BOOL b;
	switch (dw)
	{
	case 0x00:	// 成功
	{
		LOCK(m_writeLock);
		TsFlush();
		m_dwSpace = dwSpace;
		m_dwChannel = dwChannel;
	}
	case 0x01:	// fall-through / チャンネルロックされてる
		b = TRUE;
		break;
	default:
		b = FALSE;
		break;
	}
	return b;
}

const DWORD cProxyClient::GetCurSpace(void)
{
	return m_dwSpace;
}

const DWORD cProxyClient::GetCurChannel(void)
{
	return m_dwChannel;
}

const DWORD cProxyClient::GetTotalDeviceNum(void)
{
	if (!m_bTuner)
		return 0;
	makePacket(eGetTotalDeviceNum);
	m_dwResEvent[edwResGetTotalDeviceNum]->Wait();
	{
		LOCK(m_readLock);
		return m_dwRes[edwResGetTotalDeviceNum];
	}
}

const DWORD cProxyClient::GetActiveDeviceNum(void)
{
	if (!m_bTuner)
		return 0;
	makePacket(eGetActiveDeviceNum);
	m_dwResEvent[edwResGetActiveDeviceNum]->Wait();
	{
		LOCK(m_readLock);
		return m_dwRes[edwResGetActiveDeviceNum];
	}
}

const BOOL cProxyClient::SetLnbPower(const BOOL bEnable)
{
	if (!m_bTuner)
		return FALSE;
	makePacket(eSetLnbPower, bEnable);
	m_bResEvent[ebResSetLnbPower]->Wait();
	{
		LOCK(m_readLock);
		return m_bRes[ebResSetLnbPower];
	}
}

static SOCKET Connect(char *host, unsigned short port)
{
	sockaddr_in server;
	hostent *he;
	SOCKET sock;
	int i, bf;
	fd_set wd;
	timeval tv;

	if (g_UseMagicPacket)
	{
		char sendbuf[128];
		memset(sendbuf, 0xff, 6);
		for (i = 1; i <= 16; i++)
			memcpy(&sendbuf[i * 6], g_TargetMac, 6);

		sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock == INVALID_SOCKET)
			return INVALID_SOCKET;

		BOOL opt = TRUE;
		if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&opt, sizeof(opt)) != 0)
		{
			close(sock);
			return INVALID_SOCKET;
		}

		memset((char *)&server, 0, sizeof(server));
		server.sin_family = AF_INET;
		server.sin_addr.s_addr = inet_addr(g_TargetHost);
		if (server.sin_addr.s_addr == INADDR_NONE)
		{
			he = gethostbyname(g_TargetHost);
			if (he == NULL)
			{
				close(sock);
				return INVALID_SOCKET;
			}
			memcpy(&(server.sin_addr), *(he->h_addr_list), he->h_length);
		}
		server.sin_port = htons(g_TargetPort);
		int ret = sendto(sock, sendbuf, 102, 0, (sockaddr *)&server, sizeof(server));
		close(sock);
		if (ret != 102)
			return INVALID_SOCKET;
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
		return INVALID_SOCKET;
	memset((char *)&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = inet_addr(host);
	if (server.sin_addr.s_addr == INADDR_NONE)
	{
		he = gethostbyname(host);
		if (he == NULL)
		{
			close(sock);
			return INVALID_SOCKET;
		}
		memcpy(&(server.sin_addr), *(he->h_addr_list), he->h_length);
	}
	server.sin_port = htons(port);
	bf = TRUE;
	ioctl(sock, FIONBIO, &bf);
	tv.tv_sec = g_ConnectTimeOut;
	tv.tv_usec = 0;
	FD_ZERO(&wd);
	FD_SET(sock, &wd);
	connect(sock, (sockaddr *)&server, sizeof(server));
	if ((i = select((int)(sock + 1), 0, &wd, 0, &tv)) == SOCKET_ERROR)
	{
		close(sock);
		return INVALID_SOCKET;
	}
	if (i == 0)
	{
		close(sock);
		return INVALID_SOCKET;
	}
	bf = FALSE;
	ioctl(sock, FIONBIO, &bf);
	return sock;
}

extern "C" IBonDriver *CreateBonDriver()
{
	if (Init() != 0)
		return NULL;

	SOCKET s = Connect(g_Host, g_Port);
	if (s == INVALID_SOCKET)
		return NULL;

	cProxyClient *pProxy = new cProxyClient();
	pProxy->setSocket(s);
	pthread_t ht;
	if (::pthread_create(&ht, NULL, cProxyClient::ProcessEntry, pProxy))
		goto err;
	pProxy->setThreadHandle(ht);

	pProxy->WaitSingleShot();
	if (pProxy->IsError())
		goto err;

	if (!pProxy->SelectBonDriver())
		goto err;

	if (pProxy->CreateBonDriver())
	{
		LOCK(Lock_Global);
		InstanceList.push_back(pProxy);
		return pProxy;
	}

err:
	delete pProxy;
	return NULL;
}

extern "C" BOOL SetBonDriver(LPCSTR p)
{
	LOCK(Lock_Global);
	if (strlen(p) >= sizeof(g_BonDriver))
		return FALSE;
	strcpy(g_BonDriver, p);
	return TRUE;
}
