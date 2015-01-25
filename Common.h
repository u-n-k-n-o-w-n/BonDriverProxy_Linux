#ifndef __COMMON_H__
#define __COMMON_H__

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

	DWORD Wait(cEvent *err)
	{
		cEvent *h[2] = { err, this };
		return MultipleWait(2, h);
	}

	static DWORD MultipleWait(int num, cEvent **e, BOOL bAll = FALSE)
	{
		int i, cnt;
		DWORD dwRet = 0xffffffff;
		::pthread_mutex_lock(&(e[0]->m_m));
		for (;;)
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

#endif	// __COMMON_H__
