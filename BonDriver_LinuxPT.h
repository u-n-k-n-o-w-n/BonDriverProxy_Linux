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

#define DEFAULT_CONF_NAME	"BonDriver_LinuxPT.conf"

#define MAX_CH				128
#define MAX_CN_LEN			64

#define TS_BUFSIZE			(188 * 256)
#define TS_FIFOSIZE			512
#define WAIT_TIME			10	// デバイスからのread()でエラーが発生した場合の、次のread()までの間隔(ms)
#define TUNER_NAME			"BonDriver_LinuxPT"

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
	cTSFifo(pthread_cond_t &c, pthread_mutex_t &m) : m_fifoSize(TS_FIFOSIZE), m_Event(c, m){}
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
			fprintf(stderr, "TS Queue OVERFLOW : size[%zu]\n", size());
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

struct stChannel {
	char strChName[MAX_CN_LEN];
	FREQUENCY freq;
	BOOL bUnused;
};

class cBonDriverLinuxPT : public IBonDriver2 {
	pthread_cond_t m_c;
	pthread_mutex_t m_m;
	cTSFifo m_fifoTS;
	TS_DATA *m_LastBuff;
	cCriticalSection m_writeLock;
	char m_TunerName[64];
	BOOL m_bTuner;
	float m_fSignalLevel;
	DWORD m_dwSpace;
	DWORD m_dwChannel;
	int m_fd;
	pthread_t m_hTsRead;
	DWORD m_tRet;
	volatile BOOL m_bStopTsRead;

	void TsFlush(){ m_fifoTS.Flush(); }
	static void *TsReader(LPVOID pv);

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
#endif	// _BONDRIVER_LINUXPT_H_
