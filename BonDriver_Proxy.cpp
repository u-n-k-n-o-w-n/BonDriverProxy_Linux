#include "BonDriver_Proxy.h"

namespace BonDriver_Proxy {

static BOOL IsTagMatch(const char *line, const char *tag, char **value)
{
	const int taglen = ::strlen(tag);
	const char *p;

	if (::strncmp(line, tag, taglen) != 0)
		return FALSE;
	p = line + taglen;
	while (*p == ' ' || *p == '\t')
		p++;
	if (value == NULL && *p == '\0')
		return TRUE;
	if (*p++ != '=')
		return FALSE;
	while (*p == ' ' || *p == '\t')
		p++;
	*value = const_cast<char *>(p);
	return TRUE;
}

static int Init()
{
	FILE *fp;
	char *p, buf[512];

	Dl_info info;
	if (::dladdr((void *)Init, &info) == 0)
		return -1;
	::strncpy(buf, info.dli_fname, sizeof(buf) - 8);
	buf[sizeof(buf) - 8] = '\0';
	::strcat(buf, ".conf");

	fp = ::fopen(buf, "r");
	if (fp == NULL)
		return -2;

	BOOL bHost, bPort, bBonDriver, bChannelLock, bConnectTimeOut, bUseMagicPacket;
	BOOL bTargetHost, bTargetPort, bTargetMac;
	BOOL bPacketFifoSize, bTsFifoSize, bTsPacketBufSize;
	bHost = bPort = bBonDriver = bChannelLock = bConnectTimeOut = bUseMagicPacket = FALSE;
	bTargetHost = bTargetPort = bTargetMac = FALSE;
	bPacketFifoSize = bTsFifoSize = bTsPacketBufSize = FALSE;
	while (::fgets(buf, sizeof(buf), fp))
	{
		if (buf[0] == ';')
			continue;
		p = buf + ::strlen(buf) - 1;
		while ((p >= buf) && (*p == '\r' || *p == '\n'))
			*p-- = '\0';
		if (p < buf)
			continue;
		if (!bHost && IsTagMatch(buf, "ADDRESS", &p))
		{
			::strncpy(g_Host, p, sizeof(g_Host) - 1);
			g_Host[sizeof(g_Host) - 1] = '\0';
			bHost = TRUE;
		}
		else if (!bPort && IsTagMatch(buf, "PORT", &p))
		{
			::strncpy(g_Port, p, sizeof(g_Port) - 1);
			g_Port[sizeof(g_Port) - 1] = '\0';
			bPort = TRUE;
		}
		else if (!bBonDriver && IsTagMatch(buf, "BONDRIVER", &p))
		{
			::strncpy(g_BonDriver, p, sizeof(g_BonDriver) - 1);
			g_BonDriver[sizeof(g_BonDriver) - 1] = '\0';
			bBonDriver = TRUE;
		}
		else if (!bChannelLock && IsTagMatch(buf, "CHANNEL_LOCK", &p))
		{
			g_ChannelLock = ::atoi(p);
			bChannelLock = TRUE;
		}
		else if (!bConnectTimeOut && IsTagMatch(buf, "CONNECT_TIMEOUT", &p))
		{
			g_ConnectTimeOut = ::atoi(p);
			bConnectTimeOut = TRUE;
		}
		else if (!bUseMagicPacket && IsTagMatch(buf, "USE_MAGICPACKET", &p))
		{
			g_UseMagicPacket = ::atoi(p);
			bUseMagicPacket = TRUE;
		}
		else if (!bTargetHost && IsTagMatch(buf, "TARGET_ADDRESS", &p))
		{
			::strncpy(g_TargetHost, p, sizeof(g_TargetHost) - 1);
			g_TargetHost[sizeof(g_TargetHost) - 1] = '\0';
			bTargetHost = TRUE;
		}
		else if (!bTargetPort && IsTagMatch(buf, "TARGET_PORT", &p))
		{
			::strncpy(g_TargetPort, p, sizeof(g_TargetPort) - 1);
			g_TargetPort[sizeof(g_TargetPort) - 1] = '\0';
			bTargetPort = TRUE;
		}
		else if (!bTargetMac && IsTagMatch(buf, "TARGET_MACADDRESS", &p))
		{
			char mac[32];
			::memset(mac, 0, sizeof(mac));
			::strncpy(mac, p, sizeof(mac) - 1);
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
		else if (!bPacketFifoSize && IsTagMatch(buf, "PACKET_FIFO_SIZE", &p))
		{
			g_PacketFifoSize = ::atoi(p);
			bPacketFifoSize = TRUE;
		}
		else if (!bTsFifoSize && IsTagMatch(buf, "TS_FIFO_SIZE", &p))
		{
			g_TsFifoSize = ::atoi(p);
			bTsFifoSize = TRUE;
		}
		else if (!bTsPacketBufSize && IsTagMatch(buf, "TSPACKET_BUFSIZE", &p))
		{
			g_TsPacketBufSize = ::atoi(p);
			bTsPacketBufSize = TRUE;
		}
	}
	::fclose(fp);

	if (!bHost || !bPort || !bBonDriver)
		return -3;

	if (g_UseMagicPacket)
	{
		if (!bTargetMac)
			return -4;
		if (!bTargetHost)
			::strcpy(g_TargetHost, g_Host);
		if (!bTargetPort)
			::strcpy(g_TargetPort, g_Port);
	}

	return 0;
}

cProxyClient::cProxyClient() : m_Error(m_c, m_m), m_SingleShot(m_c, m_m), m_fifoSend(m_c, m_m), m_fifoRecv(m_c, m_m), m_fifoTS(m_c, m_m)
{
	m_s = INVALID_SOCKET;
	m_LastBuf = NULL;
	m_dwBufPos = 0;
	::memset(m_pBuf, 0, sizeof(m_pBuf));
	m_bBonDriver = m_bTuner = m_bRereased = FALSE;
	m_fSignalLevel = 0;
	m_dwSpace = m_dwChannel = 0x7fffffff;	// INT_MAX
	m_hThread = 0;
//	m_iEndCount = -1;
	size_t n = 0;
	char *p = (char *)TUNER_NAME;
	for (;;)
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
			delete[] m_pBuf[i];
		TsFlush();
		delete m_LastBuf;
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
		return 1;
	}

	if (::pthread_create(&hThread[1], NULL, cProxyClient::Receiver, this))
	{
		m_Error.Set();
		::pthread_join(hThread[0], NULL);
		return 2;
	}

//	m_iEndCount = 0;
	m_SingleShot.Set();

	cEvent *h[2] = { &m_Error, m_fifoRecv.GetEventHandle() };
	for (;;)
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

						pPh->SetDeleteFlag(FALSE);
						TS_DATA *pData = new TS_DATA();
						pData->dwSize = dwSize;
						pData->pbBufHead = pPh->m_pBuf;
						pData->pbBuf = &(pPh->m_pPacket->payload[sizeof(DWORD) * 2]);
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

int cProxyClient::ReceiverHelper(char *pDst, DWORD left)
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

		if ((len = ::recv(m_s, pDst, left, 0)) <= 0)
		{
			ret = -3;
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
	DWORD left, &ret = pProxy->m_tRet;
	char *p;
	cPacketHolder *pPh = NULL;
	const DWORD MaxPacketBufSize = g_TsPacketBufSize + (sizeof(DWORD) * 2);

	for (;;)
	{
		pPh = new cPacketHolder(MaxPacketBufSize);
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

		left = pPh->GetBodyLength();
		if (left == 0)
		{
			pProxy->m_fifoRecv.Push(pPh);
			continue;
		}

		if (left > MaxPacketBufSize)
		{
			pProxy->m_Error.Set();
			ret = 203;
			goto end;
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
	for (;;)
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
		LOCK(g_Lock);
		makePacket(eSelectBonDriver, g_BonDriver);
	}
	if (m_bResEvent[ebResSelectBonDriver]->Wait(&m_Error) != WAIT_OBJECT_0)
	{
		LOCK(m_readLock);
		return m_bRes[ebResSelectBonDriver];
	}
	return FALSE;
}

BOOL cProxyClient::CreateBonDriver()
{
	makePacket(eCreateBonDriver);
	if (m_bResEvent[ebResCreateBonDriver]->Wait(&m_Error) != WAIT_OBJECT_0)
	{
		LOCK(m_readLock);
		if (m_bRes[ebResCreateBonDriver])
			m_bBonDriver = TRUE;
		return m_bRes[ebResCreateBonDriver];
	}
	return FALSE;
}

const BOOL cProxyClient::OpenTuner(void)
{
	if (!m_bBonDriver)
		return FALSE;
	makePacket(eOpenTuner);
	if (m_bResEvent[ebResOpenTuner]->Wait(&m_Error) != WAIT_OBJECT_0)
	{
		LOCK(m_readLock);
		if (m_bRes[ebResOpenTuner])
			m_bTuner = TRUE;
		return m_bRes[ebResOpenTuner];
	}
	return FALSE;
}

void cProxyClient::CloseTuner(void)
{
	if (!m_bTuner)
		return;

	makePacket(eCloseTuner);
	m_bTuner = FALSE;
	m_fSignalLevel = 0;
	m_dwSpace = m_dwChannel = 0x7fffffff;	// INT_MAX
	{
		LOCK(m_writeLock);
		m_dwBufPos = 0;
		for (int i = 0; i < 8; i++)
			delete[] m_pBuf[i];
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
			delete m_LastBuf;
			m_fifoTS.Pop(&m_LastBuf);
			*ppDst = m_LastBuf->pbBuf;
			*pdwSize = m_LastBuf->dwSize;
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
	if (m_bResEvent[ebResPurgeTsStream]->Wait(&m_Error) != WAIT_OBJECT_0)
	{
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
}

void cProxyClient::Release(void)
{
	if (m_bTuner)
		CloseTuner();
	makePacket(eRelease);
	m_bRereased = TRUE;
	{
		LOCK(g_Lock);
		g_InstanceList.remove(this);
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
	if (m_pResEvent[epResEnumTuningSpace]->Wait(&m_Error) != WAIT_OBJECT_0)
	{
		LOCK(m_readLock);
		return m_pRes[epResEnumTuningSpace];
	}
	return NULL;
}

LPCTSTR cProxyClient::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	if (!m_bTuner)
		return NULL;
	makePacket(eEnumChannelName, dwSpace, dwChannel);
	if (m_pResEvent[epResEnumChannelName]->Wait(&m_Error) != WAIT_OBJECT_0)
	{
		LOCK(m_readLock);
		return m_pRes[epResEnumChannelName];
	}
	return NULL;
}

const BOOL cProxyClient::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	if (!m_bTuner)
		goto err;
//	if ((m_dwSpace == dwSpace) && (m_dwChannel == dwChannel))
//		return TRUE;
	makePacket(eSetChannel2, dwSpace, dwChannel, g_ChannelLock);
	DWORD dw;
	if (m_dwResEvent[edwResSetChannel2]->Wait(&m_Error) != WAIT_OBJECT_0)
	{
		LOCK(m_readLock);
		dw = m_dwRes[edwResSetChannel2];
	}
	else
		dw = 0xff;
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
		return TRUE;
	default:
		break;
	}
err:
	m_fSignalLevel = 0;
	return FALSE;
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
	if (m_dwResEvent[edwResGetTotalDeviceNum]->Wait(&m_Error) != WAIT_OBJECT_0)
	{
		LOCK(m_readLock);
		return m_dwRes[edwResGetTotalDeviceNum];
	}
	return 0;
}

const DWORD cProxyClient::GetActiveDeviceNum(void)
{
	if (!m_bTuner)
		return 0;
	makePacket(eGetActiveDeviceNum);
	if (m_dwResEvent[edwResGetActiveDeviceNum]->Wait(&m_Error) != WAIT_OBJECT_0)
	{
		LOCK(m_readLock);
		return m_dwRes[edwResGetActiveDeviceNum];
	}
	return 0;
}

const BOOL cProxyClient::SetLnbPower(const BOOL bEnable)
{
	if (!m_bTuner)
		return FALSE;
	makePacket(eSetLnbPower, bEnable);
	if (m_bResEvent[ebResSetLnbPower]->Wait(&m_Error) != WAIT_OBJECT_0)
	{
		LOCK(m_readLock);
		return m_bRes[ebResSetLnbPower];
	}
	return FALSE;
}

static SOCKET Connect(char *host, char *port)
{
	addrinfo hints, *results, *rp;
	SOCKET sock;
	int i, bf;
	fd_set wd;
	timeval tv;

	::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	if (g_UseMagicPacket)
	{
		char sendbuf[128];
		::memset(sendbuf, 0xff, 6);
		for (i = 1; i <= 16; i++)
			::memcpy(&sendbuf[i * 6], g_TargetMac, 6);

		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;
		hints.ai_flags = AI_NUMERICHOST;
		if (::getaddrinfo(g_TargetHost, g_TargetPort, &hints, &results) != 0)
		{
			hints.ai_flags = 0;
			if (::getaddrinfo(g_TargetHost, g_TargetPort, &hints, &results) != 0)
				return INVALID_SOCKET;
		}

		for (rp = results; rp != NULL; rp = rp->ai_next)
		{
			sock = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
			if (sock == INVALID_SOCKET)
				continue;

			BOOL opt = TRUE;
			if (::setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&opt, sizeof(opt)) != 0)
			{
				::close(sock);
				continue;
			}

			int ret = ::sendto(sock, sendbuf, 102, 0, rp->ai_addr, (int)(rp->ai_addrlen));
			::close(sock);
			if (ret == 102)
				break;
		}
		::freeaddrinfo(results);
		if (rp == NULL)
			return INVALID_SOCKET;
	}

	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_NUMERICHOST;
	if (::getaddrinfo(host, port, &hints, &results) != 0)
	{
		hints.ai_flags = 0;
		if (::getaddrinfo(host, port, &hints, &results) != 0)
			return INVALID_SOCKET;
	}

	for (rp = results; rp != NULL; rp = rp->ai_next)
	{
		sock = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sock == INVALID_SOCKET)
			continue;

		bf = TRUE;
		::ioctl(sock, FIONBIO, &bf);
		tv.tv_sec = g_ConnectTimeOut;
		tv.tv_usec = 0;
		FD_ZERO(&wd);
		FD_SET(sock, &wd);
		::connect(sock, rp->ai_addr, (int)(rp->ai_addrlen));
		if ((i = ::select((int)(sock + 1), 0, &wd, 0, &tv)) != SOCKET_ERROR)
		{
			// タイムアウト時間が"全体の"ではなく"個々のソケットの"になるけど、とりあえずこれで
			if (i != 0)
			{
				bf = FALSE;
				::ioctl(sock, FIONBIO, &bf);
				break;
			}
		}
		::close(sock);
	}
	::freeaddrinfo(results);
	if (rp == NULL)
		return INVALID_SOCKET;

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

	if (pProxy->WaitSingleShot() == WAIT_OBJECT_0)
		goto err;

	if (!pProxy->SelectBonDriver())
		goto err;

	if (pProxy->CreateBonDriver())
	{
		LOCK(g_Lock);
		g_InstanceList.push_back(pProxy);
		return pProxy;
	}

err:
	delete pProxy;
	return NULL;
}

extern "C" BOOL SetBonDriver(LPCSTR p)
{
	LOCK(g_Lock);
	if (::strlen(p) >= sizeof(g_BonDriver))
		return FALSE;
	::strcpy(g_BonDriver, p);
	return TRUE;
}

}
