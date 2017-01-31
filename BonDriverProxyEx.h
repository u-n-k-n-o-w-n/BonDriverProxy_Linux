#ifndef __BONDRIVER_PROXYEX_H__
#define __BONDRIVER_PROXYEX_H__

// Shut up warning
//   warning: 'daemon' is deprecated: first deprecated in OS X 10.5
#if __APPLE__
#define daemon fake_daemon
#endif

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <pthread.h>
#include <vector>
#include <list>
#include <queue>
#include <map>
#include "typedef.h"
#include "IBonDriver3.h"

#if __APPLE__
#undef daemon
extern "C" {
    extern int daemon(int, int);
}
#endif

namespace BonDriverProxyEx {

#define WAIT_TIME	10	// GetTsStream()の後で、dwRemainが0だった場合に待つ時間(ms)

////////////////////////////////////////////////////////////////////////////////

static char g_Host[256] = "127.0.0.1";
static char g_Port[8] = "1192";
static size_t g_PacketFifoSize = 64;
static DWORD g_TsPacketBufSize = (188 * 1024);
static DWORD g_OpenTunerRetDelay = 0;
static BOOL g_DisableUnloadBonDriver = TRUE;	// bdplの標準はTRUEにする

#define MAX_DRIVERS	64		// ドライバのグループ数とグループ内の数の両方
static char **g_ppDriver[MAX_DRIVERS];
struct stDriver {
	char *strBonDriver;
	HMODULE hModule;
	BOOL bUsed;
	time_t tLoad;
};
static std::map<char *, std::vector<stDriver> > DriversMap;

////////////////////////////////////////////////////////////////////////////////

#include "Common.h"
#include "BdpPacket.h"

////////////////////////////////////////////////////////////////////////////////

class cProxyServerEx;

struct stTsReaderArg {
	IBonDriver *pIBon;
	volatile BOOL StopTsRead;
	volatile BOOL ChannelChanged;
	DWORD pos;
	std::list<cProxyServerEx *> TsReceiversList;
	std::list<cProxyServerEx *> WaitExclusivePrivList;
	cCriticalSection TsLock;
	stTsReaderArg()
	{
		StopTsRead = FALSE;
		ChannelChanged = TRUE;
		pos = 0;
	}
};

class cProxyServerEx {
	IBonDriver *m_pIBon;
	IBonDriver2 *m_pIBon2;
	IBonDriver3 *m_pIBon3;
	HMODULE m_hModule;
	SOCKET m_s;
	pthread_cond_t m_c;
	pthread_mutex_t m_m;
	cEvent m_Error;
	BOOL m_bTunerOpen;
	BYTE m_bChannelLock;
	DWORD m_tRet;
	pthread_t m_hTsRead;
	stTsReaderArg *m_pTsReaderArg;
	DWORD m_dwSpace;
	DWORD m_dwChannel;
	char *m_pDriversMapKey;
	int m_iDriverNo;
	int m_iDriverUseOrder;
	cPacketFifo m_fifoSend;
	cPacketFifo m_fifoRecv;

	DWORD Process();
	int ReceiverHelper(char *pDst, DWORD left);
	static void *Receiver(LPVOID pv);
	void makePacket(enumCommand eCmd, BOOL b);
	void makePacket(enumCommand eCmd, DWORD dw);
	void makePacket(enumCommand eCmd, LPCTSTR str);
	void makePacket(enumCommand eCmd, BYTE *pSrc, DWORD dwSize, float fSignalLevel);
	static void *Sender(LPVOID pv);
	static void *TsReader(LPVOID pv);
	void StopTsReceive();

	BOOL SelectBonDriver(LPCSTR p, BYTE bChannelLock);
	IBonDriver *CreateBonDriver();

	// IBonDriver
	const BOOL OpenTuner(void);
	void CloseTuner(void);
	void PurgeTsStream(void);

	// IBonDriver2
	LPCTSTR EnumTuningSpace(const DWORD dwSpace);
	LPCTSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel);
	const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel);

	// IBonDriver3
	const DWORD GetTotalDeviceNum(void);
	const DWORD GetActiveDeviceNum(void);
	const BOOL SetLnbPower(const BOOL bEnable);

public:
	cProxyServerEx();
	~cProxyServerEx();
	void setSocket(SOCKET s){ m_s = s; }
	static void *Reception(LPVOID pv);
};

static std::list<cProxyServerEx *> g_InstanceList;
static cCriticalSection g_Lock;

}
#endif	// __BONDRIVER_PROXYEX_H__
