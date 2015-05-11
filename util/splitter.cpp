#ifdef _WINDOWS
// cl /W3 /O2 /D_WINDOWS splitter.cpp
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int				BOOL;
typedef unsigned char	BYTE;
typedef unsigned int	DWORD;
#define TRUE			1
#define FALSE			0
#else
// g++ -O2 -Wall -o splitter splitter.cpp
#define _FILE_OFFSET_BITS	64
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../typedef.h"
#endif

#define TS_SYNC_BYTE	0x47
#define TS_PKTSIZE		188
#define TTS_PKTSIZE		192
#define TS_FEC_PKTSIZE	204
#define TTS_FEC_PKTSIZE	208
#define TS_BUFSIZE		(TS_PKTSIZE * 256)
#define TS_WRITESIZE	(TS_BUFSIZE * 348)	// 約16MB

static DWORD g_Crc32Table[256];

static void InitCrc32Table()
{
	DWORD i, j, crc;
	for (i = 0; i < 256; i++)
	{
		crc = i << 24;
		for (j = 0; j < 8; j++)
			crc = (crc << 1) ^ ((crc & 0x80000000) ? 0x04c11db7 : 0);
		g_Crc32Table[i] = crc;
	}
}

static DWORD CalcCRC32(BYTE *p, DWORD len)
{
	DWORD i, crc = 0xffffffff;
	for (i = 0; i < len; i++)
		crc = (crc << 8) ^ g_Crc32Table[(crc >> 24) ^ p[i]];
	return crc;
}

class cTsSync {
	DWORD m_dwUnitSize;
	DWORD m_dwSyncBufPos;
	BYTE m_SyncBuf[256];
public:
	cTsSync(){m_dwUnitSize = m_dwSyncBufPos = 0;}
	BOOL TsSync(BYTE *pSrc, DWORD dwSrc, BYTE **ppDst, DWORD *pdwDst);
};

