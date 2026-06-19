<script>
  import { getProgram, postSettings } from '../../api.js';
  import {
    coerceDecimal,
    decimalError,
    filterDecimalInput,
    filterIntegerInput,
    GRAVITY_LIMITS,
    HOURS_LIMITS,
    showDecimalInvalid,
    STABLE_HOURS_LIMITS,
    targetLimits,
  } from '../../validation.js';

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
  let dragFrom = $state(null);
  let dragOver = $state(null);
  let importInputs = [];

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

  function reorderStep(pi, from, to) {
    if (from === to || from < 0 || to < 0 || from >= progs[pi].steps.length) return;
    const arr = progs[pi].steps.slice();
    const [item] = arr.splice(from, 1);
    arr.splice(to, 0, item);
    progs[pi].steps = arr;
  }

  function onDragStart(pi, si, e) {
    dragFrom = { pi, si };
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/plain', `${pi}:${si}`);
  }

  function onDragOver(pi, si, e) {
    e.preventDefault();
    e.dataTransfer.dropEffect = 'move';
    dragOver = { pi, si };
  }

  function onDrop(pi, si, e) {
    e.preventDefault();
    if (dragFrom && dragFrom.pi === pi) {
      reorderStep(pi, dragFrom.si, si);
    }
    clearDrag();
  }

  function clearDrag() {
    dragFrom = null;
    dragOver = null;
  }

  const tempLimits = $derived(targetLimits(unit));

  function normalizeStep(step) {
    step.target = coerceDecimal(step.target, tempLimits);
    step.hours = coerceDecimal(step.hours, HOURS_LIMITS);
    if (step.exit === 1 || step.exit === 3) {
      step.gravity = coerceDecimal(step.gravity, GRAVITY_LIMITS);
    }
    if (step.exit === 2) {
      step.stableHours = coerceDecimal(step.stableHours, STABLE_HOURS_LIMITS);
    }
  }

  function validateSteps(steps) {
    const errors = [];
    steps.forEach((step, i) => {
      const label = 'Step ' + (i + 1);
      const targetErr = decimalError(step.target, tempLimits, label + ' target');
      if (targetErr) errors.push(targetErr);
      const hoursErr = decimalError(step.hours, HOURS_LIMITS, label + ' ramp/hold hours');
      if (hoursErr) errors.push(hoursErr);
      if (step.exit === 1 || step.exit === 3) {
        const gravityErr = decimalError(step.gravity, GRAVITY_LIMITS, label + ' gravity');
        if (gravityErr) errors.push(gravityErr);
      }
      if (step.exit === 2) {
        const stableErr = decimalError(
          step.stableHours,
          STABLE_HOURS_LIMITS,
          label + ' stable hours',
        );
        if (stableErr) errors.push(stableErr);
      }
    });
    return errors;
  }

  function prepareProgram(prog) {
    prog.steps.forEach(normalizeStep);
    return validateSteps(prog.steps);
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
    const errors = prepareProgram(progs[pi]);
    if (errors.length > 0) {
      saveStatus = errors[0];
      return;
    }
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
    const errors = prepareProgram(progs[pi]);
    if (errors.length > 0) {
      saveStatus = errors[0];
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
      progs[pi].steps = steps.slice(0, maxSteps).map((s) => {
        const step = {
          type: s.type | 0,
          exit: s.exit | 0,
          target: String(s.target ?? '68.0'),
          hours: String(s.hours ?? '24'),
          gravity: String(s.gravity ?? '1.010'),
          stableHours: String(s.stableHours ?? '24'),
        };
        normalizeStep(step);
        return step;
      });
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

<section class="stackedCard">
  <header class="stackedCardHeader">
    <h2>Programs</h2>
    <p class="stackedCardDesc">
      The two Custom slots run as multi-step programs. Each step holds or ramps to a
      target and advances on time, gravity, or a manual nudge. Editing here only
      changes the saved program; press Start to run it.
    </p>
  </header>

{#each progs as prog, pi}
  <article class="stackedRow">
    <div class="stackedRowTop">
      <div class="stackedRowTitle">
        <h3>{prog.name}</h3>
        {#if isRunning(pi)}<span class="runBadge">running &middot; step {run.stepIndex + 1}/{run.stepCount}</span>{/if}
      </div>
    </div>

    <div class="stepRows" role="list">
      <div class="stepRow stepHead">
        <span></span><span>Type</span><span>Target {deg}{unit}</span>
        <span>Ramp/hold h</span><span>Advance when</span><span>Condition</span><span></span>
      </div>
      {#each prog.steps as step, si}
        <div
          class="stepRow"
          role="listitem"
          class:activeStep={isRunning(pi) && run.stepIndex === si}
          class:dragOver={dragOver?.pi === pi && dragOver?.si === si}
          class:dragging={dragFrom?.pi === pi && dragFrom?.si === si}
          ondragover={(e) => onDragOver(pi, si, e)}
          ondrop={(e) => onDrop(pi, si, e)}
          ondragleave={() => {
            if (dragOver?.pi === pi && dragOver?.si === si) dragOver = null;
          }}
        >
          <button
            type="button"
            class="dragHandle"
            title="Drag to reorder"
            draggable="true"
            ondragstart={(e) => onDragStart(pi, si, e)}
            ondragend={clearDrag}
          >
            <span class="dragGrip" aria-hidden="true">
              <span></span><span></span><span></span><span></span><span></span><span></span>
            </span>
          </button>
          <select bind:value={step.type}>
            {#each STEP_TYPES as t, ti}<option value={ti}>{t}</option>{/each}
          </select>
          <input
            inputmode="decimal"
            step="0.1"
            class:inputInvalid={showDecimalInvalid(step.target, tempLimits)}
            value={step.target}
            oninput={(e) => {
              step.target = filterDecimalInput(e.currentTarget.value);
            }}
            onblur={() => {
              step.target = coerceDecimal(step.target, tempLimits);
            }}
          />
          <input
            inputmode="decimal"
            step="0.5"
            min="0"
            class:inputInvalid={showDecimalInvalid(step.hours, HOURS_LIMITS)}
            value={step.hours}
            oninput={(e) => {
              step.hours = filterDecimalInput(e.currentTarget.value);
            }}
            onblur={() => {
              step.hours = coerceDecimal(step.hours, HOURS_LIMITS);
            }}
          />
          <select bind:value={step.exit}>
            {#each EXIT_TYPES as x, xi}<option value={xi}>{x}</option>{/each}
          </select>
          <div class="condBox" class:condPassive={step.exit === 0 || step.exit === 4}>
            {#if step.exit === 1}
              <span class="condLabel">SG</span>
              <span class="condOp">&le;</span>
              <input
                class="condValue"
                class:inputInvalid={showDecimalInvalid(step.gravity, GRAVITY_LIMITS)}
                inputmode="decimal"
                step="0.001"
                value={step.gravity}
                oninput={(e) => {
                  step.gravity = filterDecimalInput(e.currentTarget.value);
                }}
                onblur={() => {
                  step.gravity = coerceDecimal(step.gravity, GRAVITY_LIMITS);
                }}
              />
            {:else if step.exit === 2}
              <span class="condLabel">Stable</span>
              <input
                class="condValue condValueWide"
                class:inputInvalid={showDecimalInvalid(step.stableHours, STABLE_HOURS_LIMITS)}
                inputmode="numeric"
                step="1"
                min="1"
                value={step.stableHours}
                oninput={(e) => {
                  step.stableHours = filterIntegerInput(e.currentTarget.value);
                }}
                onblur={() => {
                  step.stableHours = coerceDecimal(step.stableHours, STABLE_HOURS_LIMITS);
                }}
              />
              <span class="condUnit">h</span>
            {:else if step.exit === 3}
              <span class="condLabel">Vel</span>
              <span class="condOp">&le;</span>
              <input
                class="condValue"
                class:inputInvalid={showDecimalInvalid(step.gravity, GRAVITY_LIMITS)}
                inputmode="decimal"
                step="0.001"
                value={step.gravity}
                oninput={(e) => {
                  step.gravity = filterDecimalInput(e.currentTarget.value);
                }}
                onblur={() => {
                  step.gravity = coerceDecimal(step.gravity, GRAVITY_LIMITS);
                }}
              />
            {:else if step.exit === 0}
              <span class="condHint">Ramp/hold timer</span>
            {:else}
              <span class="condHint">Manual confirm</span>
            {/if}
          </div>
          <span class="rowBtns">
            <button type="button" title="Move up" aria-label="Move step up" disabled={si === 0} onclick={() => reorderStep(pi, si, si - 1)}>&#8593;</button>
            <button type="button" title="Move down" aria-label="Move step down" disabled={si === prog.steps.length - 1} onclick={() => reorderStep(pi, si, si + 1)}>&#8595;</button>
            <button type="button" title="Duplicate" onclick={() => dupStep(pi, si)}>&#10697;</button>
            <button type="button" title="Remove" class="danger" onclick={() => removeStep(pi, si)}>&times;</button>
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
      <button type="button" onclick={() => importInputs[pi].click()}>Import</button>
      <input type="file" class="importInput" accept="application/json" bind:this={importInputs[pi]} onchange={(e) => importProg(pi, e)} />
    </div>
  </article>
{/each}
</section>

<div class="saveStatus">{saveStatus}</div>

<style>
  .stepRows { margin-top: 8px; display: flex; flex-direction: column; gap: 6px; }
  .stepRow {
    display: grid;
    /* Fixed action column — per-row `auto` was narrower on the header row and shifted labels. */
    grid-template-columns: 32px 1.2fr 0.9fr 0.9fr 1.2fr 1.4fr 132px;
    gap: 6px;
    align-items: center;
  }
  .stepHead { font-size: 0.72rem; color: var(--muted); font-weight: 600; letter-spacing: 0.02em; }
  .stepHead span:not(:first-child):not(:last-child) { text-align: center; }
  .stepRow input, .stepRow select { width: 100%; min-width: 0; margin-top: 0; }
  .stepRow input.inputInvalid { border-color: var(--fault); }
  .condBox {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 6px;
    min-height: 42px;
    padding: 6px 10px;
    background: var(--face);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) - 3px);
    min-width: 0;
  }
  .condBox.condPassive {
    background: transparent;
    border-style: dashed;
  }
  .condLabel {
    color: var(--muted);
    font-size: 0.68rem;
    font-weight: 600;
    letter-spacing: 0.04em;
    text-transform: uppercase;
    white-space: nowrap;
  }
  .condOp {
    color: var(--accent);
    font-weight: 700;
    line-height: 1;
  }
  .condUnit {
    color: var(--muted);
    font-size: 0.78rem;
    font-weight: 600;
  }
  .condValue {
    width: 4.6em !important;
    min-width: 4.6em;
    margin: 0 !important;
    padding: 7px 6px !important;
    text-align: center;
    font-weight: 600;
    font-size: 0.88rem;
  }
  .condValueWide { width: 3.2em !important; min-width: 3.2em; }
  .condHint {
    color: var(--muted);
    font-size: 0.78rem;
    line-height: 1.25;
    text-align: center;
  }
  .activeStep { outline: 2px solid var(--ok); border-radius: calc(var(--radius) - 3px); }
  .stepRow.dragOver { outline: 1px dashed var(--primary); border-radius: calc(var(--radius) - 3px); }
  .stepRow.dragging { opacity: 0.45; }
  .dragHandle {
    display: flex;
    align-items: center;
    justify-content: center;
    width: 32px;
    height: 32px;
    margin: 0;
    padding: 0;
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) - 3px);
    background: var(--panel2);
    color: var(--muted);
    cursor: grab;
    touch-action: none;
  }
  .dragHandle:hover { color: var(--text); }
  .dragHandle:active { cursor: grabbing; color: var(--primary); }
  .dragGrip {
    display: grid;
    grid-template-columns: repeat(2, 4px);
    gap: 3px;
  }
  .dragGrip span {
    width: 4px;
    height: 4px;
    border-radius: 50%;
    background: currentColor;
  }
  .rowBtns { display: flex; gap: 4px; }
  .rowBtns button { padding: 5px 9px; margin: 0; line-height: 1; }
  .rowBtns button.danger, .progActions button.danger { color: var(--fault); }
  .progActions { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin-top: 14px; }
  .progActions .spacer { flex: 1; }
  .importInput { display: none; }
  .runBadge {
    font-size: 0.7rem; margin-left: 8px; padding: 2px 9px;
    font-weight: 600; letter-spacing: 0.02em;
    color: #04140d;
    background: #2a6;
    border-radius: 10px; vertical-align: middle;
  }
</style>