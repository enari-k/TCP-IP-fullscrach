# TCP/IP Protocol Stack from Scratch

Cで実装したユーザー空間TCP/IPプロトコルスタックです。

『ゼロからのTCP/IPプロトコルスタック自作入門』の実装をベースに、教材を一通り完走したあと、

- 複数クライアントを同時に扱う
- TCPサーバーとして継続的に動作させる
- 接続数を増やして負荷を掛ける
- リソース管理や並行処理の問題を修正する

といった拡張を行っています。

現在は、**4095個のTCP接続を同時に維持できることを確認済み**です。

---

## 実装しているもの

現在、以下のプロトコルや機能を実装しています。

- Ethernet
- ARP
- IPv4
- ICMP
- UDP
- TCP
- Socket API
- TCP Echo Server
- 複数TCPクライアントの同時接続
- connectionごとのworker thread
- TCP connection churn test
- 大量同時接続テスト

---

## このリポジトリで追加・修正したもの

教材の実装を完成させたあと、複数クライアントを接続できるサーバーとして動作させるために追加の修正を行っています。

主な変更点は以下です。

### TCP PCBのリソース管理修正

`CLOSED`状態のTCP PCBを`close()`した際、PCBがpoolへ返却されないケースを修正しました。

これにより、

```text
allocate
↓
close
↓
release
↓
同じslotを再利用
```

というリソースのライフサイクルが正しく動作するようになっています。

### 解放済みsocketの参照を防止

socket layerでは固定長配列を利用しています。

解放済みのsocketについて、配列のindex自体は有効であるため、`sock_get()`が未使用slotを取得できてしまう問題がありました。

現在は、

```c
if (s->used != 1)
    return NULL;
```

として、使用中のsocketだけを取得するようにしています。

### `accept()`中のglobal lock保持を修正

以前は、blockingする`tcp_cmd_accept()`を呼び出している間もsocket layer全体のlockを保持していました。

そのため、

```text
main thread
    accept待ち
    global lock保持

worker thread
    recv/sendしたい
    ↓
    global lock待ち
```

となり、次のclientが接続するまで既存connectionの処理まで止まる問題がありました。

現在は、

```text
lock
    listener確認
    socket slot予約
unlock

tcp_cmd_accept()

lock
    accepted socketを初期化
unlock
```

という形に変更しています。

### accept用socket slotの事前予約

`tcp_cmd_accept()`成功後にsocket slotを確保できない場合、

```text
TCP child PCBは存在する
↓
対応するsocketが存在しない
```

という状態になります。

これを避けるため、現在は`accept()`前にsocket slotを1個予約しています。

そのため、最大N clientを扱うには現在の設計上、

```text
TCP PCB:
    LISTEN PCB    1
    client PCB    N
    ----------------
    N + 1

socket:
    listener       1
    client socket  N
    pending accept 1
    ----------------
    N + 2
```

のslotが必要になります。

---

## 大量接続テスト

接続数を増やしながら、TCP connectionを維持するテストを行いました。

確認結果は以下です。

| Client connections | Threads | RSS | VSZ |
| ---: | ---: | ---: | ---: |
| 255 | 257 | 約5.1 MiB | 約2.1 GiB |
| 1023 | 1025 | 約14.5 MiB | 約8.1 GiB |
| 2047 | 2049 | 約27 MiB | 約16.2 GiB |
| 4095 | 4097 | 約52 MiB | 約32.3 GiB |

4096個のTCP PCBを用意した場合、1個をLISTEN PCBとして使用するため、保持できるclient connectionは4095個になります。

```text
LISTEN PCB      1
client PCB   4095
----------------
total        4096
```

4096 client connectionsを保持したい場合は、TCP PCBを4097個以上用意する必要があります。

---

## TCP PCB枯渇時の挙動

connection数を増やした際、一部の`connect()`だけ1秒以上かかる問題が発生しました。

48並列で合計4800 connectionを作成するテストでは、TCP PCBを64個にした場合、

```text
4800 / 4800 success

>=100ms = 59 / 4800
>=500ms = 59 / 4800
>=900ms = 59 / 4800

connect p99 ≈ 1024ms
max         ≈ 2047ms
```

