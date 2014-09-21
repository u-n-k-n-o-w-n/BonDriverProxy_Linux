#include "BonDriver_DVB.h"

namespace BonDriver_DVB {

static char g_strSpace[32];
static stChannel g_stChannels[2][MAX_CH];
static int g_AdapterNo;
static int g_Type;			// 0 : ISDB_S / 1 : ISDB_T  <-  判定はm_fefdにioctl()かけるまで保留
static BOOL g_UseReadSnr;	// 0 : FE_READ_SIGNAL_STRENGTH / 1 : FE_READ_SNR
static BOOL g_UseLNB;
static BOOL g_UseServiceID;
static DWORD g_Crc32Table[256];

static int Convert(char *src, char *dst, size_t dstsize)
{
	iconv_t d = ::iconv_open("UTF-16LE", "UTF-8");
	if (d == (iconv_t)-1)
		return -1;
	size_t srclen = ::strlen(src) + 1;
	size_t dstlen = dstsize - 2;
	size_t ret = ::iconv(d, &src, &srclen, &dst, &dstlen);
	*dst = *(dst + 1) = '\0';
	::iconv_close(d);
	if (ret == (size_t)-1)
		return -2;
	return 0;
}

static void InitCrc32Table()
{
	DWORD i, j, crc;
	for (i = 0; i < 256; i++)
	{
		crc = i << 24;
		for (j = 0; j < 8; j++)
			crc = (crc << 1) ^ ((crc & 0x80000000) ? 0x04c11db7 : 0);
		g_Crc32Table[i] = crc;
	}
}

static DWORD CalcCRC32(BYTE *p, DWORD len)
{
	DWORD i, crc = 0xffffffff;
	for (i = 0; i < len; i++)
		crc = (crc << 8) ^ g_Crc32Table[(crc >> 24) ^ p[i]];
	return crc;
}

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
	for (int i = 0; i < MAX_CH; i++)
	{
		g_stChannels[0][i].bUnused = TRUE;
		g_stChannels[1][i].bUnused = TRUE;
	}

	int idx = 0;
	BOOL baFlag = FALSE;
	BOOL brFlag = FALSE;
	BOOL blFlag = FALSE;
	BOOL bsFlag = FALSE;
	while (::fgets(buf, sizeof(buf), fp))
	{
		if (buf[0] == ';')
			continue;
		p = buf + ::strlen(buf) - 1;
		while ((p >= buf) && (*p == '\r' || *p == '\n'))
			*p-- = '\0';
		if (p < buf)
			continue;
		if ((idx != 0) && IsTagMatch(buf, "#ISDB_S", NULL))
			idx = 0;
		else if ((idx != 1) && IsTagMatch(buf, "#ISDB_T", NULL))
			idx = 1;
		else if (!baFlag && IsTagMatch(buf, "#ADAPTER_NO", &p))
		{
			g_AdapterNo = ::atoi(p);
			baFlag = TRUE;
		}
		else if (!brFlag && IsTagMatch(buf, "#USEREADSNR", &p))
		{
			g_UseReadSnr = ::atoi(p);
			brFlag = TRUE;
		}
		else if (!blFlag && IsTagMatch(buf, "#USELNB", &p))
		{
			g_UseLNB = ::atoi(p);
			blFlag = TRUE;
		}
		else if (!bsFlag && IsTagMatch(buf, "#USESERVICEID", &p))
		{
			g_UseServiceID = ::atoi(p);
			bsFlag = TRUE;
		}
		else
		{
			int n = 0;
			char *cp[5];
			BOOL bOk = FALSE;
			p = cp[n++] = buf;
			while (1)
			{
				p = ::strchr(p, '\t');
				if (p)
				{
					*p++ = '\0';
					cp[n++] = p;
					if (g_UseServiceID)
					{
						if (n > 4)
						{
							bOk = TRUE;
							break;
						}
					}
					else
					{
						if (n > 3)
						{
							bOk = TRUE;
							break;
						}
					}
				}
				else
					break;
			}
			if (bOk)
			{
				DWORD dw = ::atoi(cp[1]);
				if (dw < MAX_CH)
				{
					if (Convert(cp[0], g_stChannels[idx][dw].strChName, MAX_CN_LEN) < 0)
					{
						::fclose(fp);
						return -3;
					}
					g_stChannels[idx][dw].freq.frequencyno = ::atoi(cp[2]);
					g_stChannels[idx][dw].freq.tsid = ::strtoul(cp[3], NULL, 16);
					if (g_UseServiceID)
						g_stChannels[idx][dw].ServiceID = ::strtoul(cp[4], NULL, 10);
					g_stChannels[idx][dw].bUnused = FALSE;
				}
			}
		}
	}
	::fclose(fp);
	if (g_UseServiceID)
		InitCrc32Table();
	return 0;
}

