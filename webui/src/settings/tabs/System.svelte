<script>
  import { postForm, postBrightnessPreview, getWifiScan } from '../../api.js';

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
      <dt>Firmware</dt>
      <dd>{config.firmwareName}</dd>
    </dl>
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
    <dl class="metaLine">
      <dt>Connected IP</dt>
      <dd>{config.wifiIp || 'not connected'}</dd>
    </dl>
    <dl class="metaLine">
      <dt>Setup AP</dt>
      <dd>{config.apSsid}</dd>
    </dl>
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