/*
$ g++ -O2 -Wall -pthread -shared -fPIC -o BonDriver_LinuxPT.so BonDriver_LinuxPT.cpp -ldl
*/
#include "BonDriver_LinuxPT.h"
#include "plex.cpp"

namespace BonDriver_LinuxPT {

static char g_strSpace[32];
static stChannel g_stChannels[2][MAX_CH];
static char g_Device[32];
static int g_Type;		// 0 : ISDB_S / 1 : ISDB_T
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

static int Init()
{
	FILE *fp;
	char *p, buf[512];

	Dl_info info;
	if (::dladdr((void *)Init, &info) != 0)
	{
		::strncpy(buf, info.dli_fname, sizeof(buf) - 8);
		buf[sizeof(buf) - 8] = '\0';
		::strcat(buf, ".conf");
	}
	else
		::strcpy(buf, DEFAULT_CONF_NAME);

	fp = ::fopen(buf, "r");
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
	BOOL bsFlag = FALSE;
	while (::fgets(buf, sizeof(buf), fp))
	{
		if (buf[0] == ';')
			continue;
		p = buf + ::strlen(buf) - 1;
		while (*p == '\r' || *p == '\n')
			*p-- = '\0';
		if ((idx != 0) && (::strncmp(buf, "#ISDB_S", 7) == 0))
			idx = 0;
		else if ((idx != 1) && (::strncmp(buf, "#ISDB_T", 7) == 0))
			idx = 1;
		else if (!bdFlag && (::strncmp(buf, "#DEVICE=", 8) == 0))
		{
			p = &buf[8];
			while (*p == ' ' || *p == '\t')
				p++;
			::strncpy(g_Device, p, sizeof(g_Device) - 1);
			g_Device[sizeof(g_Device) - 1] = '\0';
			if ((p = ::strstr(g_Device, "video")) != NULL)	// PT
				g_Type = (::atoi(p + 5) / 2) % 2;
			else
			{
				if((p = ::strstr(g_Device, "asv5220")) != NULL)	// PX-W3PE
					g_Type = (::atoi(p + 7) / 2) % 2;
				else if((p = ::strstr(g_Device, "pxq3pe")) != NULL)	// PX-Q3PE
					g_Type = (::atoi(p + 6) / 2) % 2;
				else if((p = ::strstr(g_Device, "pxw3u3")) != NULL)	// PX-W3U3
					g_Type = ::atoi(p + 6) % 2;
				else if((p = ::strstr(g_Device, "pxs3u")) != NULL)	// PX-S3U / PX-S3U2
					g_Type = ::atoi(p + 5) % 2;
				cBonDriverLinuxPT::m_sbPT = FALSE;
			}
			if (g_Type == 0)
				p = (char *)"BS/CS110";
			else
				p = (char *)"UHF";
			if (Convert(p, g_strSpace, sizeof(g_strSpace)) < 0)
			{
				::fclose(fp);
				return -2;
			}
			bdFlag = TRUE;
		}
		else if (!blFlag && (::strncmp(buf, "#USELNB=", 8) == 0))
		{
			p = &buf[8];
			while (*p == ' ' || *p == '\t')
				p++;
			g_UseLNB = ::atoi(p);
			blFlag = TRUE;
		}
		else if (!bsFlag && (::strncmp(buf, "#USESERVICEID=", 14) == 0))
		{
			p = &buf[14];
			while (*p == ' ' || *p == '\t')
				p++;
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
					g_stChannels[idx][dw].freq.slot = ::atoi(cp[3]);
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
	double P = ::log10(5505024 / (double)signal) * 10;
	return (float)((0.000024 * P * P * P * P) - (0.0016 * P * P * P) + (0.0398 * P * P) + (0.5491 * P) + 3.0965);
}

cBonDriverLinuxPT *cBonDriverLinuxPT::m_spThis = NULL;
cCriticalSection cBonDriverLinuxPT::m_sInstanceLock;
BOOL cBonDriverLinuxPT::m_sbInit = TRUE;
BOOL cBonDriverLinuxPT::m_sbPT = TRUE;

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

cBonDriverLinuxPT::cBonDriverLinuxPT() : m_fifoTS(m_c, m_m), m_fifoRawTS(m_c, m_m), m_StopTsSplit(m_c, m_m)
{
	m_spThis = this;
	Convert((char *)TUNER_NAME, m_TunerName, sizeof(m_TunerName));
	m_LastBuff = NULL;
	m_bTuner = FALSE;
	m_fSignalLevel = 0;
	m_dwSpace = m_dwChannel = 0xff;
	m_dwServiceID = 0xffffffff;
	m_fd = -1;
	m_hTsRead = m_hTsSplit = 0;
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
		TsFlush(g_UseServiceID);
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
	if (m_sbPT == FALSE)
		PLEX::InitPlexTuner(m_fd);
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
		TsFlush(g_UseServiceID);
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
	if (dwChannel == m_dwChannel)
		return TRUE;

	BOOL bFlag = TRUE;
	if (g_UseServiceID)
	{
		if (m_dwChannel != 0xff)
		{
			if ((g_stChannels[g_Type][dwChannel].freq.frequencyno == g_stChannels[g_Type][m_dwChannel].freq.frequencyno) &&
				(g_stChannels[g_Type][dwChannel].freq.slot == g_stChannels[g_Type][m_dwChannel].freq.slot))
			{
				bFlag = FALSE;
			}
		}
		m_dwServiceID = g_stChannels[g_Type][dwChannel].ServiceID;
	}

	if (bFlag)
	{
		if (::ioctl(m_fd, SET_CHANNEL, &(g_stChannels[g_Type][dwChannel].freq)) < 0)
		{
			::fprintf(stderr, "SetChannel() ioctl(SET_CHANNEL) error: %s\n", g_Device);
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
		if (::pthread_create(&m_hTsRead, NULL, cBonDriverLinuxPT::TsReader, this))
		{
			::perror("pthread_create1");
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

	if (g_UseServiceID)
	{
		if (::pthread_create(&(pLinuxPT->m_hTsSplit), NULL, cBonDriverLinuxPT::TsSplitter, pLinuxPT))
		{
			::perror("pthread_create2");
			ret = 301;
			return &ret;
		}
	}

	ts.tv_sec = 0;
	ts.tv_nsec = WAIT_TIME * 1000 * 1000;

	if (::ioctl(pLinuxPT->m_fd, START_REC, 0) < 0)
	{
		::fprintf(stderr, "TsReader() ioctl(START_REC) error: %s\n", g_Device);
		ret = 302;
		goto end;
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
					f = GetSignalLevel_S(signal);
				else
					f = GetSignalLevel_T(signal);
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
			if (g_UseServiceID)
				pLinuxPT->m_fifoRawTS.Push(pData);
			else
				pLinuxPT->m_fifoTS.Push(pData);
			pTsBuf = new BYTE[TS_BUFSIZE];
			pos = 0;
		}
	}
	delete[] pTsBuf;
end:
	if (g_UseServiceID)
	{
		pLinuxPT->m_StopTsSplit.Set();
		::pthread_join(pLinuxPT->m_hTsSplit, NULL);
		pLinuxPT->m_hTsSplit = 0;
		pLinuxPT->m_StopTsSplit.Reset();
	}
	return &ret;
}

#define MAX_PID	0x2000		// (8 * sizeof(int))で割り切れる
#define PID_SET(pid, map)	((map)->bits[(pid) / (8 * sizeof(int))] |= (1 << ((pid) % (8 * sizeof(int)))))
#define PID_ISSET(pid, map)	((map)->bits[(pid) / (8 * sizeof(int))] & (1 << ((pid) % (8 * sizeof(int)))))
#define PID_ZERO(map)		(::memset((map), 0 , sizeof(*(map))))
struct pid_set {
	int bits[MAX_PID / (8 * sizeof(int))];
};

void *cBonDriverLinuxPT::TsSplitter(LPVOID pv)
{
	cBonDriverLinuxPT *pLinuxPT = static_cast<cBonDriverLinuxPT *>(pv);
	BYTE *pTsBuf, pPAT[TS_PKTSIZE], pPMT[TS_PKTSIZE * 2];
	int pos;
	unsigned char pat_ci, pmt_ci;
	unsigned short ltsid, pidPMT;
	BOOL bChangePMT, bSplitPMT;
	pid_set pids;

	pTsBuf = new BYTE[TS_BUFSIZE];
	pos = 0;
	pat_ci = 0x10;				// 0x1(payloadのみ) << 4 | 0x0(ci初期値)
	ltsid = pidPMT = 0xffff;	// 現在のTSID及びPMTのPID
	bChangePMT = bSplitPMT = FALSE;
	PID_ZERO(&pids);

	cEvent *h[2] = { &(pLinuxPT->m_StopTsSplit), pLinuxPT->m_fifoRawTS.GetEventHandle() };
	while (1)
	{
		DWORD dwRet = cEvent::MultipleWait(2, h);
		switch (dwRet)
		{
		case WAIT_OBJECT_0:
			goto end;

		case WAIT_OBJECT_0 + 1:
		{
			TS_DATA *pRawBuff = NULL;
			pLinuxPT->m_fifoRawTS.Pop(&pRawBuff);
			BYTE *pSrc = pRawBuff->pbBuff;
			DWORD dwLeft = pRawBuff->dwSize;	// 必ずTS_PKTSIZEの倍数で来る
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
							if (pLinuxPT->m_dwServiceID == sid)
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

								// PIDマップ初期化
								PID_ZERO(&pids);
								// PMT PIDセット
								PID_SET(pid, &pids);
								// SDT PIDセット
								PID_SET(0x11, &pids);
								// EIT PIDセット
								PID_SET(0x12, &pids);
								// TOT PIDセット
								PID_SET(0x14, &pids);
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
				else if(pid == pidPMT)	// PMT
				{
					if (bChangePMT)	// PMTが変更された
					{
						int len, adplen;
						BYTE *p;
						BOOL bProc = TRUE;
						// payload先頭を待つ(adaptation_fieldは無し、PSIのpointer_fieldは0x00の前提)
						if ((pSrc[1] & 0x40) && !(pSrc[3] & 0x20) && (pSrc[4] == 0x00))
						{
							// section_length
							len = (((int)(pSrc[6] & 0x0f) << 8) | pSrc[7]);
							if (len > (TS_PKTSIZE - 8))	// TSパケットを跨ってる
							{
								::memcpy(pPMT, pSrc, TS_PKTSIZE);
								bSplitPMT = TRUE;
								bProc = FALSE;
								pmt_ci = pSrc[3] & 0x0f;
								if (pmt_ci == 0x0f)
									pmt_ci = 0;
								else
									pmt_ci++;
							}
							else
								p = pSrc;
						}
						else
						{
							if (bSplitPMT)	// 分割PMTの続き待ち
							{
								// CIが期待している値で、かつpayloadあり
								if (((pSrc[3] & 0x0f) == pmt_ci) && (pSrc[3] & 0x10))
								{
									if (pSrc[3] & 0x20)	// adaptation_field有り(まあ無いとは思うけど一応)
										adplen = pSrc[4] + 1;
									else
										adplen = 0;
									if (adplen >= (TS_PKTSIZE - 4))
									{
										// adaptation_fieldの長さが異常なので最初からやり直し
										bSplitPMT = FALSE;
										bProc = FALSE;
									}
									else
									{
										// 分割PMTの続きコピー
										::memcpy(&pPMT[TS_PKTSIZE], &pSrc[4 + adplen], TS_PKTSIZE - 4 - adplen);
										// section_length
										len = (((int)(pPMT[6] & 0x0f) << 8) | pPMT[7]);
										p = pPMT;
									}
								}
								else
								{
									// CIが一致してない時は最初からやり直し
									bSplitPMT = FALSE;
									bProc = FALSE;
								}
							}
							else
								bProc = FALSE;
						}

						if (bProc)
						{
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
								int cdesc_len = 2 + p[off+1];
								if (cdesc_len > left)	// descriptor長さ異常
									break;
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
							int limit;
							if (bSplitPMT)
								limit = (TS_PKTSIZE * 2) - 4 - adplen;	// 2TSパケット分までしか確認しない
							else
								limit = TS_PKTSIZE;	// 分割ではない時はadaptation_field無しなのはチェック済み
							while (len >= 5)
							{
								if ((off + 4) >= limit)
									break;
								if (p[off] != 0x0d)	// データ放送は破棄
								{
									pid = GetPID(&p[off+1]);
									PID_SET(pid, &pids);
								}
								// ES_info_length + 5(stream_typeからES_info_lengthまでの5バイト)
								int cdesc_len = (((int)(p[off+3] & 0x0f) << 8) | p[off+4]) + 5;
								off += cdesc_len;
								len -= cdesc_len;
							}
							// PMTが複数パケットに分かれている場合は、pTsBufのバウンダリチェックが
							// メンドクサイので今回のPMTは破棄(次回以降のから保存する事になる)
							if (!bSplitPMT)
							{
								::memcpy(&pTsBuf[pos], pSrc, TS_PKTSIZE);
								pos += TS_PKTSIZE;
							}
							bChangePMT = bSplitPMT = FALSE;
						}
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

				pSrc += TS_PKTSIZE;
				dwLeft -= TS_PKTSIZE;

				// 1ループでのposの増加は0もしくはTS_PKTSIZEなので、
				// バウンダリチェックはこれで大丈夫なハズ
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
			delete pRawBuff;
		}
		}
	}
end:
	delete[] pTsBuf;
	return NULL;
}

}
