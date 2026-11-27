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
  const run = () => module._convert_state(...inputs.flat(), rom.length);
  assert.equal(run(), 1, module.UTF8ToString(module._result_error()));
  const start = module._result_data();
  const result = module.HEAPU8.slice(start, start + module._result_size());
  await writeFile(output, result);
  module.HEAPU8[inputs[2][0] + 64] ^= 1;
  assert.equal(run(), 0);
  assert.match(module.UTF8ToString(module._result_error()), /different ROM/);
} finally {
  for (const [pointer] of inputs) module._free(pointer);
  module._clear_result();
}
