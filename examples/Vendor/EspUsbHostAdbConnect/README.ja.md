# EspUsbHostAdbConnect

> English: [README.md](README.md)

Androidデバイスと USB ADB で通信するための骨子です。ADBインターフェースを探し、汎用のvendor bulk APIでclaimし、ADBの `A_CNXN` ハンドシェイクを送り、デバイスの最初の応答を表示します。

**これは出発点であり、完全なADBクライアントではありません。** 最初の応答までで止まります。実用クライアントは、shell/syncストリームを `A_OPEN` する前に `A_AUTH`（RSAトークン署名 / デバイス側の「常に許可」ダイアログ）を実装する必要があります。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- **USBデバッグを有効化した**Androidデバイス（開発者向けオプション）

## ADBはvendor-specific bulkの上に乗っている

ADBインターフェースは以下で識別されるvendor-specificインターフェースです：

| フィールド | 値 |
|-----------|-----|
| `bInterfaceClass` | `0xff` |
| `bInterfaceSubClass` | `0x42` |
| `bInterfaceProtocol` | `0x01` |

`vendorOpen()` はvendorインターフェースを class `0xff` とインターフェース*番号*だけで選択し、subclass/protocolでは絞り込みません。そのためこのスケッチは `getInterfaces()` でインターフェースを列挙し、`ff/42/01` の組を自分で見つけて、その番号を `vendorOpen(address, number)` に渡します。

## 動作内容

1. 接続時にADBインターフェース番号を探してclaim
2. `A_CNXN`（`host::` バナー）を送信
3. bulk INを読み、ADBメッセージを再構成して逐次報告：
   - `CNXN` 応答 → デバイスは既に認証済み
   - `AUTH` 応答 → デバイスがRSA署名トークンを要求（本スケッチでは未実装）

## シリアルコマンド

| コマンド | 動作 |
|----------|------|
| `r` | `A_CNXN` を再送 |

## ADBメッセージ形式（24バイトヘッダ）

| オフセット | フィールド | 備考 |
|-----------|-----------|------|
| 0 | command | 4バイトタグ、例 `CNXN` |
| 4 | arg0 | version / id |
| 8 | arg1 | maxdata / id |
| 12 | data_length | ペイロード長 |
| 16 | data_checksum | **単純なバイト総和**、CRC32ではない |
| 20 | magic | `command ^ 0xffffffff` |

## シリアル出力例（認証済みデバイス）

```
EspUsbHost ADB connect skeleton start
connected: device: address=1 portId=0x01 vid=18d1 pid=4ee7 class=0x00(Device) speed=high product="Pixel"
ADB interface found: number=1
CNXN send: ok
recv CNXN arg0=0x01000001 arg1=0x00100000 len=... banner=device::ro.product.name=...
-> device accepted connection (already authorized).
```

このホストを未認証のデバイスでは、代わりに `AUTH` 応答が返ります：

```
recv AUTH arg0=0x00000001 arg1=0x00000000 len=20
-> device requests AUTH (RSA token). Signing is not implemented in this skeleton.
```

## 実用クライアントに向けた次のステップ

- `A_AUTH` の実装：20バイトのトークンをRSA-2048鍵で署名（type `2` で返信）、または公開鍵を送って（type `3`）「USBデバッグを許可しますか？」ダイアログを出す
- デバイスが `CNXN` を返した後、`A_OPEN`（例 `"shell:ls\0"`）でストリームを開き、`A_WRTE` / `A_OKAY` をやり取りする
- デバイスごとの受信バッファは512バイト：広告する `maxdata` を小さく保ち、bulk INを速やかに吸い出すこと
