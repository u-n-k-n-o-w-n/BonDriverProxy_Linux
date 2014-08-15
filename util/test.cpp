/*
$ g++ -O2 -Wall -rdynamic -pthread -o test test.cpp -ldl
*/
#define FILE_OFFSET_BITS	64
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iconv.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <dlfcn.h>
#include <pthread.h>
#include <string>
#include <vector>
#include "typedef.h"
#include "IBonDriver2.h"

static int wfd;
static volatile BOOL bStop = FALSE;
static void handler(int sig)
{
	bStop = TRUE;
}

static void usage(char *p)
{
	fprintf(stderr, "usage: %s [-b bondriver] ( [-s space_no] [-o filename] )\n", p);
	exit(0);
}

static int RevConvert(char *src, char *dst, size_t dstsize)
{
	iconv_t d = iconv_open("UTF-8", "UTF-16LE");
	if (d == (iconv_t)-1)
		return -1;
	int n = 0;
	while (1)
	{
		if(src[n]=='\0' && src[n+1]=='\0')
			break;
		n += 2;
	}
	size_t srclen = n + 2;	// 終端NULL込み
	size_t dstlen = dstsize - 1;
	size_t ret = iconv(d, &src, &srclen, &dst, &dstlen);
	*dst = '\0';
	iconv_close(d);
	if (ret == (size_t)-1)
		return -2;
	return 0;
}

static void *TsReader(LPVOID pv)
{
	IBonDriver *pIBon = static_cast<IBonDriver *>(pv);
	// TS取得開始
	BYTE *pBuf;
	DWORD dwSize, dwRemain;
	timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = 10 * 1000 * 1000;	// 10ms
	while (!bStop)
	{
		// TSストリーム取得
		dwSize = dwRemain = 0;
		if (pIBon->GetTsStream(&pBuf, &dwSize, &dwRemain) && (dwSize != 0))
		{
			// この時点でpBufに長さdwSize分のTSストリームを取得した状態なので、それを書き出し
			// (もしバッファリングやデスクランブルやTS分離処理を入れるならこの辺で)
			int len, left = (int)dwSize;
			do
			{
				len = write(wfd, pBuf, left);
				if (len < 0)
				{
					perror("write");
					bStop = TRUE;
					break;
				}
				left -= len;
				pBuf += len;
			} while (left > 0);
		}
		// 取得待ちTSデータが無ければ適当な時間待つ
		if (dwRemain == 0)
			nanosleep(&ts, NULL);
	}
	return NULL;
}

int main(int argc, char *argv[])
{
	int opt, bfind, ofind;
	char *bon, *output;
	DWORD dwSpace, dwChannel;

	// パラメータ処理
	bon = output = NULL;
	bfind = ofind = 0;
	dwSpace = dwChannel = 0;
	while ((opt = getopt(argc, argv, "b:s:o:")) != -1)
	{
		switch (opt)
		{
		case 'b':	// BonDriver指定
			bon = optarg;
			bfind = 1;
			break;
		case 's':	// 使用スペース指定(デフォルトは0)
			dwSpace = strtoul(optarg, NULL, 10);
			break;
		case 'o':	// 出力先指定
			output = optarg;
			ofind = 1;
			break;
		default:
			usage(argv[0]);
		}
	}
	if (!bfind)
		usage(argv[0]);

	// モジュールロード
	void *hModule = dlopen(bon, RTLD_LAZY);
	if (!hModule)
	{
		fprintf(stderr, "dlopen error: %s\n", dlerror());
		return -1;
	}

	// 出力先指定無しなら標準出力へ
	if (!ofind)
		wfd = 1;
	else
	{
		wfd = open(output, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);
		if (wfd < 0)
		{
			perror("open");
			dlclose(hModule);
			return -2;
		}
	}

	// インスタンス作成
	IBonDriver *pIBon;
	IBonDriver2 *pIBon2;
	pIBon = pIBon2 = NULL;
	char *err;
	IBonDriver *(*f)() = (IBonDriver *(*)())dlsym(hModule, "CreateBonDriver");
	if ((err = dlerror()) == NULL)
	{
		pIBon = f();
		if (pIBon)
			pIBon2 = dynamic_cast<IBonDriver2 *>(pIBon);
	}
	else
	{
		fprintf(stderr, "dlsym error: %s\n", err);
		dlclose(hModule);
		close(wfd);
		return -2;
	}
	if (!pIBon || !pIBon2)
	{
		fprintf(stderr, "CreateBonDriver error: pIBon[%p] pIBon2[%p]\n", pIBon, pIBon2);
		dlclose(hModule);
		close(wfd);
		return -3;
	}

	// ここから実質のチューナオープン & TS取得処理
	BOOL b = pIBon->OpenTuner();
	if (!b)
	{
		fputs("OpenTuner error\n", stderr);
		pIBon->Release();
		dlclose(hModule);
		close(wfd);
		return -3;
	}

	// チャンネル名列挙
	std::vector<std::string> vChName;
	char buf[128];
	LPCTSTR name;
	DWORD ch = 0;
	while (1)
	{
		name = pIBon2->EnumChannelName(dwSpace, ch++);
		if (name == NULL)
			break;
		RevConvert((char *)name, buf, sizeof(buf));
		vChName.push_back(buf);
	}
	for (unsigned int i = 0; i < vChName.size(); i++)
	{
		fprintf(stderr, "[ %u : %s ] ", i, vChName[i].c_str());
		if (i && (((i+1) % 3) == 0))
			fputc('\n', stderr);
	}
	if ((vChName.size() % 3) != 0)
		fputc('\n', stderr);
	fputs("[ p : チャンネル一覧再表示 ] [ q : 終了 ]\n", stderr);

	// 停止シグナル用ハンドラ登録
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
//	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	// チャンネル選択ループ
	pthread_t hTsRead = 0;
	while (!bStop)
	{
		if (fgets(buf, sizeof(buf), stdin) == NULL)
			continue;
		if (buf[0] < '0' || buf[0] > '9')
		{
			if (buf[0] == 'q')
			{
				bStop = TRUE;
				break;
			}
			if (buf[0] == 'p')
			{
				for (unsigned int i = 0; i < vChName.size(); i++)
				{
					fprintf(stderr, "[ %u : %s ] ", i, vChName[i].c_str());
					if (i && (((i+1) % 3) == 0))
						fputc('\n', stderr);
				}
				if ((vChName.size() % 3) != 0)
					fputc('\n', stderr);
				fputs("[ p : チャンネル一覧再表示 ] [ q : 終了 ]\n", stderr);
				continue;
			}
			fprintf(stderr, "ch_no select error.\n");
			continue;
		}
		dwChannel = strtoul(buf, NULL, 10);
		if (dwChannel < vChName.size())
		{
			if (pIBon2->SetChannel(dwSpace, dwChannel) == TRUE)
			{
				if (!hTsRead)
				{
					if (pthread_create(&hTsRead, NULL, TsReader, pIBon))
					{
						perror("pthread_create");
						goto err;
					}
				}
			}
			else
				fprintf(stderr, "SetChannel(%u, %u) error.\n", dwSpace, dwChannel);
		}
		else
			fprintf(stderr, "ch_no select error.\n");
	}

	pthread_join(hTsRead, NULL);

err:
	if (ofind)
		close(wfd);
	// チューナクローズ
	pIBon->CloseTuner();
	// インスタンス解放 & モジュールリリース
	pIBon->Release();
	dlclose(hModule);
	return 0;
}
