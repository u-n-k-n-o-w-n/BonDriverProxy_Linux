/*
$ g++ -O2 -Wall -rdynamic -o sample sample.cpp -ldl
*/
#define FILE_OFFSET_BITS	64
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <dlfcn.h>
#include "typedef.h"
#include "IBonDriver2.h"

volatile BOOL bStop = FALSE;
static void handler(int sig)
{
	bStop = TRUE;
}

void usage(char *p)
{
	fprintf(stderr, "usage: %s [-b bondriver] [-s space_no] [-c channel_no] ( [-t sec] [-o filename] )\n", p);
	exit(0);
}

int main(int argc, char *argv[])
{
	int opt, bfind, sfind, cfind, ofind, wfd;
	char *bon, *output;
	DWORD sec, dwSpace, dwChannel;

	// パラメータ処理
	sec = 0xffffffff;
	bon = output = NULL;
	bfind = sfind = cfind = ofind = 0;
	dwSpace = dwChannel = 0;
	while ((opt = getopt(argc, argv, "b:s:c:t:o:")) != -1)
	{
		switch (opt)
		{
		case 'b':	// BonDriver指定
			bon = optarg;
			bfind = 1;
			break;
		case 's':	// 使用スペース指定
			dwSpace = strtoul(optarg, NULL, 10);
			sfind = 1;
			break;
		case 'c':	// 使用チャンネル指定
			dwChannel = strtoul(optarg, NULL, 10);
			cfind = 1;
			break;
		case 't':	// チューナ使用時間指定(単位:秒)
			sec = strtoul(optarg, NULL, 10);
			break;
		case 'o':	// 出力先指定
			output = optarg;
			ofind = 1;
			break;
		default:
			usage(argv[0]);
		}
	}
	if (!bfind || !sfind || !cfind)
		usage(argv[0]);

	// 出力先指定無しなら標準出力へ
	if (!ofind)
		wfd = 1;
	else
	{
		wfd = open(output, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);
		if (wfd < 0)
		{
			perror("open");
			return -1;
		}
	}

	// モジュールロード
	void *hModule = dlopen(bon, RTLD_LAZY);
	if (!hModule)
	{
		perror("dlopen");
		return -2;
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
		fprintf(stderr, "OpenTuner error\n");
		pIBon->Release();
		dlclose(hModule);
		close(wfd);
		return -3;
	}

	// 指定チャンネルにセット
	pIBon2->SetChannel(dwSpace, dwChannel);

	// 終了タイマー & 停止シグナル用ハンドラ登録
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGALRM, &sa, NULL);

	// 終了タイマーセット
	alarm(sec);

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
					goto err;
				}
				left -= len;
				pBuf += len;
			} while (left > 0);
		}
		// 取得待ちTSデータが無ければ適当な時間待つ
		if (dwRemain == 0)
			nanosleep(&ts, NULL);
	}

	// チューナクローズ
	pIBon->CloseTuner();

	// BonDriver内のバッファに残っている分があれば一応回収
	while (1)
	{
		dwSize = dwRemain = 0;
		if (pIBon->GetTsStream(&pBuf, &dwSize, &dwRemain) && (dwSize != 0))
		{
			int len, left = (int)dwSize;
			do
			{
				len = write(wfd, pBuf, left);
				if (len < 0)
				{
					perror("write");
					goto err;
				}
				left -= len;
				pBuf += len;
			} while (left > 0);
		}
		if (dwRemain == 0)
			break;
	}

err:
	close(wfd);
	// インスタンス解放 & モジュールリリース
	pIBon->Release();
	dlclose(hModule);
	return 0;
}
