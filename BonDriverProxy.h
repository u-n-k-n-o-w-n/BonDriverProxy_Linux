#ifndef __BONDRIVERPROXY_H__
#define __BONDRIVERPROXY_H__
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <pthread.h>
#include <list>
#include <queue>
#include "typedef.h"
#include "IBonDriver3.h"

#define WAIT_TIME	10	// GetTsStream()の後で、dwRemainが0だった場合に待つ時間(ms)

////////////////////////////////////////////////////////////////////////////////

static char g_Host[64];
static unsigned short g_Port;
static size_t g_PacketFifoSize = 64;
static DWORD g_TsPacketBufSize = (188 * 1024);

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
	volatile BOOL m_bActive;

public:
	cEvent(pthread_cond_t &c, pthread_mutex_t &m) : m_c(c), m_m(m) { m_bActive = FALSE; }
	~cEvent(){}
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

	static DWORD MultipleWait(int num, cEvent **e, BOOL bAll = FALSE)
	{
		DWORD dwRet = 0xffffffff;
		::pthread_mutex_lock(&(e[0]->m_m));
		while (1)
		{
			if (bAll)
			{
				int cnt = 0;
				for (int i = 0; i < num; i++)
				{
					if (e[i]->IsSet())
						cnt++;
				}
				if (cnt == num)
				{
					dwRet = num;
					break;
				}
			}
			else
			{
				for (int i = 0; i < num; i++)
				{
					if (e[i]->IsSet())
					{
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
	friend class cProxyServer;
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
			fprintf(stderr, "Packet Queue OVERFLOW : size[%lu]\n", size());
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

class cProxyServer {
	IBonDriver *m_pIBon;
	IBonDriver2 *m_pIBon2;
	IBonDriver3 *m_pIBon3;
	HMODULE m_hModule;
	SOCKET m_s;
	pthread_cond_t m_c;
	pthread_mutex_t m_m;
	cEvent m_Error;
	char m_strBonDriver[512];
	BOOL m_bTunerOpen;
	DWORD m_tRet;
	pthread_t m_hTsRead;
	BOOL * volatile m_pStopTsRead;
	cCriticalSection *m_pTsLock;
	DWORD *m_ppos;
	BOOL m_bChannelLock;
	cPacketFifo m_fifoSend;
	cPacketFifo m_fifoRecv;

	DWORD Process();
	int ReceiverHelper(char *pDst, int left);
	static void *Receiver(LPVOID pv);
	void makePacket(enumCommand eCmd, BOOL b);
	void makePacket(enumCommand eCmd, DWORD dw);
	void makePacket(enumCommand eCmd, LPCTSTR str);
	void makePacket(enumCommand eCmd, BYTE *pSrc, DWORD dwSize, float fSignalLevel);
	static void *Sender(LPVOID pv);
	static void *TsReader(LPVOID pv);

	BOOL SelectBonDriver(LPCSTR p);
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
	cProxyServer();
	~cProxyServer();
	void setSocket(SOCKET s){ m_s = s; }
	static void *Reception(LPVOID pv);
};
#endif	// __BONDRIVERPROXY_H__
