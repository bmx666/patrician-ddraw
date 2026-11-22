// Prints one case of tests/sgl_sweep_oracle.cpp for the original library and the replacement, with the pixels that
// differ. Usage: node tests/sweep-case.mjs <oracle.exe> <game directory> <replacement ddraw_Dll.dll> <suite> <case>...
import { spawnSync } from 'child_process';
import { join, resolve } from 'path';

const [oracle, game, replacement, suite, ...cases] = process.argv.slice(2);
const run = (library, number) => spawnSync(resolve(oracle),
  [resolve(library), resolve(game), suite, number, number, '--dump'], { encoding: 'utf8', timeout: 60000 });
for (const number of cases) {
  const want = run(join(resolve(game), 'ddraw_Dll.dll'), number), got = run(replacement, number);
  const a = want.stdout.trim().split(' '), b = got.stdout.trim().split(' ');
  const pixels = a.filter(word => /^[0-9a-f]{8}$/.test(word));
  const head = a.slice(0, a.length - pixels.length).join(' ');
  const size = Math.round(Math.sqrt(pixels.length));
  console.log(`${head}${got.status ? `  [replacement exit ${got.status}: ${got.stderr.trim()}]` : ''}`);
  const offset = a.length - pixels.length;
  const rows = [];
  for (let index = 0; index < pixels.length; ++index) {
    if (a[offset + index] !== b[offset + index])
      rows.push(`(${index % size},${Math.floor(index / size)}) ${a[offset + index]}/${b[offset + index]}`);
  }
  console.log(`  ${rows.length} of ${pixels.length} pixels differ (original/replacement): ${rows.slice(0, 12).join(' ')}`);
}
