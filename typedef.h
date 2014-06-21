#ifndef __TYPEDEF_H__
#define __TYPEDEF_H__
typedef int						BOOL;
typedef unsigned char			BYTE;
typedef unsigned int			DWORD;
typedef const unsigned short *	LPCWSTR;
typedef LPCWSTR					LPCTSTR;
typedef unsigned short			WCHAR;
typedef WCHAR					TCHAR;
typedef const char *			LPCSTR;
typedef int						SOCKET;
typedef void *					HMODULE;
typedef void *					LPVOID;

#define TRUE			1
#define FALSE			0
#define INVALID_SOCKET	-1
#define SOCKET_ERROR	-1

#define WAIT_OBJECT_0	0
#define WAIT_ABANDONED	0x00000080
#define WAIT_TIMEOUT	0x00000102
#endif	// __TYPEDEF_H__
