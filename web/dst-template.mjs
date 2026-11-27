export function isRawSave(file, bytes) {
  if (new TextDecoder().decode(bytes.subarray(0, 14)) === "DeSmuME SState") return false;
  return /\.sav$/i.test(file.name) && bytes.length >= 512
    && bytes.length <= 64 * 1024 * 1024 && (bytes.length & (bytes.length - 1)) === 0;
}

export async function makeTemplate(rom, save) {
  if (rom.size > 512 * 1024 * 1024) throw new Error("ROMは512 MiBまでです。");
  const { default: createDeSmuME } = await import("./desmume-template.mjs");
  const emulator = await createDeSmuME();
  emulator.FS.writeFile("/game.nds", new Uint8Array(await rom.arrayBuffer()), { canOwn: true });
  emulator.FS.writeFile("/import.sav", save);
  const errors = [
    "", "DeSmuMEを初期化できませんでした。", "DeSmuMEでROMを読み込めませんでした。",
    "DeSmuMEでゲーム内セーブを読み込めませんでした。", "変換用ステートを書き出せませんでした。",
  ];
  const status = emulator._make_template();
  if (status) throw new Error(errors[status] ?? errors[4]);
  return emulator.FS.readFile("/template.dst");
}
