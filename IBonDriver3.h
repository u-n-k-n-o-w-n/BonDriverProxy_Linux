// IBonDriver3.h: IBonDriver3 クラスのインターフェイス
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(_IBONDRIVER3_H_)
#define _IBONDRIVER3_H_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


#include "IBonDriver2.h"


/////////////////////////////////////////////////////////////////////////////
// Bonドライバインタフェース3
/////////////////////////////////////////////////////////////////////////////

class IBonDriver3 : public IBonDriver2
{
public:
// IBonDriver3
	virtual const DWORD GetTotalDeviceNum(void) = 0;
	virtual const DWORD GetActiveDeviceNum(void) = 0;
	virtual const BOOL SetLnbPower(const BOOL bEnable) = 0;
	
// IBonDriver
	virtual void Release(void) = 0;
};
#endif
