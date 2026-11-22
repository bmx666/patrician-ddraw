// Compares sgl_DrawBitmapRect for every source format, color flags, blend byte and modulation color combination
// between the original library and the replacement (tests/blend_matrix_oracle.cpp). The replacement ends the process
// on an unsupported combination, so it is restarted from the next case and the case is reported as unsupported.
// Usage: node tests/run-blend-matrix.mjs <oracle.exe> <game directory with the original ddraw_Dll.dll> <replacement ddraw_Dll.dll>
import { spawnSync } from 'child_process';
import { join, resolve } from 'path';

const [oracle, game, replacement] = process.argv.slice(2).map(value => resolve(value));
if (!oracle || !game || !replacement) {
  console.error('Usage: node tests/run-blend-matrix.mjs <oracle.exe> <game directory> <replacement ddraw_Dll.dll>');
  process.exit(2);
}
const original = join(game, 'ddraw_Dll.dll');
const parse = text => new Map(text.split(/\r?\n/).filter(Boolean).map(line => [Number(line.split(' ', 1)[0]), line]));
const run = (library, first, extra = []) =>
  spawnSync(oracle, [library, game, String(first), ...extra], { encoding: 'utf8', maxBuffer: 64 << 20 });

const reference = run(original, 0, ['--original']);
if (reference.status !== 0) throw new Error(`original library run failed: ${reference.status} ${reference.stderr}`);
const expected = parse(reference.stdout);
const actual = new Map();
const unsupported = [];
for (let next = 0; next < expected.size;) {
  const result = run(replacement, next);
  for (const [number, line] of parse(result.stdout)) { actual.set(number, line); next = number + 1; }
  if (result.status === 0) break;
  unsupported.push(`${expected.get(next)?.split(' ').slice(0, 5).join(' ')}: ${result.stderr.trim()}`);
  next += 1;
}
const different = [...expected.keys()].filter(number => actual.has(number) && actual.get(number) !== expected.get(number));
console.log(`cases: ${expected.size}, unsupported: ${unsupported.length}, different: ${different.length}`);
for (const line of unsupported) console.log(`unsupported ${line}`);
for (const number of different) {
  const want = expected.get(number).split(' ');
  const got = actual.get(number).split(' ');
  const pixels = [];
  for (let index = 5; index < want.length && pixels.length < 4; ++index)
    if (want[index] !== got[index]) pixels.push(`#${index - 5} ${want[index]}/${got[index]}`);
  console.log(`different ${want.slice(0, 5).join(' ')}: ${pixels.join(' ')}`);
}
process.exit(unsupported.length || different.length ? 1 : 0);
