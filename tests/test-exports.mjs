import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {dirname, join, resolve} from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = dirname(dirname(fileURLToPath(import.meta.url)));
const build = resolve(process.argv[2] || join(root, 'build', 'Release'));
const library = join(build, 'ddraw_Dll.dll');
const probe = join(build, 'export_probe.exe');
const manifest = JSON.parse(readFileSync(join(root, 'research/sgl-original/dispatch.json'), 'utf8'));
const names = manifest.exports.map(entry => entry.name);
const lookup = spawnSync(probe, [library, 'lookup', ...names], {encoding: 'utf8'});
assert.equal(lookup.status, 0, `Export lookup failed: ${lookup.stderr}`);
const archive = spawnSync(probe, [library, 'archive', 'sgl_SetArchive'], {encoding: 'utf8'});
assert.equal(archive.status, 0, `Archive state failed: ${archive.stderr}`);
const texture = spawnSync(probe, [library, 'texture_info', 'sgl_GetTextureInfo'], {encoding: 'utf8'});
assert.equal(texture.status, 0, `Texture information failed: ${texture.stderr}`);
const blend = spawnSync(probe, [library, 'blend_mode', 'sgl_SetBlendMode'], {encoding: 'utf8'});
assert.equal(blend.status, 0, `Blend state failed: ${blend.stderr}`);
const solidRect = spawnSync(probe, [library, 'fill_solid_rect', 'sgl_FillSolidRect'], {encoding: 'utf8'});
assert.equal(solidRect.status, 0, `Solid rectangle fill failed: ${solidRect.stderr}`);
const clipRect = spawnSync(probe, [library, 'clip_rect', 'sgl_SetClipRect'], {encoding: 'utf8'});
assert.equal(clipRect.status, 0, `Clip rectangle state failed: ${clipRect.stderr}`);
const missingDependencies = spawnSync(probe, [library, 'init_without_deps', 'unused'], {encoding: 'utf8'});
assert.equal(missingDependencies.status, 0, `Missing dependencies returned ${missingDependencies.status}`);
for (const entry of manifest.exports.filter(item => item.classification === 'no_op')) {
    const mode = entry.instructions === 'ret' ? 'ret' : 'zero';
    const result = spawnSync(probe, [library, mode, entry.name], {encoding: 'utf8'});
    assert.equal(result.status, 0, `No-op contract mismatch for ${entry.name}`);
}
for (const name of ['sgl_GetProjectionData']) {
    const result = spawnSync(probe, [library, 'invoke', name], {encoding: 'utf8'});
    assert.equal(result.status, 127, `${name} returned ${result.status}`);
    assert.equal(result.stderr, `Unimplemented SGL export: ${name}\n`);
}
for (const [mode, name] of [['image_flags_without_archiver', 'sgl_LoadMemoryTexture archiver'], ['image_high_flags_without_archiver', 'sgl_LoadMemoryTexture archiver'], ['unsupported_lock', 'sgl_LockTexture flags'],
    ['unsupported_draw', 'sgl_DrawBitmapRect source not selected']]) {
    const result = spawnSync(probe, [library, mode, 'unused'], {encoding: 'utf8'});
    assert.equal(result.status, 127, `${mode} returned ${result.status}`);
    assert.equal(result.stderr, `Unimplemented SGL export: ${name}\n`);
}
const oversize = spawnSync(probe, [library, 'oversize_create', 'unused'], {encoding: 'utf8'});
assert.equal(oversize.status, 0, `Oversize texture returned ${oversize.status}`);
console.log('All 180 exports resolve; archive, blend, solid-rectangle and clip state, nine no-op contracts and explicit unimplemented failures match.');
