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

#define DEFAULT_CONF_NAME	"BonDriver_Proxy.conf"
#define TUNER_NAME			"BonDriver_Proxy"

////////////////////////////////////////////////////////////////////////////////

#define MAX_HOST_LEN	64
static char g_Host[MAX_HOST_LEN];
static unsigned short g_Port;
static char g_BonDriver[512];
static BOOL g_ChannelLock = FALSE;
static size_t g_PacketFifoSize = 32;
static size_t g_TsFifoSize = 64;
static DWORD g_TsPacketBufSize = (188 * 1024);
static int g_ConnectTimeOut = 5;
static BOOL g_UseMagicPacket = FALSE;
static char g_TargetMac[6];
static char g_TargetHost[MAX_HOST_LEN];
static unsigned short g_TargetPort;

////////////////////////////////////////////////////////////////////////////////

class cCriticalSection {
	pthread_mutex_t m_m;
public:
	cCriticalSection()
	{
		pthread_mutexattr_t attr;
		::pthread_mutexattr_init(&attr);
		::pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		::pthread_mutex_init(&m_m, &attr);
	}
	~cCriticalSection(){ ::pthread_mutex_destroy(&m_m); }
	void Enter(){ ::pthread_mutex_lock(&m_m); }
	void Leave(){ ::pthread_mutex_unlock(&m_m); }
};

class cLock {
	cCriticalSection &m_c;
public:
	cLock(cCriticalSection &ref) : m_c(ref) { m_c.Enter(); }
	~cLock(){ m_c.Leave(); }
};

#define LOCK(key) cLock __Lock__(key)

class cEvent {
	pthread_cond_t &m_c;
	pthread_mutex_t &m_m;
	BOOL m_bAutoReset;
	volatile BOOL m_bActive;

public:
	cEvent(pthread_cond_t &c, pthread_mutex_t &m) : m_c(c), m_m(m)
	{
		m_bActive = FALSE;
		m_bAutoReset = FALSE;
	}
	~cEvent(){}
	void SetAutoReset(BOOL b){ m_bAutoReset = b; }
	inline BOOL IsSet(){ return m_bActive; }

	BOOL Set()
	{
		::pthread_mutex_lock(&m_m);
		m_bActive = TRUE;
		::pthread_cond_broadcast(&m_c);
		::pthread_mutex_unlock(&m_m);
		return TRUE;
	}

	BOOL Reset()
	{
		::pthread_mutex_lock(&m_m);
		m_bActive = FALSE;
		::pthread_mutex_unlock(&m_m);
		return TRUE;
	}

	DWORD Wait()
	{
		cEvent *h[1] = { this };
		return MultipleWait(1, h);
	}

	static DWORD MultipleWait(int num, cEvent **e, BOOL bAll = FALSE)
	{
		int i, cnt;
		DWORD dwRet = 0xffffffff;
		::pthread_mutex_lock(&(e[0]->m_m));
		while (1)
		{
			if (bAll)
			{
				cnt = 0;
				for (i = 0; i < num; i++)
				{
					if (e[i]->IsSet())
						cnt++;
				}
				if (cnt == num)
				{
					for (i = 0; i < num; i++)
					{
						if (e[i]->m_bAutoReset)
							e[i]->m_bActive = FALSE;
					}
					dwRet = num;
					break;
				}
			}
			else
			{
				for (i = 0; i < num; i++)
				{
					if (e[i]->IsSet())
					{
						if (e[i]->m_bAutoReset)
							e[i]->m_bActive = FALSE;
						dwRet = i;
						break;
					}
				}
				if (dwRet != 0xffffffff)
					break;
			}
			::pthread_cond_wait(&(e[0]->m_c), &(e[0]->m_m));
		}
		::pthread_mutex_unlock(&(e[0]->m_m));
		return dwRet;
	}
};

////////////////////////////////////////////////////////////////////////////////

enum enumCommand {
	eSelectBonDriver = 0,
	eCreateBonDriver,
	eOpenTuner,
	eCloseTuner,
	eSetChannel1,
	eGetSignalLevel,
	eWaitTsStream,
	eGetReadyCount,
	eGetTsStream,
	ePurgeTsStream,
	eRelease,

	eGetTunerName,
	eIsTunerOpening,
	eEnumTuningSpace,
	eEnumChannelName,
	eSetChannel2,
	eGetCurSpace,
	eGetCurChannel,

	eGetTotalDeviceNum,
	eGetActiveDeviceNum,
	eSetLnbPower,
};

struct stPacketHead {
	BYTE m_bSync;
	BYTE m_bCommand;
	BYTE m_bReserved1;
	BYTE m_bReserved2;
	DWORD m_dwBodyLength;
} __attribute__((packed));

struct stPacket {
	stPacketHead head;
	BYTE payload[1];
} __attribute__((packed));

