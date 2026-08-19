# esp-spi-master

ESP-IDF の SPI Master ドライバーを使用し、ESP32 を SPI マスターとして動作させるサンプルです。

対向プログラムの [`esp-spi-slave`](https://github.com/amjs456/esp-spi-slave) と組み合わせることで、2台の ESP32 間で UART から入力した文字列を全二重 SPI 通信できます。通常の MOSI、MISO、SCLK、CS に加え、通信開始を通知する2本の IRQ（ハンドシェイク）信号を使用します。

## 主な機能

- ESP32 の `SPI2_HOST` を SPI マスターとして使用
- SPI mode 0、1 MHz、64バイト固定長の全二重転送
- DMA 対応の送受信バッファを使用
- UART0 の標準入力から送信文字列を受け付け
- 受信した文字列を UART0 の標準出力へ表示
- 2本の GPIO IRQ により、マスターとスレーブのどちらからでも転送を開始可能
- GPIO番号、バッファ数、バッファサイズを `menuconfig` で設定可能

## 必要なもの

- ESP32 開発ボード 2台
- ESP-IDF が利用できる開発環境
- USBケーブル 2本
- ESP32間を接続するジャンパーワイヤー
- 対向プログラムの [`esp-spi-slave`](https://github.com/amjs456/esp-spi-slave)

このプロジェクトは ESP32 をターゲットとして作成されています。信号レベルは 3.3 V とし、2台の GND を必ず共通にしてください。

## 配線

デフォルト設定では、次のように接続します。

| 信号 | マスター側 GPIO | スレーブ側 GPIO | 方向 |
| --- | ---: | ---: | --- |
| MOSI | 13 | 12 | Master → Slave |
| MISO | 12 | 13 | Slave → Master |
| SCLK | 14 | 14 | Master → Slave |
| CS | 15 | 15 | Master → Slave |
| Master → Slave IRQ | 4 | 4 | Master → Slave |
| Slave → Master IRQ | 2 | 2 | Slave → Master |
| GND | GND | GND | 共通GND |

IRQ信号は通常 High で、通信要求時に Low へ変化します。入力側は立ち下がりエッジを検出します。内蔵プルアップ／プルダウンは使用せず、各ESP32の出力によってレベルを決めます。

> [!CAUTION]
> GPIO12 は ESP32 のストラッピングピンです。リセット中に接続先が不適切なレベルで駆動すると、起動に影響する場合があります。書き込みや起動に失敗する場合は、起動時に相手側が信号を駆動しないようにするか、両プロジェクトのGPIO設定と配線を変更してください。

## 通信仕様

| 項目 | 設定 |
| --- | --- |
| SPIホスト | `SPI2_HOST` |
| SPIモード | mode 0（CPOL=0、CPHA=0） |
| クロック | 1 MHz |
| 転送方式 | 全二重 |
| 1回の転送長 | 64バイト（512ビット） |
| ビット順 | ESP-IDF のデフォルト（MSB first） |
| CS | Active Low |

一方が UART から文字列を受け取ると、IRQ線を Low にして相手へ転送要求を通知します。相手に送信データがない場合は、先頭が `\0` のダミーデータを送信キューへ格納します。双方がSPIトランザクションを準備した後、マスターがクロックを出力し、64バイトを同時に送受信します。転送後、受信バッファは文字列として UART へ出力されます。

## セットアップ

### 1. マスター側

ESP-IDF のターミナルで本リポジトリへ移動し、ターゲットを設定します。

```sh
idf.py set-target esp32
```

必要に応じて設定を変更します。

```sh
idf.py menuconfig
```

`ESP-SPI-MASTER settings` メニューで、SPIおよびIRQに使用するGPIOなどを設定できます。

続いてビルドし、マスター用ESP32へ書き込みます。`PORT` は使用するシリアルポートに置き換えてください。

```sh
idf.py build
idf.py -p PORT flash monitor
```

### 2. スレーブ側

別のディレクトリへ対向リポジトリを取得し、もう1台のESP32へ書き込みます。

```sh
git clone https://github.com/amjs456/esp-spi-slave.git
cd esp-spi-slave
idf.py set-target esp32
idf.py build
idf.py -p PORT flash monitor
```

両方のシリアルモニターを同時に開く場合は、それぞれ異なるポートを指定してください。

## 使い方

1. 配線を確認して両方のESP32を起動します。
2. マスター側またはスレーブ側のシリアルモニターで文字列を入力し、Enterを押します。
3. 入力した文字列が SPI で送信され、相手側のシリアルモニターへ表示されます。
4. 双方が送信データを持つ場合は、同じ全二重トランザクションで文字列を交換します。

入力できる文字列は最大63文字です。`fgets()` が64バイトの入力領域へ最大63文字と終端の `\0` を格納し、末尾の改行コードは送信前に除去します。

## 設定項目

`main/KConfig.projbuild` では次の項目を定義しています。

| 設定 | デフォルト | 内容 |
| --- | ---: | --- |
| `CONFIG_SPI_MOSI_GPIO` | 13 | マスターがMOSIを出力するGPIO |
| `CONFIG_SPI_MISO_GPIO` | 12 | マスターがMISOを入力するGPIO |
| `CONFIG_SPI_SCLK_GPIO` | 14 | SPIクロック出力GPIO |
| `CONFIG_SPI_CS_GPIO` | 15 | チップセレクト出力GPIO |
| `CONFIG_MASTER_TO_SLAVE_IRQ_GPIO` | 4 | スレーブへの通信要求出力GPIO |
| `CONFIG_SLAVE_TO_MASTER_IRQ_GPIO` | 2 | スレーブからの通信要求入力GPIO |
| `CONFIG_QUEUE_SIZE` | 3 | DMAトランザクションバッファ数 |
| `CONFIG_BUF_SIZE` | 64 | 1トランザクションのバッファサイズ |

GPIOを変更する場合は、対向するスレーブ側の設定と実際の配線も一致させてください。

> [!IMPORTANT]
> 現在の実装には64バイト固定のタスク用バッファとキュー項目があるため、`CONFIG_BUF_SIZE` はデフォルトの64のまま使用してください。対向する `esp-spi-slave` も同じ転送長にする必要があります。

## プログラム構成

- `uart_vfs_init()` — UART0 を標準入出力として初期化
- `spi_init()` — SPI2バスを初期化し、マスターデバイスを追加
- `dma_buf_init()` — DMA送受信バッファとSPIトランザクションを初期化
- `gpio_irq_init()` — 2本のIRQ GPIOと割り込みハンドラーを初期化
- `stdin_fgets_task()` — UART入力を受け取り、IRQでスレーブと同期して送信キューへ格納
- `create_dummy_task()` — スレーブから要求された場合にダミーデータを送信キューへ格納
- `transaction_task()` — SPI転送、IRQ制御、受信データの表示を実行

## 実装上の注意

- バイナリプロトコルではなく、NULL終端された文字列の交換を想定しています。
- 受信データを `printf("%s")` で表示するため、受信する64バイト内に `\0` が必要です。
- IRQの待機とSPI転送完了待ちにはタイムアウトがありません。対向ボードが未起動、未接続、または応答不能の場合、タスクが待機し続けることがあります。
- SPI APIとGPIO APIの戻り値を一部確認していないため、配線や設定に問題があると通信が開始されない場合があります。
- 両プロジェクトでSPIモード、転送長、GPIO割り当てを揃えてください。
- IRQ線は両側とも起動時に High へ設定されます。誤った配線によるGPIO出力同士の衝突に注意してください。

## ディレクトリ構成

```text
.
├── CMakeLists.txt
├── README.md
└── main
    ├── CMakeLists.txt
    ├── KConfig.projbuild
    └── esp-spi.c
```
