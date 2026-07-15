<script>
  import { postForm, postBrightnessPreview, getWifiScan } from '../../api.js';

  const OTA_MANIFEST_URL = 'https://lichuu.github.io/fermentdial/ota/manifest.json';

  let { status, config } = $props();

  let fermenterName = $state('');
  let brightness = $state(128);
  let deviceStatus = $state('');

  let newPassword = $state('');
  let confirmPassword = $state('');
  let securityStatus = $state('');

  let ssid = $state('');
  let pass = $state('');
  let wifiScanStatus = $state('');
  let wifiNetworks = $state([]);
  let firmwareFile = $state(null);
  let firmwareInput = $state(null);
  let factoryResetConfirm = $state('');
  let factoryResetStatus = $state('');
  let brightnessAdjusting = $state(false);
  let brightnessTouchedAt = 0;
  let brightnessPreviewTimer = null;
  let brightnessPreviewAbort = null;

  const wifiConnected = $derived(!!config.wifiIp);

  $effect(() => {
    fermenterName = status.fermenterName;
    if (!brightnessAdjusting && Date.now() - brightnessTouchedAt > 4000) {
      brightness = status.brightness;
    }
    ssid = config.wifiSsid;
  });

  function scheduleBrightnessPreview() {
    clearTimeout(brightnessPreviewTimer);
    brightnessPreviewTimer = setTimeout(sendBrightnessPreview, 60);
  }

  async function sendBrightnessPreview() {
    brightnessPreviewAbort?.abort();
    brightnessPreviewAbort = new AbortController();
    try {
      await postBrightnessPreview(Number(brightness), {
        signal: brightnessPreviewAbort.signal,
      });
    } catch (e) {
      // Ignore transient preview failures (including aborts) while dragging.
    }
  }

  function finishBrightnessAdjust() {
    clearTimeout(brightnessPreviewTimer);
    void sendBrightnessPreview();
    brightnessTouchedAt = Date.now();
    brightnessAdjusting = false;
  }

  async function saveDevice() {
    deviceStatus = 'Saving...';
    await postForm('/settings/device', { fermenterName, brightness });
    deviceStatus = 'Saved.';
  }

  async function saveSecurity() {
    securityStatus = 'Saving...';
    const r = await postForm('/settings/security', { newPassword, confirmPassword });
    securityStatus = r.ok ? 'Saved.' : 'Passwords did not match.';
    if (r.ok) {
      newPassword = '';
      confirmPassword = '';
    }
  }

  function pickWifi(s) {
    ssid = s;
  }

  function onFirmwarePick(ev) {
    firmwareFile = ev.target.files[0] || null;
  }

  // Online update: the browser fetches the release from GitHub Pages (the
  // device itself never talks to GitHub) and streams it to POST /firmware.
  let otaPhase = $state('idle'); // idle|checking|current|available|downloading|installing|rebooting|error
  let otaManifest = $state(null);
  let otaProgress = $state(0);
  let otaLoaded = $state(0);
  let otaTotal = $state(0);
  let otaRebootSecs = $state(0);
  let otaError = $state('');

  const mb = (bytes) => (bytes / 1048576).toFixed(1);

  function versionParts(v) {
    return String(v || '').replace(/^v/, '').split('.').map((n) => parseInt(n, 10) || 0);
  }

  function isNewer(remote, local) {
    const r = versionParts(remote);
    const l = versionParts(local);
    for (let i = 0; i < Math.max(r.length, l.length); i++) {
      if ((r[i] || 0) !== (l[i] || 0)) return (r[i] || 0) > (l[i] || 0);
    }
    return false;
  }

  async function checkForUpdate() {
    otaPhase = 'checking';
    otaError = '';
    try {
      const res = await fetch(OTA_MANIFEST_URL, { cache: 'no-store' });
      if (!res.ok) throw new Error('HTTP ' + res.status);
      otaManifest = await res.json();
      otaPhase = isNewer(otaManifest.version, config.firmwareVersion) ? 'available' : 'current';
    } catch (e) {
      otaPhase = 'error';
      otaError = 'Could not fetch update info — no release published yet, or this browser is offline.';
    }
  }

  async function sha256Hex(blob) {
    // SubtleCrypto needs a secure context; the dashboard is plain http, so
    // this is best-effort (TLS to GitHub already covers transit integrity).
    if (!crypto?.subtle) return null;
    const digest = await crypto.subtle.digest('SHA-256', await blob.arrayBuffer());
    return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, '0')).join('');
  }

  async function installUpdate() {
    const variant = status.demo ? 'demo' : 'wifi';
    const build = otaManifest?.builds?.[variant];
    if (!build) {
      otaPhase = 'error';
      otaError = `This release has no "${variant}" build.`;
      return;
    }
    otaPhase = 'downloading';
    otaProgress = 0;
    try {
      const res = await fetch(new URL(build.file, OTA_MANIFEST_URL).href, { cache: 'no-store' });
      if (!res.ok || !res.body) throw new Error('HTTP ' + res.status);
      const total = Number(res.headers.get('content-length')) || build.size || 0;
      const reader = res.body.getReader();
      const chunks = [];
      let received = 0;
      otaTotal = total;
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        chunks.push(value);
        received += value.length;
        otaLoaded = received;
        if (total) otaProgress = Math.round((received / total) * 100);
      }
      const blob = new Blob(chunks, { type: 'application/octet-stream' });
      if (build.size && blob.size !== build.size) throw new Error('size mismatch');
      const digest = await sha256Hex(blob);
      if (digest && build.sha256 && digest !== build.sha256) throw new Error('checksum mismatch');

      otaPhase = 'installing';
      otaProgress = 0;
      otaLoaded = 0;
      await postFirmwareBlob(blob, build.file);

      otaPhase = 'rebooting';
      otaRebootSecs = 0;
      await waitForReboot(otaManifest.version);
    } catch (e) {
      otaPhase = 'error';
      otaError = 'Update failed (' + (e?.message || e) + '). The device keeps its old firmware until a flash completes.';
    }
  }

  // fetch cannot observe upload progress; XHR drives the installing bar.
  function postFirmwareBlob(blob, filename) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/firmware');
      xhr.upload.onprogress = (e) => {
        if (!e.lengthComputable) return;
        otaLoaded = e.loaded;
        otaTotal = e.total;
        otaProgress = Math.round((e.loaded / e.total) * 100);
      };
      xhr.onload = () => {
        // An auth-gated device redirects to /login instead of flashing.
        if (xhr.responseURL && !xhr.responseURL.endsWith('/firmware')) {
          reject(new Error('not logged in'));
        } else if (xhr.status >= 200 && xhr.status < 300) {
          resolve();
        } else {
          reject(new Error('upload HTTP ' + xhr.status));
        }
      };
      xhr.onerror = () => reject(new Error('upload failed'));
      const fd = new FormData();
      fd.append('firmware', blob, filename);
      xhr.send(fd);
    });
  }

  async function waitForReboot(targetVersion) {
    for (let i = 0; i < 60; i++) {
      await new Promise((r) => setTimeout(r, 2000));
      otaRebootSecs = (i + 1) * 2;
      try {
        // Plain fetch with a short timeout: a rebooting device leaves normal
        // requests hanging for ~30 s, which froze this counter.
        const res = await fetch('/api/status', {
          cache: 'no-store',
          signal: AbortSignal.timeout(1800),
        });
        const s = await res.json();
        if (s.firmwareVersion === targetVersion) {
          location.reload();
          return;
        }
      } catch (e) {
        // Device still rebooting.
      }
    }
    otaPhase = 'error';
    otaError = 'The device did not report the new version after the update — check it manually.';
  }

  async function factoryReset() {
    if (factoryResetConfirm !== 'RESET') {
      factoryResetStatus = 'Type RESET to confirm.';
      return;
    }
    factoryResetStatus = 'Resetting...';
    try {
      const r = await postForm('/settings/factory-reset', {
        confirm: factoryResetConfirm,
      });
      factoryResetStatus = r.ok
        ? 'Reset complete. Reconnect to the setup Wi-Fi to configure the device.'
        : 'Factory reset failed.';
    } catch (e) {
      factoryResetStatus =
        'Device is rebooting. Join the FermentDial setup network to reconnect.';
    }
  }

  async function scanWifi() {
    wifiScanStatus = 'Scanning...';
    wifiNetworks = [];
    try {
      const data = await getWifiScan();
      const nets = (data.networks || []).filter((n) => n.ssid);
      if (!nets.length) {
        wifiScanStatus = 'No networks found.';
        return;
      }
      wifiScanStatus = '';
      wifiNetworks = nets;
    } catch (e) {
      wifiScanStatus = 'Scan failed.';
    }
  }
