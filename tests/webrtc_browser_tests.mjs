/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const html = fs.readFileSync(new URL('../main/webrtc.html', import.meta.url), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = new Map(['start','stop','status','preview','metrics'].map(id => [id, {
  textContent: '', disabled: false, getContext: () => ({drawImage(){}})
}]));
const requests = [], peers = [], sent = [];
let failed = false, hidden;
class Peer {
  constructor() { this.iceGatheringState = 'complete'; this.connectionState = 'new'; peers.push(this); }
  createDataChannel(label, config) {
    assert.equal(label, 'jpeg'); assert.equal(config.ordered, true);
    this.channel = {readyState: 'connecting', send: value => sent.push(value)};
    return this.channel;
  }
  async createOffer() { return {type: 'offer', sdp: 'v=0\r\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n'}; }
  async setLocalDescription(value) { this.localDescription = value; }
  async setRemoteDescription(value) { assert.equal(value.type, 'answer'); this.channel.readyState = 'open'; this.channel.onopen(); }
  close() { this.closed = true; this.channel.readyState = 'closed'; if (this.channel.onclose) this.channel.onclose(); }
}
const context = vm.createContext({
  document: {getElementById: id => elements.get(id)},
  navigator: {sendBeacon: (...args) => requests.push(args)},
  addEventListener: (event, callback) => { if (event === 'pagehide') hidden = callback; },
  RTCPeerConnection: Peer, ArrayBuffer, DataView, Uint8Array, Blob, AbortSignal,
  setTimeout, clearTimeout,
  createImageBitmap: async () => ({width:640,height:480,close(){}}),
  fetch: async (path, options) => {
    requests.push({path, options});
    return {ok: !failed, text: async () => failed ? 'Another video session is active' : 'v=0\r\nanswer'};
  }
});
vm.runInContext(script + '\nglobalThis.test = {viewer, JpegAssembler};', context);
const {viewer, JpegAssembler} = context.test;
function packet(id, total, offset, payload) {
  const data = new ArrayBuffer(16 + payload.length), view = new DataView(data);
  view.setUint32(0, 0x4a504731); view.setUint32(4, id); view.setUint32(8, total); view.setUint32(12, offset);
  new Uint8Array(data, 16).set(payload); return data;
}
const assembler = new JpegAssembler();
assert.equal(assembler.push(packet(1, 6, 0, [255,216,1])), null);
assert.deepEqual([...assembler.push(packet(1, 6, 3, [2,255,217]))], [255,216,1,2,255,217]);
assert.throws(() => assembler.push(packet(2, 524289, 0, [1])), /size/);
assert.throws(() => assembler.push(packet(2, 6, 3, [1])), /Incomplete/);
assert.throws(() => assembler.push(new ArrayBuffer(16)), /chunk/);
await viewer.start();
assert.equal(viewer.owns, true); assert.equal(sent.at(-1), 'next');
assert.equal(requests.find(x => x.path === '/webrtc/offer').options.headers['Content-Type'], 'application/sdp');
await viewer.channel.onmessage({data:packet(1, 4, 0, [255,216,255,217])});
assert.equal(viewer.frames, 1); assert.match(elements.get('metrics').textContent, /1 JPEG frames/);
await viewer.stop();
assert.equal(viewer.pc, null); assert.equal(viewer.owns, false);
assert(requests.some(x => x.path === '/webrtc/close'));
const closes = requests.filter(x => x.path === '/webrtc/close').length;
failed = true;
await viewer.start();
assert.match(elements.get('status').textContent, /Another video session/);
assert.equal(requests.filter(x => x.path === '/webrtc/close').length, closes, 'rejected viewer cannot stop another owner');
assert.equal(elements.get('start').disabled, false);
failed = false; await viewer.start(); hidden();
assert(Array.isArray(requests.at(-1)) && requests.at(-1)[0] === '/webrtc/close');
await viewer.stop();
console.log('PASS: WebRTC viewer chunk bounds, reassembly, signaling, rendering, ownership and cleanup');
