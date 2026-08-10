# EspUsbHostCcidFelicaIdm

> English: [README.md](README.md)

Sony RC-S300 を使って、System Code を指定した FeliCa の IDm を読み取ります。

## `ccidPowerOn()` + Get UID では駄目な理由

非接触の CCID リーダーは自分でフィールドをポーリングし、応答したものを slot の
カードとしてホストに見せます。CCID にはポーリング対象という概念が無く（仕様に
「何を探すか」を指示するメッセージが存在しません）、ホストはリーダーが自前の
ワイルドカードポーリングで捕まえたものを受け取るだけで、それ以外を要求できません。

システムが1つしか無いカードならこれで十分です。しかしスマートフォンでは困ります。
Suica をウォレットに入れた iPhone に対して、リーダー自前のポーリングが見つけたものと、
その直後に同じ端末に対してこの example が見つけたものを並べます。

| 経路 | 応答 |
|------|------|
| リーダー自前のポーリング（`ccidPowerOn()` + Get UID） | historical bytes を持たない ATR と、先頭が `0x08` の4バイト NFCID1 —— ISO 14443 Type A のランダム ID。ウォレットが Type A で応答しており、Suica は登場しません |
| この example の FeliCa Polling、System Code `0xffff` | 8バイトの IDm、応答元 System Code は `0x0003` —— Suica |
| この example の FeliCa Polling、System Code `0x0003` | 同じ IDm |

つまり交通系カードに届く決め手は「FeliCa として Polling すること」そのもので、これは
CCID ホストがリーダーに要求できない動作です。System Code はその上で、得られた IDm が
どのシステムのものかを確定させる役割を持ちます。`0xffff` はターゲットが先に出した
システムを拾って応答でそれを名乗るだけですが、具体的なコードを指定すればそのシステムに
届くか何も返らないかのどちらかになります。

この example では、リーダーから RF フィールドを奪い取り、両方を送ります。

```
System Code 0xffff  ワイルドカード。ターゲットが先に出したシステム
System Code 0x0003  JR 系の交通系 IC カードが載っているシステム
```

## ハードウェア

