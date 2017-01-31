#include "BonDriverProxyEx.h"

namespace BonDriverProxyEx {

#ifdef DEBUG
static BOOL g_bStop;	// 初期値FALSE
static void Handler(int sig)
{
	g_bStop = TRUE;
}

static void CleanUp()
{
	for (int i = 0; i < MAX_DRIVERS; i++)
	{
		if (g_ppDriver[i] == NULL)
			break;
		std::vector<stDriver> &v = DriversMap[g_ppDriver[i][0]];
		for (size_t j = 0; j < v.size(); j++)
		{
			if (v[j].hModule != NULL)
			{
				::dlclose(v[j].hModule);
				::fprintf(stderr, "[%s] unloaded\n", v[j].strBonDriver);
			}
		}
		delete[] g_ppDriver[i][0];
		delete[] g_ppDriver[i];
	}
	DriversMap.clear();
}
#endif

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

static void DeleteSpace(char *dst, const char *src)
{
	while (*src != '\0')
	{
		if (*src != ' ')
			*dst++ = *src;
		src++;
	} 
	*dst = '\0';
}

static int Init(int ac, char *av[])
{
	if (ac > 1)
	{
		if (*av[1] == '-')
		{
			if ((*(av[1] + 1) == 'h') || (*(av[1] + 1) == 'H'))
				return -1;
		}
	}

	FILE *fp;
	char *p, buf[1024], buf2[1024];

#ifdef LINUX
	ssize_t len;
	if ((len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 8)) == -1)
		return -2;
	buf[len] = '\0';
#else
	Dl_info info;
	if (::dladdr((void *)Init, &info) == 0)
		return -2;
	::strncpy(buf, info.dli_fname, sizeof(buf) - 8);
	buf[sizeof(buf) - 8] = '\0';
#endif
	::strcat(buf, ".conf");

#ifdef DEBUG
	::fprintf(stderr, "conf: [%s]\n", buf);
#endif

	fp = ::fopen(buf, "r");
	if (fp == NULL)
		return -3;

	int cntD, cntT = 0, num = 0;
	char *str, *pos, *pp[MAX_DRIVERS], **ppDriver;
	char tag[4];
	g_ppDriver[0] = NULL;
	while (::fgets(buf, sizeof(buf), fp))
	{
		if (buf[0] == ';')
			continue;
		p = buf + ::strlen(buf) - 1;
		while ((p >= buf) && (*p == '\r' || *p == '\n'))
			*p-- = '\0';
		if (p < buf)
			continue;

		// ;OPTION
		DeleteSpace(buf2, buf);
		if ((::strncmp(buf2, "ADDRESS=", 8) == 0) && (::strlen(buf2) > 8))
		{
			::strncpy(g_Host, buf2 + 8, sizeof(g_Host) - 1);
			g_Host[sizeof(g_Host) - 1] = '\0';
			continue;
		}
		if ((::strncmp(buf2, "PORT=", 5) == 0) && (::strlen(buf2) > 5))
		{
			::strncpy(g_Port, buf2 + 5, sizeof(g_Port) - 1);
			g_Port[sizeof(g_Port) - 1] = '\0';
			continue;
		}
		if ((::strncmp(buf2, "OPENTUNER_RETURN_DELAY=", 23) == 0) && (::strlen(buf2) > 23))
		{
			g_OpenTunerRetDelay = ::atoi(buf2 + 23);
			continue;
		}
		if ((::strncmp(buf2, "PACKET_FIFO_SIZE=", 17) == 0) && (::strlen(buf2) > 17))
		{
			g_PacketFifoSize = ::atoi(buf2 + 17);
			continue;
		}
		if ((::strncmp(buf2, "TSPACKET_BUFSIZE=", 17) == 0) && (::strlen(buf2) > 17))
		{
			g_TsPacketBufSize = ::atoi(buf2 + 17);
			continue;
		}

		// ;BONDRIVER
		// 00=PT-T;./BonDriver_LinuxPT-T0.so;./BonDriver_LinuxPT-T1.so
		// 01=PT-S;./BonDriver_LinuxPT-S0.so;./BonDriver_LinuxPT-S1.so
		if (num < MAX_DRIVERS)
		{
			tag[0] = (char)('0' + (num / 10));
			tag[1] = (char)('0' + (num % 10));
			tag[2] = '\0';
			num++;
			if (IsTagMatch(buf, tag, &p))
			{
				// format: GroupName;BonDriver1;BonDriver2;BonDriver3...
				// e.g.  : PT-T;./BonDriver_LinuxPT-T0.so;./BonDriver_LinuxPT-T1.so
				str = new char[::strlen(p) + 1];
				::strcpy(str, p);
				pos = pp[0] = str;
				cntD = 1;
				for (;;)
				{
					p = ::strchr(pos, ';');
					if (p)
					{
						*p = '\0';
						pos = pp[cntD++] = p + 1;
						if (cntD > (MAX_DRIVERS - 1))
							break;
					}
					else
						break;
				}
				if (cntD == 1)
				{
					delete[] str;
					continue;
				}
				ppDriver = g_ppDriver[cntT++] = new char *[cntD];
				::memcpy(ppDriver, pp, sizeof(char *) * cntD);
				std::vector<stDriver> vstDriver(cntD - 1);
				for (int i = 1; i < cntD; i++)
				{
					vstDriver[i-1].strBonDriver = ppDriver[i];
					vstDriver[i-1].hModule = NULL;
					vstDriver[i-1].bUsed = FALSE;
				}
				DriversMap[ppDriver[0]] = vstDriver;
			}
		}
	}
	::fclose(fp);
	if (cntT < MAX_DRIVERS)
		g_ppDriver[cntT] = NULL;

#ifdef DEBUG
	for (int i = 0; i < MAX_DRIVERS; i++)
	{
		if (g_ppDriver[i] == NULL)
			break;
		::fprintf(stderr, "%02d: %s\n", i, g_ppDriver[i][0]);
		std::vector<stDriver> &v = DriversMap[g_ppDriver[i][0]];
		for (size_t j = 0; j < v.size(); j++)
			::fprintf(stderr, "  : %s\n", v[j].strBonDriver);
	}
#endif

	switch (ac)
	{
		case 6:
			g_TsPacketBufSize = ::atoi(av[5]);
		case 5:
			g_PacketFifoSize = ::atoi(av[4]);
		case 4:
			g_OpenTunerRetDelay = ::atoi(av[3]);
		case 3:
			::strncpy(g_Port, av[2], sizeof(g_Port) - 1);
			g_Port[sizeof(g_Port) - 1] = '\0';
		case 2:
			::strncpy(g_Host, av[1], sizeof(g_Host) - 1);
			g_Host[sizeof(g_Host) - 1] = '\0';
		case 1:
			break;
		default:
			return -1;
	}

