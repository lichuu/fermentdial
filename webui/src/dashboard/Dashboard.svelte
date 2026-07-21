<script>
  import Menu from '../shared/Menu.svelte';
  import RingGauge from './RingGauge.svelte';
  import Sparkline from './Sparkline.svelte';
  import HydrometerChart from './HydrometerChart.svelte';
  import EventLog from './EventLog.svelte';
  import { formatMode, getStatus, getHistory, getHistoryCsv, postSettings, releaseTagUrl } from '../api.js';
  import {
    coerceDecimal,
    decimalError,
    DREST_HOURS_LIMITS,
    filterDecimalInput,
    filterIntegerInput,
    targetLimits,
  } from '../validation.js';

  const deg = String.fromCharCode(176);
  const HISTORY_PREF_KEY = 'fermentdial.historySource';

  let s = $state(null);
  let spark = $state([]);
  let graphSamples = $state([]);
  let graphSource = $state('live');
  let graphSourceLabel = $state('Live');
  /** User preference: 'live' | 'csv'. Live is the default; Full (csv) only
      when explicitly chosen. Legacy stored 'auto' resolves to live. */
  let historyPref = $state(
    typeof localStorage !== 'undefined'
      ? localStorage.getItem(HISTORY_PREF_KEY) || 'live'
      : 'live',
  );
  let liveGraphSamples = $state([]);
  let csvGraphSamples = $state([]);
  let liveWindowText = $state('Live');

  let offline = $state(false);
  let failStreak = $state(0);
  let banner = $state('');
  let bannerTimer = null;

  let profileSelectEl;
  let targetInputEl;
  let dRestTargetInputEl;
  let dRestHoursInputEl;
  let dRestReturnSelectEl;

  function showBanner(msg, ms = 3500) {
    banner = msg;
    clearTimeout(bannerTimer);
    if (ms > 0) {
      bannerTimer = setTimeout(() => {
        banner = '';
        bannerTimer = null;
      }, ms);
    }
  }

  function remainingText(sec) {
    sec = Math.max(0, sec || 0);
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    return h > 0 ? h + 'h ' + m + 'm' : m + 'm';
  }

  async function post(fields) {
    if (offline) {
      showBanner("Can't reach controller");
      return;
    }
    try {
      await postSettings(fields);
      await tick();
    } catch (e) {
      // Banner is the user-facing signal; do not rethrow (avoids unhandled rejection).
      showBanner('Save failed — check connection');
    }
  }

  const ATTN_LABELS = {
    fault: 'Sensor or interlock fault',
    'hydro-stale': 'Hydrometer stale',
    'not-reaching-target': 'Temp off target too long',
    'long-runtime': 'Heater/pump on over 4h',
  };
  function attentionLabel(slug) {
    return ATTN_LABELS[slug] || slug;
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
    try {
      const next = await getStatus();
      s = next;
      failStreak = 0;
      offline = false;
      document.body.dataset.state = next.state.toLowerCase();
      document.title = next.fermenterName + ' - FermentDial';

      // Mode may go OFF from dial/API; drop a stale arm state.
      if (next.mode === 'OFF' && offArmed) {
        disarmOff();
      }

      if (profileSelectEl && document.activeElement !== profileSelectEl) {
        profileSelectEl.value = String(next.activeProfile);
      }
      if (targetInputEl && document.activeElement !== targetInputEl) {
        targetInputEl.value = next.target.toFixed(1);
      }

      const r = next.diacetylRest || {};
      if (dRestTargetInputEl && document.activeElement !== dRestTargetInputEl) {
        dRestTargetInputEl.value = (r.target || 70).toFixed(1);
      }
      if (dRestHoursInputEl && document.activeElement !== dRestHoursInputEl) {
        dRestHoursInputEl.value = String(Math.round(r.durationHours || 48));
      }
      if (dRestReturnSelectEl && document.activeElement !== dRestReturnSelectEl) {
        dRestReturnSelectEl.value = String(r.returnProfile || 0);
      }
    } catch (e) {
      failStreak += 1;
      if (failStreak >= 2) {
        offline = true;
      }
    }
  }

  function applyHistoryPreference() {
    const hasCsv =
      csvGraphSamples.length >= 1 && hasGraphSeries(csvGraphSamples);
    let use = historyPref === 'auto' ? 'live' : historyPref;
    if (use === 'csv' && hasCsv) {
      graphSamples = csvGraphSamples;
      graphSource = 'csv';
      graphSourceLabel = 'Fermentation history';
    } else {
      graphSamples = liveGraphSamples;
      graphSource = 'live';
      graphSourceLabel = liveWindowText;
    }
  }

  function setHistoryPref(pref) {
    historyPref = pref;
    try {
      localStorage.setItem(HISTORY_PREF_KEY, pref);
    } catch (e) {
      /* private mode */
    }
    applyHistoryPreference();
  }

  async function loadHistory() {
    try {
      const live = await getHistory();
      spark = live.tempsC || [];
      liveGraphSamples = liveSamples(live, s?.clock);
      liveWindowText = liveWindowLabel(live);

      try {
        const persisted = csvSamples(await getHistoryCsv());
        csvGraphSamples = persisted;
      } catch (e) {
        csvGraphSamples = [];
      }
      applyHistoryPreference();
    } catch (e) {
      // keep last chart on transient failure
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
      clearTimeout(bannerTimer);
      offArmTimer = null;
    };
  });

  const unit = $derived(s ? deg + s.unit : '');
  const rest = $derived(s?.diacetylRest || {});
  const hydro = $derived(s?.hydrometer || {});
  // Firmware floors attenuation at 0; past 100 the ring simply stays full
  // while the label keeps the true value. Older firmware has no attenuation
  // field, so fall back to computing it from OG and current gravity.
  const attnPct = $derived.by(() => {
    if (!hydro.valid) return null;
    if (hydro.attenuation != null) return hydro.attenuation;
    const og = hydro.originalGravity;
    if (og == null || !(og > 1.0) || hydro.gravity == null) return null;
    return Math.max(0, ((og - hydro.gravity) / (og - 1.0)) * 100);
  });
  // Temperature gauge: ring fill is distance from target, saturating at
  // TEMP_GAUGE_SPAN display degrees; heat/cool color by direction.
  const TEMP_GAUGE_SPAN = 2.0;
  const tempDelta = $derived(
    hydro.valid && s ? hydro.temperature - s.target : null,
  );
  const tempDeltaText = $derived(
    tempDelta == null
      ? '--' + deg
      : (tempDelta >= 0 ? '+' : '-') + Math.abs(tempDelta).toFixed(1) + deg,
  );
  const tempGaugePct = $derived(
    tempDelta == null
      ? null
      : (Math.min(Math.abs(tempDelta), TEMP_GAUGE_SPAN) / TEMP_GAUGE_SPAN) * 100,
  );
  const tempStroke = $derived(
    tempDelta == null
      ? 'var(--muted)'
      : Math.abs(tempDelta) < 0.3
        ? 'var(--ok)'
        : tempDelta > 0
          ? 'var(--heat)'
          : 'var(--cool)',
  );
  const prog = $derived(s?.program || {});
  const heroSub = $derived.by(() => {
    if (!s) return 'Waiting for controller status';
    if (s.hintDetail) return s.hintDetail;
    if (!s.tempValid) return 'Sensor fault - outputs forced off';
    if (rest.active) return 'D-rest ' + remainingText(rest.remainingSeconds) + ' remaining';
    if (s.attentionText) return s.attentionText;
    if (s.attention?.length) {
      return s.attention.map(attentionLabel).join(' · ');
    }
    return formatMode(s.mode) + ' mode';
  });
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

  let batchNameInput = $state('');
  async function startBatch() {
    if (offline) {
      showBanner("Can't reach controller");
      return;
    }
    try {
      // Omit batchName when the field is empty so the previous name is kept
      // (input only shows it as placeholder — empty must not clear).
      const fields = { batchAction: 'new' };
      const name = (batchNameInput || '').trim();
      if (name) {
        fields.batchName = name;
      }
      await postSettings(fields);
      await tick();
      showBanner('Batch started');
    } catch (e) {
      showBanner('Batch start failed');
    }
  }

  function programExitText(p) {
    if (!p) return '';
    if (p.stepExit === 0) {
      return remainingText(p.stepRemainingSeconds) + ' left';
    }
    if (p.stepExit === 1 && p.stepGravity != null) {
      return 'Advances when SG ≤ ' + Number(p.stepGravity).toFixed(3);
    }
    if (p.stepExit === 2) {
      return 'Advances when SG stable ' + (p.stepStableHours || 24) + 'h';
    }
    if (p.stepExit === 3) {
      return 'Advances on gravity velocity';
    }
    if (p.stepExit === 4) {
      return 'Waits for manual skip';
    }
    return 'Advances on ' + (PROG_EXIT_TYPES[p.stepExit] || 'condition');
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
    {#if offline}
      <div class="offlineBanner" role="alert">Can't reach controller — controls paused</div>
    {:else if banner}
      <div class="offlineBanner offlineBanner-info" role="status">{banner}</div>
    {/if}

    <div class="top">
      <div>
        <div class="brand">Ferment<span>Dial</span></div>
        <div class="deviceName">
          {s ? (s.batchName || s.fermenterName) : 'Fermenter'}
          {#if s?.batchName}
            <span class="batchMeta"> · {s.fermenterName}</span>
          {/if}
        </div>
      </div>
      <div class="statusbar">
        <span class="pill">{s ? (s.wifiConnected ? s.ip : s.wifiStatus) : 'Wi-Fi'}</span>
        {#if s?.attention?.length}
          <span class="pill attention">{s.attentionText || attentionLabel(s.attention[0])}</span>
        {/if}
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
          <div class="state">{s ? s.hint || s.state : 'Loading'}</div>
          <div class="sub">{heroSub}</div>
        </div>
        <div class="outputs">
          <div class="out heat" class:on={s?.heater}>{s?.heater ? 'HEATER ON' : 'HEATER OFF'}</div>
          <div class="out cool" class:on={s?.pump}>{s?.pump ? 'PUMP ON' : 'PUMP OFF'}</div>
        </div>
      </div>
    </section>

    <section class="controls">
      <div class="panel">
        <h2>Control</h2>
        <div class="controlRow">
          <div>
            <div class="fieldLabel">Mode</div>
            <div class="modes modesRow" style="margin-top:6px">
              <button
                class="danger"
                class:active={s?.mode === 'OFF'}
                class:armed={offArmed}
                disabled={offline}
                onclick={setModeOff}
              >{offArmed ? 'CONFIRM' : 'OFF'}</button>
              <button class:active={s?.mode === 'AUTO'} disabled={offline} onclick={() => setMode('AUTO')}>AUTO</button>
              <button class="heat" class:active={s?.mode === 'HEAT_ONLY'} disabled={offline} onclick={() => setMode('HEAT_ONLY')}>HEAT</button>
              <button class="cool" class:active={s?.mode === 'COOL_ONLY'} disabled={offline} onclick={() => setMode('COOL_ONLY')}>COOL</button>
            </div>
          </div>
          <div>
            <div class="fieldLabel">Profile</div>
            <select
              bind:this={profileSelectEl}
              onchange={selectProfile}
              disabled={offline}
              style="margin-top:6px"
            >
              {#each s?.profiles || [] as p}
                <option value={p.index}>{p.name}</option>
              {/each}
            </select>
          </div>
          <div>
            <div class="fieldLabel">Live setpoint (this batch)</div>
            <div class="targetCtl" style="margin-top:6px">
              <button disabled={offline} onclick={() => nudge(-0.1)}>-</button>
              <input
                bind:this={targetInputEl}
                inputmode="decimal"
                step="0.1"
                disabled={offline}
                oninput={onTargetInput}
                onchange={applyTarget}
                onblur={applyTarget}
              />
              <button disabled={offline} onclick={() => nudge(0.1)}>+</button>
            </div>
          </div>
        </div>
      </div>
      <details class="panel">
        <summary>
          <h2>Diacetyl Rest</h2>
          <span class="summaryHint">
            {rest.active ? 'Active - ' + remainingText(rest.remainingSeconds) + ' left' : 'Ready'}
          </span>
        </summary>
        <div class="row" style="margin-top:12px">
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
      </details>
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
          {programExitText(prog)}
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

    <section class="grid hydroGrid" hidden={!hydro.selected}>
      <div class="card">
        <div class="label">Hydrometer</div>
        <div class="value">{hydro.label || 'Hydrometer'}</div>
        <div class="metaRows">
          <span>Type</span>
          <span>{hydro.type || '—'}{hydro.color ? ' · ' + hydro.color : ''}</span>
          <span>Signal</span>
          <span>{hydro.rssi ? hydro.rssi + ' dBm' : '—'}</span>
          <span>Battery</span>
          <span>{hydro.batteryV != null ? hydro.batteryV.toFixed(2) + ' V' : '—'}</span>
        </div>
      </div>
      <div class="card gaugeCard">
        <div class="label">Temperature</div>
        <div class="gaugeBody">
          <RingGauge pct={tempGaugePct} label={tempDeltaText} stroke={tempStroke} />
          <div class="gaugeReadout">
            <div class="gaugeVal">
              <span class="gaugeValNum">{hydro.valid ? hydro.temperature.toFixed(1) : '--.-'}</span>
              <span class="gaugeValUnit">{unit || 'F'}</span>
            </div>
            <div class="gaugeSub">
              {#if s}
                target <b>{s.target.toFixed(1)}{unit}</b>
                {#if tempDelta != null}
                  · <b>{tempDeltaText}{s.unit}</b> from target
                {/if}
              {/if}
            </div>
          </div>
        </div>
      </div>
      <div class="card gaugeCard">
        <div class="label">Attenuation</div>
        <div class="gaugeBody">
          <RingGauge
            pct={attnPct}
            label={attnPct != null ? Math.round(attnPct) + '%' : '--%'}
          />
          <div class="gaugeReadout">
            <div class="gaugeVal">
              <span class="gaugeValNum">{hydro.valid ? hydro.gravity.toFixed(3) : hydro.stale ? 'stale' : 'waiting'}</span>
              {#if hydro.valid}<span class="gaugeValUnit">SG</span>{/if}
            </div>
            <div class="gaugeSub">
              {#if hydro.originalGravity != null}
                from OG <b>{hydro.originalGravity.toFixed(3)}</b>
                {#if hydro.valid && hydro.abv != null}
                  · <b>{hydro.abv.toFixed(1)}%</b> ABV
                {/if}
              {:else}
                OG not recorded yet
              {/if}
            </div>
          </div>
        </div>
      </div>
    </section>

    <section class="panel historyPanel">
      <div class="panelHead">
        <h2>Fermentation History</h2>
        <div class="historyToggle" role="group" aria-label="History source">
          <button
            type="button"
            class:active={graphSource === 'live'}
            onclick={() => setHistoryPref('live')}
          >Live</button>
          <button
            type="button"
            class:active={graphSource === 'csv'}
            disabled={!(csvGraphSamples.length >= 1 && hasGraphSeries(csvGraphSamples))}
            onclick={() => setHistoryPref('csv')}
          >Full</button>
        </div>
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
        <a class="dashFooterLink" href="/api/export.json" download>Status snapshot</a>
      </div>
      <div class="dashFooterRow batchRow">
        <label class="fieldLabel" style="flex:1;min-width:140px;margin:0">
          Batch name
          <input
            bind:value={batchNameInput}
            maxlength="24"
            placeholder={s?.batchName || 'e.g. Pale Ale #3'}
            disabled={offline}
          />
        </label>
        <button type="button" class="btnCompact primary" disabled={offline} onclick={startBatch}>
          New batch
        </button>
      </div>
    </section>

    <footer class="buildFoot">
      FermentDial
      <a href={releaseTagUrl(s?.firmwareVersion)} target="_blank" rel="noreferrer"
        >v{s?.firmwareVersion || '—'}</a
      >
      · {(s?.firmwareGitSha || '').slice(0, 7) || '———————'}
    </footer>
  </div>
</main>
