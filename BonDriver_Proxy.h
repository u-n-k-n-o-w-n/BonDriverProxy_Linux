#ifndef __BONDRIVER_PROXY_H__
#define __BONDRIVER_PROXY_H__
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <pthread.h>
#include <list>
#include <queue>
#include "typedef.h"
#include "IBonDriver3.h"

namespace BonDriver_Proxy {

#define TUNER_NAME			"BonDriver_Proxy"

////////////////////////////////////////////////////////////////////////////////

#define MAX_HOST_LEN	256
#define MAX_PORT_LEN	8
static char g_Host[MAX_HOST_LEN];
static char g_Port[MAX_PORT_LEN];
static char g_BonDriver[512];
static BOOL g_ChannelLock = FALSE;
static size_t g_PacketFifoSize = 32;
static size_t g_TsFifoSize = 64;
static DWORD g_TsPacketBufSize = (188 * 1024);
static int g_ConnectTimeOut = 5;
static BOOL g_UseMagicPacket = FALSE;
static char g_TargetMac[6];
static char g_TargetHost[MAX_HOST_LEN];
static char g_TargetPort[MAX_PORT_LEN];

////////////////////////////////////////////////////////////////////////////////

#include "Common.h"
#include "BdpPacket.h"
#include "TsQueue.h"

////////////////////////////////////////////////////////////////////////////////

enum enumbRes {
	ebResSelectBonDriver = 0,
	ebResCreateBonDriver,
	ebResOpenTuner,
	ebResPurgeTsStream,
	ebResSetLnbPower,
	ebResNum,
};

enum enumdwRes {
	edwResSetChannel2 = 0,
	edwResGetTotalDeviceNum,
	edwResGetActiveDeviceNum,
	edwResNum,
};

enum enumpRes {
	epResEnumTuningSpace = 0,
	epResEnumChannelName,
	epResNum,
};

class cProxyClient : public IBonDriver3 {
	SOCKET m_s;
	pthread_t m_hThread;
	DWORD m_tRet;
//	volatile int m_iEndCount;
	pthread_cond_t m_c;
	pthread_mutex_t m_m;
	cEvent m_Error;
	cEvent m_SingleShot;
	cPacketFifo m_fifoSend;
	cPacketFifo m_fifoRecv;
	cTSFifo m_fifoTS;
	TS_DATA *m_LastBuf;
	cEvent *m_bResEvent[ebResNum];
	BOOL m_bRes[ebResNum];
	cEvent *m_dwResEvent[edwResNum];
	DWORD m_dwRes[edwResNum];
	cEvent *m_pResEvent[epResNum];
	TCHAR *m_pRes[epResNum];
	DWORD m_dwBufPos;
	TCHAR *m_pBuf[8];
	float m_fSignalLevel;
	DWORD m_dwSpace;
	DWORD m_dwChannel;
	BOOL m_bBonDriver;
	BOOL m_bTuner;
	BOOL m_bRereased;
	char m_TunerName[64];
	cCriticalSection m_writeLock;
	cCriticalSection m_readLock;	// 一応ロックしてるけど、厳密には本来求めてるロックは保証できてない

	DWORD Process();
	int ReceiverHelper(char *pDst, DWORD left);
	static void *Receiver(LPVOID pv);
	void makePacket(enumCommand eCmd);
	void makePacket(enumCommand eCmd, LPCSTR);
	void makePacket(enumCommand eCmd, BOOL b);
	void makePacket(enumCommand eCmd, DWORD dw);
	void makePacket(enumCommand eCmd, DWORD dw1, DWORD dw2);
	void makePacket(enumCommand eCmd, DWORD dw1, DWORD dw2, BOOL b);
	static void *Sender(LPVOID pv);
	void TsFlush(){ m_fifoTS.Flush(); }
//	void SleepLock(int n){ while (m_iEndCount != n){ ::usleep(1000); }; }

public:
	cProxyClient();
	virtual ~cProxyClient();
	void setSocket(SOCKET s){ m_s = s; }
	void setThreadHandle(pthread_t h){ m_hThread = h; }
	DWORD WaitSingleShot(){ return m_SingleShot.Wait(&m_Error); }
	static void *ProcessEntry(LPVOID pv);

	BOOL SelectBonDriver();
	BOOL CreateBonDriver();

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

	// IBonDriver3
	const DWORD GetTotalDeviceNum(void);
	const DWORD GetActiveDeviceNum(void);
	const BOOL SetLnbPower(const BOOL bEnable);
};

static std::list<cProxyClient *> g_InstanceList;
static cCriticalSection g_Lock;

}
#endif	// __BONDRIVER_PROXY_H__