#define SYNC_BYTE	0xff
class cPacketHolder {
	friend class cProxyClient;
	union {
		stPacket *m_pPacket;
		BYTE *m_pBuff;
	};
	size_t m_Size;

	inline void init(size_t PayloadSize)
	{
		m_pBuff = new BYTE[sizeof(stPacketHead) + PayloadSize];
		::memset(m_pBuff, 0, sizeof(stPacketHead));
		m_pPacket->head.m_bSync = SYNC_BYTE;
		m_pPacket->head.m_dwBodyLength = htonl((DWORD)PayloadSize);
		m_Size = sizeof(stPacketHead) + PayloadSize;
	}

public:
	cPacketHolder(size_t PayloadSize)
	{
		init(PayloadSize);
	}

	cPacketHolder(enumCommand eCmd, size_t PayloadSize)
	{
		init(PayloadSize);
		SetCommand(eCmd);
	}

	~cPacketHolder()
	{
		if (m_pBuff)
		{
			delete[] m_pBuff;
			m_pBuff = NULL;
		}
	}
	inline BOOL IsValid(){ return (m_pPacket->head.m_bSync == SYNC_BYTE); }
	inline BOOL IsTS(){ return (m_pPacket->head.m_bCommand == (BYTE)eGetTsStream); }
	inline enumCommand GetCommand(){ return (enumCommand)m_pPacket->head.m_bCommand; }
	inline void SetCommand(enumCommand eCmd){ m_pPacket->head.m_bCommand = (BYTE)eCmd; }
	inline DWORD GetBodyLength(){ return ntohl(m_pPacket->head.m_dwBodyLength); }
};

class cPacketFifo : protected std::queue<cPacketHolder *> {
	const size_t m_fifoSize;
	cCriticalSection m_Lock;
	cEvent m_Event;

public:
	cPacketFifo(pthread_cond_t &c, pthread_mutex_t &m) : m_fifoSize(g_PacketFifoSize), m_Event(c, m){}
	~cPacketFifo()
	{
		LOCK(m_Lock);
		while (!empty())
		{
			cPacketHolder *p = front();
			pop();
			delete p;
		}
	}

	void Push(cPacketHolder *p)
	{
		LOCK(m_Lock);
		if (size() >= m_fifoSize)
		{
			::fprintf(stderr, "Packet Queue OVERFLOW : size[%zu]\n", size());
			// TSの場合のみドロップ
			if (p->IsTS())
			{
				delete p;
				return;
			}
		}
		push(p);
		m_Event.Set();
	}

	void Pop(cPacketHolder **p)
	{
		LOCK(m_Lock);
		if (!empty())
		{
			*p = front();
			pop();
			if (empty())
				m_Event.Reset();
		}
		else
			m_Event.Reset();
	}

	cEvent *GetEventHandle()
	{
		return &m_Event;
	}
};

////////////////////////////////////////////////////////////////////////////////

struct TS_DATA {
	BYTE *pbBuff;
	DWORD dwSize;
	TS_DATA(void)
	{
		pbBuff = NULL;
		dwSize = 0;
	}
	~TS_DATA(void)
	{
		if (pbBuff)
			delete[] pbBuff;
	}
};

class cTSFifo : protected std::queue<TS_DATA *> {
	const size_t m_fifoSize;
	cCriticalSection m_Lock;
	cEvent m_Event;

public:
	cTSFifo(pthread_cond_t &c, pthread_mutex_t &m) : m_fifoSize(g_TsFifoSize), m_Event(c, m){}
	~cTSFifo(){ Flush(); }

	void Flush()
	{
		LOCK(m_Lock);
		while (!empty())
		{
			TS_DATA *p = front();
			pop();
			delete p;
		}
	}

	void Push(TS_DATA *p)
	{
		LOCK(m_Lock);
		if (size() >= m_fifoSize)
		{
			::fprintf(stderr, "TS Queue OVERFLOW : size[%zu]\n", size());
			TS_DATA *pDel = front();
			pop();
			delete pDel;
		}
		push(p);
//		m_Event.Set();
	}

	void Pop(TS_DATA **p)
	{
		LOCK(m_Lock);
		if (!empty())
		{
			*p = front();
			pop();
//			if (empty())
//				m_Event.Reset();
		}
//		else
//			m_Event.Reset();
	}

	cEvent *GetEventHandle()
	{
		return &m_Event;
	}

	inline size_t Size()
	{
		return size();
	}
};

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
	TS_DATA *m_LastBuff;
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
	int ReceiverHelper(char *pDst, int left);
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
	BOOL IsError(){ return m_Error.IsSet(); }
	void WaitSingleShot(){ m_SingleShot.Wait(); }
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

}
#endif	// __BONDRIVER_PROXY_H__