BOOL cTsSync::TsSync(BYTE *pSrc, DWORD dwSrc, BYTE **ppDst, DWORD *pdwDst)
{
	// 同期チェックの開始位置
	DWORD dwCheckStartPos = 0;
	// 既に同期済みか？
	if (m_dwUnitSize != 0)
	{
		for (DWORD pos = m_dwUnitSize - m_dwSyncBufPos; pos < dwSrc; pos += m_dwUnitSize)
		{
			if (pSrc[pos] != TS_SYNC_BYTE)
			{
				// 今回の入力バッファで同期が崩れてしまうので要再同期
				m_dwUnitSize = 0;
				// 今回の入力バッファの先頭から同期の崩れた場所までは破棄する事になる
				dwCheckStartPos = pos;
				goto resync;
			}
		}
		DWORD dwDst = TS_PKTSIZE * (((m_dwSyncBufPos + dwSrc) - 1) / m_dwUnitSize);
		if (dwDst == 0)
		{
			// 同期用繰り越しバッファと今回の入力バッファを合わせてもユニットサイズ+1に
			// 届かなかった(==次の同期バイトのチェックが行えなかった)ので、
			// 今回の入力バッファを同期用繰り越しバッファに追加するだけで終了
			::memcpy(&m_SyncBuf[m_dwSyncBufPos], pSrc, dwSrc);
			m_dwSyncBufPos += dwSrc;
			*ppDst = NULL;	// 呼び出し側でのdelete[]を保証する
			*pdwDst = 0;
			return FALSE;
		}
		BYTE *pDst = new BYTE[dwDst];
		if (m_dwSyncBufPos >= TS_PKTSIZE)
			::memcpy(pDst, m_SyncBuf, TS_PKTSIZE);
		else
		{
			if (m_dwSyncBufPos == 0)
				::memcpy(pDst, pSrc, TS_PKTSIZE);
			else
			{
				::memcpy(pDst, m_SyncBuf, m_dwSyncBufPos);
				::memcpy(&pDst[m_dwSyncBufPos], pSrc, TS_PKTSIZE - m_dwSyncBufPos);
			}
		}
		DWORD dwSrcPos = m_dwUnitSize - m_dwSyncBufPos;
		if (m_dwUnitSize == TS_PKTSIZE)
		{
			// 普通のTSパケットの場合はそのままコピーできる
			if ((dwDst - TS_PKTSIZE) != 0)
			{
				::memcpy(&pDst[TS_PKTSIZE], &pSrc[dwSrcPos], (dwDst - TS_PKTSIZE));
				dwSrcPos += (dwDst - TS_PKTSIZE);
			}
		}
		else
		{
			// それ以外のパケットの場合は普通のTSパケットに変換
			for (DWORD pos = TS_PKTSIZE; (dwSrcPos + m_dwUnitSize) < dwSrc; dwSrcPos += m_dwUnitSize, pos += TS_PKTSIZE)
				::memcpy(&pDst[pos], &pSrc[dwSrcPos], TS_PKTSIZE);
		}
		if ((dwSrc - dwSrcPos) != 0)
		{
			// 入力バッファに余りがあるので同期用繰り越しバッファに保存
			::memcpy(m_SyncBuf, &pSrc[dwSrcPos], (dwSrc - dwSrcPos));
			m_dwSyncBufPos = dwSrc - dwSrcPos;
		}
		else
			m_dwSyncBufPos = 0;
		*ppDst = pDst;
		*pdwDst = dwDst;
		return TRUE;
	}

resync:
	// 同期処理開始
	DWORD dwSyncBufPos = m_dwSyncBufPos;
	for (DWORD off = dwCheckStartPos; (off + TS_PKTSIZE) < (dwSyncBufPos + dwSrc); off++)
	{
		if (((off >= dwSyncBufPos) && (pSrc[off - dwSyncBufPos] == TS_SYNC_BYTE)) || ((off < dwSyncBufPos) && (m_SyncBuf[off] == TS_SYNC_BYTE)))
		{
			for (int type = 0; type < 4; type++)
			{
				DWORD dwUnitSize;
				switch (type)
				{
				case 0:
					dwUnitSize = TS_PKTSIZE;
					break;
				case 1:
					dwUnitSize = TTS_PKTSIZE;
					break;
				case 2:
					dwUnitSize = TS_FEC_PKTSIZE;
					break;
				default:
					dwUnitSize = TTS_FEC_PKTSIZE;
					break;
				}
				BOOL bSync = TRUE;
				// 次の同期バイトが同期用繰り越しバッファ内に含まれている可能性があるか？
				if (dwUnitSize >= dwSyncBufPos)
				{
					// なかった場合は同期用繰り越しバッファのチェックは不要
					DWORD pos = off + (dwUnitSize - dwSyncBufPos);
					if (pos >= dwSrc)
					{
						// bSync = FALSE;
						// これ以降のユニットサイズではこの場所で同期成功する事は無いのでbreak
						break;
					}
					else
					{
						// 同一ユニットサイズのバッファが8個もしくは今回の入力バッファの
						// 最後まで並んでいるなら同期成功とみなす
						int n = 0;
						do
						{
							if (pSrc[pos] != TS_SYNC_BYTE)
							{
								bSync = FALSE;
								break;
							}
							pos += dwUnitSize;
							n++;
						} while ((n < 8) && (pos < dwSrc));
					}
				}
				else
				{
					DWORD pos = off + dwUnitSize;
					if (pos >= (dwSyncBufPos + dwSrc))
					{
						// bSync = FALSE;
						// これ以降のユニットサイズではこの場所で同期成功する事は無いのでbreak
						break;
					}
					else
					{
						// 同一ユニットサイズのバッファが8個もしくは今回の入力バッファの
						// 最後まで並んでいるなら同期成功とみなす
						int n = 0;
						do
						{
							if (((pos >= dwSyncBufPos) && (pSrc[pos - dwSyncBufPos] != TS_SYNC_BYTE)) || ((pos < dwSyncBufPos) && (m_SyncBuf[pos] != TS_SYNC_BYTE)))
							{
								bSync = FALSE;
								break;
							}
							pos += dwUnitSize;
							n++;
						} while ((n < 8) && (pos < (dwSyncBufPos + dwSrc)));
					}
				}
				if (bSync)
				{
					m_dwUnitSize = dwUnitSize;
					if (off < dwSyncBufPos)
					{
						if (off != 0)
						{
							dwSyncBufPos -= off;
							::memmove(m_SyncBuf, &m_SyncBuf[off], dwSyncBufPos);
						}
						// この同期検出ロジックでは↓の状態は起こり得ないハズ
#if 0
						// 同期済み時の同期用繰り越しバッファサイズはユニットサイズ以下である必要がある
						if (dwSyncBufPos > dwUnitSize)
						{
							dwSyncBufPos -= dwUnitSize;
							::memmove(m_SyncBuf, &m_SyncBuf[dwUnitSize], dwSyncBufPos);
						}
#endif
						m_dwSyncBufPos = dwSyncBufPos;
						return TsSync(pSrc, dwSrc, ppDst, pdwDst);
					}
					else
					{
						m_dwSyncBufPos = 0;
						return TsSync(&pSrc[off - dwSyncBufPos], (dwSrc - (off - dwSyncBufPos)), ppDst, pdwDst);
					}
				}
			}
		}
	}

	// 今回の入力では同期できなかったので、同期用繰り越しバッファに保存だけして終了
	if (dwSrc >= sizeof(m_SyncBuf))
	{
		::memcpy(m_SyncBuf, &pSrc[dwSrc - sizeof(m_SyncBuf)], sizeof(m_SyncBuf));
		m_dwSyncBufPos = sizeof(m_SyncBuf);
	}
	else if ((dwSyncBufPos + dwSrc) > sizeof(m_SyncBuf))
	{
		::memmove(m_SyncBuf, &m_SyncBuf[(dwSyncBufPos + dwSrc) - sizeof(m_SyncBuf)], (sizeof(m_SyncBuf) - dwSrc));
		::memcpy(&m_SyncBuf[sizeof(m_SyncBuf) - dwSrc], pSrc, dwSrc);
		m_dwSyncBufPos = sizeof(m_SyncBuf);
	}
	else
	{
		::memcpy(&m_SyncBuf[dwSyncBufPos], pSrc, dwSrc);
		m_dwSyncBufPos += dwSrc;
	}
	*ppDst = NULL;	// 呼び出し側でのdelete[]を保証する
	*pdwDst = 0;
	return FALSE;
}

static inline unsigned short GetPID(BYTE *p){ return (((unsigned short)(p[0] & 0x1f) << 8) | p[1]); }
static inline unsigned short GetSID(BYTE *p){ return (((unsigned short)p[0] << 8) | p[1]); }