static float GetSignalLevel_S(int16_t signal)
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

static float GetSignalLevel_T(int16_t signal)
{
	double P = ::log10(5505024 / (double)signal) * 10;
	return (float)((0.000024 * P * P * P * P) - (0.0016 * P * P * P) + (0.0398 * P * P) + (0.5491 * P) + 3.0965);
}

static unsigned int GetFrequency_S(int ch)
{
	unsigned int freq;
	if (ch < 12)
		freq = 1049480 + 38360 * ch;
	else if (ch < 24)
		freq = 1613000 + 40000 * (ch - 12);
	else if (ch < 36)
		freq = 1593000 + 40000 * (ch - 24);
	else
		freq = 1049480;	// 変な値なので0chに
	return freq;
}

static unsigned int GetFrequency_T(int ch)
{
	unsigned int freq;
	if (ch < 12)
		freq = 93142857 + 6000000 * ch;
	else if (ch < 17)
		freq = 167142857 + 6000000 * (ch - 12);
	else if (ch < 63)
		freq = 195142857 + 6000000 * (ch - 17);
	else if (ch < 113)
		freq = 473142857 + 6000000 * (ch - 63);
	else
		freq = 93142857;	// 変な値なので0chに
	return freq;
}

cBonDriverDVB *cBonDriverDVB::m_spThis = NULL;
cCriticalSection cBonDriverDVB::m_sInstanceLock;
BOOL cBonDriverDVB::m_sbInit = TRUE;

extern "C" IBonDriver *CreateBonDriver()
{
	LOCK(cBonDriverDVB::m_sInstanceLock);
	if (cBonDriverDVB::m_sbInit)
	{
		if (Init() < 0)
			return NULL;
		cBonDriverDVB::m_sbInit = FALSE;
	}

	// 複数読み込み禁止
	cBonDriverDVB *pDVB = NULL;
	if (cBonDriverDVB::m_spThis == NULL)
		pDVB = new cBonDriverDVB();
	return pDVB;
}

cBonDriverDVB::cBonDriverDVB() : m_fifoTS(m_c, m_m), m_fifoRawTS(m_c, m_m), m_StopTsSplit(m_c, m_m)
{
	m_spThis = this;
	Convert((char *)TUNER_NAME, m_TunerName, sizeof(m_TunerName));
	m_LastBuf = NULL;
	m_bTuner = FALSE;
	m_fSignalLevel = 0;
	m_dwSpace = m_dwChannel = 0xff;
	m_dwServiceID = 0xffffffff;
	m_fefd = m_dmxfd = m_dvrfd = -1;
	m_hTsRead = m_hTsSplit = 0;
	m_bStopTsRead = FALSE;

	pthread_mutexattr_t attr;
	::pthread_mutexattr_init(&attr);
	::pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	::pthread_mutex_init(&m_m, &attr);
	::pthread_cond_init(&m_c, NULL);
}

cBonDriverDVB::~cBonDriverDVB()
{
	CloseTuner();
	{
		LOCK(m_writeLock);
		TsFlush(g_UseServiceID);
		delete m_LastBuf;
	}
	::pthread_cond_destroy(&m_c);
	::pthread_mutex_destroy(&m_m);
	m_spThis = NULL;
}

