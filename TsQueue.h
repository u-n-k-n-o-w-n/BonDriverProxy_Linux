#ifndef __TSQUEUE_H__
#define __TSQUEUE_H__

struct TS_DATA {
#ifdef __BONDRIVER_PROXY_H__
	BYTE *pbBufHead;
#endif
	BYTE *pbBuf;
	DWORD dwSize;
	TS_DATA(void)
	{
#ifdef __BONDRIVER_PROXY_H__
		pbBufHead = NULL;
#endif
		pbBuf = NULL;
		dwSize = 0;
	}
	~TS_DATA(void)
	{
#ifdef __BONDRIVER_PROXY_H__
		delete[] pbBufHead;
#else
		delete[] pbBuf;
#endif
	}
};

class cRawTSFifo : protected std::queue<TS_DATA *> {
protected:
	const size_t m_fifoSize;
	cCriticalSection m_Lock;
	cEvent m_Event;

public:
	cRawTSFifo(pthread_cond_t &c, pthread_mutex_t &m) : m_fifoSize(g_TsFifoSize), m_Event(c, m){}
	~cRawTSFifo(){ Flush(); }	// ポリモーフィズムは使わない前提

	void Flush()
	{
		LOCK(m_Lock);
		while (!empty())
		{
			TS_DATA *p = front();
			pop();
			delete p;
		}
		m_Event.Reset();
	}

	void Push(TS_DATA *p)
	{
		LOCK(m_Lock);
		if (size() >= m_fifoSize)
		{
			::fprintf(stderr, "Raw TS Queue OVERFLOW : size[%zu]\n", size());
			TS_DATA *pDel = front();
			pop();
			delete pDel;
		}
		push(p);
		m_Event.Set();
	}

	void Pop(TS_DATA **p)
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

	inline size_t Size()
	{
		return size();
	}
};

class cTSFifo : public cRawTSFifo {
public:
	cTSFifo(pthread_cond_t &c, pthread_mutex_t &m) : cRawTSFifo(c, m){}

	void Flush()
	{
		LOCK(m_Lock);
		while (!empty())
		{
			TS_DATA *p = front();
			pop();
			delete p;
		}
//		m_Event.Reset();
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
};

#endif	// __TSQUEUE_H__
