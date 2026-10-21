/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

export const CONFIG_OFFSET = 0x190000;
export const CONFIG_SIZE = 0x6000;

export function makeManifest(base, mode, configURL) {
  if (!['install', 'update', 'config'].includes(mode)) throw new Error('Invalid operation');
  if (mode !== 'update' && !configURL) throw new Error('Select a private config.bin first');
  const parts = [];
  if (mode !== 'config') parts.push({path: new URL('firmware.bin', base).href, offset: 0});
  if (mode !== 'update') parts.push({path: configURL, offset: CONFIG_OFFSET});
  return {
    ...baseManifest,
    // Without Improv, false would silently default to a whole-chip erase!
    new_install_prompt_erase: true,
    new_install_improv_wait_time: 0,
    builds: [{chipFamily: 'ESP32', parts}],
  };
}
let baseManifest = {name: 'ESP32-CAM', version: 'development'};

export function mountInstaller(document, location, URLApi = URL) {
  const mode = document.getElementById('mode');
  const file = document.getElementById('config');
  const status = document.getElementById('status');
  const install = document.getElementById('install');
  let configURL, manifestURL;
  const clear = () => {
    install.replaceChildren();
    if (configURL) URLApi.revokeObjectURL(configURL);
    if (manifestURL) URLApi.revokeObjectURL(manifestURL);
    configURL = manifestURL = undefined;
  };
  const refresh = () => {
    clear();
    const update = mode.value === 'update';
    file.disabled = update;
    document.getElementById('config-label').hidden = update;
    status.className = '';
    const selected = file.files?.[0];
    if (!update && !selected) {
      status.textContent = 'Select a config.bin to continue.'; return;
    }
    if (!update && selected.size !== CONFIG_SIZE) {
      status.textContent = 'Invalid size: config.bin must be exactly 24 KiB. Use the configuration generator.';
      status.className = 'error'; return;
    }
    if (!update) configURL = URLApi.createObjectURL(selected);
    const manifest = makeManifest(location.href, mode.value, configURL);
    manifestURL = URLApi.createObjectURL(new Blob([JSON.stringify(manifest)], {type: 'application/json'}));
    const button = document.createElement('esp-web-install-button');
    button.manifest = manifestURL;
    install.append(button);
    status.textContent = update
      ? 'Ready to update firmware. Keep Erase device unchecked to preserve your NVS settings.'
      : 'Ready. Settings will be written to the camera; firmware contains no Wi-Fi credentials.';
  };
  mode.addEventListener('change', refresh);
  file.addEventListener('change', refresh);
  refresh();
  return {refresh, clear};
}
if (typeof document !== 'undefined') {
  try {
    const response = await fetch('manifest.json');
    if (!response.ok) throw new Error('Firmware manifest is unavailable. Prepare the web-flash package first.');
    baseManifest = await response.json();
    mountInstaller(document, location);
  } catch (error) {
    document.getElementById('status').textContent = error.message;
  }
}
