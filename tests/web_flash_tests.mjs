/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

import assert from 'node:assert/strict';
import {makeManifest, mountInstaller, CONFIG_SIZE, CONFIG_OFFSET} from '../web-flash/installer.mjs';
const base = 'https://example.invalid/camera/index.html';
const setup = makeManifest(base, 'install', 'blob:private-settings');
assert.deepEqual(setup.builds[0].parts, [
  {path:'https://example.invalid/camera/firmware.bin',offset:0},
  {path:'blob:private-settings',offset:CONFIG_OFFSET},
]);
assert.equal(setup.new_install_prompt_erase,true);
assert.equal(setup.new_install_improv_wait_time,0);
assert.equal(makeManifest(base,'update').builds[0].parts.length,1);
assert.deepEqual(makeManifest(base,'config','blob:private-settings').builds[0].parts,
  [{path:'blob:private-settings',offset:CONFIG_OFFSET}]);
assert.throws(()=>makeManifest(base,'install'));
assert.throws(()=>makeManifest(base,'unknown','blob:private-settings'));
const elements = Object.fromEntries(['mode','config','status','install','config-label'].map(id=>[id,{
  value:'install',files:[],addEventListener(){},replaceChildren(){this.child=undefined;},append(child){this.child=child;}
}]));
const document = {getElementById:id=>elements[id],createElement:tag=>({tag})};
const objects=[],revoked=[];
const url = {createObjectURL:value=>{objects.push(value);return `blob:test-${objects.length}`;},revokeObjectURL:value=>revoked.push(value)};
const mounted=mountInstaller(document,{href:base},url);
assert.equal(elements.install.child,undefined);
elements.config.files=[{size:CONFIG_SIZE-1}]; mounted.refresh();
assert.equal(elements.install.child,undefined);
assert.equal(elements.status.className,'error');
elements.config.files=[new Blob([new Uint8Array(CONFIG_SIZE)])]; mounted.refresh();
assert.equal(elements.install.child.tag,'esp-web-install-button');
let manifest=JSON.parse(await objects.at(-1).text());
assert.equal(manifest.builds[0].parts[1].offset,CONFIG_OFFSET);
elements.mode.value='update'; mounted.refresh();
assert.equal(elements.config.disabled,true);
manifest=JSON.parse(await objects.at(-1).text());
assert.equal(manifest.builds[0].parts.length,1);
assert.ok(revoked.length>=2);
mounted.clear();
console.log('PASS: local config selection, manifest offsets, update preservation, erase prompt and object URL cleanup');