const BOOL cBonDriverDVB::OpenTuner(void)
{
	if (m_bTuner)
		return TRUE;

	char buf[64];
	::sprintf(buf, "/dev/dvb/adapter%d/frontend0", g_AdapterNo);
	m_fefd = ::open(buf, O_RDWR);
	if (m_fefd < 0)
	{
		::fprintf(stderr, "OpenTuner() frontend open() error: adapter%d\n", g_AdapterNo);
		return FALSE;
	}

	dvb_frontend_info info;
	if (::ioctl(m_fefd, FE_GET_INFO, &info) < 0)
	{
		::fprintf(stderr, "OpenTuner() ioctl(FE_GET_INFO) error: adapter%d\n", g_AdapterNo);
		goto err1;
	}

	char *p;
	if (info.type == FE_QPSK)
	{
		g_Type = 0;
		p = (char *)"BS/CS110";
	}
	else if (info.type == FE_OFDM)
	{
		g_Type = 1;
		p = (char *)"UHF";
	}
	else
	{
		::fprintf(stderr, "OpenTuner() adapter type unknown: adapter%d\n", g_AdapterNo);
		goto err1;
	}
	if (Convert(p, g_strSpace, sizeof(g_strSpace)) < 0)
		goto err1;

	::sprintf(buf, "/dev/dvb/adapter%d/demux0", g_AdapterNo);
	m_dmxfd = ::open(buf, O_RDONLY);
	if (m_dmxfd < 0)
	{
		::fprintf(stderr, "OpenTuner() demux open() error: adapter%d\n", g_AdapterNo);
		goto err1;
	}

	dmx_pes_filter_params filter;
//	::memset(&filter, 0, sizeof(filter));
	filter.pid = 0x2000;
	filter.input = DMX_IN_FRONTEND;
	filter.output = DMX_OUT_TS_TAP;
	filter.pes_type =  DMX_PES_VIDEO;
	filter.flags = DMX_IMMEDIATE_START;

	if (::ioctl(m_dmxfd, DMX_SET_PES_FILTER, &filter) < 0)
	{
		::fprintf(stderr, "OpenTuner() ioctl(DMX_SET_PES_FILTER) error: adapter%d\n", g_AdapterNo);
		goto err0;
	}

	::sprintf(buf, "/dev/dvb/adapter%d/dvr0", g_AdapterNo);
	m_dvrfd = ::open(buf, O_RDONLY);
	if (m_dvrfd < 0)
	{
		::fprintf(stderr, "OpenTuner() dvr open() error: adapter%d\n", g_AdapterNo);
		goto err0;
	}

	if (g_UseLNB && (g_Type == 0))
	{
		dtv_property prop[1];
		prop[0].cmd = DTV_VOLTAGE;
		prop[0].u.data = SEC_VOLTAGE_18;

		dtv_properties props;
//		::memset(&props, 0, sizeof(props));
		props.props = prop;
		props.num = 1;

		if (::ioctl(m_fefd, FE_SET_PROPERTY, &props) < 0)
			::fprintf(stderr, "LNB ON failed: adapter%d\n", g_AdapterNo);
	}

	m_bTuner = TRUE;
	return TRUE;

err0:
	::close(m_dmxfd);
	m_dmxfd = -1;
err1:
	::close(m_fefd);
	m_fefd = -1;
	return FALSE;
}

void cBonDriverDVB::CloseTuner(void)
{
	if (m_bTuner)
	{
		if (m_hTsRead)
		{
			m_bStopTsRead = TRUE;
			::pthread_join(m_hTsRead, NULL);
			m_hTsRead = 0;
		}
		if (g_UseLNB && (g_Type == 0))
		{
			dtv_property prop[1];
			prop[0].cmd = DTV_VOLTAGE;
			prop[0].u.data = SEC_VOLTAGE_OFF;

			dtv_properties props;
//			::memset(&props, 0, sizeof(props));
			props.props = prop;
			props.num = 1;

			if (::ioctl(m_fefd, FE_SET_PROPERTY, &props) < 0)
				::fprintf(stderr, "LNB OFF failed: adapter%d\n", g_AdapterNo);
		}
		::close(m_dvrfd);
		::close(m_dmxfd);
		::close(m_fefd);
		m_fefd = m_dmxfd = m_dvrfd = -1;
		m_bTuner = FALSE;
	}
}

const BOOL cBonDriverDVB::SetChannel(const BYTE bCh)
{
	return TRUE;
}

const float cBonDriverDVB::GetSignalLevel(void)
{
	return m_fSignalLevel;
}

const DWORD cBonDriverDVB::WaitTsStream(const DWORD dwTimeOut)
{
	if (!m_bTuner)
		return WAIT_ABANDONED;
	if (m_fifoTS.Size() != 0)
		return WAIT_OBJECT_0;
	else
		return WAIT_TIMEOUT;	// 手抜き
}

const DWORD cBonDriverDVB::GetReadyCount(void)
{
	if (!m_bTuner)
		return 0;
	return (DWORD)m_fifoTS.Size();
}

const BOOL cBonDriverDVB::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
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

const BOOL cBonDriverDVB::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain)
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

void cBonDriverDVB::PurgeTsStream(void)
{
	if (!m_bTuner)
		return;
	{
		LOCK(m_writeLock);
		TsFlush(g_UseServiceID);
	}
}

void cBonDriverDVB::Release(void)
{
	LOCK(m_sInstanceLock);
	delete this;
}

LPCTSTR cBonDriverDVB::GetTunerName(void)
{
	return (LPCTSTR)m_TunerName;
}

