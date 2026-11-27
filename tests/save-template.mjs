import assert from "node:assert/strict";
import { readFile, writeFile } from "node:fs/promises";
import { isRawSave, makeTemplate } from "../dist/dst-template.mjs";

const [romPath, output, savePath] = process.argv.slice(2);
const rom = new Blob([await readFile(romPath)]);
const save = savePath ? await readFile(savePath) : Buffer.alloc(64 * 1024, 0xff);
if (!savePath) save.write("ML2DST battery save");
assert(isRawSave({ name: "test.sav" }, save));

const template = Buffer.from(await makeTemplate(rom, save));
assert.equal(template.subarray(0, 16).toString(), "DeSmuME SState\0\0");
assert.equal(template.readUInt32LE(16), 12);
assert.equal(template.readUInt32LE(28), 0xffffffff);
let backup;
for (let offset = 32; template.readUInt32LE(offset) !== 0xffffffff;) {
  const kind = template.readUInt32LE(offset);
  const size = template.readUInt32LE(offset + 4);
  if (kind === 61) backup = template.subarray(offset + 8, offset + 8 + size);
  offset += 8 + size;
}
assert(backup?.includes(save), "DeSmuME's state must contain the imported battery save");
await writeFile(output, template);
