# キーボード

レイアウトのコードが0x7Fぐらいまでなので最低でも0x8Fまで拡張する
0x87 (ろ / \ キー)
0x89 (¥ / | キー)
このへんのキーは絶対に必要

# USB Serial
DTR/RTS 関連のテスト
自動テストだと切断状態になりテストしにくい

ESP32を接続してリセットをするテストを追加
ただし、シリアルは特定のものだけ対象となる
まだテストがおかしい

# USB Audio

主な残作業はこのあたりです。

USB Audio IN の peer 実証

onAudioData() の受信経路はありますが、Arduino 側 USBAudioCard.write() で peer から実データを流す確認がまだ安定していません。
今 peer で確実に通っているのは Host → Device の Audio OUT です。
Feature Unit / Mixer / Volume / Mute 制御

USBスピーカーやオーディオIFは音量・ミュート・ゲイン制御が別リクエストになることがあります。
現状は生PCM送信とサンプルレート設定が中心です。
Clock Source / UAC2 対応

今の実装は主に UAC1 Type I 前提です。
UAC2 デバイスでは Clock Source / Clock Selector 周りが必要になる場合があります。
フォーマット選択の強化

現状は取得した Audio Streaming 情報を表示して、利用側が選ぶ形です。
「48kHz 16bit mono/stereo の OUT を自動選択」みたいな便利 API はまだ最低限です。
実機互換性テスト

USBスピーカー、USBマイク、USBオーディオIFでの手動確認。
特に alt setting、サンプルレート、最大パケットサイズ、同期方式で差が出やすいです。
Audio Input サンプルの実受信確認

Input サンプルはビルドと情報表示までは整えました。
実USBマイクで audio: ... bytes_per_sec=... が継続して出るか確認したいです。
ドキュメントの対応範囲明記

OUT は peer で送信確認済み。
IN は API あり、peer 実データは未確定。
UAC1中心、UAC2/Feature Unit は限定的、という注意書きをもう少し明確にしてよさそうです。