const BOOL cBonDriverDVB::IsTunerOpening(void)
{
	return FALSE;
}

LPCTSTR cBonDriverDVB::EnumTuningSpace(const DWORD dwSpace)
{
	if (!m_bTuner)
		return NULL;
	if (dwSpace != 0)
		return NULL;
	return (LPCTSTR)g_strSpace;
}

LPCTSTR cBonDriverDVB::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	if (!m_bTuner)
		return NULL;
	if (dwSpace != 0)
		return NULL;
	if (dwChannel >= MAX_CH)
		return NULL;
	if (g_stChannels[g_Type][dwChannel].bUnused)
		return NULL;
	return (LPCTSTR)(g_stChannels[g_Type][dwChannel].strChName);
}

// 経緯的にはDTV_ISDBS_TS_IDがDTV_STREAM_IDに置き換わり、その際にDTV_ISDBS_TS_ID_LEGACYが
// 追加されたらしいので、多分これで大丈夫…
#ifndef DTV_STREAM_ID
#define DTV_STREAM_ID		DTV_ISDBS_TS_ID
#endif

const BOOL cBonDriverDVB::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	if (!m_bTuner)
		return FALSE;
	if (dwSpace != 0)
		return FALSE;
	if (dwChannel >= MAX_CH)
		return FALSE;
	if (g_stChannels[g_Type][dwChannel].bUnused)
		return FALSE;
	if (dwChannel == m_dwChannel)
		return TRUE;

	BOOL bFlag = TRUE;
	if (g_UseServiceID)
	{
		if (m_dwChannel != 0xff)
		{
			if ((g_stChannels[g_Type][dwChannel].freq.frequencyno == g_stChannels[g_Type][m_dwChannel].freq.frequencyno) &&
				(g_stChannels[g_Type][dwChannel].freq.tsid == g_stChannels[g_Type][m_dwChannel].freq.tsid))
			{
				bFlag = FALSE;
			}
		}
		m_dwServiceID = g_stChannels[g_Type][dwChannel].ServiceID;
	}

	if (bFlag)
	{
		unsigned int f;
		dtv_property prop[3];
		dtv_properties props;
//		::memset(&props, 0, sizeof(props));
		if (g_Type == 0)
		{
			f = GetFrequency_S(g_stChannels[g_Type][dwChannel].freq.frequencyno);
			prop[0].cmd = DTV_FREQUENCY;
			prop[0].u.data = f;
			prop[1].cmd = DTV_STREAM_ID;
			prop[1].u.data = g_stChannels[g_Type][dwChannel].freq.tsid;
			prop[2].cmd = DTV_TUNE;
			props.props = prop;
			props.num = 3;
		}
		else
		{
			f = GetFrequency_T(g_stChannels[g_Type][dwChannel].freq.frequencyno);
			prop[0].cmd = DTV_FREQUENCY;
			prop[0].u.data = f;
			prop[1].cmd = DTV_TUNE;
			props.props = prop;
			props.num = 2;
		}

		if (::ioctl(m_fefd, FE_SET_PROPERTY, &props) < 0)
		{
			::fprintf(stderr, "SetChannel() ioctl(FE_SET_PROPERTY) error: adapter%d\n", g_AdapterNo);
			return FALSE;
		}

		fe_status_t status;
		timespec ts;
		ts.tv_sec = 0;
		ts.tv_nsec = 250 * 1000 * 1000;	// 250ms
		BOOL bOk = FALSE;
		for (int i = 0; i < 4; i++)	// 250ms * 4で最大1秒待つ
		{
			if (::ioctl(m_fefd, FE_READ_STATUS, &status) < 0)
			{
				::fprintf(stderr, "SetChannel() ioctl(FE_READ_STATUS) error: adapter%d\n", g_AdapterNo);
				return FALSE;
			}
			if (status & FE_HAS_LOCK)
			{
				bOk = TRUE;
				break;
			}
			::nanosleep(&ts, NULL);
		}
		if (!bOk)
		{
			::fprintf(stderr, "SetChannel() timeout: adapter%d\n", g_AdapterNo);
			return FALSE;
		}
	}

	{
		LOCK(m_writeLock);
		TsFlush(g_UseServiceID);
	}

	if (!m_hTsRead)
	{
		m_bStopTsRead = FALSE;
		if (::pthread_create(&m_hTsRead, NULL, cBonDriverDVB::TsReader, this))
		{
			::perror("pthread_create1");
			return FALSE;
		}
	}

	m_dwSpace = dwSpace;
	m_dwChannel = dwChannel;
	return TRUE;
}

