/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

// Exercise the real embedded controller with a fake DOM, HTTP, and clock.
// This verifies command/poll behavior; it is not a hardware/browser MJPEG test.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const html = fs.readFileSync(new URL('../main/index.html', import.meta.url), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = new Map();
for (const id of ['camera', 'status', 'quality', 'resolution', 'qualityValue', 'nodeName', 'autoOffHint',
                  'autoOff', 'autoOffMinutes', 'autoOffMinutesValue', 'start', 'stop', 'apply', 'telemetry', 'details']) {
  elements.set(id, {
    value: id === 'quality' ? '12' : 'VGA',
    textContent: '',
    disabled: false,
    checked: true,
    removeAttribute(name) { delete this[name]; }
  });
}
const sample = {
  resolution: 'VGA', jpeg_quality: 12, sensor: 'OV2640', width: 640, height: 480,
  format: 'JPEG', fps: 7.5, frame_bytes: 4096, kib_per_second: 30,
  camera_state: 'standby', wifi_rssi_dbm: -50, wifi_modem_sleep: true,
  psram_bytes: 8388608, psram_free_bytes: 3000000, free_heap_bytes: 150000,
  uptime_seconds: 1, frames_sent: 8, capture_errors: 0, idf_version: 'v6.1',
  sensor_pid: '0x2642', settings_pending: false, streaming: false,
  node_name: 'ESP32-CAM02', stream_remaining_seconds: 0,
  stream_limit_seconds: 1200, stream_timed_out: false, auto_off: true, auto_off_minutes: 20,
  camera_error_code: 0
};
const requests = [];
const timers = new Map();
let timerId = 0;
const context = vm.createContext({
  document: { getElementById: id => elements.get(id) },
  location: { href: 'http://192.0.2.1/' },
  URL, URLSearchParams, AbortSignal,
  fetch: async (path, options) => {
    requests.push({ path, options });
    return { ok: true, json: async () => ({ ...sample }) };
  },
  clearTimeout: id => timers.delete(id),
  setTimeout: (callback, delay) => { timers.set(++timerId, { callback, delay }); return timerId; }
});
vm.runInContext(script + '\nglobalThis.controller = page;', context);
const page = context.controller;
// Flush initial fetch and render promises before taking ownership of polling.
await new Promise(resolve => setImmediate(resolve));
assert.equal(timers.size, 1);
assert.equal([...timers.values()][0].delay, 5000, 'idle polls every five seconds');
assert.match(elements.get('telemetry').textContent, /7\.5 sent fps/);
assert.match(elements.get('details').textContent, /ESP-IDF v6\.1/);
assert.equal(elements.get('nodeName').textContent, 'ESP32-CAM02 Live');
assert.equal(context.document.title, 'ESP32-CAM02 Live');

assert.match(html, /id="autoOffMinutes" type="range" min="5" max="120" step="1" value="20"/);
assert.equal(elements.get('autoOff').checked, true);
assert.equal(Number(elements.get('autoOffMinutes').value), 20);
elements.get('autoOffMinutes').value = '120';
elements.get('autoOffMinutes').oninput();
page.render(sample);
assert.equal(elements.get('autoOffMinutes').value, '120', 'polling preserves slider draft');
elements.get('autoOff').checked = false;
await page.savePolicy();
const policy = requests.find(request => request.path === '/auto-off');
assert.equal(policy.options.body.toString(), 'auto_off=false&auto_off_minutes=120');
assert.equal(page.policyBusy, false);
assert.equal(elements.get('autoOff').disabled, false);
await new Promise(resolve => setImmediate(resolve));

page.start();
await new Promise(resolve => setImmediate(resolve));
assert.equal(new URL(elements.get('camera').src).port, '81');
assert.equal(new URL(elements.get('camera').src).pathname, '/stream');
assert.equal([...timers.values()][0].delay, 1000, 'active intent polls every second');

elements.get('resolution').value = 'UXGA';
elements.get('resolution').onchange();
page.render(sample);
assert.equal(elements.get('resolution').value, 'UXGA', 'polling preserves unsaved settings');
elements.get('quality').value = '0';
await page.apply();
const control = requests.find(request => request.path === '/control');
assert.equal(control.options.body.toString(), 'resolution=UXGA&quality=0');
assert.equal(page.commandBusy, false);