となりました。

packet captureを確認すると、

```text
client → server SYN
↓
SYN+ACKが返らない
↓
約1秒後にSYN retransmission
↓
再び応答なし
↓
SYN retransmission
↓
SYN+ACK
```

となっていました。

server側では同時に、

```text
tcp_pcb_alloc() failure
```

が発生しており、TCP PCB poolの一時的な枯渇によってSYNへ応答できなくなっていました。

TCP PCBを128個へ増やしたところ、

```text
4800 / 4800 success

>=100ms = 0
>=500ms = 0
>=900ms = 0

connect max < 67ms
echo max    < 70ms
```

となり、SYN再送による遅延が解消されました。

---

## Benchmark

大量接続時の問題を調べるため、いくつかのテスト用クライアントを用意しています。

### Connection churn

複数workerで、

```text
socket
↓
connect
↓
send
↓
recv
↓
close
```

を繰り返します。

測定項目として、

- connect成功数
- echo成功数
- connect latency
- echo RTT
- mean
- median
- p95
- p99
- max
- 100ms以上のconnect数
- 500ms以上のconnect数
- 900ms以上のconnect数

などを確認できます。

例:

```bash
python3 sock_cliant_period.py \
    --workers 48 \
    --cycles 100
```

### Idle connection test

大量のTCP connectionを確立し、その状態を一定時間維持します。

例:

```bash
python3 sock_cliant_idle.py 4095 30
```

4095 connectionを確立し、そのまま30秒間維持します。

---

## Debug logの無効化

大量接続時はdebug log自体の負荷が測定結果へ影響するため、benchmark時にはcompile-timeでdebug logを無効化できます。

```c
#ifdef DISABLE_DEBUG_LOG
#define debugf(...) ((void)0)
#else
#define debugf(fmt, ...) \
    logf('D', fmt, ##__VA_ARGS__)
#endif
```

build:

```bash
make clean
make CFLAGS+=' -DDISABLE_DEBUG_LOG'
```

標準出力をredirectするだけでは関数呼び出しや引数評価自体は残るため、benchmark時はこちらを利用しています。

---

## Thread-per-connection

現在のEcho Serverでは、acceptしたconnectionごとにpthreadを1本作成します。

```text
0 connections:
threads = 2

64 connections:
threads = 66

255 connections:
threads = 257

1023 connections:
threads = 1025

2047 connections:
threads = 2049

4095 connections:
threads = 4097
```

4095接続時には、

```text
RSS ≈ 52 MiB
VSZ ≈ 32.3 GiB
```

となりました。

現在のところ、接続ごとのworker threadは通信がない間はsleepしており、idle時のCPU使用率の主要因にはなっていません。

一方で、threadごとにstack用の仮想メモリが予約されるため、接続数に比例してVSZは大きくなります。

---

## 現在確認している性能上の課題

4095 connectionを維持した状態では、接続ごとのworkerとは別に存在する1本の基盤threadが40%前後のCPUを使用することを確認しています。

thread単位で確認すると、

```text
4097 threads
↓
4095 connection workersはほぼsleep
↓
1本の内部threadだけCPU使用率が高い
```

という状態でした。

このthreadに`strace`を行ったところ、

```text
% time     seconds  usecs/call  calls  syscall
100.00    0.174573          29   5897  rt_sigtimedwait
```

となっていました。

10秒間で約5900回`rt_sigtimedwait()`を呼び出しています。

ただし、`rt_sigtimedwait()`内部で消費しているCPU時間自体は小さいため、signalを受け取った後のユーザー空間の処理が主なCPU使用箇所である可能性があります。

今後、

- TCP timerによるPCB走査
- TCP PCB lookup
- scheduler / event処理
- signal発生回数
- connection数と内部処理量の関係

などを調査する予定です。

---

## 現在の制約

現在の実装には以下のような制約があります。

- IPv4のみ
- TCP PCBは固定長配列
- socket tableも固定長配列
- connectionごとにpthreadを1本作成
- TCP PCBの検索に線形走査を利用する箇所がある
- 大量接続時の内部threadのCPU使用率について調査中
- production用途を目的とした実装ではない