</script>

<section class="stackedCard">
  <header class="stackedCardHeader">
    <h2>System</h2>
    <p class="stackedCardDesc">Device identity, security, network, and firmware updates.</p>
  </header>

  <article class="stackedRow">
    <div class="stackedRowTop">
      <div class="stackedRowTitle">
        <h3>Device</h3>
        <span class="panelBadge panelBadge-info">v{config.firmwareVersion}</span>
      </div>
    </div>
    <form class="panelForm" onsubmit={(e) => { e.preventDefault(); saveDevice(); }}>
      <label>Fermenter name<input maxlength="24" bind:value={fermenterName} /></label>
      <label>
        Screen brightness
        <input
          type="range"
          min="30"
          max="255"
          step="5"
          bind:value={brightness}
          onpointerdown={() => {
            brightnessAdjusting = true;
            brightnessTouchedAt = Date.now();
          }}
          onpointerup={finishBrightnessAdjust}
          onpointercancel={finishBrightnessAdjust}
          oninput={scheduleBrightnessPreview}
          onchange={finishBrightnessAdjust}
        />
      </label>
      <button type="submit" class="formSubmit">Save</button>
    </form>
    {#if deviceStatus}<p class="formFeedback">{deviceStatus}</p>{/if}
    <dl class="metaLine">
      <dt>Hostname</dt>
      <dd><code>{config.hostname}</code></dd>
    </dl>
  </article>

  <article class="stackedRow">
    <div class="stackedRowTop">
      <div class="stackedRowTitle">
        <h3>Security</h3>
        <span class="panelBadge" class:panelBadge-on={config.passwordSet} class:panelBadge-warn={!config.passwordSet}>
          {config.passwordSet ? 'Locked' : 'Unlocked'}
        </span>
      </div>
    </div>
    <form class="panelForm" onsubmit={(e) => { e.preventDefault(); saveSecurity(); }}>
      <label>New admin password<input type="password" autocomplete="new-password" placeholder="leave blank to disable" bind:value={newPassword} /></label>
      <label>Confirm password<input type="password" autocomplete="new-password" bind:value={confirmPassword} /></label>
      <button type="submit" class="formSubmit">Save</button>
    </form>
    {#if securityStatus}<p class="formFeedback">{securityStatus}</p>{/if}
    <p class="formHint">
      {#if config.passwordSet}
        Config pages require the admin password. Dashboard and metrics stay public.
      {:else}
        Anyone on the network can change settings until a password is set.
      {/if}
    </p>
  </article>

  <article class="stackedRow">
    <div class="stackedRowTop">
      <div class="stackedRowTitle">
        <h3>Wi-Fi</h3>
        <span class="panelBadge" class:panelBadge-on={wifiConnected} class:panelBadge-off={!wifiConnected}>
          {wifiConnected ? 'Connected' : 'Offline'}
        </span>
      </div>
    </div>
    <form class="panelForm" method="post" action="/wifi">
      <label>Network name<input name="ssid" autocomplete="off" required bind:value={ssid} /></label>
      <div class="formSection">
        <button type="button" class="btnSecondary" onclick={scanWifi}>Scan networks</button>
        {#if wifiScanStatus}<p class="formHint">{wifiScanStatus}</p>{/if}
        {#if wifiNetworks.length}
          <div class="networkList">
            {#each wifiNetworks as n}
              <button type="button" class:selected={ssid === n.ssid} onclick={() => pickWifi(n.ssid)}>
                <span>{n.ssid}</span>
                <span class="networkMeta">{(n.secure ? 'secured' : 'open') + ' · ' + n.rssi + ' dBm'}</span>
              </button>
            {/each}
          </div>
        {/if}
      </div>
      <label>Password<input name="pass" type="password" placeholder="leave blank to keep saved" bind:value={pass} /></label>
      <button type="submit" class="formSubmit">Save &amp; reboot</button>
    </form>
    <div class="metaStack">
      <dl class="metaLine">
        <dt>Connected IP</dt>
        <dd>{config.wifiIp || 'not connected'}</dd>
      </dl>
      <dl class="metaLine">
        <dt>Setup AP</dt>
        <dd>{config.apSsid}</dd>
      </dl>
    </div>
  </article>

  <article class="stackedRow dangerZone">
    <div class="stackedRowTop">
      <div class="stackedRowTitle">
        <h3>Factory Reset</h3>
        <span class="panelBadge panelBadge-warn">Destructive</span>
      </div>
    </div>
    <p class="formHint">
      Clears controller settings, Wi-Fi credentials, integrations, admin password,
      event log, and CSV history, then reboots into setup mode. You will need to
      rejoin the setup access point and configure Wi-Fi again.
    </p>
    <form class="panelForm" onsubmit={(e) => { e.preventDefault(); factoryReset(); }}>
      <label>
        Type RESET to confirm
        <input autocomplete="off" placeholder="RESET" bind:value={factoryResetConfirm} />
      </label>
      <button type="submit" class="formSubmit danger" disabled={factoryResetConfirm !== 'RESET'}>
        Factory reset &amp; reboot
      </button>
    </form>
    {#if factoryResetStatus}<p class="formFeedback">{factoryResetStatus}</p>{/if}
  </article>

  <article class="stackedRow">
    <div class="stackedRowTop">
      <div class="stackedRowTitle">
        <h3>Firmware Update</h3>
        <span class="panelBadge" class:panelBadge-on={status.otaEnabled} class:panelBadge-off={!status.otaEnabled}>
          {status.otaEnabled ? 'Ready' : 'Disabled'}
        </span>
      </div>
    </div>
    {#if status.otaEnabled}
      <div class="formSection">
        {#if otaPhase === 'idle' || otaPhase === 'checking'}
          <button type="button" class="btnSecondary" onclick={checkForUpdate} disabled={otaPhase === 'checking'}>
            {otaPhase === 'checking' ? 'Checking…' : 'Check for updates'}
          </button>
        {:else if otaPhase === 'current'}
          <p class="formHint">
            Up to date — v{config.firmwareVersion} is the latest release.
            <a href={otaManifest.notes_url} target="_blank" rel="noreferrer">Release notes</a>
          </p>
          <button type="button" class="btnSecondary" onclick={checkForUpdate}>Check again</button>
        {:else if otaPhase === 'available'}
          <p class="formHint">
            Version {otaManifest.version} is available (running v{config.firmwareVersion}).
            <a href={otaManifest.notes_url} target="_blank" rel="noreferrer">Release notes</a>
          </p>
          <button type="button" class="formSubmit" onclick={installUpdate}>Download &amp; install</button>
        {:else if otaPhase === 'downloading' || otaPhase === 'installing'}
          <p class="formHint">
            {otaPhase === 'downloading' ? 'Downloading' : 'Installing'}… {otaProgress}%
            {#if otaTotal}({mb(otaLoaded)} / {mb(otaTotal)} MB){/if}
          </p>
          <div class="otaProgress"><div style="width:{otaProgress}%"></div></div>
          {#if otaPhase === 'installing'}
            <p class="formHint">Outputs are off. Do not power down the Dial.</p>
          {/if}
        {:else if otaPhase === 'rebooting'}
          <p class="formHint">Flashed. Waiting for the device to reboot… ({otaRebootSecs} s)</p>
          <div class="otaProgress indeterminate"><div></div></div>
        {:else if otaPhase === 'error'}
          <p class="formFeedback">{otaError}</p>
          <button type="button" class="btnSecondary" onclick={checkForUpdate}>Try again</button>
        {/if}
      </div>
      <form class="panelForm" method="post" action="/firmware" enctype="multipart/form-data">
        <label>
          Firmware .bin
          <div class="fileRow">
            <span class="fileName" class:selected={!!firmwareFile}>
              {firmwareFile ? firmwareFile.name : 'No file selected'}
            </span>
            <button type="button" class="btnBrowse" onclick={() => firmwareInput.click()}>Browse</button>
            <input type="file" name="firmware" accept=".bin" required bind:this={firmwareInput} onchange={onFirmwarePick} />
          </div>
        </label>
        <button type="submit" class="formSubmit" disabled={!firmwareFile}>Upload &amp; reboot</button>
      </form>
    {:else}
      <p class="formHint">Firmware upload is disabled in this build.</p>
    {/if}
    <p class="formHint">Outputs turn off during upload. Device reboots on success.</p>
    <p class="formHint">
      Build with <code>uv run platformio run -e m5stack_dial_demo</code>, then upload
      <code>.pio/build/m5stack_dial_demo/firmware.bin</code>.
    </p>
  </article>
</section>