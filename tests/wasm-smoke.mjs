import assert from "node:assert/strict";
import { readFile, writeFile } from "node:fs/promises";
import createConverter from "../dist/converter.mjs";

const [mlPath, dstPath, romPath, output] = process.argv.slice(2);
const [ml, dst, rom] = await Promise.all([mlPath, dstPath, romPath].map((path) => readFile(path)));
const header = rom.subarray(0, 64);
const code = Buffer.concat([header, ...[0x20, 0x30].map((at) => {
  const start = header.readUInt32LE(at);
  return rom.subarray(start, start + header.readUInt32LE(at + 12));
})]);

const module = await createConverter();
const inputs = [ml, dst, code].map((bytes) => {
  const pointer = module._malloc(bytes.length);
  assert(pointer);
  module.HEAPU8.set(bytes, pointer);
  return [pointer, bytes.length];
});
try {
  const run = () => module._convert_state(...inputs.flat(), rom.length, 0, 0);
  assert.equal(run(), 1, module.UTF8ToString(module._result_error()));
  const start = module._result_data();
  const result = module.HEAPU8.slice(start, start + module._result_size());
  assert.equal(new DataView(result.buffer).getUint32(16, true), dst.readUInt32LE(16));
  await writeFile(output, result);

  // Exercise a version-13 header with the same chunk layout as the upstream fixture.
  const [dstPointer] = inputs[1];
  new DataView(module.HEAPU8.buffer).setUint32(dstPointer + 16, 13, true);
  assert.equal(run(), 1, module.UTF8ToString(module._result_error()));
  const start13 = module._result_data();
  const result13 = module.HEAPU8.slice(start13, start13 + module._result_size());
  assert.equal(new DataView(result13.buffer).getUint32(16, true), 13);
  assert.deepEqual(result13.subarray(20), result.subarray(20));

  module.HEAPU8[inputs[2][0] + 64] ^= 1;
  assert.equal(run(), 0);
  assert.match(module.UTF8ToString(module._result_error()), /different ROM|ROM照合が一致しません/);
  module.HEAPU8[dstPointer] ^= 1;
  assert.equal(run(), 0);
  assert.match(module.UTF8ToString(module._result_error()), /ステートヘッダーがありません/);
} finally {
  for (const [pointer] of inputs) module._free(pointer);
  module._clear_result();
}
