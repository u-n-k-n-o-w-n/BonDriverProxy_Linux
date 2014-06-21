/*
$ g++ -O2 -shared -fPIC -Wall -o BonDriver_LinuxPT.so BonDriver_LinuxPT.cpp -lpthread -ldl
*/
#include "BonDriver_LinuxPT.h"

static char g_strSpace[32];
static stChannel g_stChannels[2][MAX_CH];
static char g_Device[32];
static int g_Type;		// 0 : ISDB_S / 1 : ISDB_T
static BOOL g_UseLNB;

static int Convert(char *src, char *dst, size_t dstsize)
{
	iconv_t d = iconv_open("UTF-16LE", "UTF-8");
	if (d == (iconv_t)-1)
		return -1;
	size_t srclen = strlen(src) + 1;
	size_t dstlen = dstsize - 2;
	size_t ret = iconv(d, &src, &srclen, &dst, &dstlen);
	*dst = *(dst + 1) = '\0';
	iconv_close(d);
	if (ret == (size_t)-1)
		return -2;
	return 0;
}

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
	for (int i = 0; i < MAX_CH; i++)
	{
		g_stChannels[0][i].bUnused = TRUE;
		g_stChannels[1][i].bUnused = TRUE;
	}

	int idx = 0;
	BOOL bdFlag = FALSE;
	BOOL blFlag = FALSE;
	while (fgets(buf, sizeof(buf), fp))
	{
		if (buf[0] == ';')
			continue;
		p = buf + strlen(buf) - 1;
		while (*p == '\r' || *p == '\n')
			*p-- = '\0';
		if ((idx != 0) && (strncmp(buf, "#ISDB_S", 7) == 0))
			idx = 0;
		else if ((idx != 1) && (strncmp(buf, "#ISDB_T", 7) == 0))
			idx = 1;
		else if (!bdFlag && (strncmp(buf, "#DEVICE=", 8) == 0))
		{
			p = &buf[8];
			while (*p == ' ' || *p == '\t')
				p++;
			strncpy(g_Device, p, sizeof(g_Device) - 1);
			g_Device[sizeof(g_Device) - 1] = '\0';
			p = strstr(g_Device, "video");
			if (p)
				g_Type = (atoi(p + 5) / 2) % 2;
			if (g_Type == 0)
				p = (char *)"BS/CS110";
			else
				p = (char *)"UHF";
			if (Convert(p, g_strSpace, sizeof(g_strSpace)) < 0)
			{
				fclose(fp);
				return -2;
			}
			bdFlag = TRUE;
		}
		else if (!blFlag && (strncmp(buf, "#USELNB=", 8) == 0))
		{
			p = &buf[8];
			while (*p == ' ' || *p == '\t')
				p++;
			g_UseLNB = atoi(p);
			blFlag = TRUE;
		}
		else
		{
			int n = 0;
			char *cp[4];
			BOOL bOk = FALSE;
			p = cp[n++] = buf;
			while (1)
			{
				p = strchr(p, '\t');
				if (p)
				{
					*p++ = '\0';
					cp[n++] = p;
					if (n > 3)
					{
						bOk = TRUE;
						break;
					}
				}
				else
					break;
			}
			if (bOk)
			{
				DWORD dw = atoi(cp[1]);
				if (dw < MAX_CH)
				{
					if (Convert(cp[0], g_stChannels[idx][dw].strChName, MAX_CN_LEN) < 0)
					{
						fclose(fp);
						return -3;
					}
					g_stChannels[idx][dw].freq.frequencyno = atoi(cp[2]);
					g_stChannels[idx][dw].freq.slot = atoi(cp[3]);
					g_stChannels[idx][dw].bUnused = FALSE;
				}
			}
		}
	}
	fclose(fp);
	return 0;
}

static float GetSignalLevel_S(int signal)
{
	static const float fLevelTable[] = {
		24.07f,	// 00	00	  0			24.07dB
		24.07f,	// 10	00	  4096		24.07dB
		18.61f,	// 20	00	  8192		18.61dB
		15.21f,	// 30	00	  12288		15.21dB
		12.50f,	// 40	00	  16384		12.50dB
		10.19f,	// 50	00	  20480		10.19dB
		8.140f,	// 60	00	  24576		8.140dB
		6.270f,	// 70	00	  28672		6.270dB
		4.550f,	// 80	00	  32768		4.550dB
		3.730f,	// 88	00	  34816		3.730dB
		3.630f,	// 88	FF	  35071		3.630dB
		2.940f,	// 90	00	  36864		2.940dB
		1.420f,	// A0	00	  40960		1.420dB
		0.000f	// B0	00	  45056		-0.01dB
	};

	unsigned char idx = (signal >> 12) & 0xff;
	if (idx <= 0x1)
		return 24.07f;
	else if (idx >= 0xb)
		return 0.0f;
	else
	{
		// 線形補間
		float wf;
		unsigned short off = signal & 0x0fff;
		if (idx == 0x08)
		{
			if (off >= 0x08ff)
			{
				wf = 1793.0f;
				idx = 0x0a;
			}
			else if (off >= 0x0800)
			{
				wf = 255.0f;
				idx = 0x09;
			}
			else
			{
				wf = 2048.0f;
				idx = 0x08;
			}
		}
		else
		{
			wf = 4096.0f;
			if (idx >= 0x09)
				idx += 2;
		}
		return fLevelTable[idx] - (((fLevelTable[idx] - fLevelTable[idx + 1]) / wf) * off);
	}
}

