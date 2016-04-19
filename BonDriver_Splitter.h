#ifndef __BONDRIVER_SPLITTER_H__
#define __BONDRIVER_SPLITTER_H__
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
#include <fcntl.h>
#include <dlfcn.h>
#include <pthread.h>
#include <string>
#include <queue>
#include <map>
#include "typedef.h"
#include "IBonDriver2.h"

namespace BonDriver_Splitter {

#define TS_SYNC_BYTE		0x47
#define TS_PKTSIZE			188
#define TTS_PKTSIZE			192
#define TS_FEC_PKTSIZE		204
#define TTS_FEC_PKTSIZE		208
#define TS_BUFSIZE			(TS_PKTSIZE * 256)
#define TS_FIFOSIZE			512
#define WAIT_TIME			10	// GetTsStream()の後で、dwRemainが0だった場合に待つ時間(ms)
#define TUNER_NAME			"BonDriver_Splitter"

////////////////////////////////////////////////////////////////////////////////

#include "Common.h"
static const size_t g_TsFifoSize = TS_FIFOSIZE;
#include "TsQueue.h"

////////////////////////////////////////////////////////////////////////////////

#define MAX_DRIVER			16		// 99以下
#define MAX_SPACE			16		// 99以下
#define MAX_SPACE_SIZE		128
#define MAX_CH				128		// 999以下
#define MAX_CN_SIZE			128

struct stChannel {
	char ChName[MAX_CN_SIZE];
	int BonNo;
	DWORD BonSpace;
	DWORD BonChannel;
	DWORD ServiceID;
};

struct stSpace {
	char SpaceName[MAX_SPACE_SIZE];
	BOOL bUseServiceID;
	std::vector<stChannel> vstChannel;
};

////////////////////////////////////////////////////////////////////////////////

class cBonDriverSplitter : public IBonDriver2 {
	pthread_cond_t m_c;
	pthread_mutex_t m_m;
	HMODULE m_hBonModule;
	IBonDriver2 *m_pIBon2;
	cCriticalSection m_bonLock;
	cCriticalSection m_writeLock;
	cCriticalSection m_splitterLock;
	cTSFifo m_fifoTS;
	cRawTSFifo m_fifoRawTS;
	TS_DATA *m_LastBuf;
	char m_TunerName[64];
	BOOL m_bTuner;
	int m_iBonNo;
	DWORD m_dwSpace;
	DWORD m_dwChannel;
	DWORD m_dwServiceID;
	BOOL m_bUseServiceID;
	pthread_t m_hTsRead;
	pthread_t m_hTsSplit;
	DWORD m_tRet;
	volatile BOOL m_bStopTsRead;
	volatile BOOL m_bChannelChanged;
	DWORD m_dwPos;
	DWORD m_dwSplitterPos;
	cEvent m_StopTsSplit;
	DWORD m_dwUnitSize;
	DWORD m_dwSyncBufPos;
	BYTE m_SyncBuf[256];

	void TsFlush()
	{
		m_splitterLock.Enter();
		m_fifoTS.Flush();
		m_fifoRawTS.Flush();
		m_dwPos = 0;
		m_dwSplitterPos = 0;
		m_splitterLock.Leave();
	}
	static void *TsReader(LPVOID pv);
	static void *TsSplitter(LPVOID pv);
	BOOL TsSync(BYTE *pSrc, DWORD dwSrc, BYTE **ppDst, DWORD *pdwDst);
	static inline unsigned short GetPID(BYTE *p){ return (((unsigned short)(p[0] & 0x1f) << 8) | p[1]); }
	static inline unsigned short GetSID(BYTE *p){ return (((unsigned short)p[0] << 8) | p[1]); }

public:
	static cCriticalSection m_sInstanceLock;
	static cBonDriverSplitter *m_spThis;
	static BOOL m_sbInit;

	cBonDriverSplitter();
	virtual ~cBonDriverSplitter();

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
#endif	// __BONDRIVER_SPLITTER_H__
