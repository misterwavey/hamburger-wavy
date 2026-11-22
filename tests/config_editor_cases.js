// SPDX-License-Identifier: GPL-3.0-only
// Emits test cases from the config editor's own logic, as JSON on stdout:
// [{text, expect}] where expect is what the editor believes the firmware will
// load. tests/test_release.py feeds each text to the firmware's parser.
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'docs/config-editor.html'), 'utf8');
const logic = html.split('/*LOGIC-BEGIN*/')[1].split('/*LOGIC-END*/')[0];
const HW = new Function(logic + '\nreturn HW;')();

let seed = 12345;
const random = (n) => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return Math.floor(seed / 0x80000000 * n); };
const pick = (list) => list[random(list.length)];
const tuple = (rc, s) => [s.playlist || '-', rc, s.play, s.cv, s.delay, s.drive, s.crush, s.filter, s.resonance, s.shape,
                          s.tremolo, s.ring, s.rate, s.wah, s.phaser, s.flanger];
const REJECTED = ['-', -2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
const cases = [];
const fail = (message) => { console.error(message); process.exit(1); };

function randomState() {
  const s = HW.defaults();
  s.play = random(12); s.cv = random(6); s.delay = random(3);
  for (const key of ['drive', 'crush', 'cutoff', 'resonance', 'wah', 'rate'])
    s[key] = random(3) ? random(10) : 0;
  s.shape = pick(['low', 'high', 'band']);
  s.sweep = pick(['none', 'phaser', 'flanger']); s.sweepAmt = 1 + random(9);
  s.mod = pick(['none', 'tremolo', 'ring']); s.modAmt = 1 + random(9);
  s.comment = pick(['Hamburger Wavy configuration', '', 'x', 'a'.repeat(80), 'café — test', '  spaced  ']);
  s.sampleRate = pick([44100, 48000, 88200, 96000]); s.pitch = random(32) - 24;
  s.playlist = pick(['', '', 'drums', 'A~1', 'Live2026', 'toolongname', 'bad name', 'config', 'x']);
  return s;
}

// 1. Generated configs: valid, within limits, and understood as intended.
for (let i = 0; i < 1500; i++) {
  const state = i ? randomState() : HW.defaults();
  const text = HW.generate(state);
  const lines = text.split('\n');
  if (lines.pop() !== '') fail('generated text must end with a newline');
  if (lines.length > 16) fail('generated more lines than the firmware reads:\n' + text);
  for (const l of lines) if (Buffer.byteLength(l) > 63) fail('generated line too long: ' + l);
  if (!/^[\x20-\x7e\n]*$/.test(text)) fail('generated text is not plain ASCII:\n' + text);
  const parsed = HW.parse(text);
  if (!parsed.ok) fail('editor cannot load its own output:\n' + text + parsed.error);
  if (parsed.notes.length) fail('editor output draws its own warnings:\n' + text + parsed.notes.join('\n'));
  const want = HW.settings(state);
  if (JSON.stringify(tuple(1, parsed.settings)) !== JSON.stringify(tuple(1, want)))
    fail('generate and parse disagree:\n' + text);
  const again = HW.generate(HW.stateFrom(parsed.settings, lines[0].replace(/^#\s?/, '')));
  if (again !== text) fail('load then generate changes the file:\n' + text + '---\n' + again);
  const analysis = HW.analyse(state);
  if (analysis.stages.filter((x) => x.runs).length > analysis.budget) fail('more stages run than allowed');
  cases.push({ text, expect: tuple(1, want) });
}

// 2. Hand-written and damaged configs: the editor's loader must agree with
// the firmware about what is accepted and what each line does.
const junk = ['', '#', '# note', 'Drive 12', 'Drive', 'Drive  7', 'drive\t3', ' Crush 4', 'Filter', 'Filter 5 ',
  'Lowpass 2', 'HIGHPASS 0', 'bandpass 9', 'Resonance 10', 'Wah 3', 'Phaser 4', 'Flanger 5', 'Ring 6',
  'Tremolo 2', 'Rate 8', 'Rate x', 'Short', 'medium', 'LONG', 'Loop', 'Quantize', 'Colour', 'nonsense',
  'Playlist drums', 'playlist Drums.TXT', 'PLAYLIST a~1 .txt', 'Playlist', 'Playlist 123456789', 'Playlist a.wav',
  'Playlist a b', 'Playlists a', 'Playlist .txt', 'Playlist  x  ', 'Playlist a.txt.txt', 'Playlist a.tx',
  'x'.repeat(63), 'x'.repeat(64), 'Drive 5' + ' '.repeat(57), 'Drive 5' + ' '.repeat(56), 'Tape', 'Select File'];
const firsts = ['# c', '#', ' # c', 'c', '', '﻿# c', '﻿#' + 'c'.repeat(59), '﻿#' + 'c'.repeat(60),
  '#' + 'c'.repeat(62), '#' + 'c'.repeat(63), '# c  '];
const plays = ['Loop', 'loop', ' LOOPING', 'Random', 'TG Loop', 'tg sequence', 'CV Flip', 'CV  Flip', 'Chop ', 'Flipp', '', 'Tape'];
const cvs = ['Quantize', 'quantized', 'Free', 'Select File', 'Color', 'colour', 'Filter', 'RATE', 'Rate 5', '', 'Pitch'];
for (let i = 0; i < 4000; i++) {
  const lines = [pick(firsts), pick(plays), pick(cvs)];
  const extra = random(18);
  for (let k = 0; k < extra; k++) lines.push(pick(junk));
  lines.length = Math.max(0, lines.length - (random(6) ? 0 : random(4)));
  const eol = pick(['\n', '\r\n']);
  const text = lines.join(eol) + (random(4) ? eol : '');
  if (Buffer.byteLength(text) > 500) continue;
  const parsed = HW.parse(text);
  cases.push({ text, expect: parsed.ok ? tuple(1, parsed.settings) : REJECTED });
}
for (const name of fs.readdirSync(path.join(root, 'examples/config')).sort()) {
  const text = fs.readFileSync(path.join(root, 'examples/config', name), 'utf8');
  const parsed = HW.parse(text);
  if (!parsed.ok) fail('example rejected by the editor: ' + name + ': ' + parsed.error);
  cases.push({ text, expect: tuple(1, parsed.settings) });
}

// 3. Playlists: the editor's reading of a list against the firmware's, on a
// card that holds A.wav, B.wav and C.wav.
const card = ['A.WAV', 'B.WAV', 'C.WAV', 'My Long Sample Name 01.wav', 'kick drum.wav'];
const clusters = { 'A.WAV': 3, 'B.WAV': 35, 'C.WAV': 67, 'MY LONG SAMPLE NAME 01.WAV': 3, 'KICK DRUM.WAV': 35 };
const entries = ['A.wav', 'b.WAV', 'C.wav', 'c.wav', 'missing.wav', 'A.wave', 'A.wa', '.wav', 'A B.wav', 'A-1.wav',
  ' A.wav', 'A.wav ', 'A.wav\t', '', 'ABCDEFGHI.wav', 'A', 'a~1.wav', 'A.WAV.wav', 'kick drum.wav', 'KICK DRUM.WAV',
  'My Long Sample Name 01.wav', 'my long sample name 01.wav', 'My Long Sample Name 0.wav', 'My Long Sample Name 011.wav',
  'My Long Sample.wav', 'café.wav', 'x'.repeat(59) + '.wav', 'x'.repeat(60) + '.wav'];
for (let i = 0; i < 2500; i++) {
  const lines = [];
  for (let k = random(9); k > 0; k--) lines.push(pick(entries));
  const eol = pick(['\n', '\r\n']);
  const text = lines.join(eol) + (random(4) ? eol : '');
  if (Buffer.byteLength(text) > 500) continue;
  const result = HW.playlistParse(text, card);
  cases.push({ kind: '%', text, expect: ['P', result.used.length, ...result.used.map((n) => String(clusters[n.toUpperCase()]))] });
  const checked = HW.playlistCheck(lines.map((l) => l.replace(/[ \t]+$/, '')), card);
  const reached = checked.filter((e) => e.status === 'ok').length;
  if (lines.every((l) => l === l.replace(/[ \t]+$/, '')) && reached !== result.used.length)
    fail('playlistCheck and playlistParse disagree:\n' + text);
}
const round = ['A.wav', 'C.wav', 'A.wav'];
if (JSON.stringify(HW.playlistParse(HW.playlistGenerate(round), card).used) !== JSON.stringify(round))
  fail('playlist round trip');
if (HW.playlistParse('A.wav\n', null).used.length !== 1) fail('playlist without a card listing');

// 4. Names for the Playlist line.
for (const [name, ok] of [['drums', true], ['A~1', true], ['12345678', true], ['123456789', false], ['', false],
  ['a b', false], ['config', false], ['PlayList', false], ['oneprof', false], ['a.txt', false]])
  if (HW.validListName(name) !== ok) fail('validListName ' + name);

// 5. WAV headers, against the rules in firmware/wave.c.
function wav({ tag = 1, channels = 1, rate = 48000, bits = 16, junk = 0, frames = 100, extensible = false }) {
  const align = bits / 8 * channels, data = frames * align, fmtSize = extensible ? 40 : 16;
  const size = 12 + (junk ? 8 + junk : 0) + 8 + fmtSize + 8 + data;
  const b = Buffer.alloc(size);
  let o = 0;
  b.write('RIFF', o); b.writeUInt32LE(size - 8, o + 4); b.write('WAVE', o + 8); o += 12;
  if (junk) { b.write('JUNK', o); b.writeUInt32LE(junk, o + 4); o += 8 + junk; }
  b.write('fmt ', o); b.writeUInt32LE(fmtSize, o + 4); o += 8;
  b.writeUInt16LE(extensible ? 0xfffe : tag, o); b.writeUInt16LE(channels, o + 2); b.writeUInt32LE(rate, o + 4);
  b.writeUInt32LE(rate * align, o + 8); b.writeUInt16LE(align, o + 12); b.writeUInt16LE(bits, o + 14);
  if (extensible) {
    b.writeUInt16LE(22, o + 16); b.writeUInt16LE(bits, o + 18);
    Buffer.from([1, 0, 0, 0, 0, 0, 16, 0, 128, 0, 0, 170, 0, 56, 155, 113]).copy(b, o + 24);
  }
  o += fmtSize;
  b.write('data', o); b.writeUInt32LE(data, o + 4);
  return new Uint8Array(b);
}
const inspect = (options) => { const b = wav(options); return HW.wavInspect(b, b.length); };
for (const [options, ok] of [[{}, true], [{ bits: 24, rate: 96000 }, true], [{ rate: 8000 }, true], [{ junk: 26 }, true],
  [{ extensible: true, bits: 24 }, true], [{ channels: 2 }, false], [{ bits: 8 }, false], [{ bits: 32 }, false],
  [{ rate: 192000 }, false], [{ rate: 4000 }, false], [{ tag: 3 }, false]]) {
  const result = inspect(options);
  if (result.ok !== ok) fail('wavInspect ' + JSON.stringify(options) + ' gave ' + JSON.stringify(result));
}
const good = inspect({ bits: 24, rate: 44100, frames: 44100 });
if (good.rate !== 44100 || good.bits !== 24 || Math.abs(good.seconds - 1) > 1e-9) fail('wavInspect details');
if (HW.wavInspect(new Uint8Array(40), 40).ok !== false) fail('wavInspect on a non-WAV');
const far = wav({ junk: 4000 });
if (HW.wavInspect(far.slice(0, 512), far.length).ok !== null) fail('wavInspect should say when it cannot see the header');

// 6. Playlist notes in the analysis.
const base = HW.defaults();
const noteText = (state, context) => HW.analyse(state, context).notes.map((n) => n.text).join('\n');
if (!/stops early/.test(noteText(base, { lists: { PLAYLIST: ['A.wav', 'nope.wav', 'B.wav'] }, cardNames: card }))) fail('early stop note');
if (!/nothing will play/.test(noteText(base, { lists: { PLAYLIST: ['nope.wav'] }, cardNames: card }))) fail('empty list note');
if (HW.analyse(Object.assign({}, base, { playlist: 'drums' }), { lists: { DRUMS: ['C.wav'], PLAYLIST: ['A.wav', 'B.wav'] }, cardNames: card }).count !== 1) fail('named list count');
if (HW.analyse(Object.assign({}, base, { playlist: 'drums' }), { lists: { PLAYLIST: ['A.wav', 'B.wav'] }, cardNames: card }).source !== 'PLAYLIST') fail('fallback source');
if (HW.analyse(base, { lists: {}, cardNames: card }).count !== 5) fail('card order count');

if (HW.buildOf('ONE firmware build 54\n   ') !== 54 || HW.buildOf('nothing') !== null) fail('buildOf');
if (!/update the firmware/.test(noteText(base, { lists: {}, cardNames: card, build: 48 }))) fail('old build note');
if (/update the firmware/.test(noteText(base, { lists: {}, cardNames: card, build: HW.BUILD }))) fail('current build note');
if (/update the firmware/.test(noteText(base, { lists: {}, cardNames: card, build: null }))) fail('unknown build note');

// 7. The guide renderer: structure, links and escaping.
const names = { docs: ['filter', 'user-guide'], examples: ['wah.txt'] };
const md = (text) => HW.mdRender(text, names).html;
const expectHtml = (text, want) => { if (md(text) !== want) fail('mdRender(' + JSON.stringify(text) + ') gave\n' + md(text) + '\nwanted\n' + want); };
expectHtml('# Using `it` <b>', '<h1 id="using-it-b">Using <code>it</code> &lt;b&gt;</h1>');
expectHtml('One\ntwo **bold** & <i>.', '<p>One two <strong>bold</strong> &amp; &lt;i&gt;.</p>');
expectHtml('- a\n- b `x<y`\n  more\n\n1. one\n2. two', '<ul><li>a</li><li>b <code>x&lt;y</code> more</li></ul>\n<ol><li>one</li><li>two</li></ol>');
expectHtml('```text\n# not a heading\n<tag> **x**\n```', '<pre><code># not a heading\n&lt;tag&gt; **x**</code></pre>');
expectHtml('| A | B |\n| --- | ---: |\n| `x` | 2 |', '<div class="tablewrap"><table><thead><tr><th>A</th><th style="text-align:right">B</th></tr></thead><tbody><tr><td><code>x</code></td><td style="text-align:right">2</td></tr></tbody></table></div>');
expectHtml('[F](filter.md#limits) [h](#effects) [e](../examples/config/wah.txt) [x](../LICENSE) [w](https://example.org/a?b=1&c=2)',
  '<p><a href="#" data-doc="filter" data-anchor="limits">F</a> <a href="#" data-anchor="effects">h</a> ' +
  '<a href="#" data-example="wah.txt" title="Load this example into the editor">e</a> x ' +
  '<a href="https://example.org/a?b=1&amp;c=2" target="_blank" rel="noopener noreferrer">w</a></p>');
expectHtml('[bad](javascript:alert(1)) [q]("onmouseover=x)', '<p>bad) q</p>');
if (HW.mdRender('# Title here\n\ntext', names).title !== 'Title here') fail('mdRender title');
// Every shipped document renders, links only to things that exist, and
// leaves no raw Markdown link behind.
const bundle = JSON.parse(html.split('<script type="application/json" id="docs">')[1].split('</script><!--docs-end-->')[0]);
const all = { docs: Object.keys(bundle.docs), examples: Object.keys(bundle.examples) };
if (!all.docs.includes('user-guide') || all.examples.length < 20) fail('embedded guide bundle is incomplete');
for (const key of all.docs) {
  const rendered = HW.mdRender(bundle.docs[key], all);
  if (!rendered.title) fail('no title in ' + key);
  if (/\]\(/.test(rendered.html.replace(/<code>[\s\S]*?<\/code>/g, ''))) fail('unrendered link in ' + key);
  for (const m of rendered.html.matchAll(/data-doc="([^"]+)"(?: data-anchor="([^"]+)")?/g)) {
    if (!bundle.docs[m[1]]) fail(key + ' links to a missing page ' + m[1]);
    if (m[2] && !HW.mdRender(bundle.docs[m[1]], all).html.includes(' id="' + m[2] + '"')) fail(key + ' links to a missing heading ' + m[1] + '#' + m[2]);
  }
  for (const m of rendered.html.matchAll(/<a href="#" data-anchor="([^"]+)"/g))
    if (!rendered.html.includes(' id="' + m[1] + '"')) fail(key + ' links to a missing heading #' + m[1]);
}
for (const name of all.examples) if (!HW.parse(bundle.examples[name]).ok) fail('embedded example is not loadable: ' + name);

// 8. Firmware helpers.
const crypto = require('crypto');
for (const size of [0, 1, 55, 56, 63, 64, 65, 119, 120, 1000, 70000]) {
  const data = Buffer.alloc(size);
  for (let i = 0; i < size; i++) data[i] = (i * 131 + size) & 255;
  if (HW.sha256(new Uint8Array(data)) !== crypto.createHash('sha256').update(data).digest('hex')) fail('sha256 at size ' + size);
}
const record = (address, type, payload) => {
  const b = [payload.length, address >> 8, address & 255, type, ...payload];
  b.push((256 - b.reduce((x, y) => (x + y) & 255, 0)) & 255);
  return ':' + b.map((x) => x.toString(16).padStart(2, '0').toUpperCase()).join('');
};
const imageLines = [record(0, 4, [0x1d, 0])];
for (let i = 0; i < 100; i++) imageLines.push(record(i * 16, 0, Array.from({ length: 16 }, (_, k) => (i + k) & 255)));
imageLines.push(record(0, 1, []));
const asBytes = (text) => new Uint8Array(Buffer.from(text));
const goodImage = imageLines.join('\r\n') + '\r\n';
if (!HW.hexCheck(asBytes(goodImage)).ok || HW.hexCheck(asBytes(goodImage)).bytes !== 1600) fail('hexCheck on a good image');
if (HW.hexCheck(asBytes(imageLines.slice(0, -1).join('\n'))).ok) fail('hexCheck must want an end record');
if (HW.hexCheck(asBytes(goodImage.replace(':10001000', ':10001100'))).ok) fail('hexCheck must verify record checksums');
if (HW.hexCheck(asBytes('# Hamburger Wavy configuration\nLoop\nQuantize\n'.repeat(40))).ok) fail('hexCheck on a text file');
if (HW.hexCheck(asBytes(goodImage + record(0, 0, [1]) + '\n')).ok) fail('hexCheck on data after the end');
const built = path.join(root, 'build/extended/image.hex');
if (fs.existsSync(built) && !HW.hexCheck(new Uint8Array(fs.readFileSync(built))).ok) fail('hexCheck rejects the real image');
const sum = 'a'.repeat(64);
if (!HW.firmwareInfo({ build: 54, file: '54/image.hex', sha256: sum })) fail('firmwareInfo on a good listing');
for (const bad of [null, {}, { build: '54', file: 'x', sha256: sum }, { build: 54, file: '../x', sha256: sum },
  { build: 54, file: '/x', sha256: sum }, { build: 54, file: 'https://e.org/x', sha256: sum }, { build: 54, file: 'x', sha256: 'abc' }])
  if (HW.firmwareInfo(bad)) fail('firmwareInfo accepted ' + JSON.stringify(bad));

// 9. The firmware status shown for a card.
const [img54] = Object.keys(HW.KNOWN_IMAGES).filter((k) => HW.KNOWN_IMAGES[k].build === 54 && !HW.KNOWN_IMAGES[k].kind);
const offer54 = { sha256: img54, build: 54, label: 'build 54' };
const offer55 = { sha256: 'd'.repeat(64), build: 55, label: 'build 55' };
const localFile = { sha256: 'b'.repeat(64), build: null, label: 'image.hex' };
const fwCase = (args, state, canInstall, pattern) => {
  const got = HW.firmwareState(...args);
  if (got.state !== state || got.canInstall !== canInstall || !pattern.test(got.message))
    fail('firmwareState ' + JSON.stringify(args) + ' gave ' + JSON.stringify(got));
};
fwCase([null, null, null, false, false], 'unknown', false, /Open the card/);
fwCase([53, null, null, true, true], 'unknown', false, /no image\.hex.*last ran build 53/);
fwCase([53, img54, null, true, true], 'pending', false, /build 54\. It is not installed yet.*last ran build 53/); // no network check needed
fwCase([54, img54, null, true, true], 'unknown', false, /build 54\. The module is running it/);
fwCase([null, img54, null, true, true], 'pending', false, /has not run with this card yet/);
fwCase([53, 'c'.repeat(64), null, true, true], 'unknown', false, /not a released build.*last ran build 53/);
fwCase([null, null, offer54, false, false], 'nocard', false, /Build 54 is ready/);
fwCase([53, null, offer54, true, true], 'available', true, /Build 54 is available/);
fwCase([53, 'c'.repeat(64), offer54, true, true], 'available', true, /Build 54 is available/);
fwCase([53, null, offer54, false, true], 'available', false, /cannot write/);
fwCase([53, img54, offer54, true, true], 'pending', false, /not installed yet/);
fwCase([54, img54, offer54, true, true], 'current', false, /running it/);
fwCase([54, null, offer54, true, true], 'current', false, /running build 54, the latest/);
fwCase([55, null, offer54, true, true], 'newer', false, /latest release is build 54/);
fwCase([54, img54, offer55, true, true], 'available', true, /Build 55 is available/);
fwCase([null, null, offer54, true, true], 'available', true, /Build 54 is available/);
fwCase([54, null, localFile, true, true], 'available', true, /image\.hex is available/);
fwCase([54, 'b'.repeat(64), localFile, true, true], 'pending', false, /image\.hex is on the card/);
// Nothing may claim the module's last-run build as the card's firmware.
for (const args of [[53, img54, null, true, true], [53, img54, offer54, true, true]])
  if (/^The module last ran|Firmware build 53/.test(HW.firmwareState(...args).message)) fail('firmware message leads with the old build');
if (HW.imageName(img54).text !== '54' || HW.imageName('e'.repeat(64)).text !== '?' || HW.imageName(null).build !== null) fail('imageName');
if (!/not installed yet/.test(noteText(base, { lists: {}, cardNames: card, build: 53, imageBuild: 54 }))) fail('pending install note');
if (/update the firmware/.test(noteText(base, { lists: {}, cardNames: card, build: 53, imageBuild: 54 }))) fail('pending install must not say update');
// 10. Plain-language summary.
if (HW.WAYS.length !== HW.PLAY.length || HW.KNOBS.length !== HW.CV.length) fail('WAYS/KNOBS must cover every mode');
const d0 = HW.describe(base, { lists: {}, cardNames: card });
if (d0.sounds !== 'Every sound on the card (5)' || d0.way !== 'Loop' || d0.knob !== 'Pitch, in semitones' || d0.effects !== 'None') fail('describe defaults ' + JSON.stringify(d0));
const d1 = HW.describe(Object.assign({}, base, { play: 4, cv: 5, playlist: 'drums', shape: 'band', cutoff: 5, wah: 6, sweep: 'flanger', mod: 'ring', drive: 3 }),
  { lists: { DRUMS: ['A.wav', 'C.wav'] }, cardNames: card });
if (d1.sounds !== '2 sounds from drums.txt' || d1.way !== 'Next sound each time' || d1.knob !== 'Effect speed' ||
    d1.effects !== 'Filter with wah, flanger, ring modulation, grit') fail('describe ' + JSON.stringify(d1));
if (HW.describe(Object.assign({}, base, { play: 10 }), { lists: {}, cardNames: null }).knob !== 'Used by this way of playing') fail('describe tape knob');
for (let i = 0; i < 300; i++) { const d = HW.describe(randomState(), { lists: {}, cardNames: card }); if (!d.short || /undefined|null/.test(d.short)) fail('describe short: ' + d.short); }
const fxState = Object.assign({}, base, { cutoff: 5, sweep: 'phaser', mod: 'tremolo', drive: 4 });
const rw = HW.rateWarnings(fxState, [{ name: 'A.wav', rate: 48000 }, { name: 'B.wav', rate: 96000 }, { name: 'C.wav', rate: 96000 }, { name: 'D.wav', rate: 44100 }]);
if (rw.length !== 1 || !/2 sounds \(B\.wav, C\.wav\) are 96 kHz files/.test(rw[0]) || !/only the filter/.test(rw[0]) || !/the swirl, the pulse and the grit will be skipped/.test(rw[0])) fail('rateWarnings ' + JSON.stringify(rw));
if (HW.rateWarnings(fxState, [{ name: 'A.wav', rate: 48000 }]).length) fail('rateWarnings at 48 kHz');
if (HW.rateWarnings(base, [{ name: 'B.wav', rate: 96000 }]).length) fail('rateWarnings with no effects');
if (!/B\.wav is a 96 kHz file/.test(HW.rateWarnings(Object.assign({}, base, { sweep: 'flanger', drive: 2 }), [{ name: 'B.wav', rate: 96000 }])[0])) fail('rateWarnings single file');

process.stdout.write(JSON.stringify(cases));
