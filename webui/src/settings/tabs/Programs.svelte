<script>
  import { getProgram, postSettings } from '../../api.js';

  let { status, refresh } = $props();
  const deg = String.fromCharCode(176);

  const STEP_TYPES = ['Hold', 'Ramp', 'Crash', 'D-Rest', 'Manual wait'];
  const EXIT_TYPES = [
    'Time',
    'Gravity below',
    'Gravity stable',
    'Velocity below',
    'Manual',
  ];

  let progs = $state([]);
  let unit = $state('F');
  let maxSteps = $state(12);
  let saveStatus = $state('');

  function blankStep() {
    return {
      type: 0,
      exit: 0,
      target: '68.0',
      hours: '24',
      gravity: '1.010',
      stableHours: '24',
    };
  }

  async function load() {
    const data = await getProgram();
    unit = data.unit;
    maxSteps = data.maxSteps;
    progs = data.programs.map((p) => ({
      index: p.index,
      slot: p.slot,
      name: p.name,
      steps: p.steps.map((s) => ({
        type: s.type,
        exit: s.exit,
        target: s.target == null ? '68.0' : s.target.toFixed(1),
        hours: String(+(s.durationSeconds / 3600).toFixed(2)),
        gravity: (s.gravityThreshold || 1.01).toFixed(3),
        stableHours: String(s.stableHours),
      })),
    }));
  }

  $effect(() => {
    load();
  });

  function addStep(pi) {
    if (progs[pi].steps.length >= maxSteps) return;
    progs[pi].steps = [...progs[pi].steps, blankStep()];
  }
  function removeStep(pi, si) {
    progs[pi].steps = progs[pi].steps.filter((_, i) => i !== si);
  }
  function dupStep(pi, si) {
    if (progs[pi].steps.length >= maxSteps) return;
    const c = { ...progs[pi].steps[si] };
    progs[pi].steps = [
      ...progs[pi].steps.slice(0, si + 1),
      c,
      ...progs[pi].steps.slice(si + 1),
    ];
  }
  function move(pi, si, dir) {
    const j = si + dir;
    if (j < 0 || j >= progs[pi].steps.length) return;
    const arr = progs[pi].steps.slice();
    [arr[si], arr[j]] = [arr[j], arr[si]];
    progs[pi].steps = arr;
  }

  function encode(prog) {
    return prog.steps
      .map((s) =>
        [
          s.type,
          s.exit,
          s.target,
          Math.round(parseFloat(s.hours || '0') * 3600),
          s.gravity,
          s.stableHours,
        ].join(','),
      )
      .join(';');
  }

  async function save(pi) {
    saveStatus = 'Saving ' + progs[pi].name + '...';
    await postSettings({
      programSaveSlot: progs[pi].index,
      programSteps: encode(progs[pi]),
    });
    await load();
    await refresh();
    saveStatus = 'Saved ' + progs[pi].name + '.';
  }

  async function start(pi) {
    if (progs[pi].steps.length === 0) {
      saveStatus = 'Save a step before starting.';
      return;
    }
    await postSettings({ programAction: 'start', programSlot: progs[pi].index });
    await refresh();
  }
  async function stop() {
    await postSettings({ programAction: 'stop' });
    await refresh();
  }
  async function skip() {
    await postSettings({ programAction: 'skip' });
    await refresh();
  }

  function exportProg(pi) {
    const blob = new Blob([JSON.stringify(progs[pi], null, 2)], {
      type: 'application/json',
    });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = (progs[pi].name || 'program').replace(/\s+/g, '_') + '.json';
    a.click();
    URL.revokeObjectURL(url);
  }
  async function importProg(pi, ev) {
    const file = ev.target.files[0];
    if (!file) return;
    try {
      const obj = JSON.parse(await file.text());
      const steps = Array.isArray(obj.steps) ? obj.steps : obj;
      progs[pi].steps = steps.slice(0, maxSteps).map((s) => ({
        type: s.type | 0,
        exit: s.exit | 0,
        target: String(s.target ?? '68.0'),
        hours: String(s.hours ?? '24'),
        gravity: String(s.gravity ?? '1.010'),
        stableHours: String(s.stableHours ?? '24'),
      }));
      saveStatus = 'Imported into ' + progs[pi].name + ' (not yet saved).';
    } catch (e) {
      saveStatus = 'Import failed: ' + e.message;
    }
    ev.target.value = '';
  }

  let run = $derived(status?.program || {});
  function isRunning(pi) {
    return run.active && run.runIndex === pi;
  }
</script>

