<script>
  import { postSettings } from '../../api.js';

  let { status, refresh } = $props();
  const deg = String.fromCharCode(176);

  const hydro = $derived(status.hydrometer || {});
  const devices = $derived(status.hydrometerDevices || []);
  const scanType = $derived((hydro.scanType || 'UNKNOWN').toUpperCase());
  const scanOn = $derived(!!hydro.enabled && scanType !== 'UNKNOWN');

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
    scanOn ? 'Scanning for Tilt and RAPT devices.' : 'Scanning is off.'
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

  async function setScanEnabled(on) {
    await postSettings({
      hydrometerScanType: on ? 'ALL' : 'OFF',
    });
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

<section class="stackedCard">
  <header class="stackedCardHeader">
    <h2>Hydrometer</h2>
    <p class="stackedCardDesc">BLE scan, device selection, and gravity display for fermentation metrics.</p>
  </header>

  <article class="stackedRow">
    <div class="stackedRowTop">
      <div class="stackedRowTitle">
        <h3>Scan &amp; selection</h3>
        <span class="panelBadge" class:panelBadge-on={scanOn} class:panelBadge-off={!scanOn}>
          {scanOn ? 'Scanning' : 'Off'}
        </span>
      </div>
      <div class="scanModes integrationToggle">
        <button type="button" class:active={!scanOn} onclick={() => setScanEnabled(false)}>Off</button>
        <button type="button" class:active={scanOn} onclick={() => setScanEnabled(true)}>On</button>
      </div>
    </div>
    <p class="formHint">{scanHint}</p>
    <p class="formHint">The selected hydrometer is used for display and fermentation metrics only.</p>
    {#if hydro.selected}
      <div class="selectedDevice">
        <div class="selectedDeviceTitle">{selectionSummary}</div>
        <div class="selectedDeviceDetails">{selectionDetails}</div>
      </div>
    {/if}
    <div class="row">
      <button type="button" onclick={resetHydrometerOg}>Reset OG</button>
      <button type="button" onclick={clearHydrometer}>Clear selection</button>
    </div>
    <div class="saveStatus">{statusText}</div>
    <div class="networkList">
      {#if !devices.length}
        <p class="formHint">
          {scanOn
            ? 'No hydrometers have been decoded yet.'
            : 'Scanning is off.'}
        </p>
      {:else}
        {#each devices as dev}
          <button type="button" class:selected={dev.selected} onclick={() => selectHydrometer(dev.key)}>
            <span>{(dev.label || dev.key || 'Device') + (dev.selected ? ' (Selected)' : '')}</span>
            <span class="networkMeta">{deviceMeta(dev)}</span>
          </button>
        {/each}
      {/if}
    </div>
  </article>
</section>