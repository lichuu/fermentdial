<script>
  import Menu from '../shared/Menu.svelte';
  import Sparkline from './Sparkline.svelte';
  import HydrometerChart from './HydrometerChart.svelte';
  import EventLog from './EventLog.svelte';
  import { formatMode, getStatus, getHistory, getHistoryCsv, postSettings } from '../api.js';
  import {
    coerceDecimal,
    decimalError,
    DREST_HOURS_LIMITS,
    filterDecimalInput,
    filterIntegerInput,
    targetLimits,
  } from '../validation.js';

  const deg = String.fromCharCode(176);

  let s = $state(null);
  let spark = $state([]);
  let graphSamples = $state([]);
  let graphSource = $state('live');
  let graphSourceLabel = $state('Live');

  let profileSelectEl;
  let targetInputEl;
  let dRestTargetInputEl;
  let dRestHoursInputEl;
  let dRestReturnSelectEl;

  function remainingText(sec) {
    sec = Math.max(0, sec || 0);
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    return h > 0 ? h + 'h ' + m + 'm' : m + 'm';
  }

  async function post(fields) {
    await postSettings(fields);
    await tick();
  }

  function tempLimits() {
    return targetLimits(s?.unit || 'F');
  }

  function applyTarget() {
    if (!targetInputEl) return;
    const limits = tempLimits();
    targetInputEl.value = coerceDecimal(targetInputEl.value, limits);
    if (decimalError(targetInputEl.value, limits, 'Setpoint')) return;
    post({ target: targetInputEl.value });
  }

  function onTargetInput() {
    if (!targetInputEl) return;
    targetInputEl.value = filterDecimalInput(targetInputEl.value);
  }

  function nudge(delta) {
    if (!targetInputEl) return;
    const limits = tempLimits();
    const v = parseFloat(coerceDecimal(targetInputEl.value || String(limits.fallback), limits)) + delta;
    targetInputEl.value = coerceDecimal(v, limits);
    applyTarget();
  }

  function selectProfile() {
    post({ profile: profileSelectEl.value });
  }

  let offArmed = $state(false);
  let offArmTimer = null;

  function disarmOff() {
    offArmed = false;
    clearTimeout(offArmTimer);
    offArmTimer = null;
  }

  function setMode(mode) {
    disarmOff();
    post({ mode });
  }

  // OFF is destructive, so require an explicit second tap within 3s
  // (same idea as the dial's confirm step).
  function setModeOff() {
    if (s?.mode === 'OFF') {
      disarmOff();
      return;
    }
    if (!offArmed) {
      offArmed = true;
      clearTimeout(offArmTimer);
      offArmTimer = setTimeout(() => {
        offArmed = false;
        offArmTimer = null;
      }, 3000);
      return;
    }
    setMode('OFF');
  }

  function startDrest() {
    if (!dRestTargetInputEl || !dRestHoursInputEl) return;
    const limits = tempLimits();
    dRestTargetInputEl.value = coerceDecimal(dRestTargetInputEl.value, limits);
    dRestHoursInputEl.value = coerceDecimal(dRestHoursInputEl.value, DREST_HOURS_LIMITS);
    const err =
      decimalError(dRestTargetInputEl.value, limits, 'Rest temp') ||
      decimalError(dRestHoursInputEl.value, DREST_HOURS_LIMITS, 'Duration hours');
    if (err) return;
    post({
      dRestAction: 'start',
      dRestTarget: dRestTargetInputEl.value,
      dRestHours: dRestHoursInputEl.value,
      dRestReturnProfile: dRestReturnSelectEl.value,
    });
  }

  function onDrestTargetInput() {
    if (!dRestTargetInputEl) return;
    dRestTargetInputEl.value = filterDecimalInput(dRestTargetInputEl.value);
  }

  function onDrestHoursInput() {
    if (!dRestHoursInputEl) return;
    dRestHoursInputEl.value = filterIntegerInput(dRestHoursInputEl.value);
  }

  function endDrest() {
    post({ dRestAction: 'end' });
  }

  function num(value) {
    const n = parseFloat(value);
    return Number.isFinite(n) ? n : null;
  }

  function liveWindowLabel(history) {
    const intervalMs = history?.intervalMs || 60000;
    const capacity = history?.capacity || 720;
    const hours = (intervalMs * capacity) / 3600000;
    if (hours >= 2) return `Live (last ${Math.round(hours)}h)`;
    const minutes = (intervalMs * capacity) / 60000;
    return `Live (last ${Math.round(minutes)}m)`;
  }

  function liveSamples(history, clock) {
    const temps = history?.tempsC || [];
    const gravity = history?.gravity || [];
    const hydroTemps = history?.hydroTempsC || [];
    const intervalMs = history?.intervalMs || 60000;
    const count = Math.max(temps.length, gravity.length, hydroTemps.length);
    const wallClock = clock?.wallClock ?? true;
    const anchorMs = (clock?.seconds ?? Math.floor(Date.now() / 1000)) * 1000;
    const samples = [];
    for (let i = 0; i < count; i++) {
      const ageMs = (count - 1 - i) * intervalMs;
      samples.push({
        tempC: num(temps[i]),
        gravity: num(gravity[i]),
        hydroTempC: num(hydroTemps[i]),
        atMs: anchorMs - ageMs,
        wallClock,
      });
    }
    return samples;
  }

  function csvSamples(text) {
    const lines = (text || '').trim().split(/\r?\n/);
    const samples = [];
    for (let i = 1; i < lines.length; i++) {
      const row = lines[i].split(',');
      if (row.length < 5) continue;
      const atSec = num(row[0]);
      const sample = {
        tempC: num(row[2]),
        gravity: num(row[4]),
        hydroTempC: num(row[10]),
        wallClock: row[1] === '1',
        atMs: atSec != null ? atSec * 1000 : null,
      };
      if (sample.tempC != null || sample.gravity != null || sample.hydroTempC != null) {
        samples.push(sample);
      }
    }
    return samples;
  }

  function hasGraphSeries(samples) {
    return samples.some(
      (p) => p.tempC != null || p.gravity != null || p.hydroTempC != null,
    );
  }

  async function tick() {
    const next = await getStatus();
    s = next;
    document.body.dataset.state = next.state.toLowerCase();
    document.title = next.fermenterName + ' - FermentDial';

    // Mode may go OFF from dial/API; drop a stale arm state.
    if (next.mode === 'OFF' && offArmed) {
      disarmOff();
    }

    if (document.activeElement !== profileSelectEl) {
      profileSelectEl.value = String(next.activeProfile);
    }
    if (document.activeElement !== targetInputEl) {
      targetInputEl.value = next.target.toFixed(1);
    }

    const r = next.diacetylRest || {};
    if (document.activeElement !== dRestTargetInputEl) {
      dRestTargetInputEl.value = (r.target || 70).toFixed(1);
    }
    if (document.activeElement !== dRestHoursInputEl) {
      dRestHoursInputEl.value = String(Math.round(r.durationHours || 48));
    }
    if (document.activeElement !== dRestReturnSelectEl) {
      dRestReturnSelectEl.value = String(r.returnProfile || 0);
    }
  }

  async function loadHistory() {
    try {
      const live = await getHistory();
      spark = live.tempsC || [];
      graphSamples = liveSamples(live, s?.clock);
      graphSource = 'live';
      graphSourceLabel = liveWindowLabel(live);

      try {
        const persisted = csvSamples(await getHistoryCsv());
        if (persisted.length >= 1 && hasGraphSeries(persisted)) {
          graphSamples = persisted;
          graphSource = 'csv';
          graphSourceLabel = 'Fermentation history';
        }
      } catch (e) {
        // live history remains available until CSV has samples
      }
    } catch (e) {
      // ignore transient fetch failures
    }
  }

  $effect(() => {
    let cancelled = false;
    async function bootstrap() {
      await tick();
      if (!cancelled) {
        await loadHistory();
      }
    }
    bootstrap();
    const tickTimer = setInterval(tick, 2000);
    const historyTimer = setInterval(loadHistory, 30000);
    return () => {
      cancelled = true;
      clearInterval(tickTimer);
      clearInterval(historyTimer);
      clearTimeout(offArmTimer);
      offArmTimer = null;
    };
  });

  const unit = $derived(s ? deg + s.unit : '');
  const rest = $derived(s?.diacetylRest || {});
  const hydro = $derived(s?.hydrometer || {});
  const prog = $derived(s?.program || {});
  const PROG_STEP_TYPES = ['Hold', 'Ramp', 'Crash', 'D-Rest', 'Manual wait'];
  const PROG_EXIT_TYPES = [
    'time',
    'gravity target',
    'gravity stable',
    'velocity',
    'manual',
  ];

  function progStop() {
    post({ programAction: 'stop' });
  }
  function progSkip() {
    post({ programAction: 'skip' });
  }

  async function resetFerment() {
    await postSettings({ fermentReset: 1 });
    await tick();
    await loadHistory();
  }
  const dRestStatusText = $derived.by(() => {
    if (!s) return 'Ready';
    const r = rest;
    if (r.active) {
      return (
        'Active - ' +
        remainingText(r.remainingSeconds) +
        ' remaining, then ' +
        (r.returnProfileName || 'profile')
      );
    }
    return (
      'Ready - holds ' +
      Math.round(r.durationHours || 48) +
      'h at ' +
      (r.target || 70).toFixed(1) +
      unit
    );
  });
