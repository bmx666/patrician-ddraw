// Compares the replacement with the original library over every suite of tests/sgl_sweep_oracle.cpp. A library that
// ends the process or hangs on a case is restarted after that case; the case is reported with the library's message.
// Usage: node tests/run-sweep.mjs <sgl_sweep_oracle.exe> <game directory with the original ddraw_Dll.dll>
//        <replacement ddraw_Dll.dll> [suite ...] [--names <file with archive image names>]
import { spawnSync } from 'child_process';
import { readFileSync } from 'fs';
import { dirname, join, resolve } from 'path';
import { fileURLToPath } from 'url';

const args = process.argv.slice(2);
const namesAt = args.indexOf('--names');
const namesFile = namesAt >= 0 ? resolve(args.splice(namesAt, 2)[1]) : null;
const [oracle, game, replacement, ...requested] = args;
if (!oracle || !game || !replacement) {
  console.error('Usage: node tests/run-sweep.mjs <oracle.exe> <game directory> <replacement ddraw_Dll.dll> [suite ...] [--names file]');
  process.exit(2);
}
const original = join(resolve(game), 'ddraw_Dll.dll');
const all = ['blend', 'stretch', 'clip', 'fill', 'fillrow', 'mask', 'blt', 'bltrc', 'clear', 'nodest', 'lock', 'text', 'assets'];
const suites = requested.length ? requested : all.filter(suite => suite !== 'assets' || namesFile);
const extra = namesFile ? ['--names', namesFile] : [];
const chunk = 2000;
// Differences that are known and intended, each with the reason (tests/sweep-accepted.json).
const accepted = JSON.parse(readFileSync(join(dirname(fileURLToPath(import.meta.url)), 'sweep-accepted.json'), 'utf8'))
  .map(entry => ({ ...entry, pattern: new RegExp(entry.case) }));

const run = (library, suite, first, last, more = []) => spawnSync(resolve(oracle),
  [resolve(library), resolve(game), suite, String(first), String(last), ...extra, ...more],
  { encoding: 'utf8', maxBuffer: 256 << 20, timeout: 300000 });

// Runs cases first..last, restarting after a case that ends the process. Returns case number -> line or failure.
function collect(library, suite, count) {
  const results = new Map();
  for (let next = 0; next < count;) {
    const last = Math.min(count - 1, next + chunk - 1);
    const result = run(library, suite, next, last);
    const lines = result.stdout.split(/\r?\n/);
    let reached = next;
    for (const line of lines) {
      const number = Number(line.split(' ', 1)[0]);
      if (!line || Number.isNaN(number)) continue;
      results.set(number, line);
      reached = number;
    }
    if (result.status === 0 && !result.error) { next = last + 1; continue; }
    // The last line started is the case that ended the process.
    const reason = result.error ? `hang (${result.error.code})` : `exit ${result.status}: ${result.stderr.trim().split(/\r?\n/).pop()}`;
    const started = results.get(reached) ?? `${reached}`;
    results.set(reached, `${started} ENDED ${reason}`);
    next = reached + 1;
  }
  return results;
}

let failures = 0;
for (const suite of suites) {
  const counted = run(original, suite, 'count', 0);
  const count = Number(counted.stdout.trim());
  if (!count) { console.log(`${suite}: cannot count cases (${counted.stderr.trim()})`); failures++; continue; }
  const expected = collect(original, suite, count);
  const actual = collect(replacement, suite, count);
  const ended = [], different = [], undefinedInOriginal = [], known = new Map();
  for (let number = 0; number < count; ++number) {
    const want = expected.get(number), got = actual.get(number);
    if (got === want) continue;
    // A case that faults or ends the original library has no defined result to match.
    if (want?.includes(' ENDED ') || want?.endsWith(' fault')) {
      undefinedInOriginal.push(got);
      continue;
    }
    if (got?.includes(' ENDED ')) { ended.push(got); continue; }
    const entry = accepted.find(item => item.suite === suite && item.pattern.test(want));
    if (entry) known.set(entry.reason, (known.get(entry.reason) ?? 0) + 1);
    else different.push([want, got]);
  }
  const originalEnded = [...expected.values()].filter(line => line.includes(' ENDED ')).length;
  const originalFaults = [...expected.values()].filter(line => line.endsWith(' fault')).length;
  console.log(`${suite}: ${count} cases, ${ended.length} end the replacement, ${different.length} differ` +
    ` (original: ${originalEnded} ended, ${originalFaults} faulted; not compared)`);
  for (const [reason, cases] of known) console.log(`  accepted ${cases} known differences: ${reason}`);
  for (const line of ended.slice(0, 200)) console.log(`  ended    ${line}`);
  for (const [want, got] of different.slice(0, 200)) {
    console.log(`  original ${want}`);
    console.log(`  replaced ${got}`);
  }
  if (ended.length > 200 || different.length > 200) console.log('  ...');
  failures += ended.length + different.length;
}
process.exit(failures ? 1 : 0);
