/*
 * read stdin to udp/address:port
 * $ cc -O2 -Wall -o udpsend udpsend.c
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

int main(int ac, char *av[])
{
	int sock, port, left, len;
	char *host, *cp, buf[188*256];
	struct sockaddr_in server;
	struct hostent *he;
	fd_set rd;

	if (ac < 3)
	{
		fprintf(stderr, "usage: %s address port\n", av[0]);
		return 0;
	}
	host = av[1];
	port = atoi(av[2]);
	if (port < 0 || port > 65535)
	{
		fprintf(stderr, "port no error [%d]\n", port);
		return -1;
	}

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1)
		return -2;

/*
	{
	int opt = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&opt, sizeof(opt)) != 0)
	{
		close(sock);
		return -3;
	}
	}
*/

	memset((char *)&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = inet_addr(host);
	if (server.sin_addr.s_addr == INADDR_NONE)
	{
		he = gethostbyname(host);
		if (he == NULL)
		{
			close(sock);
			return -3;
		}
		memcpy(&(server.sin_addr), *(he->h_addr_list), he->h_length);
	}
	server.sin_port = htons(port);

	FD_ZERO(&rd);
	FD_SET(0, &rd);
	while (1)
	{
		if (select(1, &rd, NULL, NULL, NULL) == -1)
		{
			perror("select");
			break;
		}

		if ((left = read(0, buf, sizeof(buf))) <= 0)
		{
			perror("read");
			break;
		}

		cp = buf;
		while (left)
		{
			if ((len = sendto(sock, cp, left, 0, (struct sockaddr *)&server, sizeof(server))) < 0)
			{
				perror("sendto");
				break;
			}
			left -= len;
			cp += len;
		}
	}
	close(sock);
	return 0;
}
