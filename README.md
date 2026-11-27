# melonDS → DeSmuME ステート変換（試験版）

melonDS の MELN 14.0 ステートと、同じDS ROMで書き出した DeSmuME のステートから、DeSmuME の `.dst` を生成します。DeSmuMEの内部形式12と、変換対象のチャンク配置が形式12互換の内部形式13を扱います。出力は土台の内部形式と圧縮方式を引き継ぎます。`.sav` などの拡張子でも、ステートのヘッダーがあれば入力できます。ブラウザで処理が完結します。

1. 土台には、同じROMから **File → Save State As** で作った DeSmuME ステート、またはゲーム内のセーブデータ `.sav` を使えます。ステートを使う場合は移行元と同じ場面に近いほど適しています。
2. 変換ページで melonDS ステート、土台のファイル、`.nds` を選びます。melonDSで純正ARM7 BIOSを使い、復号済みROMからステートを作った場合は同じ `bios7.bin` も選んでください。BIOSもブラウザ内で処理します。
3. ダウンロードした `.dst` は **File → Load State From** で開きます。

ARM9/ARM7のレジスタと命令パイプライン、メインRAM、共有WRAM、ARM7 WRAM、TCM、MPU設定、割り込み、IPC FIFO、VRAM、パレット、OAM、2D表示レジスタの一部を移します。3Dコマンドキュー、音声、DMA、タイマー、スケジューラ、カートリッジ内部状態（バックアップメモリを含む）、通信は土台にしたステートの値が残ります。`.sav` の場合は先にWebAssembly版DeSmuMEでROMを起動し、ゲーム内セーブを読み込んだ状態を土台にします。DSiモードと異なるROMを拒否します。市販ゲームでの復帰は未検証です。

## GitHub Pages

ZIPを展開したフォルダはGitリポジトリです。`git submodule update --init --recursive` で両エミュレータの固定版を取得できます。自分のGitHubリポジトリに `main` をpushし、Settings → Pages → Source で **GitHub Actions** を選びます。設定前に走った場合は Actions の **Build and deploy Pages** を再実行してください。

GitHub Actions はサブモジュールを取得して上流両方をビルドし、WebAssemblyのテストを実行してからPagesへデプロイします。テストでは自作のDS ROMを使い、melonDSで保存したARM9の実行位置とRAMの値がDeSmuMEに復元され、変換後にROMのコードが続きから実行されることを確認します。公開物は変換ページとWebAssemblyです。ROMやステートは公開物に含まれません。

DeSmuMEは公式ナイトリー用の `master` を `upstream/desmume` で固定しています。ブラウザ用ビルドも同じコアを使い、`upstream/desmume-webassembly` からブラウザ向けの互換ヘッダーを取り込みます。現行の公式コアが出力する内部形式は12です。形式13のテストは、形式12互換のチャンク配置を使ったテストデータで、変換時に形式番号が維持されることを確認します。

ZIPにはビルド済みの `dist/` もあります。ローカルで使う場合は展開先で `py -m http.server --directory dist 8000`（Windows）または `python3 -m http.server --directory dist 8000` を実行し、`http://localhost:8000/` を開きます。再ビルドはEmscripten 4.0.20を有効にして `bash build-web.sh` を実行してください。