#define MAX_PID	0x2000			// (8 * sizeof(int))で割り切れる
#define PID_SET(pid, map)		((map)->bits[(pid) / (8 * sizeof(int))] |= (1 << ((pid) % (8 * sizeof(int)))))
#define PID_CLR(pid, map)		((map)->bits[(pid) / (8 * sizeof(int))] &= ~(1 << ((pid) % (8 * sizeof(int)))))
#define PID_ISSET(pid, map)		((map)->bits[(pid) / (8 * sizeof(int))] & (1 << ((pid) % (8 * sizeof(int)))))
#define PID_MERGE(map1, map2)	{for(int i=0;i<(int)(MAX_PID / (8 * sizeof(int)));i++){(map1)->bits[i] |= (map2)->bits[i];}}
#define PID_ZERO(map)			(memset((map), 0 , sizeof(*(map))))
struct pid_set {
	int bits[MAX_PID / (8 * sizeof(int))];
};
struct pid_stat {
	DWORD count;
	DWORD biterror;
	DWORD drop;
	DWORD scrambled;
	BYTE ci;
};
static inline void UpdateStat(pid_stat *p, unsigned short pid, BYTE *pPKT)
{
	p->count++;
	if (pPKT[1] & 0x80)
		p->biterror++;
	if (pPKT[3] & 0x80)
		p->scrambled++;
	if (pPKT[3] & 0x10)
	{
		if ((pPKT[3] & 0x20) && (pPKT[4] > 0) && (pPKT[5] & 0x80))
		{
			fprintf(stderr, "AdaptationField DiscontinuityIndicator on : pid[0x%04x] ci[%d] / [%d]\n", pid, pPKT[3]&0x0f, p->ci);
			p->ci = 0xff;
		}
		else
		{
			BYTE cci = pPKT[3] & 0x0f;
			if (p->ci != cci)
			{
				if (p->ci != 0xff)
				{
					BYTE d = (cci > p->ci) ? (cci - p->ci) : ((cci + 0x10) - p->ci);
					p->drop += d;
				}
			}
			p->ci = (cci + 1) & 0x0f;
		}
	}
	else
	{
//		fprintf(stderr, "AdaptationField only : pid[0x%04x] ci[%d] / [%d]\n", pid, pPKT[3]&0x0f, p->ci);
		p->ci = 0xff;
	}
}
#define FLAG_CAT 0x0001
#define FLAG_NIT 0x0002
#define FLAG_SDT 0x0004
#define FLAG_EIT 0x0008
#define FLAG_TOT 0x0010
#define FLAG_BIT 0x0020
#define FLAG_CDT 0x0040
#define FLAG_ECM 0x0080
#define FLAG_EMM 0x0100
static void TsSplitter(DWORD dwServiceID, FILE *rfp, FILE *wfp, DWORD dwDelFlag, BOOL bModPMT, BOOL bLog)
{
	BYTE *pRawBuf, *pTsBuf, *pWriteBuf, pPAT[TS_PKTSIZE];
	BYTE pPMT[4104+TS_PKTSIZE];	// 4104 = 8(TSヘッダ + pointer_field + table_idからsection_length) + 4096(セクション長最大値)
	BYTE pPMTPackets[TS_PKTSIZE*32];
	int pos, iWritePos, iNumSplit;
	unsigned char pat_ci, rpmt_ci, wpmt_ci, lpmt_version, lcat_version, ver;
	unsigned short ltsid, pidPMT, pidEMM, pmt_tail;
	BOOL bChangePMT, bSplitPMT, bEnd, bPMTComplete;
	pid_set pids, save_pids[2], *p_new_pids, *p_old_pids;
	pid_stat pidst[MAX_PID];
	cTsSync sync;
	DWORD sum;

	memset(pidst, 0, sizeof(pidst));
	for (int i = 0; i < MAX_PID; i++)
		pidst[i].ci = 0xff;
	pRawBuf = new BYTE[TS_BUFSIZE];
	pTsBuf = new BYTE[TS_BUFSIZE];
	pWriteBuf = new BYTE[TS_WRITESIZE];
	pos = iWritePos = 0;
	pat_ci = 0x10;			// 0x1(payloadのみ) << 4 | 0x0(ci初期値)
	lpmt_version = lcat_version = wpmt_ci = 0xff;
	ltsid = pidPMT = pidEMM = 0xffff;	// 現在のTSID及びPMT,EMMのPID
	bChangePMT = bSplitPMT = FALSE;
	PID_ZERO(&pids);
	p_new_pids = &save_pids[0];
	p_old_pids = &save_pids[1];
	PID_ZERO(p_new_pids);
	PID_ZERO(p_old_pids);

	bEnd = bPMTComplete = FALSE;
	while (!bEnd)
	{
		DWORD dwLeft;
		{
			int len, left = TS_BUFSIZE;
			BYTE *p = pRawBuf;
			do
			{
				len = fread(p, 1, left, rfp);
				if (len <= 0)
				{
					bEnd = TRUE;
					if (len != 0)
						fprintf(stderr, "read error.\n");
					break;
				}
				p += len;
				left -= len;
			}while(left > 0);
			dwLeft = TS_BUFSIZE - left;
		}
		BYTE *pSrc, *pSrcHead;
		sync.TsSync(pRawBuf, dwLeft, &pSrcHead, &dwLeft);
		pSrc = pSrcHead;
		while (dwLeft > 0)
		{
			unsigned short pid = GetPID(&pSrc[1]);
			if (pid == 0x0000)	// PAT
			{
				// ビットエラー無しかつpayload先頭かつadaptation_field無し、PSIのpointer_fieldは0x00の前提
				if (!(pSrc[1] & 0x80) && (pSrc[1] & 0x40) && !(pSrc[3] & 0x20) && (pSrc[4] == 0x00))
				{
					// section_length
					// 9 = transport_stream_idからlast_section_numberまでの5バイト + CRC_32の4バイト
					int len = (((int)(pSrc[6] & 0x0f) << 8) | pSrc[7]) - 9;
					// 13 = TSパケットの頭から最初のprogram_numberまでのオフセット
					int off = 13;
					// ServiceIDチェックモード
					if (dwServiceID == 0xffffffff)
					{
						int num = 0;
						while ((len >= 4) && ((off + 4) < TS_PKTSIZE))
						{
							unsigned short sid = GetSID(&pSrc[off]);
							if (sid != 0x0000)
							{
								pid = GetPID(&pSrc[off+2]);
								fprintf(stderr, "%2d: ServiceID = %d ( 0x%x ) / PID of PMT = 0x%04x\n", num++, sid, sid, pid);
							}
							off += 4;
							len -= 4;
						}
						goto end;
					}
					// PATは1TSパケットに収まってる前提
					while ((len >= 4) && ((off + 4) < TS_PKTSIZE))
					{
						unsigned short sid = GetSID(&pSrc[off]);
						if (dwServiceID == sid)
						{
							pid = GetPID(&pSrc[off+2]);
							break;
						}
						off += 4;
						len -= 4;
					}
					if (pid != 0x0000)	// 対象ServiceIDのPMTのPIDが取得できた
					{
						// transport_stream_id
						unsigned short tsid = ((unsigned short )pSrc[8] << 8) | pSrc[9];
						if (pidPMT != pid || ltsid != tsid)	// PMTのPIDが更新された or チャンネルが変更された
						{
							// TSヘッダ
							pPAT[0] = 0x47;
							pPAT[1] = 0x60;
							pPAT[2] = 0x00;
							pPAT[3] = pat_ci;
							// pointer_field
							pPAT[4] = 0x00;
							// PAT
							pPAT[5] = 0x00;		// table_id
							pPAT[6] = 0xb0;		// section_syntax_indicator(1) + '0'(1) + reserved(2) + section_length(4/12)
							pPAT[7] = 0x11;		// section_length(8/12)
							pPAT[8] = tsid >> 8;
							pPAT[9] = tsid & 0xff;
							pPAT[10] = 0xc1;	// reserved(2) + version_number(5) + current_next_indicator(1)
							pPAT[11] = 0x00;	// section_number
							pPAT[12] = 0x00;	// last_section_number

							pPAT[13] = 0x00;	// program_number(8/16)
							pPAT[14] = 0x00;	// program_number(8/16)
							pPAT[15] = 0xe0;	// reserved(3) + network_PID(5/13)
							pPAT[16] = 0x10;	// network_PID(8/13)

							// 対象ServiceIDのテーブルコピー
							pPAT[17] = pSrc[off];
							pPAT[18] = pSrc[off+1];
							pPAT[19] = pSrc[off+2];
							pPAT[20] = pSrc[off+3];

							// CRC_32
							DWORD crc = CalcCRC32(&pPAT[5], 16);
							pPAT[21] = (BYTE)(crc >> 24);
							pPAT[22] = (BYTE)((crc >> 16) & 0xff);
							pPAT[23] = (BYTE)((crc >> 8) & 0xff);
							pPAT[24] = (BYTE)(crc & 0xff);

							memset(&pPAT[25], 0xff, TS_PKTSIZE - 25);

							if (bLog)
								fprintf(stderr, "PAT OK: sid[0x%x] / tsid[0x%x] -> [0x%x] / PMT change : pid[0x%x] -> [0x%x]\n", GetSID(&pSrc[off]), ltsid, tsid, pidPMT, pid);

							ltsid = tsid;
							pidPMT = pid;
							// PAT更新時には必ずPMT及びCATの更新処理を行う
							lpmt_version = lcat_version = 0xff;
							pidEMM = 0xffff;
						}
						else
						{
							if (pat_ci == 0x1f)
								pat_ci = 0x10;
							else
								pat_ci++;
							pPAT[3] = pat_ci;
						}
						pidst[0x0000].count++;
						memcpy(&pTsBuf[pos], pPAT, TS_PKTSIZE);
						pos += TS_PKTSIZE;
					}
				}
				else
				{
					if (bLog)
						fprintf(stderr, "odd PAT!: errflag[%d] sflag[%d] adpflag[%d] pf[0x%x]\n", (pSrc[1] & 0x80) ? 1 : 0, (pSrc[1] & 0x40) ? 1 : 0, (pSrc[3] & 0x20) ? 1 : 0, pSrc[4]);
				}
			}
			else if (pid == 0x0001)	// CAT
			{
				if (!(dwDelFlag & FLAG_CAT))
				{
					// ビットエラー無しかつpayload先頭かつadaptation_field無し、PSIのpointer_fieldは0x00の前提
					if (!(pSrc[1] & 0x80) && (pSrc[1] & 0x40) && !(pSrc[3] & 0x20) && (pSrc[4] == 0x00))
					{
						// version_number
						ver = (pSrc[10] >> 1) & 0x1f;
						if (ver != lcat_version)
						{
							// section_length
							// 9 = 2つ目のreservedからlast_section_numberまでの5バイト + CRC_32の4バイト
							int len = (((int)(pSrc[6] & 0x0f) << 8) | pSrc[7]) - 9;
							if (bLog)
								fprintf(stderr, "CAT found: len[%d]\n", len);
							// 13 = TSパケットの頭から最初のdescriptorまでのオフセット
							int off = 13;
							// CATも1TSパケットに収まってる前提
							while (len >= 2)
							{
								if ((off + 2) > TS_PKTSIZE)
									break;
								int cdesc_len = 2 + pSrc[off+1];
								if (cdesc_len > len || (off + cdesc_len) > TS_PKTSIZE)	// descriptor長さ異常
									break;
								if (pSrc[off] == 0x09)	// Conditional Access Descriptor
								{
									if (pSrc[off+1] >= 4 && (pSrc[off+4] & 0xe0) == 0xe0)	// 内容が妥当なら
									{
										// EMM PIDセット
										pid = GetPID(&pSrc[off+4]);
										if (pid != pidEMM)
										{
											if (pidEMM != 0xffff)
												PID_CLR(pidEMM, &pids);
											if (!(dwDelFlag & FLAG_EMM))
											{
												PID_SET(pid, &pids);
												pidEMM = pid;
												if (bLog)
													fprintf(stderr, "  set EMM pid[0x%x]\n", pid);
											}
										}
										break;	// EMMが複数のPIDで送られてくる事は無い前提
									}
								}
								off += cdesc_len;
								len -= cdesc_len;
							}
							lcat_version = ver;
						}
						UpdateStat(&pidst[0x0001], 0x0001, pSrc);
						memcpy(&pTsBuf[pos], pSrc, TS_PKTSIZE);
						pos += TS_PKTSIZE;
					}
					else
					{
						if (bLog)
							fprintf(stderr, "odd CAT!: errflag[%d] sflag[%d] adpflag[%d] pf[0x%x]\n", (pSrc[1] & 0x80) ? 1 : 0, (pSrc[1] & 0x40) ? 1 : 0, (pSrc[3] & 0x20) ? 1 : 0, pSrc[4]);
					}
				}
			}
			else if(pid == pidPMT)	// PMT
			{
				// ビットエラーがあったら無視
				if (pSrc[1] & 0x80)
					goto next;
				// 分割PMTをまとめないで良い場合は
				if (!bModPMT)
				{
					UpdateStat(&pidst[pidPMT], pidPMT, pSrc);
					// とりあえずコピーしてしまう
					memcpy(&pTsBuf[pos], pSrc, TS_PKTSIZE);
					pos += TS_PKTSIZE;
				}
				int len;
				BYTE *p;
				// payload先頭か？(adaptation_field無し、PSIのpointer_fieldは0x00の前提)
				if ((pSrc[1] & 0x40) && !(pSrc[3] & 0x20) && (pSrc[4] == 0x00))
				{
					// version_number
					ver = (pSrc[10] >> 1) & 0x1f;
					if (ver != lpmt_version)	// バージョンが更新された
					{
						if (bLog)
							fprintf(stderr, "PMT version change: [0x%x] -> [0x%x]\n", lpmt_version, ver);
						bChangePMT = TRUE;	// PMT更新処理開始
						bSplitPMT = bPMTComplete = FALSE;
						lpmt_version = ver;
						// 分割PMTをまとめる場合は
						if (bModPMT)
						{
							// 書き込み用PMTも更新を行う
							bPMTComplete = FALSE;
							// 書き込み用PMT用CI初期値保存
							if (wpmt_ci == 0xff)
								wpmt_ci = (pSrc[3] & 0x0f) | 0x10;
						}
					}
					// PMT更新処理中でなければ何もしない
					// (バージョンチェックのelseにしないのは、分割PMTの処理中にドロップがあった場合などの為)
					if (!bChangePMT)
					{
						if (bModPMT && bPMTComplete)
						{
						complete:
							for (int i = 0; i < iNumSplit; i++)
							{
								pPMTPackets[(TS_PKTSIZE * i) + 3] = wpmt_ci;
								if (wpmt_ci == 0x1f)
									wpmt_ci = 0x10;
								else
									wpmt_ci++;
							}
							if ((pos + (TS_PKTSIZE * iNumSplit)) <= TS_BUFSIZE)
							{
								pidst[pidPMT].count += iNumSplit;
								memcpy(&pTsBuf[pos], pPMTPackets, TS_PKTSIZE * iNumSplit);
								pos += (TS_PKTSIZE * iNumSplit);
							}
							else
							{
								if (dwServiceID != 0xffffffff)
								{
									memcpy(&pWriteBuf[iWritePos], pTsBuf, pos);
									iWritePos += pos;
									int len, left = iWritePos;
									BYTE *p = pWriteBuf;
									do
									{
										len = fwrite(p, 1, left, wfp);
										if (len <= 0)
										{
											fprintf(stderr, "write error.\n");
											goto end;
										}
										p += len;
										left -= len;
									} while (left > 0);
									iWritePos = 0;
								}
								pidst[pidPMT].count += iNumSplit;
								// TS_BUFSIZEは(TS_PKTSIZE * iNumSplit(理論最大値23))より大きい前提
								memcpy(pTsBuf, pPMTPackets, TS_PKTSIZE * iNumSplit);
								pos = (TS_PKTSIZE * iNumSplit);
							}
						}
						goto next;
					}
					// section_length
					len = (((int)(pSrc[6] & 0x0f) << 8) | pSrc[7]);
					if (bLog)
						fprintf(stderr, "PMT found: len[%d]", len);
					if (len > (TS_PKTSIZE - 8))	// TSパケットを跨ってる
					{
						memcpy(pPMT, pSrc, TS_PKTSIZE);
						// コピーしたデータの終端位置
						pmt_tail = TS_PKTSIZE;
						bSplitPMT = TRUE;
						rpmt_ci = pSrc[3] & 0x0f;
						if (rpmt_ci == 0x0f)
							rpmt_ci = 0;
						else
							rpmt_ci++;
						if (bLog)
							fprintf(stderr, " Split! : left[%d]\n", len - (TS_PKTSIZE - 8));
						goto next;
					}
					// 揃った
					p = pSrc;
					if (bLog)
						fprintf(stderr, "\n");
				}
				else
				{
					if (!bChangePMT)	// PMT更新処理中でなければ
						goto next;
					if (!bSplitPMT)		// 分割PMTの続き待ち中でなければ
						goto next;
					// CIが期待している値ではない、もしくはpayloadが無い場合
					if (((pSrc[3] & 0x0f) != rpmt_ci) || !(pSrc[3] & 0x10))
					{
						if (bLog)
							fprintf(stderr, "  CI error! pmt_ci[%d]:[%d]\n", rpmt_ci, (pSrc[3] & 0x0f));
						// 最初からやり直し
						bSplitPMT = FALSE;
						goto next;
					}
					int adplen;
					if (pSrc[3] & 0x20)	// adaptation_field有り(まあ無いとは思うけど一応)
					{
						adplen = pSrc[4] + 1;
						if (bLog)
							fprintf(stderr, "  adaptation! : adplen[%d]\n",adplen);
						if (adplen >= (TS_PKTSIZE - 4))
						{
							// adaptation_fieldの長さが異常なので最初からやり直し
							bSplitPMT = FALSE;
							goto next;
						}
					}
					else
						adplen = 0;
					// 分割PMTの続きコピー
					// pPMTのサイズはTS_PKTSIZEバイト余分に確保しているのでこれでも大丈夫
					memcpy(&pPMT[pmt_tail], &pSrc[4 + adplen], TS_PKTSIZE - 4 - adplen);
					// section_length
					len = (((int)(pPMT[6] & 0x0f) << 8) | pPMT[7]);
					if (len > (pmt_tail - 8 + (TS_PKTSIZE - 4 - adplen)))	// まだ全部揃ってない
					{
						pmt_tail += (unsigned short)(TS_PKTSIZE - 4 - adplen);
						if (rpmt_ci == 0x0f)
							rpmt_ci = 0;
						else
							rpmt_ci++;
						goto next;
					}
					// 揃った
					p = pPMT;
				}
				// この時点でセクションは必ず揃っている
				int limit = 8 + len;
				// 新PIDマップ初期化
				PID_ZERO(p_new_pids);
				// PMT PIDセット(マップにセットしても意味無いけど一応)
				PID_SET(pidPMT, p_new_pids);

				if (!(dwDelFlag & FLAG_NIT))
					PID_SET(0x0010, p_new_pids);	// NIT PIDセット
				if (!(dwDelFlag & FLAG_SDT))
					PID_SET(0x0011, p_new_pids);	// SDT PIDセット
				if (!(dwDelFlag & FLAG_EIT))
				{
					PID_SET(0x0012, p_new_pids);	// EIT PIDセット
					PID_SET(0x0026, p_new_pids);
					PID_SET(0x0027, p_new_pids);
				}
				if (!(dwDelFlag & FLAG_TOT))
					PID_SET(0x0014, p_new_pids);	// TOT PIDセット
				if (!(dwDelFlag & FLAG_BIT))
					PID_SET(0x0024, p_new_pids);	// BIT PIDセット
				if (!(dwDelFlag & FLAG_CDT))
					PID_SET(0x0029, p_new_pids);	// CDT PIDセット
				if (pidEMM != 0xffff)	// FLAG_EMMが立っている時はpidEMMは必ず0xffff
					PID_SET(pidEMM, p_new_pids);	// EMM PIDセット
				// PCR PIDセット
				pid = GetPID(&p[13]);
				if (pid != 0x1fff)
					PID_SET(pid, p_new_pids);
				// program_info_length
				int desc_len = (((int)(p[15] & 0x0f) << 8) | p[16]);
				if (bLog)
					fprintf(stderr, "PMT OK: pid[0x%x] sid[0x%x] len[%d] desc_len[%d]\n  set PCR pid[0x%x]\n", GetPID(&p[1]), GetSID(&p[8]), len, desc_len, pid);
				// 17 = 最初のdescriptorのオフセット
				int off = 17;
				int left = desc_len;
				while (left >= 2)
				{
					if ((off + 2) > limit)	// program_info_length異常
					{
						bSplitPMT = FALSE;
						goto next;
					}
					int cdesc_len = 2 + p[off+1];
					if (cdesc_len > left || (off + cdesc_len) > limit)	// descriptor長さ異常
					{
						bSplitPMT = FALSE;
						goto next;
					}
					if (p[off] == 0x09)	// Conditional Access Descriptor
					{
						if (p[off+1] >= 4 && (p[off+4] & 0xe0) == 0xe0)	// 内容が妥当なら
						{
							// ECM PIDセット(第1ループに無効ECMは来ない / ARIB TR-B14/B15)
							pid = GetPID(&p[off+4]);
							if (!(dwDelFlag & FLAG_ECM))
							{
								PID_SET(pid, p_new_pids);
								if (bLog)
									fprintf(stderr, "  set ECM pid[0x%x]\n",pid);
							}
						}
					}

					if (bLog)
					{
						fprintf(stderr, "  off[0x%x]:", off);
						for (int i = 0; i < cdesc_len; i++)
							fprintf(stderr, " %02x", p[off + i]);
						fprintf(stderr, "\n");
					}

					off += cdesc_len;
					left -= cdesc_len;
				}
				// データ異常が無ければ必要無いが一応
				off = 17 + desc_len;
				// 13 = program_numberからprogram_info_lengthまでの9バイト + CRC_32の4バイト
				len -= (13 + desc_len);
				while (len >= 5)
				{
					if ((off + 5) > limit)	// program_info_length異常
					{
						bSplitPMT = FALSE;
						goto next;
					}
					pid = GetPID(&p[off+1]);
					if (p[off] != 0x0d)	// stream_type "ISO/IEC 13818-6 type D"は破棄
					{
						PID_SET(pid, p_new_pids);
						if (bLog)
							fprintf(stderr, "  set pid[0x%x] type[0x%x] ", pid, p[off]);
					}
					else
					{
						if (bLog)
							fprintf(stderr, "  strip pid[0x%x] type[0x%x] ", pid, p[off]);
					}
					// ES_info_length
					desc_len = (((int)(p[off+3] & 0x0f) << 8) | p[off+4]);
					// 5 = 最初のdescriptorのオフセット
					int coff = off + 5;
					left = desc_len;
					if (bLog)
						fprintf(stderr, "desc_len[%d]\n  (\n", desc_len);
					while (left >= 2)
					{
						if ((coff + 2) > limit)	// ES_info_length異常
						{
							bSplitPMT = FALSE;
							goto next;
						}
						int cdesc_len = 2 + p[coff+1];
						if (cdesc_len > left || (coff + cdesc_len) > limit)	// descriptor長さ異常
						{
							bSplitPMT = FALSE;
							goto next;
						}
						if (p[coff] == 0x09)	// Conditional Access Descriptor
						{
							if (p[coff+1] >= 4 && (p[coff+4] & 0xe0) == 0xe0)	// 内容が妥当なら
							{
								// ECM PIDセット
								pid = GetPID(&p[coff+4]);
								if (pid != 0x1fff)
								{
									if (!(dwDelFlag & FLAG_ECM))
									{
										PID_SET(pid, p_new_pids);
										if (bLog)
											fprintf(stderr, "   set ECM pid2[0x%x]\n", pid);
									}
								}
							}
						}

						if (bLog)
						{
							fprintf(stderr, "   off[0x%x]:", coff);
							for (int i = 0; i < cdesc_len; i++)
								fprintf(stderr, " %02x", p[coff + i]);
							fprintf(stderr, "\n");
						}

						coff += cdesc_len;
						left -= cdesc_len;
					}
					// 5 = stream_typeからES_info_lengthまでの5バイト
					off += (5 + desc_len);
					len -= (5 + desc_len);
					if (bLog)
						fprintf(stderr, "  )\n");
				}

				// section_length
				len = (((int)(p[6] & 0x0f) << 8) | p[7]);
				// CRCチェック
				// 3 = table_idからsection_lengthまでの3バイト
				if (CalcCRC32(&p[5], len + 3) == 0)
				{
					// 新PIDマップを適用
					memcpy(&pids, p_new_pids, sizeof(pids));
					// 旧PIDマップをマージ
					PID_MERGE(&pids, p_old_pids);
					// 以降は今回のPMTで示されたPIDを旧PIDマップとする
					pid_set *p_tmp_pids;
					p_tmp_pids = p_old_pids;
					p_old_pids = p_new_pids;
					p_new_pids = p_tmp_pids;
					// PMT更新処理完了
					bChangePMT = bSplitPMT = FALSE;
					// 分割PMTをまとめる場合は、書き込み用PMTパケット作成
					if (bModPMT)
					{
						// TSヘッダを除いた残りデータサイズ
						// 4 = pointer_fieldの1バイト + 上のと同じ3バイト
						int left = 4 + len;
						// このPMTをいくつのTSパケットに分割する必要があるか
						iNumSplit = ((left - 1) / (TS_PKTSIZE - 4)) + 1;
						memset(pPMTPackets, 0xff, (TS_PKTSIZE * iNumSplit));
						for (int i = 0; i < iNumSplit; i++)
						{
							// TSヘッダの4バイト分をコピー
							memcpy(&pPMTPackets[TS_PKTSIZE * i], p, 4);
							// 先頭パケット以外はunit_start_indicatorを外す
							if (i != 0)
								pPMTPackets[(TS_PKTSIZE * i) + 1] &= ~0x40;
							int n;
							if (left > (TS_PKTSIZE - 4))
								n = TS_PKTSIZE - 4;
							else
								n = left;
							memcpy(&pPMTPackets[(TS_PKTSIZE * i) + 4], &p[4 + ((TS_PKTSIZE - 4) * i)], n);
							left -= n;
						}
						bPMTComplete = TRUE;
						// まずこのパケットを出力
						goto complete;
					}
				}
				else
				{
					// CRCチェックエラーなので最初からやり直し
					bSplitPMT = FALSE;
				}
			}
			else
			{
				if (PID_ISSET(pid, &pids))
				{
					UpdateStat(&pidst[pid], pid, pSrc);
					memcpy(&pTsBuf[pos], pSrc, TS_PKTSIZE);
					pos += TS_PKTSIZE;
				}
			}

		next:
			pSrc += TS_PKTSIZE;
			dwLeft -= TS_PKTSIZE;

			// 1ループでのposの増加は0もしくはTS_PKTSIZE、あるいはチェックした上でのTS_PKTSIZE * iNumSplitなので、
			// ここでのバウンダリチェックはこれで大丈夫なハズ
			if (pos == TS_BUFSIZE)
			{
				if (dwServiceID != 0xffffffff)
				{
					memcpy(&pWriteBuf[iWritePos], pTsBuf, TS_BUFSIZE);
					iWritePos += TS_BUFSIZE;
					if (iWritePos == TS_WRITESIZE)
					{
						int len, left = TS_WRITESIZE;
						BYTE *p = pWriteBuf;
						do
						{
							len = fwrite(p, 1, left, wfp);
							if (len <= 0)
							{
								fprintf(stderr, "write error.\n");
								goto end;
							}
							p += len;
							left -= len;
						} while (left > 0);
						iWritePos = 0;
					}
				}
				pos = 0;
			}
		}
		delete[] pSrcHead;
	}
	// バッファに残っている分を書き出し
	if ((iWritePos != 0) || (pos != 0))
	{
		if (dwServiceID != 0xffffffff)
		{
			if (pos != 0)
			{
				memcpy(&pWriteBuf[iWritePos], pTsBuf, pos);
				iWritePos += pos;
			}
			int len, left = iWritePos;
			BYTE *p = pWriteBuf;
			do
			{
				len = fwrite(p, 1, left, wfp);
				if (len <= 0)
				{
					fprintf(stderr, "write error.\n");
					goto end;
				}
				p += len;
				left -= len;
			} while (left > 0);
		}
	}

	sum = 0;
	for (int i = 0; i < MAX_PID; i++)
	{
		if (pidst[i].count != 0)
		{
			sum += pidst[i].count;
			fprintf(stderr, "pid(0x%04x) : count = %u / biterror = %u, drop = %u, scrambled = %u\n", i, pidst[i].count, pidst[i].biterror, pidst[i].drop, pidst[i].scrambled);
		}
	}
	fprintf(stderr, "total %u packets.\n", sum);

end:
	delete[] pWriteBuf;
	delete[] pTsBuf;
	delete[] pRawBuf;
}