</script>

<main>
  <div class="shell">
    <div class="top">
      <div>
        <div class="brand">Ferment<span>Dial</span></div>
        <div class="deviceName">{s ? s.fermenterName : 'Fermenter'}</div>
      </div>
      <div class="statusbar">
        <span class="pill">{s ? (s.wifiConnected ? s.ip : s.wifiStatus) : 'Wi-Fi'}</span>
        <span class="pill demo" hidden={!s?.demo}>DEMO SENSOR</span>
        <Menu page="dashboard" />
      </div>
    </div>

    <section class="hero">
      <Sparkline data={spark} />
      <div class="heroTop">
        <span>{s ? s.fermenterName : 'Fermenter'}</span>
        <span>
          {s ? (rest.active ? 'D-rest' : 'target') + ' ' + s.target.toFixed(1) + unit : 'target --.-F'}
        </span>
      </div>
      <div class="readout">
        <div class="tempBlock">
          <div class="tempLine">
            <span class="temp">{s && s.tempValid ? s.temperature.toFixed(1) : '--.-'}</span>
            <span class="unit">{unit || 'F'}</span>
          </div>
          <div class="state">{s ? s.state : 'Loading'}</div>
          <div class="sub">
            {#if !s}
              Waiting for controller status
            {:else if !s.tempValid}
              Sensor fault - outputs forced off
            {:else if rest.active}
              D-rest {remainingText(rest.remainingSeconds)} remaining
            {:else}
              {formatMode(s.mode)} mode
            {/if}
          </div>
        </div>
        <div class="outputs">
          <div class="out heat" class:on={s?.heater}>{s?.heater ? 'HEATER ON' : 'HEATER OFF'}</div>
          <div class="out cool" class:on={s?.pump}>{s?.pump ? 'PUMP ON' : 'PUMP OFF'}</div>
        </div>
      </div>
    </section>

    <section class="grid">
      <div class="card"><div class="label">Fermenter</div><div class="value">{s ? s.fermenterName : 'Fermenter'}</div></div>
      <div class="card"><div class="label">Profile</div><div class="value">{s ? (rest.active ? 'D-Rest' : s.profileName) : 'Ale'}</div></div>
      <div class="card"><div class="label">Setpoint</div><div class="value"><span>{s ? s.target.toFixed(1) : '--.-'}</span><span class="unit">{unit || 'F'}</span></div></div>
      <div class="card"><div class="label">Mode</div><div class="value">{formatMode(s?.mode)}</div></div>
    </section>

    <section class="controls">
      <div class="panel">
        <h2>Mode</h2>
        <div class="modes">
          <button
            class="danger"
            class:active={s?.mode === 'OFF'}
            class:armed={offArmed}
            onclick={setModeOff}
          >{offArmed ? 'TAP TO CONFIRM' : 'OFF'}</button>
          <button class:active={s?.mode === 'AUTO'} onclick={() => setMode('AUTO')}>AUTO</button>
          <button class="heat" class:active={s?.mode === 'HEAT_ONLY'} onclick={() => setMode('HEAT_ONLY')}>HEAT</button>
          <button class="cool" class:active={s?.mode === 'COOL_ONLY'} onclick={() => setMode('COOL_ONLY')}>COOL</button>
        </div>
      </div>
      <div class="panel">
        <h2>Profile</h2>
        <select bind:this={profileSelectEl} onchange={selectProfile}>
          {#each s?.profiles || [] as p}
            <option value={p.index}>{p.name}</option>
          {/each}
        </select>
        <div class="fieldLabel" style="margin-top:12px">Setpoint</div>
        <div class="targetCtl" style="margin-top:6px">
          <button onclick={() => nudge(-0.1)}>-</button>
          <input
            bind:this={targetInputEl}
            inputmode="decimal"
            step="0.1"
            oninput={onTargetInput}
            onchange={applyTarget}
            onblur={applyTarget}
          />
          <button onclick={() => nudge(0.1)}>+</button>
        </div>
        <p class="sub" style="font-size:12px;margin-top:10px">
          Pick a profile to recall its preset, or nudge the live setpoint. Edit presets in
          <a href="/settings#profiles">Settings</a>.
        </p>
      </div>
      <div class="panel">
        <h2>Diacetyl Rest</h2>
        <div class="row">
          <label class="fieldLabel">Rest temp<input
            bind:this={dRestTargetInputEl}
            inputmode="decimal"
            step="0.1"
            oninput={onDrestTargetInput}
            onblur={() => {
              if (dRestTargetInputEl) {
                dRestTargetInputEl.value = coerceDecimal(dRestTargetInputEl.value, tempLimits());
              }
            }}
          /></label>
          <label class="fieldLabel">Hours<input
            bind:this={dRestHoursInputEl}
            inputmode="numeric"
            step="24"
            min="24"
            max="96"
            oninput={onDrestHoursInput}
            onblur={() => {
              if (dRestHoursInputEl) {
                dRestHoursInputEl.value = coerceDecimal(
                  dRestHoursInputEl.value,
                  DREST_HOURS_LIMITS,
                );
              }
            }}
          /></label>
        </div>
        <label class="fieldLabel" style="margin-top:8px">
          After rest
          <select bind:this={dRestReturnSelectEl}>
            {#each s?.profiles || [] as p}
              <option value={p.index}>{p.name}</option>
            {/each}
          </select>
        </label>
        <div class="modes" style="margin-top:10px">
          <button class="primary" onclick={startDrest}>START</button>
          <button disabled={!rest.active} onclick={endDrest}>STOP</button>
        </div>
        <p class="sub" style="font-size:12px;margin-top:10px">{dRestStatusText}</p>
      </div>
      <div class="panel" hidden={!prog.active}>
        <h2>Program</h2>
        <div class="fieldLabel">
          {s?.profileName} &middot; step {(prog.stepIndex || 0) + 1}/{prog.stepCount || 0}
        </div>
        <div class="value" style="margin-top:6px;font-size:18px">
          {PROG_STEP_TYPES[prog.stepType] || 'Step'}
          {#if prog.stepTarget != null}&rarr; {prog.stepTarget.toFixed(1)}{unit}{/if}
        </div>
        <p class="sub" style="font-size:12px;margin-top:6px">
          {#if prog.stepExit === 0}
            {remainingText(prog.stepRemainingSeconds)} left
          {:else}
            advances on {PROG_EXIT_TYPES[prog.stepExit] || 'condition'}
          {/if}
        </p>
        <div class="modes" style="margin-top:10px">
          <button onclick={progSkip}>SKIP</button>
          <button class="danger" onclick={progStop}>STOP</button>
        </div>
        <p class="sub" style="font-size:12px;margin-top:10px">
          Build programs in <a href="/settings#programs">Settings</a>.
        </p>
      </div>
    </section>

    <section class="grid" hidden={!hydro.valid}>
      <div class="card"><div class="label">Hydrometer</div><div class="value">{hydro.label || 'Hydrometer'}</div></div>
      <div class="card"><div class="label">Gravity</div><div class="value">{hydro.valid ? 'SG ' + hydro.gravity.toFixed(3) : '--.--'}</div></div>
      <div class="card"><div class="label">Temp</div><div class="value">{hydro.valid ? hydro.temperature.toFixed(1) + (unit || '') : '--.-'}</div></div>
      <div class="card"><div class="label">ABV</div><div class="value">{hydro.valid && hydro.abv != null ? hydro.abv.toFixed(1) + '%' : '--.-%'}</div></div>
    </section>

    <section class="panel historyPanel">
      <div class="panelHead">
        <h2>Fermentation History</h2>
        <span>{graphSourceLabel}</span>
        {#if s?.demo}
          <button type="button" class="btnCompact danger" onclick={resetFerment}>
            Reset ferment
          </button>
        {/if}
      </div>
      <HydrometerChart
        samples={graphSamples}
        unit={s?.unit || 'F'}
      />
    </section>

    <EventLog clock={s?.clock} uptimeSeconds={s?.uptimeSeconds} />

    <section class="panel dashFooter">
      <div class="dashFooterRow">
        <div class="dashFooterStat">
          <span class="label">Fault</span>
          <span
            class="dashFooterFault"
            class:ok={!s?.fault || s.fault === 'NONE'}
            class:bad={!!s?.fault && s.fault !== 'NONE'}
          >{s ? s.fault : 'NONE'}</span>
        </div>
        <span class="dashFooterUptime">{s ? remainingText(s.uptimeSeconds) + ' uptime' : '-- uptime'}</span>
        <a class="dashFooterLink" href="/api/history.csv" download>Download history CSV</a>
      </div>
    </section>
  </div>
</main>
