#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <queue>
#include "../typedef.h"

static size_t g_PacketFifoSize = 8;
static int g_ConnectTimeOut = 5;

#include "../Common.h"
#include "../BdpPacket.h"

class cProxyClient {

SOCKET Connect(char *host, char *port)
{
	addrinfo hints, *results, *rp;
	SOCKET sock;
	int i, bf;
	fd_set wd;
	timeval tv;

	::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_NUMERICHOST;
	if (::getaddrinfo(host, port, &hints, &results) != 0)
	{
		hints.ai_flags = 0;
		if (::getaddrinfo(host, port, &hints, &results) != 0)
			return INVALID_SOCKET;
	}

	for (rp = results; rp != NULL; rp = rp->ai_next)
	{
		sock = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sock == INVALID_SOCKET)
			continue;

		bf = TRUE;
		::ioctl(sock, FIONBIO, &bf);
		tv.tv_sec = g_ConnectTimeOut;
		tv.tv_usec = 0;
		FD_ZERO(&wd);
		FD_SET(sock, &wd);
		::connect(sock, rp->ai_addr, (int)(rp->ai_addrlen));
		if ((i = ::select((int)(sock + 1), 0, &wd, 0, &tv)) != SOCKET_ERROR)
		{
			// タイムアウト時間が"全体の"ではなく"個々のソケットの"になるけど、とりあえずこれで
			if (i != 0)
			{
				bf = FALSE;
				::ioctl(sock, FIONBIO, &bf);
				break;
			}
		}
		::close(sock);
	}
	::freeaddrinfo(results);
	if (rp == NULL)
		return INVALID_SOCKET;

	return sock;
}

int ReceiverHelper(SOCKET s, char *pDst, DWORD left)
{
	int len, ret;
	fd_set rd;
	timeval tv;

	while (left > 0)
	{
		FD_ZERO(&rd);
		FD_SET(s, &rd);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		if ((len = ::select((int)(s + 1), &rd, NULL, NULL, &tv)) == SOCKET_ERROR)
		{
			ret = -1;
			goto err;
		}

		if (len == 0)
			continue;

		if ((len = ::recv(s, pDst, left, 0)) <= 0)
		{
			ret = -2;
			goto err;
		}
		left -= len;
		pDst += len;
	}
	return 0;
err:
	return ret;
}

public:
int GetClientInfo(char *host, char *port)
{
	SOCKET s = Connect(host, port);
	if (s == INVALID_SOCKET)
	{
		::fprintf(stderr, "connect error: %s:%s\n", host, port);
		return -1;
	}

	int ret = 0;
	BOOL eflag = FALSE;

	cPacketHolder *pPh = new cPacketHolder(eGetClientInfo, 0);
	int left = (int)pPh->m_Size;
	char *p = (char *)(pPh->m_pPacket);
	while (left > 0)
	{
		int len = ::send(s, p, left, 0);
		if (len == SOCKET_ERROR)
		{
			eflag = TRUE;
			ret = -2;
			break;
		}
		left -= len;
		p += len;
	}
	delete pPh;

	if (!eflag)
	{
		pPh = new cPacketHolder(8192);
		left = sizeof(stPacketHead);
		p = (char *)&(pPh->m_pPacket->head);
		if (ReceiverHelper(s, p, left) != 0)
		{
			ret = -3;
			goto end;
		}

		if (!pPh->IsValid())
		{
			ret = -4;
			goto end;
		}

		left = pPh->GetBodyLength();
		if (left > 8192)
		{
			cPacketHolder *pPh2 = new cPacketHolder(left);
			pPh2->m_pPacket->head = pPh->m_pPacket->head;
			delete pPh;
			pPh = pPh2;
		}

		p = (char *)(pPh->m_pPacket->payload);
		if (ReceiverHelper(s, p, left) != 0)
		{
			ret = -5;
			goto end;
		}

		::printf("%s", p);
end:
		delete pPh;
	}

	::close(s);

	return ret;
}

};

int main(int argc, char *argv[])
{
	if (argc < 3)
	{
		fprintf(stderr, "usage: $ %s addr port\n", argv[0]);
		return 0;
	}

	cProxyClient c;
	int ret = c.GetClientInfo(argv[1], argv[2]);

	if (ret != 0)
		fprintf(stderr, "error: %d\n", ret);

	return ret;
}
