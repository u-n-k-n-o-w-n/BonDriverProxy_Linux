# BonDriverProxy_Linux

## これは何？

BonDriverProxyをUnix系の環境での使用の為にpthreadで再実装した物と、Linux + PTシリーズ用のBonDriverです。
動作はUbuntu 14.04 + PT3(ドライバ:[m-tsudo/pt3](https://github.com/m-tsudo/pt3))で確認しています（※ドライバ使用の為、改変版pt1_ioctl.hをincludeしています）。
主な用途としては、WindowsのTVTestからLinuxの録画サーバのチューナを使用して視聴、と言う様な感じです。

また、現状Unix系の環境ではBonDriverと言うインタフェースを利用しているソフトは無いかもしれませんが、あって困る事は無いだろ、て事でクライアント側も入れています。
録画ソフトや視聴ソフト側でBonDriverを扱える様にすれば、リアルタイム視聴と録画ソフトとでチャンネル変更の優先権を設定できるため、録画ソフト側は、このチューナが視聴に使われてた場合は…的な、面倒な資源管理をやらなくても良くなって嬉しいかもしれません。
WindowsではほぼデファクトスタンダードになっているBonDriverインタフェースを提供する事で、Windows用に書いたコードの移植がやりやすくなるので、Unix系の環境もWindows並に充実して行けばいいなと思います。

## サーバ

ソースディレクトリで、

```sh
g++ -O2 -Wall -o BonDriverProxy BonDriverProxy.cpp -lpthread -ldl
```

でコンパイルできます。
Windows版でiniファイルだった物は、コマンドラインからの引数になりました。
引数無しで実行すると例が出ます。

## クライアントモジュール

サーバ側と同じく、ソースディレクトリで、

```sh
g++ -O2 -shared -fPIC -Wall -o BonDriver_Proxy.so BonDriver_Proxy.cpp -lpthread -ldl
```

でコンパイルできます。
設定ファイルは「モジュール名.conf」をモジュールと同じディレクトリに設置する形になります。
例えばモジュールが

```
/home/unknown/work/BonDriver_Proxy.so
```

だったとすると、

```
/home/unknown/work/BonDriver_Proxy.so.conf
```

となります。
使用方法や設定内容はWindows版BonDriverProxyと同じですが、行頭「;」の行はコメント行になります。

## Linux + PTシリーズ用BonDriver

ソースディレクトリで、

```sh
g++ -O2 -shared -fPIC -Wall -o BonDriver_LinuxPT.so BonDriver_LinuxPT.cpp -lpthread -ldl
```

でコンパイルできます。
設定ファイルの名称と設置場所ルールは上記クライアントモジュールと同じで、行頭「;」の行がコメント行になるのも同じです。
デバイスの数だけモジュールをコピーして使用します。
必要な変更事項は、

```
#DEVICE=/dev/pt3video0
```

のデバイスパスをモジュール毎に被らないように変更するのと、必要であれば、

```
#USELNB=0
```

の0を1にすればLNBへの給電がオンになります…と言うか、動作確認は出来てないので、そうなる様につくってるつもりですと言う事で…。
PT3が一枚刺さっている環境で、`/home/unknown/work`にモジュールを設置するとすると、

```
// /home/unknown/workの内容
// .soファイルはコンパイルして出来たモジュールのコピー
// .confファイルはBonDriver_LinuxPT.confをコピーして必要箇所のみ変更
--------------------------------------------------------------------------------
BonDriver_LinuxPT-S0.so
BonDriver_LinuxPT-S0.so.conf
BonDriver_LinuxPT-S1.so
BonDriver_LinuxPT-S1.so.conf
BonDriver_LinuxPT-T0.so
BonDriver_LinuxPT-T0.so.conf
BonDriver_LinuxPT-T1.so
BonDriver_LinuxPT-T1.so.conf
--------------------------------------------------------------------------------
.confファイルの#DEVICE=行はそれぞれ、
// BonDriver_LinuxPT-S0.so.conf
---
#DEVICE=/dev/pt3video0
---
// BonDriver_LinuxPT-S1.so.conf
---
#DEVICE=/dev/pt3video1
---
// BonDriver_LinuxPT-T0.so.conf
---
#DEVICE=/dev/pt3video2
---
// BonDriver_LinuxPT-T1.so.conf
---
#DEVICE=/dev/pt3video3
---
```

となります。
それらより下は、説明がめんどくさいのでスルーです。
あんまりいじる必要は無いんじゃないかと思います。
詳しい方は、UHF(地デジ)に関しては、自分の地域の放送局の物理チャンネルを調べて不要箇所を削除すれば、チャンネルスキャンの時間をだいぶ減らせるかと思います（※不要箇所削除の際は、残した物のBonDriverとしてのチャンネル番号を0からの連番に採番しなおしてください）。
なお、もし編集する際は文字コードはUTF-8を使用してください。

ライセンス等はWindows版に準じますので、そちらのReadMe.txtを確認してくださいね。

Jun/18/2014 unknown <unknown_@live.jp>

## 更新履歴

* version 1.1.4.1 (Jul/04/2014)
    * サーバ側のグローバルなインスタンスロック処理を厳密に行うのをデフォルトにした  
	  副作用として、サーバプロセスがBonDriver_Proxyをロードし、それが自分自身に再帰接続(直接ではなく、それ以降の
	  プロキシチェーンのどこかからでも同じ)するような状況ではデッドロックする  
	  ただし、テスト用途以外でその様な使用方法が必要になる状況は多分存在しないので、まず問題になる事は無いハズ  
	  一応、コンパイル時にSTRICT_LOCKが定義されているかどうかで変更可能なので、以前の動作に戻したい場合は、
	  BonDriverProxy.cppの頭の方にある#defineをコメントアウトすれば良い  
	  可能性はごく低いが理論的にはあり得るメモリリークへの対策を追加
	* クライアント側のIBonDriver2の方のSetChannel()で、リクエストされたスペース/チャンネルと、現在保持している
	  スペース/チャンネルとの比較処理を廃止  
	  ※他のクライアントのチャンネル変更に引きずられた場合、実際のスペース/チャンネルと保持しているスペース/  
	  ※チャンネルにズレが生じる為

* version 1.1.4.0 (Jul/03/2014)
    * サーバ側のTSストリーム配信時のロック処理をロードしたBonDriverのインスタンス毎にグループ分けした  
	  これにより、グローバルなインスタンスリストのロックや他のBonDriverインスタンスのTSストリーム配信処理の影響を
	  受けなくなるのと、個々のクライアントへのTSパケット作成時にロック及び比較処理が不要になる
    * サーバにSIGPIPEのハンドリング(単に無視するだけ)を追加  
    * BonDriver_Proxyの各種BonDriverAPIを複数スレッドから非同期で呼び出された場合、理論的にはあるコマンドへの
	  レスポンスを他のコマンドへのレスポンスと取り違える可能性があったのに対応  
	  ※普通に考えるとそのような呼び出し方をする必然性は無いと思われるが、対応しておいても損は無さそうなので…
    * BonDriver_Proxy及びBonDriver_LinuxPTで、現在使用していないTSキューのイベント関連の処理をコメントアウトした  
	  これはWaitTsStream()を真面目に実装しようとすると必要になるが、そもそもWaitTsStream()はインタフェース的に
	  若干使い難く、使う必要はまず無い(GetTsStream()をポーリングの方が自由度が高くロスも少ない)為、少なくとも
	  当面は必要無しと判断
    * BonDriver_LinuxPTをとりあえずPLEXチューナに対応とした(ただし、動作確認をとっていただいたのはx64でのPX-W3PEのみ)  
	  もっとも、PLEXチューナのドライバはバイナリでしか提供されていない為、それが利用できる環境(CentOS等)が必要になる  
	  なお、0x8d83のioctl()の引数の構造体は{ポインタ、バイト値、ポインタ、バイト値}と言う微妙な構造であり、
	  本来はアライメントを気にする必要があるが、少なくともx64ではgcc標準のアライメント(8バイト)で大丈夫らしいので
	  特に明示していない  
	  気になる人は自分の使うアーキテクチャでの標準アライメント(x64:8バイト/x86:4バイト)を明示的に設定する方向で
    * x86用にコンパイルした時の警告に対応

* version 1.1.3.0 (Jun/25/2014)
    * クライアント側にサーバへの接続タイムアウトの指定を出来る機能を追加  
	  ※confのCONNECT_TIMEOUTで指定(単位は秒)
    * ついでにWOLのパケット自動送信機能を追加  
	  ※使い方に難しい点は無いと思うのでBonDriver_Proxy.conf参照。TARGET_MACADDRESSは必須だが、TARGET_ADDRESS及びTARGET_PORT行に関しては、無い場合はADDRESSとPORTをそれぞれ使用する。  
	  ※なお、TARGET_PORTはUDPのポートである事に注意。

* version 1.1.2.1 (Jun/21/2014)
    * アプリ側で環境依存を気にしないでSIGALRM使えるようにusleep()をnanosleep()に変更
    * BonDriver使用のサンプルプログラム追加  
	  コンパイル方法や使用方法はソース(sample.cpp)参照

* version 1.1.2.0 (Jun/19/2014)
    * SetChannel()内で、GetTsStream()でアプリに返したバッファを解放してしまうBonDriverを読み込んだ場合、
	  タイミングが悪いと解放後メモリにアクセスしてしまう可能性があったのに対応
    * BonDriver_Proxy.cpp自身もプロキシ対象のBonDriverからのレスポンスをサーバから受け取ってからではあるものの
	  上記の状態だったので、アプリに返したバッファは必須の時以外解放しないように修正
    * BonDriver_Proxy.cppのPurgeTsStream()でのロック処理漏れを修正
    * コマンドパケット/TSパケットのキューがオーバーフローした際にその旨を表示するようにした
    * デフォルトのキューサイズを大きくした(なおキューのサイズはプロセスに使用を許可するメモリ量の制限値的な
	  意味なので、大きくしておいてもその量が必要時以外に確保されるわけではない)
    * その他ロジックは(ほぼ)変更無しでのソースコード整形

* version 1.1.1.0 (Jun/18/2014)
    * 初版リリース
