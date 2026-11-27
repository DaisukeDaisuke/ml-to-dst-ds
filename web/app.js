import createConverter from "./converter.mjs";

const form = document.querySelector("#convert");
const status = document.querySelector("#status");
const button = form.querySelector("button");
const maxInput = 64 * 1024 * 1024;
let converter;

async function readRomCode(rom) {
  const header = await rom.slice(0, 64).arrayBuffer();
  if (header.byteLength !== 64 || rom.size > 0xffffffff) {
    throw new Error("ROMのサイズが不正です。");
  }
  const view = new DataView(header);
  const ranges = [0x20, 0x30].map((at) => {
    const start = view.getUint32(at, true);
    const size = view.getUint32(at + 12, true);
    if (!size || start + size > rom.size) throw new Error("ROMのヘッダーが不正です。");
    return { start, size };
  });
  const codeSize = 64 + ranges[0].size + ranges[1].size;
  if (codeSize > maxInput) throw new Error("ROMのプログラム領域が大きすぎます。");
  const code = new Uint8Array(codeSize);
  code.set(new Uint8Array(header));
  let offset = 64;
  for (const { start, size } of ranges) {
    code.set(new Uint8Array(await rom.slice(start, start + size).arrayBuffer()), offset);
    offset += size;
  }
  return code;
}

async function readState(file) {
  if (file.size > maxInput) throw new Error("ステートは64 MiBまでです。");
  return new Uint8Array(await file.arrayBuffer());
}

form.addEventListener("submit", async (event) => {
  event.preventDefault();
  button.disabled = true;
  status.textContent = "変換中…";
  const [ml, dst, rom, bios] = [...form.querySelectorAll("input[type=file]")].map((input) => input.files[0]);
  try {
    if (!converter) converter = await createConverter();
    if (bios && bios.size !== 0x4000) throw new Error("ARM7 BIOSのサイズは16 KiBです。");
    const inputs = await Promise.all([
      readState(ml), readState(dst), readRomCode(rom),
      bios ? readState(bios) : new Uint8Array(),
    ]);
    const allocations = [];
    try {
      for (const input of inputs) {
        const ptr = converter._malloc(Math.max(input.length, 1));
        if (!ptr) throw new Error("変換に必要なメモリを確保できません。");
        allocations.push([ptr, input.length]);
        converter.HEAPU8.set(input, ptr);
      }
      if (!converter._convert_state(...allocations.slice(0, 3).flat(), rom.size, ...allocations[3])) {
        throw new Error(converter.UTF8ToString(converter._result_error()));
      }
      const start = converter._result_data();
      const bytes = converter.HEAPU8.slice(start, start + converter._result_size());
      const url = URL.createObjectURL(new Blob([bytes], { type: "application/octet-stream" }));
      const link = document.createElement("a");
      link.href = url;
      link.download = ml.name.replace(/\.[^.]+$/, "") + ".dst";
      link.click();
      setTimeout(() => URL.revokeObjectURL(url), 30_000);
      status.textContent = `${link.download} を作成しました。`;
    } finally {
      for (const [ptr] of allocations) converter._free(ptr);
      converter._clear_result();
    }
  } catch (error) {
    status.textContent = `変換に失敗しました。 ${error.message}`;
  } finally {
    button.disabled = false;
  }
});