	return 0;
}

cProxyServerEx::cProxyServerEx() : m_Error(m_c, m_m), m_fifoSend(m_c, m_m), m_fifoRecv(m_c, m_m)
{
	m_s = INVALID_SOCKET;
	m_hModule = NULL;
	m_pIBon = m_pIBon2 = m_pIBon3 = NULL;
	m_bTunerOpen = FALSE;
	m_bChannelLock = 0;
	m_hTsRead = 0;
	m_pTsReaderArg = NULL;
	m_dwSpace = m_dwChannel = 0x7fffffff;	// INT_MAX
	m_pDriversMapKey = NULL;
	m_iDriverNo = -1;
	m_iDriverUseOrder = 0;

	pthread_mutexattr_t attr;
	::pthread_mutexattr_init(&attr);
	::pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	::pthread_mutex_init(&m_m, &attr);
	::pthread_cond_init(&m_c, NULL);
}

cProxyServerEx::~cProxyServerEx()
{
	LOCK(g_Lock);
	BOOL bRelease = TRUE;
	std::list<cProxyServerEx *>::iterator it = g_InstanceList.begin();
	while (it != g_InstanceList.end())
	{
		if (*it == this)
			g_InstanceList.erase(it++);
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
			m_pTsReaderArg->StopTsRead = TRUE;
			::pthread_join(m_hTsRead, NULL);
			delete m_pTsReaderArg;
		}
		if (m_pIBon)
			m_pIBon->Release();
		if (m_hModule)
		{
			std::vector<stDriver> &vstDriver = DriversMap[m_pDriversMapKey];
			vstDriver[m_iDriverNo].bUsed = FALSE;
			if (!g_DisableUnloadBonDriver)
			{
				::dlclose(m_hModule);
				vstDriver[m_iDriverNo].hModule = NULL;
#ifdef DEBUG
				::fprintf(stderr, "[%s] unloaded\n", vstDriver[m_iDriverNo].strBonDriver);
#endif
			}
		}
	}
	else
	{
		if (m_hTsRead)
			StopTsReceive();
	}

	::pthread_cond_destroy(&m_c);
	::pthread_mutex_destroy(&m_m);

	if (m_s != INVALID_SOCKET)
		::close(m_s);
}

void *cProxyServerEx::Reception(LPVOID pv)
{
	cProxyServerEx *pProxy = static_cast<cProxyServerEx *>(pv);
	pProxy->Process();
	delete pProxy;
	return NULL;
}