static float GetSignalLevel_T(int signal)
{
	double P = log10(5505024 / (double)signal) * 10;
	return (float)((0.000024 * P * P * P * P) - (0.0016 * P * P * P) + (0.0398 * P * P) + (0.5491 * P) + 3.0965);
}

cBonDriverLinuxPT *cBonDriverLinuxPT::m_spThis = NULL;
cCriticalSection cBonDriverLinuxPT::m_sInstanceLock;
BOOL cBonDriverLinuxPT::m_sbInit = TRUE;

extern "C" IBonDriver *CreateBonDriver()
{
	LOCK(cBonDriverLinuxPT::m_sInstanceLock);
	if (cBonDriverLinuxPT::m_sbInit)
	{
		if (Init() < 0)
			return NULL;
		cBonDriverLinuxPT::m_sbInit = FALSE;
	}

	// 複数読み込み禁止
	cBonDriverLinuxPT *pLinuxPT = NULL;
	if (cBonDriverLinuxPT::m_spThis == NULL)
		pLinuxPT = new cBonDriverLinuxPT();
	return pLinuxPT;
}

cBonDriverLinuxPT::cBonDriverLinuxPT() : m_fifoTS(m_c, m_m)
{
	m_spThis = this;
	::Convert((char *)TUNER_NAME, m_TunerName, sizeof(m_TunerName));
	m_LastBuff = NULL;
	m_bTuner = FALSE;
	m_fSignalLevel = 0;
	m_dwSpace = m_dwChannel = 0xff;
	m_fd = -1;
	m_hTsRead = 0;
	m_bStopTsRead = FALSE;

	pthread_mutexattr_t attr;
	::pthread_mutexattr_init(&attr);
	::pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	::pthread_mutex_init(&m_m, &attr);
	::pthread_cond_init(&m_c, NULL);
}

cBonDriverLinuxPT::~cBonDriverLinuxPT()
{
	CloseTuner();
	{
		LOCK(m_writeLock);
		TsFlush();
		if (m_LastBuff != NULL)
		{
			delete m_LastBuff;
			m_LastBuff = NULL;
		}
	}
	::pthread_cond_destroy(&m_c);
	::pthread_mutex_destroy(&m_m);
	m_spThis = NULL;
}

const BOOL cBonDriverLinuxPT::OpenTuner(void)
{
	if (m_bTuner)
		return TRUE;
	m_fd = ::open(g_Device, O_RDONLY);
	if (m_fd < 0)
		return FALSE;
	if (g_UseLNB && (g_Type == 0) && (::ioctl(m_fd, LNB_ENABLE, 2) < 0))
		::fprintf(stderr, "LNB ON failed: %s\n", g_Device);
	m_bTuner = TRUE;
	return TRUE;
}

void cBonDriverLinuxPT::CloseTuner(void)
{
	if (m_bTuner)
	{
		if (m_hTsRead)
		{
			m_bStopTsRead = TRUE;
			::pthread_join(m_hTsRead, NULL);
			m_hTsRead = 0;
			::ioctl(m_fd, STOP_REC, 0);
		}
		if (g_UseLNB && (g_Type == 0) && (::ioctl(m_fd, LNB_DISABLE, 0) < 0))
			::fprintf(stderr, "LNB OFF failed: %s\n", g_Device);
		::close(m_fd);
		m_bTuner = FALSE;
		m_fd = -1;
	}
}

const BOOL cBonDriverLinuxPT::SetChannel(const BYTE bCh)
{
	return TRUE;
}

const float cBonDriverLinuxPT::GetSignalLevel(void)
{
	return m_fSignalLevel;
}

const DWORD cBonDriverLinuxPT::WaitTsStream(const DWORD dwTimeOut)
{
	if (!m_bTuner)
		return WAIT_ABANDONED;
	if (m_fifoTS.Size() != 0)
		return WAIT_OBJECT_0;
	else
		return WAIT_TIMEOUT;	// 手抜き
}