await page.stop();
assert.equal(page.wantStream, false);
assert.equal(elements.get('camera').src, undefined, 'stop closes image connection');
assert(requests.some(request => request.path === '/stop'), 'stop explicitly requests standby');
await new Promise(resolve => setImmediate(resolve));
assert.equal([...timers.values()][0].delay, 5000);

// Timeout comes from the device, not the browser clock. A stale timeout from
// the previous session must not terminate a newly requested stream.
page.start();
page.render({ ...sample, stream_timed_out: true });
assert.equal(page.wantStream, true, 'ignore previous timeout while awaiting new session');
page.render({ ...sample, streaming: true, camera_state: 'streaming', stream_remaining_seconds: 1200 });
assert.match(elements.get('status').textContent, /20:00 remaining/);
assert.match(elements.get('autoOffHint').textContent, /after 20 minutes/);
page.render({ ...sample, streaming: true, camera_state: 'streaming', stream_remaining_seconds: 61 });
assert.match(elements.get('status').textContent, /01:01 remaining/);
assert.match(elements.get('telemetry').textContent, /01:01 remaining/);
page.render({ ...sample, streaming: true, camera_state: 'streaming', stream_remaining_seconds: 1 });
assert.match(elements.get('status').textContent, /00:01 remaining/);
page.render({ ...sample, stream_timed_out: true });
assert.equal(page.wantStream, false, 'device timeout clears browser stream intent');
assert.equal(elements.get('camera').src, undefined, 'timeout closes image without auto-restart');
assert.match(elements.get('status').textContent, /Stopped automatically after 20 minutes/);

page.start();
page.render({ ...sample, streaming: true, camera_state: 'streaming', auto_off_minutes: 7,
              stream_limit_seconds: 420, stream_remaining_seconds: 420 });
assert.match(elements.get('status').textContent, /07:00 remaining/);
page.render({ ...sample, auto_off_minutes: 7, stream_timed_out: true });
assert.match(elements.get('status').textContent, /Stopped automatically after 7 minutes/);

page.start();
page.render({ ...sample, auto_off: false, streaming: true, camera_state: 'streaming' });
assert.match(elements.get('status').textContent, /Auto-off disabled/);
assert.match(elements.get('telemetry').textContent, /Auto-off disabled/);
assert.doesNotMatch(elements.get('telemetry').textContent, /00:00 remaining/);
assert.match(elements.get('autoOffHint').textContent, /Automatic stream stop is disabled/);
assert.equal(page.wantStream, true, 'disabled timer keeps stream intent');
await page.stop();
await new Promise(resolve => setImmediate(resolve));

// Resolve two polls backwards: the older response must neither render nor
// install a second timer over the newer result.
const pending = [];
context.fetch = () => new Promise(resolve => pending.push(resolve));
const first = page.refresh();
const second = page.refresh();
pending[1]({ ok: true, json: async () => ({ ...sample, fps: 12, streaming: true }) });
await second;
pending[0]({ ok: true, json: async () => ({ ...sample, fps: 99 }) });
await first;
assert.match(elements.get('telemetry').textContent, /12\.0 sent fps/);
assert.equal(timers.size, 1, 'stale requests never create extra timers');
assert.equal([...timers.values()][0].delay, 1000);

context.fetch = async () => { throw new Error('offline'); };
await page.refresh();
assert.match(elements.get('telemetry').textContent, /reconnecting/);
assert.equal([...timers.values()][0].delay, 5000);
await page.apply();
assert.match(elements.get('status').textContent, /Could not apply settings/);
assert.equal(page.commandBusy, false, 'failure releases command buttons');
elements.get('autoOffMinutes').value = '5';
await page.savePolicy();
assert.match(elements.get('status').textContent, /Could not save auto-off/);
assert.equal(page.policyEditing, true);
assert.equal(elements.get('autoOffMinutes').disabled, false);
page.render(sample);
assert.equal(elements.get('autoOffMinutes').value, '5', 'failed policy save preserves draft');
assert.equal(elements.get('start').disabled, false);
page.render({ ...sample, camera_state: 'error', camera_error_code: 0x20004 });
assert.match(elements.get('status').textContent, /Camera failed \(code 131076\)/);
assert.equal(page.wantStream, false, 'camera failure clears stream intent');
console.log('PASS: browser commands, telemetry, edit preservation, polling, stale responses, errors');
