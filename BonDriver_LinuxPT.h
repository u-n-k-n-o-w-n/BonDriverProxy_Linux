#ifndef _BONDRIVER_LINUXPT_H_
#define _BONDRIVER_LINUXPT_H_
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iconv.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <pthread.h>
#include <queue>
#include "typedef.h"
#include "IBonDriver2.h"
#include "pt1_ioctl.h"

namespace BonDriver_LinuxPT {

#define MAX_CH				128
#define MAX_CN_LEN			64

#define TS_PKTSIZE			188
#define TS_BUFSIZE			(TS_PKTSIZE * 256)
#define TS_FIFOSIZE			512
#define WAIT_TIME			10	// デバイスからのread()でエラーが発生した場合の、次のread()までの間隔(ms)
#define TUNER_NAME			"BonDriver_LinuxPT"

////////////////////////////////////////////////////////////////////////////////

#include "Common.h"
static const size_t g_TsFifoSize = TS_FIFOSIZE;
#include "TsQueue.h"

////////////////////////////////////////////////////////////////////////////////

struct stChannel {
	char strChName[MAX_CN_LEN];
	FREQUENCY freq;
	DWORD ServiceID;	// WORDで良いんだけどアライメント入るとどうせ同じなので
	BOOL bUnused;
};

class cBonDriverLinuxPT : public IBonDriver2 {
	pthread_cond_t m_c;
	pthread_mutex_t m_m;
	cTSFifo m_fifoTS;
	cRawTSFifo m_fifoRawTS;
	TS_DATA *m_LastBuf;
	cCriticalSection m_writeLock;
	char m_TunerName[64];
	BOOL m_bTuner;
	float m_fCNR;
	DWORD m_dwSpace;
	DWORD m_dwChannel;
	DWORD m_dwServiceID;
	int m_fd;
	pthread_t m_hTsRead;
	pthread_t m_hTsSplit;
	DWORD m_tRet;
	volatile BOOL m_bStopTsRead;
	volatile BOOL m_bChannelChanged;
	volatile BOOL m_bUpdateCNR;
	cEvent m_StopTsSplit;

	void TsFlush(BOOL bUseServiceID)
	{
		m_fifoTS.Flush();
		if (bUseServiceID)
			m_fifoRawTS.Flush();
	}
	static void *TsReader(LPVOID pv);
	static void *TsSplitter(LPVOID pv);
	static inline unsigned short GetPID(BYTE *p){ return (((unsigned short)(p[0] & 0x1f) << 8) | p[1]); }
	static inline unsigned short GetSID(BYTE *p){ return (((unsigned short)p[0] << 8) | p[1]); }

public:
	static cCriticalSection m_sInstanceLock;
	static cBonDriverLinuxPT *m_spThis;
	static BOOL m_sbInit;
	static BOOL m_sbPT;

	cBonDriverLinuxPT();
	virtual ~cBonDriverLinuxPT();

	// IBonDriver
	const BOOL OpenTuner(void);
	void CloseTuner(void);
	const BOOL SetChannel(const BYTE bCh);
	const float GetSignalLevel(void);
	const DWORD WaitTsStream(const DWORD dwTimeOut = 0);
	const DWORD GetReadyCount(void);
	const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain);
	const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain);
	void PurgeTsStream(void);
	void Release(void);

	// IBonDriver2
	LPCTSTR GetTunerName(void);
	const BOOL IsTunerOpening(void);
	LPCTSTR EnumTuningSpace(const DWORD dwSpace);
	LPCTSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel);
	const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel);
	const DWORD GetCurSpace(void);
	const DWORD GetCurChannel(void);
};

}
#endif	// _BONDRIVER_LINUXPT_H_