- ESP32-S3（または Arduino-ESP32 USB Host が対応する他のボード）
- **Sony RC-S300**（`FeliCa Port/PaSoRi 4.0`、VID 0x054c PID 0x0dc8）。コマンド体系は
  CCID ではなく Sony 独自です —— [移植性](#移植性) を参照
- FeliCa ターゲット: 交通系 IC カード（Suica、PASMO など）、その他の FeliCa カード、
  または交通系カードをウォレットに入れたスマートフォン

## 動作

- リーダーの CCID interface を open し（`ccidOpen()`）、RC-S300 でなければ続行しません
- transparent session を開始し、フィールドを FeliCa に切り替え、RF を off → on して
  ターゲットを clean に立ち上げ直します
- System Code `0xffff` と `0x0003` で FeliCa Polling を送り、IDm・PMm・応答した
  System Code を表示します
- RF を off にして session を閉じ、リーダーが自前のポーリングに戻れる状態にします

```
connected: address=2 vid=054c pid=0dc8 product="FeliCa Port/PaSoRi 4.0"
RC-S300 ready: address=2
wildcard SC=ffff IDm=0114b5f2c3d4e5f6  PMm=00f0000000010b4b  answering system code=0003
transit  SC=0003 IDm=0114b5f2c3d4e5f6  PMm=00f0000000010b4b  answering system code=0003
```

## 構成

ハードウェア無しで検証できるものは全て検証できるように、3層に分けています。

| ファイル | 内容 |
|----------|------|
| [`FelicaProtocol.hpp`](FelicaProtocol.hpp) | FeliCa Polling のフレーム組立て（JIS X 6319-4）。純粋なバイト整形 |
| [`Rcs300Protocol.hpp`](Rcs300Protocol.hpp) | RC-S300 の transparent session 疑似 APDU と応答オブジェクト。純粋なバイト整形 |
| [`Rcs300Device.hpp`](Rcs300Device.hpp) | USB 側。各疑似 APDU を `ccidTransfer()` で `PC_to_RDR_XfrBlock` として送ります |

protocol 側の2つのヘッダは Arduino / USB 非依存なので、
[`tests/unit/felica_idm`](../../../tests/unit/felica_idm/) が g++ でそのまま
コンパイルして全フレームをバイト単位で検証します。実機側は
[`tests/manual/ccid_felica`](../../../tests/manual/ccid_felica/) です。

### transparent session

**ライブラリ本体には何も追加していません。** transparent session はリーダー固有の
プロトコルなので、ライブラリが既に持っている生の CCID メッセージ API の上に、この
example の中で実装しています。これはライブラリの CCID
[設計](../../../docs/ccid-api-spec.ja.md) が最初から決めていた切り分けです。

以下は [`tests/probe/rcs300_felica`](../../../tests/probe/rcs300_felica/) で実機の
RC-S300 に対して実測した結果です。

- `PC_to_RDR_Escape` は**まったく非対応**で、どのペイロードも失敗します。疑似 APDU は
  `PC_to_RDR_XfrBlock`（= `ccidTransfer()`）経由で送ります
- 疑似 APDU は `FF 50 00 <P2> <Lc> <データオブジェクト> 00` の形で、P2 がオブジェクトの
  グループを選びます: `00` manage session、`01` transparent exchange、`02` switch
  protocol。標準 PC/SC の `FF C2` 形式でも応答します
- データオブジェクトは PC/SC part 3 が transparent session 用に定義しているものです。

  | オブジェクト | 意味 |
  |--------------|------|
  | `81 00` | transparent session 開始 |
  | `82 00` | transparent session 終了 |
  | `83 00` | RF off |
  | `84 00` | RF on |
  | `8F 02 03 00` | フィールドを FeliCa に切り替え。応答オブジェクトなしで受理される。逆順の `8F 02 00 03` も受理され（一度は `8F 01 08` を返した）が、その後の Polling は一切応答されないため別のものを選んでいる |
  | `5F 46 04 <µs LE>` | ターゲットの応答を待つ時間。exchange では**必須** |
  | `95 <len> <frame>` | 送信するフレーム |

- 応答は必ず status オブジェクト `C0 03 <result> <SW1 SW2>` で始まり、続いて応答
  オブジェクト、最後に疑似 APDU 自身の status word が付きます。

  | result / SW | 意味 |
  |-------------|------|
  | `00` / `9000` | 受理 |
  | `01` / `6301` | 拒否（session 外での RF on など） |
  | `01` / `6401` | リクエスト不正（timeout オブジェクト無しの exchange など） |
  | `01` / `6700` | オブジェクト長不正（`8F 02 <p p>` の代わりに `8F 01 <p>` など） |
  | `01` / `6A81` | 値が未対応（リーダーが持たないモードを指定した場合など） |
  | `02` / `6401` | 送信したがフィールドから応答が無かった |

- 成功した exchange は status オブジェクトの後に `92 01 00`・`96 02 00 00`、そして
  ターゲットのフレームを `97` オブジェクトで返します。

  ```
  C0 03 00 9000 | 92 01 00 | 96 02 0000 | 97 14 <20バイトの Polling 応答> | 9000
  ```

- **exchange の前に RF を off → on する必要があります。** リーダーは自前でポーリングを
  していたので、その状態のまま残ったフィールドに Polling を投げても応答が返りません
  （同じ瞬間にリーダー自前の経路では IDm が読めている Suica で実測）。`RF off` の後に
  `RF on` すれば解決します。
- transmit オブジェクトに載せるフレームは FeliCa の長さバイトを含めます。省くと
  ターゲットは応答しません。

### FeliCa Polling

`LEN 00 <SC1> <SC2> <RC> <TSN>`（LEN は自身を含む長さ）で、応答は
`LEN 01 <IDm 8バイト> <PMm 8バイト> [<リクエストデータ 2バイト>]` です。Request Code
`0x01` を指定するとターゲットが応答元の System Code を返すため、ワイルドカードで
撃った場合でも「どのシステムに届いたか」が応答自体から分かります。

## 移植性

確認したのは Sony RC-S300 のみで、意図的にそれに限定しています（`open()` が VID/PID を
確認し、それ以外は拒否します）。`FelicaProtocol.hpp` はリーダー非依存で任意の System
Code を扱えますが、`Rcs300Protocol.hpp` の transparent session は Sony の方言です。同じ
PC/SC オブジェクトを実装している別のリーダーでも、device 層は別途必要になります。

## 参考

- USB Device Class Specification for Integrated Circuit Card Devices (CCID)
- PC/SC Workgroup specification part 3（transparent session とそのデータオブジェクト）
- JIS X 6319-4（FeliCa）の Polling コマンドと応答
- [`docs/ccid-api-spec.ja.md`](../../../docs/ccid-api-spec.ja.md) —— ライブラリの CCID
  設計。リーダー固有プロトコルを本体に入れない理由もここにあります

FeliCa・PaSoRi・Suica は各権利者の商標です。この example は Sony および交通事業者とは
無関係で、公開されているプロトコルをリーダーに対して話すだけのものです。
