# melonDS → DeSmuME ステート変換（試験版）

melonDS の MELN 14.0 ステートと、同じDS ROMで書き出した DeSmuME の形式12ステートから、DeSmuME の `.dst` を生成します。`.ml` や `.ml1` など、拡張子に関係なく中身を判定します。ブラウザで処理が完結します。

1. DeSmuME で同じROMを開き、**File → Save State As** で土台にする `.dst` を作ります。できるだけ移行元と同じ場面を使ってください。
2. 変換ページで melonDS ステート、DeSmuME ステート、`.nds` の3ファイルを選びます。melonDSで純正ARM7 BIOSを使い、復号済みROMからステートを作った場合は同じ `bios7.bin` も選んでください。BIOSもブラウザ内で処理します。
3. ダウンロードした `.dst` は **File → Load State From** で開きます。

ARM9/ARM7のレジスタと命令パイプライン、メインRAM、共有WRAM、ARM7 WRAM、TCM、割り込み、IPC FIFO、VRAM、パレット、OAM、2D表示レジスタの一部を移します。3Dコマンドキュー、音声、DMA、タイマー、スケジューラ、カートリッジ内部状態（バックアップメモリを含む）、通信は土台にしたステートの値が残ります。DSiモードと異なるROMを拒否します。MPU有効時は土台と保護領域・アクセス権が一致する必要があります。市販ゲームでの復帰は未検証です。

## GitHub Pages

ZIPを展開したフォルダはGitリポジトリです。`git submodule update --init --recursive` で両エミュレータの固定版を取得できます。自分のGitHubリポジトリに `main` をpushし、Settings → Pages → Source で **GitHub Actions** を選びます。設定前に走った場合は Actions の **Build and deploy Pages** を再実行してください。

GitHub Actions はサブモジュールを取得して上流両方をビルドし、WebAssemblyのテストを実行してからPagesへデプロイします。テストでは自作のDS ROMを使い、melonDSで保存したARM9の実行位置とRAMの値がDeSmuMEに復元され、変換後にROMのコードが続きから実行されることを確認します。公開物は変換ページとWebAssemblyです。ROMやステートは公開物に含まれません。

ZIPにはビルド済みの `dist/` もあります。ローカルで使う場合は展開先で `py -m http.server --directory dist 8000`（Windows）または `python3 -m http.server --directory dist 8000` を実行し、`http://localhost:8000/` を開きます。再ビルドはEmscripten 4.0.20を有効にして `bash build-web.sh` を実行してください。