const DWORD cBonDriverLinuxPT::GetReadyCount(void)
{
	if (!m_bTuner)
		return 0;
	return (DWORD)m_fifoTS.Size();
}

const BOOL cBonDriverLinuxPT::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
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

const BOOL cBonDriverLinuxPT::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain)
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

void cBonDriverLinuxPT::PurgeTsStream(void)
{
	if (!m_bTuner)
		return;
	{
		LOCK(m_writeLock);
		TsFlush();
	}
}

void cBonDriverLinuxPT::Release(void)
{
	LOCK(m_sInstanceLock);
	delete this;
}

LPCTSTR cBonDriverLinuxPT::GetTunerName(void)
{
	return (LPCTSTR)m_TunerName;
}

const BOOL cBonDriverLinuxPT::IsTunerOpening(void)
{
	return FALSE;
}

LPCTSTR cBonDriverLinuxPT::EnumTuningSpace(const DWORD dwSpace)
{
	if (dwSpace != 0)
		return NULL;
	return (LPCTSTR)g_strSpace;
}

LPCTSTR cBonDriverLinuxPT::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	if (dwSpace != 0)
		return NULL;
	if (dwChannel >= MAX_CH)
		return NULL;
	if (g_stChannels[g_Type][dwChannel].bUnused)
		return NULL;
	return (LPCTSTR)(g_stChannels[g_Type][dwChannel].strChName);
}

const BOOL cBonDriverLinuxPT::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	if (dwSpace != 0)
		return FALSE;
	if (dwChannel >= MAX_CH)
		return FALSE;
	if (g_stChannels[g_Type][dwChannel].bUnused)
		return FALSE;

	if (::ioctl(m_fd, SET_CHANNEL, &(g_stChannels[g_Type][dwChannel].freq)) < 0)
	{
		::fprintf(stderr, "SetChannel() ioctl(SET_CHANNEL) error: %s\n", g_Device);
		return FALSE;
	}

	{
		LOCK(m_writeLock);
		TsFlush();
	}

	if (!m_hTsRead)
	{
		m_bStopTsRead = FALSE;
		if (::pthread_create(&m_hTsRead, NULL, cBonDriverLinuxPT::TsReader, this))
		{
			::perror("pthread_create");
			return FALSE;
		}
	}

	m_dwSpace = dwSpace;
	m_dwChannel = dwChannel;
	return TRUE;
}

const DWORD cBonDriverLinuxPT::GetCurSpace(void)
{
	return m_dwSpace;
}

const DWORD cBonDriverLinuxPT::GetCurChannel(void)
{
	return m_dwChannel;
}

void *cBonDriverLinuxPT::TsReader(LPVOID pv)
{
	cBonDriverLinuxPT *pLinuxPT = static_cast<cBonDriverLinuxPT *>(pv);
	DWORD now, before = 0;
	DWORD &ret = pLinuxPT->m_tRet;
	ret = 300;
	BYTE *pBuf, *pTsBuf;
	timeval tv;
	timespec ts;
	int len, pos;

	ts.tv_sec = 0;
	ts.tv_nsec = WAIT_TIME * 1000 * 1000;

	if (::ioctl(pLinuxPT->m_fd, START_REC, 0) < 0)
	{
		::fprintf(stderr, "TsReader() ioctl(START_REC) error: %s\n", g_Device);
		ret = 301;
		return &ret;
	}

	// TS読み込みループ
	pTsBuf = new BYTE[TS_BUFSIZE];
	pos = 0;
	while (!pLinuxPT->m_bStopTsRead)
	{
		::gettimeofday(&tv, NULL);
		now = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
		if ((now - before) >= 1000)
		{
			float f = 0;
			int signal;
			if (::ioctl(pLinuxPT->m_fd, GET_SIGNAL_STRENGTH, &signal) < 0)
				::fprintf(stderr, "TsReader() ioctl(GET_SIGNAL_STRENGTH) error: %s\n", g_Device);
			else
			{
				if (g_Type == 0)
					f = ::GetSignalLevel_S(signal);
				else
					f = ::GetSignalLevel_T(signal);
			}
			pLinuxPT->m_fSignalLevel = f;
			before = now;
		}

		pBuf = pTsBuf + pos;
		if ((len = ::read(pLinuxPT->m_fd, pBuf, TS_BUFSIZE - pos)) <= 0)
		{
			::nanosleep(&ts, NULL);
			continue;
		}

		pos += len;

		if (pos == TS_BUFSIZE)
		{
			TS_DATA *pData = new TS_DATA();
			pData->dwSize = TS_BUFSIZE;
			pData->pbBuff = pTsBuf;
			pLinuxPT->m_fifoTS.Push(pData);
			pTsBuf = new BYTE[TS_BUFSIZE];
			pos = 0;
		}
	}
	delete[] pTsBuf;
	return &ret;
}