<section class="panel">
  <h2>Fermentation Programs</h2>
  <p class="hint">
    The two Custom slots run as multi-step programs. Each step holds or ramps to a
    target and advances on time, gravity, or a manual nudge. Editing here only
    changes the saved program; press Start to run it.
  </p>
</section>

{#each progs as prog, pi}
  <section class="panel">
    <h2>
      {prog.name}
      {#if isRunning(pi)}<span class="runBadge">running &middot; step {run.stepIndex + 1}/{run.stepCount}</span>{/if}
    </h2>

    <div class="stepRows">
      <div class="stepRow stepHead">
        <span></span><span>Type</span><span>Target {deg}{unit}</span>
        <span>Ramp/hold h</span><span>Advance when</span><span>Condition</span><span></span>
      </div>
      {#each prog.steps as step, si}
        <div class="stepRow" class:activeStep={isRunning(pi) && run.stepIndex === si}>
          <span class="ord">{si + 1}</span>
          <select bind:value={step.type}>
            {#each STEP_TYPES as t, ti}<option value={ti}>{t}</option>{/each}
          </select>
          <input inputmode="decimal" step="0.1" bind:value={step.target} />
          <input inputmode="decimal" step="0.5" min="0" bind:value={step.hours} />
          <select bind:value={step.exit}>
            {#each EXIT_TYPES as x, xi}<option value={xi}>{x}</option>{/each}
          </select>
          <span class="cond">
            {#if step.exit === 1}
              SG &le; <input inputmode="decimal" step="0.001" bind:value={step.gravity} />
            {:else if step.exit === 2}
              stable <input inputmode="numeric" step="1" min="1" bind:value={step.stableHours} />h
            {:else if step.exit === 3}
              |v| &le; <input inputmode="decimal" step="0.001" bind:value={step.gravity} />
            {:else if step.exit === 0}
              after {step.hours || 0}h
            {:else}
              manual
            {/if}
          </span>
          <span class="rowBtns">
            <button title="Move up" onclick={() => move(pi, si, -1)}>&#8593;</button>
            <button title="Move down" onclick={() => move(pi, si, 1)}>&#8595;</button>
            <button title="Duplicate" onclick={() => dupStep(pi, si)}>&#10697;</button>
            <button title="Remove" class="danger" onclick={() => removeStep(pi, si)}>&times;</button>
          </span>
        </div>
      {/each}
      {#if prog.steps.length === 0}
        <p class="hint">No steps yet. Add one to build this program.</p>
      {/if}
    </div>

    <div class="progActions">
      <button onclick={() => addStep(pi)} disabled={prog.steps.length >= maxSteps}>+ Add step</button>
      <button class="primary" onclick={() => save(pi)}>Save {prog.name}</button>
      {#if isRunning(pi)}
        <button onclick={skip}>Skip step</button>
        <button class="danger" onclick={stop}>Stop</button>
      {:else}
        <button onclick={() => start(pi)}>Start</button>
      {/if}
      <span class="spacer"></span>
      <button onclick={() => exportProg(pi)}>Export</button>
      <label class="importBtn">Import<input type="file" accept="application/json" onchange={(e) => importProg(pi, e)} /></label>
    </div>
  </section>
{/each}

<div class="saveStatus">{saveStatus}</div>

<style>
  .stepRows { margin-top: 8px; display: flex; flex-direction: column; gap: 6px; }
  .stepRow {
    display: grid;
    grid-template-columns: 28px 1.2fr 0.9fr 0.9fr 1.2fr 1.4fr auto;
    gap: 6px;
    align-items: center;
  }
  .stepHead { font-size: 0.75rem; opacity: 0.6; }
  .stepRow input, .stepRow select { width: 100%; min-width: 0; }
  .cond { display: flex; align-items: center; gap: 4px; font-size: 0.8rem; }
  .cond input { width: 5.5em; }
  .ord { opacity: 0.6; text-align: center; }
  .activeStep { outline: 2px solid #3a8; border-radius: 6px; }
  .rowBtns { display: flex; gap: 2px; }
  .rowBtns button { padding: 2px 6px; }
  .rowBtns button.danger, .progActions button.danger { color: #f66; }
  .progActions { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin-top: 12px; }
  .progActions .spacer { flex: 1; }
  .importBtn { cursor: pointer; }
  .importBtn input { display: none; }
  .runBadge {
    font-size: 0.7rem; margin-left: 8px; padding: 2px 8px;
    background: #2a6; border-radius: 10px; vertical-align: middle;
  }
</style>
