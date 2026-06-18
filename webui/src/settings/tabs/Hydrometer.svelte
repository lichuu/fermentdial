<script>
  import { postSettings } from '../../api.js';

  let { status, refresh } = $props();
  const deg = String.fromCharCode(176);

  let bleEnabled = $state(false);

  $effect(() => {
    bleEnabled = !!(status.hydrometer && status.hydrometer.enabled);
  });

  const hydro = $derived(status.hydrometer || {});
  const devices = $derived(status.hydrometerDevices || []);
  const scanType = $derived((hydro.scanType || 'UNKNOWN').toUpperCase());

  const statusText = $derived(
    hydro.selected
      ? hydro.valid
        ? hydro.stale
          ? 'Selected device is stale'
          : 'Selected device is active'
        : 'Selected device waiting for a reading'
      : 'No hydrometer selected'
  );

  const scanHint = $derived(
    scanType === 'TILT'
      ? 'Scanning for Tilt devices only.'
      : scanType === 'RAPT'
        ? 'Scanning for RAPT devices only.'
        : 'Scanning is off.'
  );

  const selectionSummary = $derived(
    hydro.selected
      ? hydro.label || hydro.name || hydro.key || 'Selected device'
      : 'No hydrometer selected'
  );

  const selectionDetails = $derived(
    hydro.selected
      ? hydro.valid
        ? deviceMeta(hydro)
        : 'Waiting for a reading' + String.fromCharCode(8230)
      : ''
  );

  function hydroAge(sec) {
    sec = Math.max(0, sec || 0);
    if (sec < 60) return sec + 's';
    const m = Math.floor(sec / 60);
    const h = Math.floor(m / 60);
    return h > 0 ? h + 'h ' + (m % 60) + 'm' : m + 'm';
  }

  function deviceMeta(dev) {
    const parts = [];
    if (dev.gravity != null) parts.push('SG ' + Number(dev.gravity).toFixed(3));
    if (dev.temperature != null) parts.push(Number(dev.temperature).toFixed(1) + deg + (status.unit || ''));
    parts.push((dev.rssi || 0) + ' dBm');
    parts.push(dev.lastSeenSeconds == null ? 'unknown' : hydroAge(dev.lastSeenSeconds));
    return parts.join(' · ');
  }

  async function setScanType(type) {
    await postSettings({ hydrometerScanType: type });
    await refresh();
  }

  async function saveHydroSettings() {
    await postSettings({ hydrometerBleEnabled: bleEnabled ? 1 : 0 });
    await refresh();
  }

  async function selectHydrometer(key) {
    await postSettings({ hydrometerSelectKey: key });
    await refresh();
  }

  async function clearHydrometer() {
    await postSettings({ hydrometerClearSelection: 1 });
    await refresh();
  }

  async function resetHydrometerOg() {
    await postSettings({ hydrometerResetOg: 1 });
    await refresh();
  }
</script>

<section class="panel">
  <h2>Hydrometer</h2>
  <label><input type="checkbox" bind:checked={bleEnabled} onchange={saveHydroSettings} />Enable BLE scanning</label>
  <div class="scanModes">
    <button class:active={scanType === 'UNKNOWN'} onclick={() => setScanType('OFF')}>Off</button>
    <button class:active={scanType === 'TILT'} onclick={() => setScanType('TILT')}>Scan Tilt</button>
    <button class:active={scanType === 'RAPT'} onclick={() => setScanType('RAPT')}>Scan RAPT</button>
  </div>
  <div class="hint">{scanHint}</div>
  <div class="hint">The selected hydrometer is used for display and fermentation metrics only.</div>
  {#if hydro.selected}
    <div class="selectedDevice">
      <div class="selectedDeviceTitle">{selectionSummary}</div>
      <div class="selectedDeviceDetails">{selectionDetails}</div>
    </div>
  {/if}
  <div class="row" style="margin-top:10px">
    <button type="button" onclick={resetHydrometerOg}>Reset OG</button>
    <button type="button" onclick={clearHydrometer}>Clear selection</button>
  </div>
  <div class="saveStatus">{statusText}</div>
  <div class="networkList">
    {#if !devices.length}
      <div class="hint">
        {scanType === 'UNKNOWN'
          ? 'Scanning is off.'
          : 'No ' + scanType + ' devices have been decoded yet.'}
      </div>
    {:else}
      {#each devices as dev}
        <button type="button" class:selected={dev.selected} onclick={() => selectHydrometer(dev.key)}>
          <span>{(dev.label || dev.key || 'Device') + (dev.selected ? ' (Selected)' : '')}</span>
          <span class="networkMeta">{deviceMeta(dev)}</span>
        </button>
      {/each}
    {/if}
  </div>
</section>