このリポジトリでは、最初から高性能な実装を目指すのではなく、

```text
負荷を掛ける
↓
問題を観測する
↓
原因を調べる
↓
修正する
↓
さらに負荷を上げる
```

という形で段階的に拡張しています。

---

## Architecture

全体としては、おおよそ以下のような構成になっています。

```text
          Application
               │
          Socket API
               │
       ┌───────┴───────┐
       │               │
      TCP             UDP
       │               │
       └───────┬───────┘
               │
              IPv4
          ┌────┴────┐
          │         │
         ICMP      ARP
          │         │
          └────┬────┘
               │
           Ethernet
               │
              TAP
               │
          Linux Kernel
```

TCP passive open時は、概念的には以下の流れになります。

```text
listener socket
      │
      ▼
  LISTEN PCB
      │
      │ SYN
      ▼
  child PCB
 SYN_RECEIVED
      │
      ▼
 ESTABLISHED
      │
      ▼
 listener backlog
      │
      ▼
 sock_accept()
      │
      ▼
 accepted socket
      │
      ▼
 worker pthread
```

---

## Build

### Requirements

- Linux
- GCC
- make
- pthread
- TAP interfaceを利用できる環境
- Python 3（benchmark client用）

### Build

```bash
make
```

debug logを無効化する場合:

```bash
make clean
make CFLAGS+=' -DDISABLE_DEBUG_LOG'
```

---

## Run

TAP interfaceなどのネットワーク設定を行った後、テスト用serverを起動します。

```bash
sudo ./test/test.exe
```

TCP Echo Serverは現在port 7を利用しています。

環境によってTAP interfaceやroute設定が異なるため、詳細なセットアップ手順は今後整理予定です。

---

## 関連記事

このリポジトリを大量接続できるように拡張する過程について、以下の記事にまとめています。

- [自作TCP/IPスタックを4095同時接続までスケールさせる過程で直したバグたち](ARTICLE_URL)

記事では、

- TCP PCBの解放漏れ
- socket lifecycle
- `accept()`とglobal lock
- PCB pool枯渇
- SYN retransmission
- benchmark
- 4095同時接続
- thread / RSS / VSZ / CPUの計測

について詳しく書いています。

---

## ベースにした教材

このリポジトリは以下の書籍をベースに実装しています。

[ゼロからのTCP/IPプロトコルスタック自作入門](https://www.amazon.co.jp/%E3%82%BC%E3%83%AD%E3%81%8B%E3%82%89%E3%81%AETCP-IP%E3%83%97%E3%83%AD%E3%83%88%E3%82%B3%E3%83%AB%E3%82%B9%E3%82%BF%E3%83%83%E3%82%AF%E8%87%AA%E4%BD%9C%E5%85%A5%E9%96%80-Compass-Books%E3%82%B7%E3%83%AA%E3%83%BC%E3%82%BA-%E5%B1%B1%E6%9C%AC%E9%9B%85%E4%B9%9F/dp/4839981248)

教材の実装を完成させたあと、複数connectionへの対応やリソース管理、負荷試験などを追加しています。

---

## OSS Contribution

拡張・検証の過程で、教材実装側にもいくつかの問題を確認し、修正をupstreamへ提出しています。

PRについては、merge後または公開可能になった段階でこちらに追加予定です。

---

## 今後やりたいこと

今後は主に以下を調べたいと考えています。

- 大量接続時にCPU使用率が高くなる内部threadの解析
- TCP timerの処理量調査
- TCP PCB lookupの高速化
- PCBの固定長配列管理の改善
- connectionごとのpthreadが実際にボトルネックになる条件の調査
- 必要であればepollのようなイベント駆動I/Oの実装
- benchmarkの拡充
- 長時間連続稼働テスト

最初からepollなどを導入するのではなく、実際に負荷を掛けてボトルネックを確認してから必要な変更を入れていく方針です。

---

## License

このリポジトリには教材および元実装をベースにしたコードが含まれています。

利用・再配布については、元リポジトリおよび本リポジトリのライセンスを確認してください。