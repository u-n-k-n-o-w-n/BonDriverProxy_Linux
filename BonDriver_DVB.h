#ifndef _BONDRIVER_DVB_H_
#define _BONDRIVER_DVB_H_
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iconv.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <pthread.h>
#include <queue>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include "typedef.h"
#include "IBonDriver2.h"

namespace BonDriver_DVB {

#define MAX_CH				128
#define MAX_CN_LEN			64

#define TS_SYNC_BYTE		0x47
#define TS_PKTSIZE			188
#define TTS_PKTSIZE			192
#define TS_FEC_PKTSIZE		204
#define TTS_FEC_PKTSIZE		208
#define TS_BUFSIZE			(TS_PKTSIZE * 1024)
#define TS_FIFOSIZE			512
#define WAIT_TIME			10	// デバイスからのread()を試みた後、次回のread()までに無条件に待つ時間(ms)
#define WAIT_TIME_POLL		500	// デバイスがread()可能か確認する際のタイムアウト時間(ms)
#define TUNER_NAME			"BonDriver_DVB"

////////////////////////////////////////////////////////////////////////////////

#include "Common.h"
static const size_t g_TsFifoSize = TS_FIFOSIZE;
#include "TsQueue.h"

////////////////////////////////////////////////////////////////////////////////

struct stFrequency {
	int frequencyno;
	unsigned int tsid;
};

struct stChannel {
	char strChName[MAX_CN_LEN];
	stFrequency freq;
	DWORD ServiceID;	// WORDで良いんだけどアライメント入るとどうせ同じなので
	BOOL bUnused;
};

class cBonDriverDVB : public IBonDriver2 {
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
	int m_fefd;
	int m_dmxfd;
	int m_dvrfd;
	pthread_t m_hTsRead;
	pthread_t m_hTsSplit;
	DWORD m_tRet;
	volatile BOOL m_bStopTsRead;
	volatile BOOL m_bChannelChanged;
	volatile BOOL m_bUpdateCNR;
	cEvent m_StopTsSplit;
	DWORD m_dwUnitSize;
	DWORD m_dwSyncBufPos;
	BYTE m_SyncBuf[256];

	void TsFlush(BOOL bUseServiceID)
	{
		m_fifoTS.Flush();
		if (bUseServiceID)
			m_fifoRawTS.Flush();
	}
	static void *TsReader(LPVOID pv);
	static void *TsSplitter(LPVOID pv);
	BOOL TsSync(BYTE *pSrc, DWORD dwSrc, BYTE **ppDst, DWORD *pdwDst);
	static inline unsigned short GetPID(BYTE *p){ return (((unsigned short)(p[0] & 0x1f) << 8) | p[1]); }
	static inline unsigned short GetSID(BYTE *p){ return (((unsigned short)p[0] << 8) | p[1]); }

public:
	static cCriticalSection m_sInstanceLock;
	static cBonDriverDVB *m_spThis;
	static BOOL m_sbInit;

	cBonDriverDVB();
	virtual ~cBonDriverDVB();

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
#endif	// _BONDRIVER_DVB_H_
