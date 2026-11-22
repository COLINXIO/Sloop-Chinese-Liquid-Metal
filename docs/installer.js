// SPDX-License-Identifier: GPL-3.0-only
import {productOf, logicalImage, sha256hex} from './fm1pkg.js';
import {Updater, FM1_PORT} from './fm1ota.js';
import {release} from './release.js';

const ERROR_MESSAGES = {
  busy: '检测到多台可用设备。请只连接要刷的那一台 FM-1。',
  notfound: '没有找到可更新的 FM-1。请检查数据线、设备电源和其他 MIDI 程序。',
  noloader: '没有找到对应的更新模式。请查看恢复指南，再重试。',
  lost: 'USB 连接已中断。请查看恢复指南后重试。',
  stopped: '设备没有完成更新。请查看恢复指南后重试。',
  noreturn: '写入后没有检测到设备重启。请按恢复指南检查。',
  mismatch: '重启后读到的设备版本与安装包不一致。',
  version: '设备已重新连接，但无法确认应用版本。请依恢复指南检查。'
};

function failure(code) {
  return Object.assign(new Error(code), {code});
}

export async function checkedImage(bytes, expected) {
  if (bytes.length !== expected.size || await sha256hex(bytes) !== expected.sha256 ||
      productOf(bytes) !== expected.product) throw new Error('固件校验失败。请刷新页面后重试。');
  return logicalImage(bytes);
}

export class ReleaseUpdater extends Updater {
  async find(filter, deep = false) {
    const matches = id => (id.model === release.model || id.text === `ota-${release.product}`) &&
      (!filter || filter(id));
    const inputs = [...this.access.inputs.values()].filter(port => port.state !== 'disconnected');
    const outputs = [...this.access.outputs.values()];
    const named = port => port.id === this.normalInputId || FM1_PORT.test(port.name || '');
    const candidates = inputs.filter(named);
    let device;
    for (const port of candidates) {
      const result = await this.probe(port, outputs, matches);
      if (!result) continue;
      if (device) {
        device.link.close(); result.link.close();
        throw failure('busy');
      }
      device = result;
    }
    if (device || !deep) return device;
    for (const port of inputs) {
      if (named(port)) continue;
      const result = await this.probe(port, outputs, matches);
      if (!result) continue;
      if (device) {
        device.link.close(); result.link.close();
        throw failure('busy');
      }
      device = result;
    }
    return device;
  }
}

async function verifyVersion(updater) {
  const device = await updater.find(id => id.text === release.product);
  if (!device) throw failure('version');
  const prefix = [0x7d, 0x46, 0x4c, 1];
  const link = device.link;
  try {
    for (let attempt = 0; attempt < 3; attempt++) {
      link.drain();
      if (!link.send([0xf0, ...prefix, 0xf7])) break;
      const deadline = Date.now() + 500;
      while (Date.now() < deadline) {
        const msg = await link.read(deadline - Date.now());
        if (!msg) break;
        if (!prefix.every((value, index) => msg[index + 1] === value)) continue;
        const end = msg.indexOf(0, 5);
        const version = String.fromCharCode(...msg.slice(5, end < 0 ? -1 : end));
        if (version === release.version) return;
      }
    }
    throw failure('version');
  } finally {
    link.close();
  }
}

export async function flashRelease(access, image, onStep, verify = verifyVersion) {
  const updater = new ReleaseUpdater(access);
  const target = await updater.find(null, true);
  if (!target) throw failure('notfound');
  const resume = target.id.text === `ota-${release.product}`;
  if (!resume) updater.normalInputId = target.link.input.id;
  target.link.close();
  if (resume) {
    if (!await updater.resume(image, onStep, {product: release.product})) throw failure('stopped');
  } else {
    await updater.install(image, release.product, onStep);
  }
  await verify(updater);
}

function mount() {
  const button = document.querySelector('#install');
  const consent = document.querySelector('#ready');
  const status = document.querySelector('#status');
  const progress = document.querySelector('#progress');
  document.querySelector('#source').href = release.source;
  if (!window.isSecureContext || !navigator.requestMIDIAccess || !crypto.subtle) {
    status.textContent = '请用 Mac 或 Windows 的 Chrome、Edge，通过 HTTPS 打开本站。';
    consent.disabled = true;
    return;
  }
  const messages = {start:'已识别 FM-1，正在开启更新。', verify:'设备正在读取和检查包。', loader:'等待更新器重新连接。', write:'正在写入，请保持 USB 连接。', reboot:'正在重启。', done:'正在核对固件版本。'};
  const onStep = stage => { status.textContent = messages[stage] || '正在刷入。'; };
  let running = false;
  const controls = () => { button.disabled = running || !consent.checked; consent.disabled = running; };
  consent.addEventListener('change', controls);
  controls();
  button.addEventListener('click', async () => {
    if (running || !consent.checked) return;
    running = true;
    controls();
    status.removeAttribute('data-error');
    status.textContent = '请允许浏览器访问 USB MIDI。';
    progress.hidden = false;
    progress.removeAttribute('value');
    try {
      const midi = await navigator.requestMIDIAccess({sysex: true});
      status.textContent = '正在下载并校验固件。';
      const response = await fetch(release.file, {cache:'no-cache'});
      if (!response.ok) throw new Error('固件下载失败。请刷新页面后重试。');
      const image = await checkedImage(new Uint8Array(await response.arrayBuffer()), release);
      status.textContent = '正在识别 FM-1。';
      await flashRelease(midi, image, onStep);
      progress.value = 100;
      status.textContent = '安装完成。FM-1 已重启，固件版本核对通过。';
    } catch (error) {
      progress.hidden = true;
      status.dataset.error = '';
      status.textContent = error.name === 'SecurityError' || error.name === 'NotAllowedError' ?
        'USB MIDI 权限未开启。请在浏览器的站点权限中允许 MIDI，然后重试。' :
        ERROR_MESSAGES[error.code] || error.message || '安装失败，请查看恢复指南。';
    } finally {
      running = false;
      controls();
    }
  });
}

if (typeof document !== 'undefined') mount();