static void usage(char *p)
{
#ifdef _WINDOWS
	fprintf(stderr, "usage: %s -i input (-s ServiceID) (-o output) (-d deletelist) (-m) (-v)\n", p);
	fprintf(stderr, "     : input = input TS filename\n");
#else
	fprintf(stderr, "usage: %s (-i input) (-s ServiceID) (-o output) (-d deletelist) (-m) (-v)\n", p);
	fprintf(stderr, "     : input = input TS filename / default = stdin\n");
#endif
	fprintf(stderr, "     : ServiceID = extract ServiceID / default = \"check sid\" mode\n");
	fprintf(stderr, "     : output = output TS filename / default = stdout\n");
	fprintf(stderr, "     : deletelist = delete PSI/SI name list(csv)\n");
	fprintf(stderr, "     :              valid value: CAT, NIT, SDT, EIT, TOT, BIT, CDT, ECM, EMM\n");
	fprintf(stderr, "     :              e.g. -d \"CAT,EIT,BIT,CDT\"\n");
	fprintf(stderr, "     : use \"-m\" option = \"PMT defrag\" enable\n");
	fprintf(stderr, "     : use \"-v\" option = verbose log enable\n");
	exit(0);
}

#ifdef _WINDOWS
#define TAG_MAX		16
static char *optarg;
struct TAG {
	int tag;
	int havearg;
};
static int getopt(int argc, char *argv[], const char *tag)
{
	int i, j;
	static struct TAG tags[TAG_MAX];
	static int ntag, index = 1;

	if (index == 1)
	{
		j = strlen(tag);
		ntag = 0;
		memset(tags, 0, sizeof(tags));
		for (i = 0; i < j; i++)
		{
			while ((tag[i] == ' ') || (tag[i] == '\t'))
				i++;
			if (tag[i] == '\0')
				break;
			tags[ntag].tag = tag[i++];
			if (tag[i] == ':')
				tags[ntag].havearg = 1;
			ntag++;
			if (ntag >= TAG_MAX)
				break;
		}
	}
	for (i = index; i < argc; i++)
	{
		if (argv[i][0] != '-')
			continue;
		for (j = 0; j < ntag; j++)
		{
			if (argv[i][1] == tags[j].tag)
			{
				if (tags[j].havearg)
					optarg = argv[++i];
				index = i + 1;
				return tags[j].tag;
			}
		}
		index = i + 1;
		return argv[i][1];
	}
	return -1;
}
#endif