DWORD cProxyServerEx::Process()
{
	pthread_t hThread[2];
	if (::pthread_create(&hThread[0], NULL, cProxyServerEx::Sender, this))
		return 1;

	if (::pthread_create(&hThread[1], NULL, cProxyServerEx::Receiver, this))
	{
		m_Error.Set();
		::pthread_join(hThread[0], NULL);
		return 2;
	}

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
			// コマンド処理の全体をロックするので、BonDriver_Proxyをロードして自分自身に
			// 接続させるとデッドロックする
			// しかしそうしなければ困る状況と言うのは多分無いと思うので、これは仕様と言う事で
			LOCK(g_Lock);
			cPacketHolder *pPh = NULL;
			m_fifoRecv.Pop(&pPh);
#ifdef DEBUG
			{
				const char *CommandName[]={
					"eSelectBonDriver",
					"eCreateBonDriver",
					"eOpenTuner",
					"eCloseTuner",
					"eSetChannel1",
					"eGetSignalLevel",
					"eWaitTsStream",
					"eGetReadyCount",
					"eGetTsStream",
					"ePurgeTsStream",
					"eRelease",

					"eGetTunerName",
					"eIsTunerOpening",
					"eEnumTuningSpace",
					"eEnumChannelName",
					"eSetChannel2",
					"eGetCurSpace",
					"eGetCurChannel",

					"eGetTotalDeviceNum",
					"eGetActiveDeviceNum",
					"eSetLnbPower",

					"eGetClientInfo",
				};
				if (pPh->GetCommand() <= eGetClientInfo)
				{
					::fprintf(stderr, "Recieve Command : [%s]\n", CommandName[pPh->GetCommand()]);
				}
				else
				{
					::fprintf(stderr, "Illegal Command : [%d]\n", (int)(pPh->GetCommand()));
				}
			}
#endif
			switch (pPh->GetCommand())
			{
			case eSelectBonDriver:
			{
				if (pPh->GetBodyLength() <= sizeof(char))
					makePacket(eSelectBonDriver, FALSE);
				else
				{
					char *p;
					if ((p = ::strrchr((char *)(pPh->m_pPacket->payload), ':')) != NULL)
					{
						if (::strcmp(p, ":desc") == 0)	// 降順
						{
							*p = '\0';
							m_iDriverUseOrder = 1;
						}
						else if (::strcmp(p, ":asc") == 0)	// 昇順
							*p = '\0';
					}
					BOOL b = SelectBonDriver((LPCSTR)(pPh->m_pPacket->payload), 0);
					if (b)
						g_InstanceList.push_back(this);
					makePacket(eSelectBonDriver, b);
				}
				break;
			}

			case eCreateBonDriver:
			{
				if (m_pIBon == NULL)
				{
					BOOL bFind = FALSE;
					for (std::list<cProxyServerEx *>::iterator it = g_InstanceList.begin(); it != g_InstanceList.end(); ++it)
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
							// ここに来るのは上より更にレアケース
							// 一応リストの最後まで検索してみて、それでも見つからなかったら
							// CreateBonDriver()をやらせてみる
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
					for (std::list<cProxyServerEx *>::iterator it = g_InstanceList.begin(); it != g_InstanceList.end(); ++it)
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
				for (std::list<cProxyServerEx *>::iterator it = g_InstanceList.begin(); it != g_InstanceList.end(); ++it)
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
				if (!bFind)
				{
					if (m_hTsRead)
					{
						m_pTsReaderArg->StopTsRead = TRUE;
						::pthread_join(m_hTsRead, NULL);
						delete m_pTsReaderArg;
					}
					CloseTuner();
				}
				else
				{
					if (m_hTsRead)
						StopTsReceive();
				}
				m_bChannelLock = 0;
				m_dwSpace = m_dwChannel = 0x7fffffff;	// INT_MAX
				m_hTsRead = 0;
				m_pTsReaderArg = NULL;
				m_bTunerOpen = FALSE;
				break;
			}

			case ePurgeTsStream:
			{
				if (m_hTsRead)
				{
					m_pTsReaderArg->TsLock.Enter();
					if (m_pTsReaderArg->TsReceiversList.size() <= 1)
					{
						PurgeTsStream();
						m_pTsReaderArg->pos = 0;
					}
#ifdef DEBUG
					::fprintf(stderr, "ePurgeTsStream : [%d] / size[%zu]\n", (m_pTsReaderArg->TsReceiversList.size() <= 1) ? 1 : 0, m_pTsReaderArg->TsReceiversList.size());
#endif
					m_pTsReaderArg->TsLock.Leave();
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
					BYTE bChannelLock = pPh->m_pPacket->payload[sizeof(DWORD) * 2];
					DWORD *pdw1 = (DWORD *)(pPh->m_pPacket->payload);
					DWORD *pdw2 = (DWORD *)(&(pPh->m_pPacket->payload[sizeof(DWORD)]));
					DWORD dwReqSpace = ntohl(*pdw1);
					DWORD dwReqChannel = ntohl(*pdw2);
					if ((dwReqSpace == m_dwSpace) && (dwReqChannel == m_dwChannel))
					{
						// 既にリクエストされたチャンネルを選局済み
#ifdef DEBUG
						::fprintf(stderr, "** already tuned! ** : m_dwSpace[%d] / m_dwChannel[%d]\n", dwReqSpace, dwReqChannel);
#endif
						// 必ずtrueのハズだけど、一応
						if (m_hTsRead)
						{
							// このインスタンスが要求している優先度が255であった場合に
							if (bChannelLock == 0xff)
							{
								// 現在の配信リストには優先度255のインスタンスが既にいるか？
								BOOL bFind = FALSE;
								m_pTsReaderArg->TsLock.Enter();
								std::list<cProxyServerEx *>::iterator it = m_pTsReaderArg->TsReceiversList.begin();
								while (it != m_pTsReaderArg->TsReceiversList.end())
								{
									if ((*it != this) && ((*it)->m_bChannelLock == 0xff))
									{
										bFind = TRUE;
										break;
									}
									++it;
								}
								m_pTsReaderArg->TsLock.Leave();
								if (bFind)
								{
									// いた場合は、このインスタンスの優先度を暫定的に254にする
									bChannelLock = 0xfe;
									// 排他権取得待ちリストにまだ自身が含まれていなければ追加
									bFind = FALSE;
									it = m_pTsReaderArg->WaitExclusivePrivList.begin();
									while (it != m_pTsReaderArg->WaitExclusivePrivList.end())
									{
										if (*it == this)
										{
											bFind = TRUE;
											break;
										}
										++it;
									}
									if (!bFind)
										m_pTsReaderArg->WaitExclusivePrivList.push_back(this);
#ifdef DEBUG
									::fprintf(stderr, "** exclusive tuner! ** : wait-exclusivepriv-list size[%zu] / added[%d]\n", m_pTsReaderArg->WaitExclusivePrivList.size(), bFind ? 0 : 1);
#endif
								}
							}
						}
						m_bChannelLock = bChannelLock;
						makePacket(eSetChannel2, (DWORD)0x00);
					}
					else
					{
						BOOL bSuccess;
						BOOL bLocked = FALSE;
						BOOL bShared = FALSE;
						BOOL bSetChannel = FALSE;
						cProxyServerEx *pHavePriv = NULL;
						for (std::list<cProxyServerEx *>::iterator it = g_InstanceList.begin(); it != g_InstanceList.end(); ++it)
						{
							if (*it == this)
								continue;
							// ひとまず現在のインスタンスが共有されているかどうかを確認しておく
							if ((m_pIBon != NULL) && (m_pIBon == (*it)->m_pIBon))
								bShared = TRUE;

							// 対象BonDriver群の中でチューナをオープンしているもの
							if (m_pDriversMapKey == (*it)->m_pDriversMapKey && (*it)->m_pIBon != NULL && (*it)->m_bTunerOpen)
							{
								// かつクライアントからの要求と同一チャンネルを選択しているもの
								if ((*it)->m_dwSpace == dwReqSpace && (*it)->m_dwChannel == dwReqChannel)
								{
									// 今クライアントがオープンしているチューナに関して
									if (m_pIBon != NULL)
									{
										BOOL bModule = FALSE;
										BOOL bIBon = FALSE;
										BOOL bTuner = FALSE;
										for (std::list<cProxyServerEx *>::iterator it2 = g_InstanceList.begin(); it2 != g_InstanceList.end(); ++it2)
										{
											if (*it2 == this)
												continue;
											if (m_hModule == (*it2)->m_hModule)
											{
												bModule = TRUE;	// モジュール使用者有り
												if (m_pIBon == (*it2)->m_pIBon)
												{
													bIBon = TRUE;	// インスタンス使用者有り
													if ((*it2)->m_bTunerOpen)
													{
														bTuner = TRUE;	// チューナ使用者有り
														break;
													}
												}
											}
										}

										// チューナ使用者無しならクローズ
										if (!bTuner)
										{
											if (m_hTsRead)
											{
												m_pTsReaderArg->StopTsRead = TRUE;
												::pthread_join(m_hTsRead, NULL);
												//m_hTsRead = 0;
												delete m_pTsReaderArg;
												//m_pTsReaderArg = NULL;
											}
											CloseTuner();
											//m_bTunerOpen = FALSE;
											// かつインスタンス使用者も無しならインスタンスリリース
											if (!bIBon)
											{
												m_pIBon->Release();
												// m_pIBon = NULL;
												// かつモジュール使用者も無しならモジュールリリース
												if (!bModule)
												{
													std::vector<stDriver> &vstDriver = DriversMap[m_pDriversMapKey];
													vstDriver[m_iDriverNo].bUsed = FALSE;
													if (!g_DisableUnloadBonDriver)
													{
														::dlclose(m_hModule);
														// m_hModule = NULL;
														vstDriver[m_iDriverNo].hModule = NULL;
#ifdef DEBUG
														::fprintf(stderr, "[%s] unloaded\n", vstDriver[m_iDriverNo].strBonDriver);
#endif
													}
												}
											}
										}
										else	// 他にチューナ使用者有りの場合
										{
											// 現在TSストリーム配信中ならその配信対象リストから自身を削除
											if (m_hTsRead)
												StopTsReceive();
										}
									}

									// このインスタンスが要求している優先度が255であった場合に
									if (bChannelLock == 0xff)
									{
										// 切り替え先チューナに対して優先度255のインスタンスが既にいるか？
										for (std::list<cProxyServerEx *>::iterator it2 = g_InstanceList.begin(); it2 != g_InstanceList.end(); ++it2)
										{
											if (*it2 == this)
												continue;
											if ((*it2)->m_pIBon == (*it)->m_pIBon)
											{
												if ((*it2)->m_bChannelLock == 0xff)
												{
													// いた場合は、このインスタンスの優先度を暫定的に254にする
													// (そうしないと、優先度255のインスタンスもチャンネル変更できなくなる為)
													bChannelLock = 0xfe;
													pHavePriv = *it2;
													break;
												}
											}
										}
									}

									// インスタンス切り替え
									m_hModule = (*it)->m_hModule;
									m_iDriverNo = (*it)->m_iDriverNo;
									m_pIBon = (*it)->m_pIBon;
									m_pIBon2 = (*it)->m_pIBon2;
									m_pIBon3 = (*it)->m_pIBon3;
									m_bTunerOpen = TRUE;
									m_hTsRead = (*it)->m_hTsRead;	// この時点でもNULLの可能性はゼロではない
									m_pTsReaderArg = (*it)->m_pTsReaderArg;
									if (m_hTsRead)
									{
										m_pTsReaderArg->TsLock.Enter();
										m_pTsReaderArg->TsReceiversList.push_back(this);
										m_pTsReaderArg->TsLock.Leave();
									}
#ifdef DEBUG
									::fprintf(stderr, "** found! ** : m_hModule[%p] / m_iDriverNo[%d] / m_pIBon[%p]\n", m_hModule, m_iDriverNo, m_pIBon);
									::fprintf(stderr, "             : m_dwSpace[%d] / m_dwChannel[%d] / m_bChannelLock[%d]\n", dwReqSpace, dwReqChannel, bChannelLock);
#endif
									// このインスタンスの優先度が下げられた場合
									if (pHavePriv != NULL)
									{
										if (m_hTsRead)
										{
											// 排他権取得待ちリストにまだ自身が含まれていなければ追加
											BOOL bFind = FALSE;
											std::list<cProxyServerEx *>::iterator it2 = m_pTsReaderArg->WaitExclusivePrivList.begin();
											while (it2 != m_pTsReaderArg->WaitExclusivePrivList.end())
											{
												if (*it2 == this)
												{
													bFind = TRUE;
													break;
												}
												++it2;
											}
											if (!bFind)
												m_pTsReaderArg->WaitExclusivePrivList.push_back(this);
#ifdef DEBUG
											::fprintf(stderr, "** exclusive tuner! ** : wait-exclusivepriv-list size[%zu] / added[%d]\n", m_pTsReaderArg->WaitExclusivePrivList.size(), bFind ? 0 : 1);
#endif
										}
										else
										{
											// この処理の意図は少し下の同じ処理のコメント参照
											pHavePriv->m_bChannelLock = 0;
											bChannelLock = 0xff;
										}
									}
									goto ok;	// これは酷い
								}
							}
						}

						// 同一チャンネルを使用中のチューナは見つからず、現在のチューナは共有されていたら
						if (bShared)
						{
							// 出来れば未使用、無理ならなるべくロックされてないチューナを選択して、
							// 一気にチューナオープン状態にまで持って行く
							if (SelectBonDriver(m_pDriversMapKey, bChannelLock))
							{
								if (m_pIBon == NULL)
								{
									// 未使用チューナがあった
									if ((CreateBonDriver() == NULL) || (m_pIBon2 == NULL))
									{
										makePacket(eSetChannel2, (DWORD)0xff);
										m_Error.Set();
										break;
									}
								}
								if (!m_bTunerOpen)
								{
									m_bTunerOpen = OpenTuner();
									if (!m_bTunerOpen)
									{
										makePacket(eSetChannel2, (DWORD)0xff);
										m_Error.Set();
										break;
									}
								}
							}
							else
							{
								makePacket(eSetChannel2, (DWORD)0xff);
								m_Error.Set();
								break;
							}

							// 使用チューナのチャンネルロック状態確認
							for (std::list<cProxyServerEx *>::iterator it = g_InstanceList.begin(); it != g_InstanceList.end(); ++it)
							{
								if (*it == this)
									continue;
								if (m_pIBon == (*it)->m_pIBon)
								{
									if ((*it)->m_bChannelLock > bChannelLock)
										bLocked = TRUE;
									else if ((*it)->m_bChannelLock == 0xff)
									{
										// 対象チューナに対して優先度255のインスタンスが既にいる状態で、このインスタンスが
										// 要求している優先度も255の場合、このインスタンスの優先度を暫定的に254にする
										// (そうしないと、優先度255のインスタンスもチャンネル変更できなくなる為)
										bChannelLock = 0xfe;
										bLocked = TRUE;
										pHavePriv = *it;
									}
									if (bLocked)
										break;
								}
							}
							// このインスタンスの優先度が下げられた場合
							if (pHavePriv != NULL)
							{
								if (m_hTsRead)
								{
									// 排他権取得待ちリストにまだ自身が含まれていなければ追加
									BOOL bFind = FALSE;
									std::list<cProxyServerEx *>::iterator it = m_pTsReaderArg->WaitExclusivePrivList.begin();
									while (it != m_pTsReaderArg->WaitExclusivePrivList.end())
									{
										if (*it == this)
										{
											bFind = TRUE;
											break;
										}
										++it;
									}
									if (!bFind)
										m_pTsReaderArg->WaitExclusivePrivList.push_back(this);
#ifdef DEBUG
									::fprintf(stderr, "** exclusive tuner! ** : wait-exclusivepriv-list size[%zu] / added[%d]\n", m_pTsReaderArg->WaitExclusivePrivList.size(), bFind ? 0 : 1);
#endif
								}
								else
								{
									// このインスタンスの優先度が下げられたが、排他権を持っているインスタンスへの配信が
									// 開始されていない場合は、そのインスタンスから排他権を奪う
									// こうする事が挙動として望ましいのかどうかは微妙だが、そもそもここに来るのは、
									// 当該インスタンスでのSetChannel()の失敗後、何もせずに接続だけ続けている状態であり、
									// 可能性としてはゼロではないものの、かなりのレアケースに限られるはず
									pHavePriv->m_bChannelLock = 0;
									bChannelLock = 0xff;
								}
							}
						}
#ifdef DEBUG
						::fprintf(stderr, "eSetChannel2 : bShared[%d] / bLocked[%d]\n", bShared, bLocked);
						::fprintf(stderr, "             : dwReqSpace[%d] / dwReqChannel[%d] / m_bChannelLock[%d]\n", dwReqSpace, dwReqChannel, bChannelLock);
#endif
						if (bLocked)
						{
							// ロックされてる時は単純にロックされてる事を通知
							// この場合クライアントアプリへのSetChannel()の戻り値は成功になる
							// (おそらく致命的な問題にはならない)
							makePacket(eSetChannel2, (DWORD)0x01);
						}
						else
						{
							if (m_hTsRead)
								m_pTsReaderArg->TsLock.Enter();
							bSuccess = SetChannel(dwReqSpace, dwReqChannel);
							if (m_hTsRead)
							{
								// 一旦ロックを外すとチャンネル変更前のデータが送信されない事を保証できなくなる為、
								// チャンネル変更前のデータの破棄とCNRの更新指示はここで行う
								if (bSuccess)
								{
									// 同一チャンネルを使用中のチューナが見つからなかった場合は、このリクエストで
									// インスタンスの切り替えが発生していたとしても、この時点ではどうせチャンネルが
									// 変更されているので、未送信バッファを破棄しても別に問題にはならないハズ
									m_pTsReaderArg->pos = 0;
									m_pTsReaderArg->ChannelChanged = TRUE;
								}
								m_pTsReaderArg->TsLock.Leave();
							}
							if (bSuccess)
							{
								bSetChannel = TRUE;
							ok:
								m_dwSpace = dwReqSpace;
								m_dwChannel = dwReqChannel;
								m_bChannelLock = bChannelLock;
								makePacket(eSetChannel2, (DWORD)0x00);
								if (m_hTsRead == 0)
								{
									m_pTsReaderArg = new stTsReaderArg();
									m_pTsReaderArg->TsReceiversList.push_back(this);
									m_pTsReaderArg->pIBon = m_pIBon;
									if (::pthread_create(&m_hTsRead, NULL, cProxyServerEx::TsReader, m_pTsReaderArg))
									{
										m_hTsRead = 0;
										delete m_pTsReaderArg;
										m_pTsReaderArg = NULL;
										m_Error.Set();
									}
								}
								if (bSetChannel)
								{
									// SetChannel()が行われた場合は、同一BonDriverインスタンスを使用しているインスタンスの保持チャンネルを変更
									for (std::list<cProxyServerEx *>::iterator it = g_InstanceList.begin(); it != g_InstanceList.end(); ++it)
									{
										if (*it == this)
											continue;
										if (m_pIBon == (*it)->m_pIBon)
										{
											(*it)->m_dwSpace = dwReqSpace;
											(*it)->m_dwChannel = dwReqChannel;
											// 対象インスタンスがまだ一度もSetChannel()を行っていなかった場合
											if ((*it)->m_hTsRead == 0)
											{
												// 強制的に配信開始
												(*it)->m_bTunerOpen = TRUE;
												(*it)->m_hTsRead = m_hTsRead;
												(*it)->m_pTsReaderArg = m_pTsReaderArg;
												if (m_hTsRead)
												{
													m_pTsReaderArg->TsLock.Enter();
													m_pTsReaderArg->TsReceiversList.push_back(*it);
													m_pTsReaderArg->TsLock.Leave();
												}
											}
										}
									}
								}
							}
							else
								makePacket(eSetChannel2, (DWORD)0xff);
						}
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

			case eGetClientInfo:
			{
				union {
					sockaddr_storage ss;
					sockaddr_in si4;
					sockaddr_in6 si6;
				};
				char addr[INET6_ADDRSTRLEN], buf[2048], info[4096], *p, *exinfo;
				int port, len, num = 0;
				size_t left, size;
				socklen_t slen;
				p = info;
				p[0] = '\0';
				left = size = sizeof(info);
				exinfo = NULL;
				std::list<cProxyServerEx *>::iterator it = g_InstanceList.begin();
				while (it != g_InstanceList.end())
				{
					slen = sizeof(ss);
					if (::getpeername((*it)->m_s, (sockaddr *)&ss, &slen) == 0)
					{
						if (ss.ss_family == AF_INET)
						{
							// IPv4
							::inet_ntop(AF_INET, &(si4.sin_addr), addr, sizeof(addr));
							port = ntohs(si4.sin_port);
						}
						else
						{
							// IPv6
							::inet_ntop(AF_INET6, &(si6.sin6_addr), addr, sizeof(addr));
							port = ntohs(si6.sin6_port);
						}
					}
					else
					{
						::strcpy(addr, "unknown host...");
						port = 0;
					}
					std::vector<stDriver> &vstDriver = DriversMap[(*it)->m_pDriversMapKey];
					len = ::sprintf(buf, "%02d: [%s]:[%d] / [%s][%s] / space[%u] ch[%u]\n", num, addr, port, (*it)->m_pDriversMapKey, vstDriver[(*it)->m_iDriverNo].strBonDriver, (*it)->m_dwSpace, (*it)->m_dwChannel);
					if ((size_t)len >= left)
					{
						left += size;
						size *= 2;
						if (exinfo != NULL)
						{
							char *bp = exinfo;
							exinfo = new char[size];
							::strcpy(exinfo, bp);
							delete[] bp;
						}
						else
						{
							exinfo = new char[size];
							::strcpy(exinfo, info);
						}
						p = exinfo + ::strlen(exinfo);
					}
					::strcpy(p, buf);
					p += len;
					left -= len;
					num++;
					++it;
				}
				if (exinfo != NULL)
				{
					size = (p - exinfo) + 1;
					p = exinfo;
				}
				else
				{
					size = (p - info) + 1;
					p = info;
				}
				cPacketHolder *ph = new cPacketHolder(eGetClientInfo, size);
				::memcpy(ph->m_pPacket->payload, p, size);
				m_fifoSend.Push(ph);
				if (exinfo != NULL)
					delete[] exinfo;
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

int cProxyServerEx::ReceiverHelper(char *pDst, DWORD left)
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

void *cProxyServerEx::Receiver(LPVOID pv)
{
	cProxyServerEx *pProxy = static_cast<cProxyServerEx *>(pv);
	char *p;
	DWORD left, &ret = pProxy->m_tRet;
	cPacketHolder *pPh = NULL;

	for (;;)
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

		left = pPh->GetBodyLength();
		if (left == 0)
		{
			pProxy->m_fifoRecv.Push(pPh);
			continue;
		}

		if (left > 16)
		{
			if (left > 512)
			{
				pProxy->m_Error.Set();
				ret = 203;
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
			ret = 204;
			goto end;
		}

		pProxy->m_fifoRecv.Push(pPh);
	}
end:
	delete pPh;
	return &ret;
}

void cProxyServerEx::makePacket(enumCommand eCmd, BOOL b)
{
	cPacketHolder *p = new cPacketHolder(eCmd, sizeof(BYTE));
	p->m_pPacket->payload[0] = (BYTE)b;
	m_fifoSend.Push(p);
}

void cProxyServerEx::makePacket(enumCommand eCmd, DWORD dw)
{
	cPacketHolder *p = new cPacketHolder(eCmd, sizeof(DWORD));
	DWORD *pos = (DWORD *)(p->m_pPacket->payload);
	*pos = htonl(dw);
	m_fifoSend.Push(p);
}

void cProxyServerEx::makePacket(enumCommand eCmd, LPCTSTR str)
{
	int i;
	for (i = 0; str[i]; i++);
	register size_t size = (i + 1) * sizeof(TCHAR);
	cPacketHolder *p = new cPacketHolder(eCmd, size);
	::memcpy(p->m_pPacket->payload, str, size);
	m_fifoSend.Push(p);
}

void cProxyServerEx::makePacket(enumCommand eCmd, BYTE *pSrc, DWORD dwSize, float fSignalLevel)
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

void *cProxyServerEx::Sender(LPVOID pv)
{
	cProxyServerEx *pProxy = static_cast<cProxyServerEx *>(pv);
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
	return &ret;
}

void *cProxyServerEx::TsReader(LPVOID pv)
{
	stTsReaderArg *pArg = static_cast<stTsReaderArg *>(pv);
	IBonDriver *pIBon = pArg->pIBon;
	volatile BOOL &StopTsRead = pArg->StopTsRead;
	volatile BOOL &ChannelChanged = pArg->ChannelChanged;
	DWORD &pos = pArg->pos;
	std::list<cProxyServerEx *> &TsReceiversList = pArg->TsReceiversList;
	cCriticalSection &TsLock = pArg->TsLock;
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
		dwSize = dwRemain = 0;
		{
			LOCK(TsLock);
			::gettimeofday(&tv, NULL);
			now = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
			if (((now - before) >= 1000) || ChannelChanged)
			{
				fSignalLevel = pIBon->GetSignalLevel();
				before = now;
				ChannelChanged = FALSE;
			}
			if (pIBon->GetTsStream(&pBuf, &dwSize, &dwRemain) && (dwSize != 0))
			{
				if ((pos + dwSize) < TsPacketBufSize)
				{
					::memcpy(&pTsBuf[pos], pBuf, dwSize);
					pos += dwSize;
					if (dwRemain == 0)
					{
						for (std::list<cProxyServerEx *>::iterator it = TsReceiversList.begin(); it != TsReceiversList.end(); ++it)
							(*it)->makePacket(eGetTsStream, pTsBuf, pos, fSignalLevel);
						pos = 0;
					}
				}
				else
				{
					DWORD left, dwLen = TsPacketBufSize - pos;
					::memcpy(&pTsBuf[pos], pBuf, dwLen);
					for (std::list<cProxyServerEx *>::iterator it = TsReceiversList.begin(); it != TsReceiversList.end(); ++it)
						(*it)->makePacket(eGetTsStream, pTsBuf, TsPacketBufSize, fSignalLevel);
					left = dwSize - dwLen;
					pBuf += dwLen;
					while (left >= TsPacketBufSize)
					{
						for (std::list<cProxyServerEx *>::iterator it = TsReceiversList.begin(); it != TsReceiversList.end(); ++it)
							(*it)->makePacket(eGetTsStream, pBuf, TsPacketBufSize, fSignalLevel);
						left -= TsPacketBufSize;
						pBuf += TsPacketBufSize;
					}
					if (left != 0)
					{
						if (dwRemain == 0)
						{
							for (std::list<cProxyServerEx *>::iterator it = TsReceiversList.begin(); it != TsReceiversList.end(); ++it)
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

void cProxyServerEx::StopTsReceive()
{
	// このメソッドは必ず、
	// 1. グローバルなインスタンスロック中
	// 2. かつ、TS受信中(m_hTsRead != 0)
	// の2つを満たす状態で呼び出す事
	m_pTsReaderArg->TsLock.Enter();
	std::list<cProxyServerEx *>::iterator it = m_pTsReaderArg->TsReceiversList.begin();
	while (it != m_pTsReaderArg->TsReceiversList.end())
	{
		if (*it == this)
		{
			m_pTsReaderArg->TsReceiversList.erase(it);
			break;
		}
		++it;
	}
	m_pTsReaderArg->TsLock.Leave();

	// このインスタンスはチャンネル排他権を持っているか？
	if (m_bChannelLock == 0xff)
	{
		// 持っていた場合は、排他権取得待ちのインスタンスは存在しているか？
		if (m_pTsReaderArg->WaitExclusivePrivList.size() > 0)
		{
			// 存在する場合は、リスト先頭のインスタンスに排他権を引き継ぎ、リストから削除
			cProxyServerEx *p = m_pTsReaderArg->WaitExclusivePrivList.front();
			m_pTsReaderArg->WaitExclusivePrivList.pop_front();
			p->m_bChannelLock = 0xff;
		}
	}
	else
	{
		// 持っていない場合は、排他権取得待ちリストに自身が含まれているかもしれないので削除
		m_pTsReaderArg->WaitExclusivePrivList.remove(this);
	}

	// 自分が最後の受信者だった場合は、TS配信スレッドも停止
	if (m_pTsReaderArg->TsReceiversList.empty())
	{
		m_pTsReaderArg->StopTsRead = TRUE;
		::pthread_join(m_hTsRead, NULL);
		m_hTsRead = 0;
		delete m_pTsReaderArg;
		m_pTsReaderArg = NULL;
	}
}

BOOL cProxyServerEx::SelectBonDriver(LPCSTR p, BYTE bChannelLock)
{
	char *pKey = NULL;
	std::vector<stDriver> *pvstDriver = NULL;
	for (std::map<char *, std::vector<stDriver> >::iterator it = DriversMap.begin(); it != DriversMap.end(); ++it)
	{
		if (::strcmp(p, it->first) == 0)
		{
			pKey = it->first;
			pvstDriver = &(it->second);
			break;
		}
	}
	if (pvstDriver == NULL)
	{
		m_hModule = NULL;
		return FALSE;
	}

	// 現在時刻を取得しておく
	time_t tNow;
	tNow = time(NULL);

	// まず使われてないのを探す
	std::vector<stDriver> &vstDriver = *pvstDriver;
	int i;
	if (m_iDriverUseOrder == 0)
		i = 0;
	else
		i = (int)(vstDriver.size() - 1);
	for (;;)
	{
		if (vstDriver[i].bUsed != FALSE)
			goto next;
		HMODULE hModule;
		if (vstDriver[i].hModule != NULL)
			hModule = vstDriver[i].hModule;
		else
		{
			hModule = ::dlopen(vstDriver[i].strBonDriver, RTLD_LAZY);
			if (hModule == NULL)
				goto next;
			vstDriver[i].hModule = hModule;
#ifdef DEBUG
			::fprintf(stderr, "[%s] loaded\n", vstDriver[i].strBonDriver);
#endif
		}
		m_hModule = hModule;
		vstDriver[i].bUsed = TRUE;
		vstDriver[i].tLoad = tNow;
		m_pDriversMapKey = pKey;
		m_iDriverNo = i;

		// 各種項目再初期化の前に、現在TSストリーム配信中ならその配信対象リストから自身を削除
		if (m_hTsRead)
			StopTsReceive();

		// eSetChannel2からも呼ばれるので、各種項目再初期化
		m_pIBon = m_pIBon2 = m_pIBon3 = NULL;
		m_bTunerOpen = FALSE;
		m_hTsRead = 0;
		m_pTsReaderArg = NULL;
		return TRUE;
	next:
		if (m_iDriverUseOrder == 0)
		{
			if (i >= (int)(vstDriver.size() - 1))
				break;
			i++;
		}
		else
		{
			if (i <= 0)
				break;
			i--;
		}
	}

	// 全部使われてたら(あるいはdlopen()出来なければ)、あるインスタンスを使用している
	// クライアント群のチャンネル優先度の最大値が最も低いインスタンスを選択する
	// 同値の物が複数あった場合はBonDriverのロード時刻(もしくは使用要求時刻)が古い物を優先
	cProxyServerEx *pCandidate = NULL;
	std::vector<cProxyServerEx *> vpCandidate;
	for (std::list<cProxyServerEx *>::iterator it = g_InstanceList.begin(); it != g_InstanceList.end(); ++it)
	{
		if (*it == this)
			continue;
		if (pKey == (*it)->m_pDriversMapKey)	// この段階では文字列比較である必要は無い
		{
			// 候補リストに既に入れているなら以後のチェックは不要
			for (i = 0; i < (int)vpCandidate.size(); i++)
			{
				if (vpCandidate[i]->m_hModule == (*it)->m_hModule)
					break;
			}
			if (i != (int)vpCandidate.size())
				continue;
			// 暫定候補
			pCandidate = *it;
			// この暫定候補が使用しているインスタンスはロックされているか？
			BOOL bLocked = FALSE;
			for (std::list<cProxyServerEx *>::iterator it2 = g_InstanceList.begin(); it2 != g_InstanceList.end(); ++it2)
			{
				if (*it2 == this)
					continue;
				if (pCandidate->m_hModule == (*it2)->m_hModule)
				{
					if (((*it2)->m_bChannelLock > bChannelLock) || ((*it2)->m_bChannelLock == 0xff))	// ロックされてた
					{
						bLocked = TRUE;
						break;
					}
				}
			}
			if (!bLocked)	// ロックされていなければ候補リストに追加
				vpCandidate.push_back(pCandidate);
		}
	}

#ifdef DEBUG
	::fprintf(stderr, "** SelectBonDriver ** : vpCandidate.size[%zd]\n", vpCandidate.size());
#endif

	// 候補リストが空でなければ(==ロックされていないインスタンスがあったなら)
	if (vpCandidate.size() != 0)
	{
		pCandidate = vpCandidate[0];
		// 候補に選択の余地はあるか？
		if (vpCandidate.size() > 1)
		{
			// 接続クライアント群のチャンネル優先度の最大値が最も低いインスタンスを探す
			// なお若干ややこしいが、自身が排他権を持っており、かつ排他権取得待ちがいた場合も、
			// 元のインスタンスはこの時点での候補リストに含まれている
			BYTE bGroupMaxPriv, bMinPriv;
			std::vector<BYTE> vbGroupMaxPriv(vpCandidate.size());
			bMinPriv = 0xff;
			for (i = 0; i < (int)vpCandidate.size(); i++)
			{
				bGroupMaxPriv = 0;
				for (std::list<cProxyServerEx *>::iterator it = g_InstanceList.begin(); it != g_InstanceList.end(); ++it)
				{
					if (*it == this)
						continue;
					if (vpCandidate[i]->m_hModule == (*it)->m_hModule)
					{
						if ((*it)->m_bChannelLock > bGroupMaxPriv)
							bGroupMaxPriv = (*it)->m_bChannelLock;
					}
				}
				vbGroupMaxPriv[i] = bGroupMaxPriv;
				if (bMinPriv > bGroupMaxPriv)
					bMinPriv = bGroupMaxPriv;
			}
			std::vector<cProxyServerEx *> vpCandidate2;
			for (i = 0; i < (int)vpCandidate.size(); i++)
			{
#ifdef DEBUG
				::fprintf(stderr, "                      : vbGroupMaxPriv[%d] : [%d]\n", i, vbGroupMaxPriv[i]);
#endif
				if (vbGroupMaxPriv[i] == bMinPriv)
					vpCandidate2.push_back(vpCandidate[i]);
			}
#ifdef DEBUG
			::fprintf(stderr, "                      : vpCandidate2.size[%zd]\n", vpCandidate2.size());
#endif
			// eSetChannel2からの呼び出しの場合
			if (m_pIBon)
			{
				for (i = 0; i < (int)vpCandidate2.size(); i++)
				{
					// この時点での候補リストに現在のインスタンスが含まれていた場合は
					if (m_hModule == vpCandidate2[i]->m_hModule)
					{
						// 現在のインスタンスを継続使用
						// 「同値の物が複数あった場合はBonDriverのロード時刻(もしくは使用要求時刻)が古い物を優先」
						// が守られない事になる場合もあるが、効率優先
						vstDriver[m_iDriverNo].tLoad = tNow;
						return TRUE;
					}
				}
			}

			// vpCandidate2.size()が1以上なのは保証されている
			pCandidate = vpCandidate2[0];
			// インスタンス毎の最大優先度の最小値が複数インスタンスで同値であった場合
			if (vpCandidate2.size() > 1)
			{
				// BonDriverのロード時刻が一番古いのを探す
				// time_tが32ビットの環境では2038年問題が発生するけど放置
				time_t t = vstDriver[vpCandidate2[0]->m_iDriverNo].tLoad;
				for (i = 1; i < (int)vpCandidate2.size(); i++)
				{
					if (t > vstDriver[vpCandidate2[i]->m_iDriverNo].tLoad)
					{
						t = vstDriver[vpCandidate2[i]->m_iDriverNo].tLoad;
						pCandidate = vpCandidate2[i];
					}
				}
			}
		}
		else
		{
			// eSetChannel2からの呼び出しの場合
			if (m_pIBon)
			{
				// 唯一の候補が現在のインスタンスと同じだった場合は
				if (m_hModule == pCandidate->m_hModule)
				{
					// 現在のインスタンスを継続使用
					vstDriver[m_iDriverNo].tLoad = tNow;
					return TRUE;
				}
			}
		}

		// eSetChannel2からの呼び出しの場合かつ現在TSストリーム配信中だったなら、
		// インスタンスが切り替わるので、現在の配信対象リストから自身を削除
		if (m_pIBon && m_hTsRead)
			StopTsReceive();
	}
	else
	{
		// eSetChannel2からの呼び出しの場合
		if (m_pIBon)
		{
			// ロックされていないインスタンスが無かったので現在のインスタンスを継続使用
			vstDriver[m_iDriverNo].tLoad = tNow;
			return TRUE;
		}
	}

	// NULLである事は無いハズだけど
	if (pCandidate != NULL)
	{
		m_hModule = pCandidate->m_hModule;
		m_pDriversMapKey = pCandidate->m_pDriversMapKey;
		m_iDriverNo = pCandidate->m_iDriverNo;
		m_pIBon = pCandidate->m_pIBon;	// pCandidate->m_pIBonがNULLの可能性はゼロではない
		m_pIBon2 = pCandidate->m_pIBon2;
		m_pIBon3 = pCandidate->m_pIBon3;
		m_bTunerOpen = pCandidate->m_bTunerOpen;
		m_hTsRead = pCandidate->m_hTsRead;
		m_pTsReaderArg = pCandidate->m_pTsReaderArg;
		m_dwSpace = pCandidate->m_dwSpace;
		m_dwChannel = pCandidate->m_dwChannel;
		// 使用するBonDriverのロード時刻(使用要求時刻)を現在時刻で更新
		vstDriver[m_iDriverNo].tLoad = tNow;
	}

	// 選択したインスタンスが既にTSストリーム配信中なら、その配信対象リストに自身を追加
	if (m_hTsRead)
	{
		m_pTsReaderArg->TsLock.Enter();
		m_pTsReaderArg->TsReceiversList.push_back(this);
		m_pTsReaderArg->TsLock.Leave();
	}

	return (m_hModule != NULL);
}

IBonDriver *cProxyServerEx::CreateBonDriver()
{
	if (m_hModule)
	{
		char *err;
		::dlerror();
		IBonDriver *(*f)() = (IBonDriver *(*)())::dlsym(m_hModule, "CreateBonDriver");
		if ((err = ::dlerror()) == NULL)
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

const BOOL cProxyServerEx::OpenTuner(void)
{
	BOOL b = FALSE;
	if (m_pIBon)
		b = m_pIBon->OpenTuner();
	if (g_OpenTunerRetDelay != 0)
	{
		timespec ts;
		ts.tv_sec = 0;
		ts.tv_nsec = g_OpenTunerRetDelay * 1000 * 1000;
		::nanosleep(&ts, NULL);
	}
	return b;
}

void cProxyServerEx::CloseTuner(void)
{
	if (m_pIBon)
		m_pIBon->CloseTuner();
}

void cProxyServerEx::PurgeTsStream(void)
{
	if (m_pIBon)
		m_pIBon->PurgeTsStream();
}

LPCTSTR cProxyServerEx::EnumTuningSpace(const DWORD dwSpace)
{
	LPCTSTR pStr = NULL;
	if (m_pIBon2)
		pStr = m_pIBon2->EnumTuningSpace(dwSpace);
	return pStr;
}

LPCTSTR cProxyServerEx::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	LPCTSTR pStr = NULL;
	if (m_pIBon2)
		pStr = m_pIBon2->EnumChannelName(dwSpace, dwChannel);
	return pStr;
}

const BOOL cProxyServerEx::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	BOOL b = FALSE;
	if (m_pIBon2)
		b = m_pIBon2->SetChannel(dwSpace, dwChannel);
	return b;
}

const DWORD cProxyServerEx::GetTotalDeviceNum(void)
{
	DWORD d = 0;
	if (m_pIBon3)
		d = m_pIBon3->GetTotalDeviceNum();
	return d;
}

const DWORD cProxyServerEx::GetActiveDeviceNum(void)
{
	DWORD d = 0;
	if (m_pIBon3)
		d = m_pIBon3->GetActiveDeviceNum();
	return d;
}

const BOOL cProxyServerEx::SetLnbPower(const BOOL bEnable)
{
	BOOL b = FALSE;
	if (m_pIBon3)
		b = m_pIBon3->SetLnbPower(bEnable);
	return b;
}

static int Listen(char *host, char *port)
{
	addrinfo hints, *results, *rp;
	SOCKET lsock, csock;

	::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;
	if (::getaddrinfo(host, port, &hints, &results) != 0)
	{
		hints.ai_flags = AI_PASSIVE;
		if (::getaddrinfo(host, port, &hints, &results) != 0)
			return 1;
	}

	for (rp = results; rp != NULL; rp = rp->ai_next)
	{
		lsock = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (lsock == INVALID_SOCKET)
			continue;

		BOOL reuse = TRUE;
		::setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

		if (::bind(lsock, rp->ai_addr, (int)(rp->ai_addrlen)) != SOCKET_ERROR)
			break;

		::close(lsock);
	}
	::freeaddrinfo(results);
	if (rp == NULL)
		return 2;

	if (::listen(lsock, 4) == SOCKET_ERROR)
	{
		::close(lsock);
		return 3;
	}

	for (;;)
	{
		csock = ::accept(lsock, NULL, NULL);
		if (csock == INVALID_SOCKET)
		{
#ifdef DEBUG
			if ((errno == EINTR) && g_bStop)
				break;
#endif
			continue;
		}

		cProxyServerEx *pProxy = new cProxyServerEx();
		pProxy->setSocket(csock);

		pthread_t ht;
		pthread_attr_t attr;
		if (::pthread_attr_init(&attr))
			goto retry;
		if (::pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
			goto retry;
		if (::pthread_create(&ht, &attr, cProxyServerEx::Reception, pProxy) == 0)
			continue;
	retry:
		delete pProxy;
	}

	return 0;
}

}

int main(int argc, char *argv[])
{
	if (BonDriverProxyEx::Init(argc, argv) != 0)
	{
		fprintf(stderr, "usage: %s address port (opentuner_return_delay_time packet_fifo_size tspacket_bufsize)\ne.g. $ %s 192.168.0.100 1192\n", argv[0],argv[0]);
		return 0;
	}

#ifndef DEBUG
	if (daemon(1, 1))
	{
		perror("daemon");
		return -1;
	}
#endif

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGPIPE, &sa, NULL))
	{
		perror("sigaction1");
		return -2;
	}
#ifdef DEBUG
	sa.sa_handler = BonDriverProxyEx::Handler;
	if (sigaction(SIGINT, &sa, NULL))
	{
		perror("sigaction2");
		return -3;
	}
#endif

	int ret = BonDriverProxyEx::Listen(BonDriverProxyEx::g_Host, BonDriverProxyEx::g_Port);

#ifdef DEBUG
	BonDriverProxyEx::g_Lock.Enter();
	BonDriverProxyEx::CleanUp();
	BonDriverProxyEx::g_Lock.Leave();
#endif

	return ret;
}
