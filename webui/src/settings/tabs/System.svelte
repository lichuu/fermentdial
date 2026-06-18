<script>
  import { postForm, getWifiScan } from '../../api.js';

  let { status, config } = $props();

  let fermenterName = $state('');
  let brightness = $state(128);
  let deviceStatus = $state('');

  let newPassword = $state('');
  let confirmPassword = $state('');
  let securityStatus = $state('');

  let ssid = $state('');
  let pass = $state('');
  let wifiScanStatus = $state('Select a scanned network or enter the name manually.');
  let wifiNetworks = $state([]);

  $effect(() => {
    fermenterName = status.fermenterName;
    brightness = status.brightness;
    ssid = config.wifiSsid;
  });

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

  async function scanWifi() {
    wifiScanStatus = 'Scanning...';
    wifiNetworks = [];
    try {
      const data = await getWifiScan();
      const nets = (data.networks || []).filter((n) => n.ssid);
      if (!nets.length) {
        wifiScanStatus = 'No networks found. Enter the name manually.';
        return;
      }
      wifiScanStatus = 'Select a network or enter the name manually.';
      wifiNetworks = nets;
    } catch (e) {
      wifiScanStatus = 'Scan failed. Enter the name manually.';
    }
  }
</script>

<div class="grid">
  <section class="panel">
    <h2>Device</h2>
    <form onsubmit={(e) => { e.preventDefault(); saveDevice(); }}>
      <label>Fermenter name<input maxlength="24" bind:value={fermenterName} /></label>
      <label>Screen brightness<input type="range" min="30" max="255" step="5" bind:value={brightness} /></label>
      <button type="submit">Save device settings</button>
    </form>
    <div class="saveStatus">{deviceStatus}</div>
    <p class="hint">Firmware: {config.firmwareName} v{config.firmwareVersion}</p>
    <p class="hint">Hostname: <code>{config.hostname}</code></p>
  </section>

  <section class="panel">
    <h2>Security</h2>
    <form onsubmit={(e) => { e.preventDefault(); saveSecurity(); }}>
      <label>New admin password<input type="password" autocomplete="new-password" placeholder="leave blank to disable the lock" bind:value={newPassword} /></label>
      <label>Confirm password<input type="password" autocomplete="new-password" bind:value={confirmPassword} /></label>
      <button type="submit">Update password</button>
    </form>
    <div class="saveStatus">{securityStatus}</div>
    <p class="hint">
      {#if config.passwordSet}
        Config pages are <b>locked</b>. The live dashboard and metrics stay public.
      {:else}
        Config pages are <span class="warn">unlocked</span> &mdash; anyone on the network can change settings.
      {/if}
    </p>
  </section>

  <section class="panel">
    <h2>Wi-Fi</h2>
    <form method="post" action="/wifi">
      <label>Wi-Fi name<input name="ssid" autocomplete="off" required bind:value={ssid} /></label>
      <div class="wifiTools">
        <button type="button" onclick={scanWifi}>Scan for networks</button>
        <div class="hint scanStatus">{wifiScanStatus}</div>
        <div class="networkList">
          {#each wifiNetworks as n}
            <button type="button" onclick={() => pickWifi(n.ssid)}>
              <span>{n.ssid}</span>
              <span class="networkMeta">{(n.secure ? 'secured' : 'open') + ' ' + n.rssi + ' dBm'}</span>
            </button>
          {/each}
        </div>
      </div>
      <label>Password<input name="pass" type="password" placeholder="leave blank to keep saved" bind:value={pass} /></label>
      <button type="submit">Save and reboot</button>
    </form>
    <p class="hint">Connected IP: {config.wifiIp || 'not connected'}</p>
    <p class="hint">Setup AP: {config.apSsid} (open network)</p>
  </section>

  <section class="panel">
    <h2>Firmware Update</h2>
    <p class="warn">Outputs turn off before upload starts. The controller reboots after a successful update.</p>
    {#if status.otaEnabled}
      <form method="post" action="/firmware" enctype="multipart/form-data">
        <label>Firmware .bin<input type="file" name="firmware" accept=".bin" required /></label>
        <button type="submit">Upload and reboot</button>
      </form>
    {:else}
      <p class="hint">Firmware upload is disabled in this build.</p>
    {/if}
    <p class="hint">
      Build with <code>uv run platformio run -e m5stack_dial_demo</code>, then upload
      <code>.pio/build/m5stack_dial_demo/firmware.bin</code>.
    </p>
  </section>
</div>