int main(int argc, char *argv[])
{
	int opt, sfind, ifind, ofind, mfind, vfind;
	char *input, *output, *dlist = NULL;
	const char *name[] = {"CAT", "NIT", "SDT", "EIT", "TOT", "BIT", "CDT", "ECM", "EMM", NULL};
	DWORD dflag = 0, sid = 0xffffffff;
	FILE *rfp, *wfp;

	// パラメータ処理
	sfind = ifind = ofind = mfind = vfind = 0;
	while ((opt = getopt(argc, argv, "i:s:o:d:mv")) != -1)
	{
		switch (opt)
		{
		case 'i':	// 入力元ファイル名指定
			input = optarg;
			ifind = 1;
			break;
		case 's':	// サービスID指定
			sid = strtoul(optarg, NULL, 0);
			sfind = 1;
			break;
		case 'o':	// 出力先ファイル名指定
			output = optarg;
			ofind = 1;
			break;
		case 'd':	// 削除PSI/SI名指定
			dlist = optarg;
			break;
		case 'm':	// PMTデフラグ
			mfind = 1;
			break;
		case 'v':	// 処理経過冗長出力
			vfind = 1;
			break;
		default:
			usage(argv[0]);
		}
	}
#ifdef _WINDOWS
	// windowsではstdinからの入力はサポートしない(手抜き)
	if (!ifind || (sfind && (sid > 0xffff)))
		usage(argv[0]);
#else
	// check sidモードは基本的にファイルに対して使う
	// (引数無しで実行した場合にusage表示したい為でもある)
	// また、明らかに有効でないサービスIDはエラー
	if ((!ifind && !sfind) || (sfind && (sid > 0xffff)))
		usage(argv[0]);
#endif

	// 削除PSI/SI指定パース
	if (dlist != NULL)
	{
		char *p = dlist;
		int n, cnt = 1;
		while (*p != '\0')
		{
			if (*p == ',')
				cnt++;
			p++;
		}
		char **pp = new char *[cnt];
		p = dlist;
		n = 0;
		do
		{
			while (*p == '\t' || *p == ' ')
				p++;
			pp[n++] = p;
			while (*p != '\t' && *p != ' ' && *p != ',' && *p != '\0')
				p++;
			if (*p != ',' && *p != '\0')
			{
				*p++ = '\0';
				while (*p != ',' && *p != '\0')
					p++;
			}
			*p++ = '\0';
		} while (n < cnt);
		for (int i = 0; i < cnt; i++)
		{
			for (int j = 0; name[j] != NULL; j++)
			{
				if (strcmp(pp[i], name[j]) == 0)
				{
					dflag |= (1 << j);
					break;
				}
			}
		}
		delete[] pp;
	}

	if (ifind)
	{
		rfp = fopen(input, "rb");
		if (rfp == NULL)
		{
			fprintf(stderr, "input file open error: %s\n", input);
			return -1;
		}
	}
	else
		rfp = stdin;

	if (ofind)
	{
		wfp = fopen(output, "wb");
		if (wfp == NULL)
		{
			fprintf(stderr, "output file open error: %s\n", output);
			if (ifind)
				fclose(rfp);
			return -1;
		}
	}
	else
		wfp = stdout;

	InitCrc32Table();

	TsSplitter(sid, rfp, wfp, dflag, mfind, vfind);

	if (ifind)
		fclose(rfp);
	if (ofind)
		fclose(wfp);

	return 0;
}
