// SPDX-License-Identifier: GPL-3.0-only
import {packed, colors} from './preview-art.js';

const size = 240;
const frames = Array.from({length: 4}, (_, frame) => {
  const mesh = new Uint8Array(256);
  for (let i = 0, p = 0; i < 192; i += 3) {
    const j = frame * 192 + i;
    const word = packed[j] | packed[j + 1] << 8 | packed[j + 2] << 16;
    for (let shift = 0; shift < 24; shift += 6) mesh[p++] = (word >> shift) & 63;
  }
  return mesh;
});

const coord = n => Math.max(0, Math.min(size - 1, n));
function sample(mesh, x, y) {
  const i = (y & ~15) + (x >> 4), fx = x & 15, fy = y & 15;
  const a = mesh[i] * (16 - fx) + mesh[i + 1] * fx;
  const b = mesh[i + 16] * (16 - fx) + mesh[i + 17] * fx;
  return (a * (16 - fy) + b * fy) >> 8;
}

export function renderWallpaper(pixels, elapsed, time, touches = []) {
  const phase = Math.floor((elapsed % 102400) * .07);
  const ix = (phase >> 8) & 3, mix = phase & 255;
  const base = frames[ix].map((value, i) => (value * (256 - mix) + frames[(ix + 1) & 3][i] * mix) >> 6);
  const pulses = touches.filter(t => time - t.at >= 0 && time - t.at < 2880).map(t => {
    const age = time - t.at;
    const radius = t.radius + Math.min(age, 2240) / 32;
    const curve = age < 512 ? 30 + age * 3 / 16 : (2880 - age) * 126 / 2368;
    return {...t, square: radius * radius, strength: curve * 2.4};
  });
  const displaced = pulses.length ? base.map((value, i) => {
    const x = (i & 15) * 16, y = (i >> 4) * 16;
    let sx = x, sy = y;
    for (const pulse of pulses) {
      const dx = x - pulse.x, dy = y - pulse.y;
      const d = 1 - (dx * dx + dy * dy) / pulse.square;
      if (d <= 0) continue;
      const push = Math.min(255, d * d * pulse.strength) / 256;
      sx -= Math.floor(dx * push); sy -= Math.floor(dy * push);
    }
    return sx === x && sy === y ? value : sample(base, coord(sx), coord(sy));
  }) : base;
  for (let y = 0, i = 0; y < size; y++) {
    for (let x = 0; x < size; x++, i += 4) {
      const color = colors[sample(displaced, x, y) >> 2];
      pixels[i] = color[0]; pixels[i + 1] = color[1]; pixels[i + 2] = color[2]; pixels[i + 3] = 255;
    }
  }
}

export function newTouch(previous, now, rng = Math.random) {
  for (let i = 0; i < 20; i++) {
    const x = 12 + Math.floor(rng() * 216), y = 12 + Math.floor(rng() * 216);
    if (!previous || (x - previous.x) ** 2 + (y - previous.y) ** 2 >= 72 ** 2)
      return {x, y, radius: 82 + Math.floor(rng() * 39), at: now};
  }
  return {x: previous.x < 120 ? 227 : 12, y: previous.y < 120 ? 227 : 12, radius: 100, at: now};
}

function mountPreview() {
  const root = document.querySelector('.theme-preview');
  if (!root) return;
  const canvas = root.querySelector('canvas');
  const ctx = canvas.getContext('2d');
  const toggle = root.querySelector('.preview-toggle');
  if (!ctx) {
    root.querySelector('.preview-keys').hidden = true;
    toggle.hidden = true;
    return;
  }
  const image = ctx.createImageData(size, size);
  let paused = matchMedia('(prefers-reduced-motion: reduce)').matches;
  let since = 0, time = 3000, lastDraw = -Infinity, request = 0, previous;
  let touches = [];

  function draw(now) {
    renderWallpaper(image.data, time, now, touches);
    ctx.putImageData(image, 0, 0);
  }
  function frame(now) {
    request = 0;
    if (since && !paused) time += Math.min(now - since, 90);
    since = now;
    touches = touches.filter(t => now - t.at < 2880);
    if (now - lastDraw >= 1000 / 30 || (paused && !touches.length)) {
      draw(now);
      lastDraw = now;
    }
    start();
  }
  function start() {
    if (!request && !document.hidden && (!paused || touches.length)) request = requestAnimationFrame(frame);
  }
  function syncToggle() {
    toggle.textContent = paused ? '播放背景' : '暂停背景';
    toggle.setAttribute('aria-pressed', String(paused));
  }
  toggle.addEventListener('click', () => {
    paused = !paused;
    since = 0;
    syncToggle();
    start();
  });
  root.querySelector('.preview-keys').addEventListener('click', event => {
    if (!event.target.closest('button[data-note]')) return;
    const now = performance.now();
    previous = newTouch(previous, now);
    touches = [...touches.slice(-7), previous];
    draw(now);
    start();
  });
  document.addEventListener('visibilitychange', () => {
    since = 0;
    if (document.hidden) {
      cancelAnimationFrame(request);
      request = 0;
    } else start();
  });
  draw(performance.now());
  canvas.hidden = false;
  syncToggle();
  start();
}

if (typeof document !== 'undefined') mountPreview();