const DWORD cBonDriverDVB::GetCurSpace(void)
{
	return m_dwSpace;
}

const DWORD cBonDriverDVB::GetCurChannel(void)
{
	return m_dwChannel;
}

void *cBonDriverDVB::TsReader(LPVOID pv)
{
	cBonDriverDVB *pDVB = static_cast<cBonDriverDVB *>(pv);
	DWORD now, before = 0;
	DWORD &ret = pDVB->m_tRet;
	ret = 300;
	BYTE *pBuf, *pTsBuf;
	timeval tv;
	timespec ts;
	int len, pos;

	if (g_UseServiceID)
	{
		if (::pthread_create(&(pDVB->m_hTsSplit), NULL, cBonDriverDVB::TsSplitter, pDVB))
		{
			::perror("pthread_create2");
			ret = 301;
			return &ret;
		}
	}

	ts.tv_sec = 0;
	ts.tv_nsec = WAIT_TIME * 1000 * 1000;

	// TS読み込みループ
	pTsBuf = new BYTE[TS_BUFSIZE];
	pos = 0;
	while (!pDVB->m_bStopTsRead)
	{
		::gettimeofday(&tv, NULL);
		now = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
		if ((now - before) >= 1000)
		{
			float f = 0;
			if (g_UseReadSnr)
			{
				int16_t cnr;
				if (::ioctl(pDVB->m_fefd, FE_READ_SNR, &cnr) < 0)
					::fprintf(stderr, "TsReader() ioctl(FE_READ_SNR) error: adapter%d\n", g_AdapterNo);
				else
					f = (float)cnr / 1000;
			}
			else
			{
				int16_t signal;
				if (::ioctl(pDVB->m_fefd, FE_READ_SIGNAL_STRENGTH, &signal) < 0)
					::fprintf(stderr, "TsReader() ioctl(FE_READ_SIGNAL_STRENGTH) error: adapter%d\n", g_AdapterNo);
				else
				{
					if (g_Type == 0)
						f = GetSignalLevel_S(signal);
					else
						f = GetSignalLevel_T(signal);
				}
			}
			pDVB->m_fSignalLevel = f;
			before = now;
		}

		pBuf = pTsBuf + pos;
		if ((len = ::read(pDVB->m_dvrfd, pBuf, TS_BUFSIZE - pos)) <= 0)
		{
			::nanosleep(&ts, NULL);
			continue;
		}

		pos += len;

		if (pos == TS_BUFSIZE)
		{
			TS_DATA *pData = new TS_DATA();
			pData->dwSize = TS_BUFSIZE;
			pData->pbBuf = pTsBuf;
			if (g_UseServiceID)
				pDVB->m_fifoRawTS.Push(pData);
			else
				pDVB->m_fifoTS.Push(pData);
			pTsBuf = new BYTE[TS_BUFSIZE];
			pos = 0;
		}
	}
	delete[] pTsBuf;

	if (g_UseServiceID)
	{
		pDVB->m_StopTsSplit.Set();
		::pthread_join(pDVB->m_hTsSplit, NULL);
		pDVB->m_hTsSplit = 0;
		pDVB->m_StopTsSplit.Reset();
	}
	return &ret;
}

#define MAX_PID	0x2000		// (8 * sizeof(int))で割り切れる
#define PID_SET(pid, map)	((map)->bits[(pid) / (8 * sizeof(int))] |= (1 << ((pid) % (8 * sizeof(int)))))
#define PID_CLR(pid, map)	((map)->bits[(pid) / (8 * sizeof(int))] &= ~(1 << ((pid) % (8 * sizeof(int)))))
#define PID_ISSET(pid, map)	((map)->bits[(pid) / (8 * sizeof(int))] & (1 << ((pid) % (8 * sizeof(int)))))
#define PID_ZERO(map)		(::memset((map), 0 , sizeof(*(map))))
struct pid_set {
	int bits[MAX_PID / (8 * sizeof(int))];
};

