// SPDX-License-Identifier: GPL-3.0-only
// Simulator adapted from SLOOP's web/test_web.mjs.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import {pack7, unpack7} from './docs/fm1ota.js';
import {release} from './docs/release.js';
import {checkedImage, flashRelease} from './docs/installer.js';
const eq = (a,b) => a.length === b.length && a.every((v,i)=>v===b[i]);
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];

/* an FM-1 on WebMIDI: identity, then "device asks, host answers" reads of the image */
class FakeFM1 {
  constructor(image, { unplugAfter = Infinity, finalIdentity = "FM-1_900", name = "FM-1", identity = "FM-1_015", loaderName = "Felucca Update" } = {}) {
    this.image = image; this.unplugAfter = unplugAfter; this.served = 0; this.bad = 0;
    this.finalIdentity = finalIdentity; this.loaderName = loaderName;
    this.access = { inputs: new Map(), outputs: new Map() };
    this.boot(identity, name);
  }
  boot(identity, name) {
    this.identity = identity; this.waiting = null; this.queue = [];
    for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    const id = Math.random().toString(36).slice(2);
    this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { id: "o" + id, name, state: "connected", open: async () => {}, send: (d) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(d)), 1);
    } };
    this.access.inputs.set(this.input.id, this.input);
    this.access.outputs.set(this.output.id, this.output);
  }
  tx(bytes) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(bytes) }); }, 1); }
  rx(d) {
    if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(this.identity)];
      const body = [0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)];
      this.tx([0xF0, ...pack7(body), 0xF7]);
    } else if (eq(d, [0xf0,0x7d,0x46,0x4c,1,0xf7])) {
      this.tx([0xf0,0x7d,0x46,0x4c,1,...new TextEncoder().encode(this.version ?? release.version),0,0xf7]);
    } else if (eq(d, UPGRADE)) {
      this.queue = this.identity.startsWith("ota-")
        ? [...Array.from({ length: 6 }, (_, k) => [k * 512, 512]), [0xF0000000, 8]]
        : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next();
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1));
      const [addr, len] = this.waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
      if (!eq(got, want)) this.bad++;
      this.waiting = null;
      this.served++;
      if (this.served >= this.unplugAfter) { this.input.state = this.output.state = "disconnected"; return; }
      if (addr === 0xE0000000) setTimeout(() => this.boot("ota-FM-1_900", this.loaderName), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot(this.finalIdentity, "Felucca"), 300);
      else this.next();
    }
  }
  next() {
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...pack7(u), 0xF7]);
  }
}


const bytes = new Uint8Array(await readFile(new URL('./docs/fm1-liquid-metal.fwsc',import.meta.url)));
const image = await checkedImage(bytes, release);
test('bundled firmware is pinned and corruption is rejected', async () => {
  const changed = bytes.slice();
  changed[4000] ^= 1;
  await assert.rejects(checkedImage(changed, release), /校验失败/);
});
test('standard install, generic Windows loader, and version readback', async () => {
  const dev = new FakeFM1(image, {loaderName:'USB MIDI Device', name:'USB MIDI Device'});
  const steps = [];
  await flashRelease(dev.access, image, step => steps.push(step));
  assert.equal(dev.bad,0);
  assert.deepEqual(steps.filter(s=>!['verify','write'].includes(s)),['start','loader','reboot','done']);
});
test('supported loader resumes, and installed version is checked', async () => {
  const dev = new FakeFM1(image);
  dev.boot('ota-FM-1_900','USB MIDI Device');
  dev.version = 'FELUCCA SLOOP 2.8.5 WRONG';
  await assert.rejects(flashRelease(dev.access, image, ()=>{}), {code:'version'});
  assert.equal(dev.bad,0);
});
test('foreign loader and duplicate FM-1 units are never written', async () => {
  const foreign = new FakeFM1(image);
  foreign.boot('ota-FM-1_015','FM-1 Update');
  await assert.rejects(flashRelease(foreign.access, image,()=>{}), {code:'notfound'});
  assert.equal(foreign.served,0);
  const one = new FakeFM1(image,{name:'Felucca One'});
  const two = new FakeFM1(image,{name:'Felucca Two'});
  const access={inputs:new Map([...one.access.inputs,...two.access.inputs]),outputs:new Map([...one.access.outputs,...two.access.outputs])};
  await assert.rejects(flashRelease(access,image,()=>{}), {code:'busy'});
  assert.equal(one.served+two.served,0);
});