void *cBonDriverDVB::TsSplitter(LPVOID pv)
{
	cBonDriverDVB *pDVB = static_cast<cBonDriverDVB *>(pv);
	BYTE *pTsBuf, pPAT[TS_PKTSIZE];
	BYTE pPMT[4104+TS_PKTSIZE];	// 4104 = 8(TSヘッダ + pointer_field + table_idからsection_length) + 4096(セクション長最大値)
	int pos;
	unsigned char pat_ci, pmt_ci, lcat_version;
	unsigned short ltsid, pidPMT, pidEMM, pmt_tail;
	BOOL bChangePMT, bSplitPMT;
	pid_set pids;

	pTsBuf = new BYTE[TS_BUFSIZE];
	pos = 0;
	pat_ci = 0x10;						// 0x1(payloadのみ) << 4 | 0x0(ci初期値)
	lcat_version = 0xff;
	ltsid = pidPMT = pidEMM = 0xffff;	// 現在のTSID及びPMT,EMMのPID
	bChangePMT = bSplitPMT = FALSE;
	PID_ZERO(&pids);

	cEvent *h[2] = { &(pDVB->m_StopTsSplit), pDVB->m_fifoRawTS.GetEventHandle() };
	while (1)
	{
		DWORD dwRet = cEvent::MultipleWait(2, h);
		switch (dwRet)
		{
		case WAIT_OBJECT_0:
			goto end;

		case WAIT_OBJECT_0 + 1:
		{
			TS_DATA *pRawBuf = NULL;
			pDVB->m_fifoRawTS.Pop(&pRawBuf);
			if (pRawBuf == NULL)	// イベントのトリガからPop()までの間に別スレッドにFlush()される可能性はゼロではない
				break;
			BYTE *pSrc = pRawBuf->pbBuf;
			DWORD dwLeft = pRawBuf->dwSize;	// 必ずTS_PKTSIZEの倍数で来る
			while (dwLeft > 0)
			{
				unsigned short pid = GetPID(&pSrc[1]);
				if (pid == 0x0000)	// PAT
				{
					// payload先頭かつadaptation_field無し、PSIのpointer_fieldは0x00の前提
					if ((pSrc[1] & 0x40) && !(pSrc[3] & 0x20) && (pSrc[4] == 0x00))
					{
						// section_length
						// 9 = transport_stream_idからlast_section_numberまでの5バイト + CRC_32の4バイト
						int len = (((int)(pSrc[6] & 0x0f) << 8) | pSrc[7]) - 9;
						// 13 = TSパケットの頭から最初のprogram_numberまでのオフセット
						int off = 13;
						// PATは1TSパケットに収まってる前提
						while ((len >= 4) && ((off + 4) < TS_PKTSIZE))
						{
							unsigned short sid = GetSID(&pSrc[off]);
							if (pDVB->m_dwServiceID == sid)
							{
								pid = GetPID(&pSrc[off+2]);
								break;
							}
							off += 4;
							len -= 4;
						}
						if (pid != 0x0000)	// 対象ServiceIDのPMTのPIDが取得できた
						{
							// transport_stream_id
							unsigned short tsid = ((unsigned short )pSrc[8] << 8) | pSrc[9];
							if (pidPMT != pid || ltsid != tsid)	// PMTのPIDが更新された or チャンネルが変更された
							{
								// TSヘッダ
								pPAT[0] = 0x47;
								pPAT[1] = 0x60;
								pPAT[2] = 0x00;
								pPAT[3] = pat_ci;
								// pointer_field
								pPAT[4] = 0x00;
								// PAT
								pPAT[5] = 0x00;		// table_id
								pPAT[6] = 0xb0;		// section_syntax_indicator(1) + '0'(1) + reserved(2) + section_length(4/12)
								pPAT[7] = 0x11;		// section_length(8/12)
								pPAT[8] = tsid >> 8;
								pPAT[9] = tsid & 0xff;
								pPAT[10] = 0xc1;	// reserved(2) + version_number(5) + current_next_indicator(1)
								pPAT[11] = 0x00;	// section_number
								pPAT[12] = 0x00;	// last_section_number

								pPAT[13] = 0x00;	// program_number(8/16)
								pPAT[14] = 0x00;	// program_number(8/16)
								pPAT[15] = 0xe0;	// reserved(3) + network_PID(5/13)
								pPAT[16] = 0x10;	// network_PID(8/13)

								// 対象ServiceIDのテーブルコピー
								pPAT[17] = pSrc[off];
								pPAT[18] = pSrc[off+1];
								pPAT[19] = pSrc[off+2];
								pPAT[20] = pSrc[off+3];

								// CRC_32
								DWORD crc = CalcCRC32(&pPAT[5], 16);
								pPAT[21] = (BYTE)(crc >> 24);
								pPAT[22] = (BYTE)((crc >> 16) & 0xff);
								pPAT[23] = (BYTE)((crc >> 8) & 0xff);
								pPAT[24] = (BYTE)(crc & 0xff);

								::memset(&pPAT[25], 0xff, TS_PKTSIZE - 25);

								ltsid = tsid;
								pidPMT = pid;
								bChangePMT = TRUE;
								bSplitPMT = FALSE;
								lcat_version = 0xff;
								pidEMM = 0xffff;

								// PIDマップ初期化
								PID_ZERO(&pids);
								// PMT PIDセット(マップにセットしても意味無いけど一応)
								PID_SET(pid, &pids);
								// CAT PIDセット(同上)
								PID_SET(0x0001, &pids);
								// NIT PIDセット
								PID_SET(0x0010, &pids);
								// SDT PIDセット
								PID_SET(0x0011, &pids);
								// EIT PIDセット
								PID_SET(0x0012, &pids);
								PID_SET(0x0026, &pids);
								PID_SET(0x0027, &pids);
								// TOT PIDセット
								PID_SET(0x0014, &pids);
								// CDT PIDセット
								PID_SET(0x0029, &pids);
							}
							else
							{
								if (pat_ci == 0x1f)
									pat_ci = 0x10;
								else
									pat_ci++;
								pPAT[3] = pat_ci;
							}
							::memcpy(&pTsBuf[pos], pPAT, TS_PKTSIZE);
							pos += TS_PKTSIZE;
						}
					}
				}
				else if (pid == 0x0001)	// CAT
				{
					// payload先頭かつadaptation_field無し、PSIのpointer_fieldは0x00の前提
					if ((pSrc[1] & 0x40) && !(pSrc[3] & 0x20) && (pSrc[4] == 0x00))
					{
						// version_number
						unsigned char ver = (pSrc[10] >> 1) & 0x1f;
						if (ver != lcat_version)
						{
							// section_length
							// 9 = 2つ目のreservedからlast_section_numberまでの5バイト + CRC_32の4バイト
							int len = (((int)(pSrc[6] & 0x0f) << 8) | pSrc[7]) - 9;
							// 13 = TSパケットの頭から最初のdescriptorまでのオフセット
							int off = 13;
							// CATも1TSパケットに収まってる前提
							while (len >= 2)
							{
								if ((off + 2) > TS_PKTSIZE)
									break;
								int cdesc_len = 2 + pSrc[off+1];
								if (cdesc_len > len || (off + cdesc_len) > TS_PKTSIZE)	// descriptor長さ異常
									break;
								if (pSrc[off] == 0x09)	// Conditional Access Descriptor
								{
									if (pSrc[off+1] >= 4 && (pSrc[off+4] & 0xe0) == 0xe0)	// 内容が妥当なら
									{
										// EMM PIDセット
										pid = GetPID(&pSrc[off+4]);
										if (pid != pidEMM)
										{
											if (pidEMM != 0xffff)
												PID_CLR(pidEMM, &pids);
											PID_SET(pid, &pids);
											pidEMM = pid;
										}
										break;	// EMMが複数のPIDで送られてくる事は無い前提
									}
								}
								off += cdesc_len;
								len -= cdesc_len;
							}
							lcat_version = ver;
						}
						::memcpy(&pTsBuf[pos], pSrc, TS_PKTSIZE);
						pos += TS_PKTSIZE;
					}
				}
				else if(pid == pidPMT)	// PMT
				{
					if (bChangePMT)	// PMTが変更された
					{
						int len;
						BYTE *p;
						// payload先頭を待つ(adaptation_fieldは無し、PSIのpointer_fieldは0x00の前提)
						if ((pSrc[1] & 0x40) && !(pSrc[3] & 0x20) && (pSrc[4] == 0x00))
						{
							// section_length
							len = (((int)(pSrc[6] & 0x0f) << 8) | pSrc[7]);
							if (len > (TS_PKTSIZE - 8))	// TSパケットを跨ってる
							{
								::memcpy(pPMT, pSrc, TS_PKTSIZE);
								// コピーしたデータの終端位置
								pmt_tail = TS_PKTSIZE;
								bSplitPMT = TRUE;
								pmt_ci = pSrc[3] & 0x0f;
								if (pmt_ci == 0x0f)
									pmt_ci = 0;
								else
									pmt_ci++;
								goto next;
							}
							// 揃った
							p = pSrc;
						}
						else
						{
							if (!bSplitPMT)	// 分割PMTの続き待ち中でなければ
								goto next;
							// CIが期待している値ではない、もしくはpayloadが無い場合
							if (((pSrc[3] & 0x0f) != pmt_ci) || !(pSrc[3] & 0x10))
							{
								// 最初からやり直し
								bSplitPMT = FALSE;
								goto next;
							}
							int adplen;
							if (pSrc[3] & 0x20)	// adaptation_field有り(まあ無いとは思うけど一応)
							{
								adplen = pSrc[4] + 1;
								if (adplen >= (TS_PKTSIZE - 4))
								{
									// adaptation_fieldの長さが異常なので最初からやり直し
									bSplitPMT = FALSE;
									goto next;
								}
							}
							else
								adplen = 0;
							// 分割PMTの続きコピー
							// pPMTのサイズはTS_PKTSIZEバイト余分に確保しているのでこれでも大丈夫
							::memcpy(&pPMT[pmt_tail], &pSrc[4 + adplen], TS_PKTSIZE - 4 - adplen);
							// section_length
							len = (((int)(pPMT[6] & 0x0f) << 8) | pPMT[7]);
							if (len > (pmt_tail - 8 + (TS_PKTSIZE - 4 - adplen)))	// まだ全部揃ってない
							{
								pmt_tail += (TS_PKTSIZE - 4 - adplen);
								if (pmt_ci == 0x0f)
									pmt_ci = 0;
								else
									pmt_ci++;
								goto next;
							}
							// 揃った
							p = pPMT;
						}
						// この時点でセクションは必ず揃っている
						int limit = 8 + len;
						// PCR PIDセット
						pid = GetPID(&p[13]);
						PID_SET(pid, &pids);
						// program_info_length
						int desc_len = (((int)(p[15] & 0x0f) << 8) | p[16]);
						// 17 = 最初のdescriptorのオフセット
						int off = 17;
						int left = desc_len;
						while (left >= 2)
						{
							if ((off + 2) > limit)	// program_info_length異常
							{
								bSplitPMT = FALSE;
								goto next;
							}
							int cdesc_len = 2 + p[off+1];
							if (cdesc_len > left || (off + cdesc_len) > limit)	// descriptor長さ異常
							{
								bSplitPMT = FALSE;
								goto next;
							}
							if (p[off] == 0x09)	// Conditional Access Descriptor
							{
								if (p[off+1] >= 4 && (p[off+4] & 0xe0) == 0xe0)	// 内容が妥当なら
								{
									// ECM PIDセット
									pid = GetPID(&p[off+4]);
									PID_SET(pid, &pids);
								}
							}
							off += cdesc_len;
							left -= cdesc_len;
						}
						// データ異常が無ければ必要無いが一応
						off = 17 + desc_len;
						// 13 = program_numberからprogram_info_lengthまでの9バイト + CRC_32の4バイト
						len -= (13 + desc_len);
						while (len >= 5)
						{
							if ((off + 5) > limit)	// program_info_length異常
							{
								bSplitPMT = FALSE;
								goto next;
							}
							if (p[off] != 0x0d)	// stream_type "ISO/IEC 13818-6 type D"は破棄
							{
								pid = GetPID(&p[off+1]);
								PID_SET(pid, &pids);
							}
							// ES_info_length + 5(stream_typeからES_info_lengthまでの5バイト)
							int cdesc_len = (((int)(p[off+3] & 0x0f) << 8) | p[off+4]) + 5;
							off += cdesc_len;
							len -= cdesc_len;
						}
						// PMTが複数パケットに分かれていない場合のみ保存する
						// 複数パケットに分かれていた場合は今回のPMTは破棄(次回以降のから保存する事になる)
						if (!bSplitPMT)
						{
							::memcpy(&pTsBuf[pos], pSrc, TS_PKTSIZE);
							pos += TS_PKTSIZE;
						}
						bChangePMT = bSplitPMT = FALSE;
					}
					else
					{
						::memcpy(&pTsBuf[pos], pSrc, TS_PKTSIZE);
						pos += TS_PKTSIZE;
					}
				}
				else
				{
					if (PID_ISSET(pid, &pids))
					{
						::memcpy(&pTsBuf[pos], pSrc, TS_PKTSIZE);
						pos += TS_PKTSIZE;
					}
				}

			next:
				pSrc += TS_PKTSIZE;
				dwLeft -= TS_PKTSIZE;

				// 1ループでのposの増加は0もしくはTS_PKTSIZEなので、
				// バウンダリチェックはこれで大丈夫なハズ
				if (pos == TS_BUFSIZE)
				{
					TS_DATA *pData = new TS_DATA();
					pData->dwSize = TS_BUFSIZE;
					pData->pbBuf = pTsBuf;
					pDVB->m_fifoTS.Push(pData);
					pTsBuf = new BYTE[TS_BUFSIZE];
					pos = 0;
				}
			}
			delete pRawBuf;
		}
		}
	}
end:
	delete[] pTsBuf;
	return NULL;
}

}
