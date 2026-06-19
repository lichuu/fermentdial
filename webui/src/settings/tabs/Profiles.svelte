<script>
  import { postSettings } from '../../api.js';
  import {
    coerceDecimal,
    decimalError,
    DREST_HOURS_LIMITS,
    filterDecimalInput,
    filterIntegerInput,
    gradualStepLimits,
    INTERVAL_HOURS_LIMITS,
    showDecimalInvalid,
    targetLimits,
  } from '../../validation.js';

  let { status, refresh } = $props();
  const deg = String.fromCharCode(176);

  let names = $state([]);
  let targets = $state([]);
  let profilesStatus = $state('');

  let gradualEnabled = $state(false);
  let gradualStep = $state('5.0');
  let gradualStepHours = $state('12');
  let crashStatus = $state('');

  let dRestTarget = $state('70.0');
  let dRestHours = $state('48');
  let dRestReturn = $state(0);
  let dRestStatus = $state('');

  $effect(() => {
    names = status.profiles.map((p) => p.name);
    targets = status.profiles.map((p) => p.target.toFixed(1));
    gradualEnabled = !!status.gradualCrashEnabled;
    gradualStep = (status.gradualCrashStep || 0).toFixed(1);
    gradualStepHours = String(status.gradualCrashStepHours || 12);
    const r = status.diacetylRest || {};
    dRestTarget = (r.target || 70).toFixed(1);
    dRestHours = String(Math.round(r.durationHours || 48));
    dRestReturn = r.returnProfile || 0;
  });

  const tempLimits = $derived(targetLimits(status.unit));
  const stepLimits = $derived(gradualStepLimits(status.unit));

  async function saveProfiles() {
    const errors = [];
    status.profiles.forEach((p, i) => {
      if (!p.editable) return;
      targets[i] = coerceDecimal(targets[i], tempLimits);
      const err = decimalError(targets[i], tempLimits, p.name + ' target');
      if (err) errors.push(err);
    });
    if (errors.length > 0) {
      profilesStatus = errors[0];
      return;
    }
    profilesStatus = 'Saving...';
    const fields = {};
    status.profiles.forEach((p, i) => {
      if (!p.editable) return;
      fields['profile' + i + 'Name'] = names[i];
      fields['profile' + i + 'Target'] = targets[i];
    });
    await postSettings(fields);
    await refresh();
    profilesStatus = 'Saved.';
  }

  async function saveCrashGradual() {
    gradualStep = coerceDecimal(gradualStep, stepLimits);
    gradualStepHours = coerceDecimal(gradualStepHours, INTERVAL_HOURS_LIMITS);
    const err =
      decimalError(gradualStep, stepLimits, 'Step size') ||
      decimalError(gradualStepHours, INTERVAL_HOURS_LIMITS, 'Step interval');
    if (err) {
      crashStatus = err;
      return;
    }
    crashStatus = 'Saving...';
    await postSettings({
      gradualCrashEnabled: gradualEnabled ? 1 : 0,
      gradualCrashStep: gradualStep,
      gradualCrashStepHours: gradualStepHours,
    });
    await refresh();
    crashStatus = 'Saved.';
  }

  async function saveDrestDefaults() {
    dRestTarget = coerceDecimal(dRestTarget, tempLimits);
    dRestHours = coerceDecimal(dRestHours, DREST_HOURS_LIMITS);
    const err =
      decimalError(dRestTarget, tempLimits, 'Rest temp') ||
      decimalError(dRestHours, DREST_HOURS_LIMITS, 'Duration hours');
    if (err) {
      dRestStatus = err;
      return;
    }
    dRestStatus = 'Saving...';
    await postSettings({
      dRestTarget,
      dRestHours,
      dRestReturnProfile: dRestReturn,
    });
    await refresh();
    dRestStatus = 'Saved.';
  }
</script>

<section class="stackedCard">
  <header class="stackedCardHeader">
    <h2>Profiles</h2>
    <p class="stackedCardDesc">Temperature presets, cold crash behavior, and diacetyl rest defaults.</p>
  </header>

  <article class="stackedRow">
    <div class="stackedRowTop">
      <div class="stackedRowTitle"><h3>Fermentation Profiles</h3></div>
    </div>
    <p class="formHint">
      Built-in profiles use fixed names and targets. Custom 1 and Custom 2 can be renamed
      and retargeted here.
    </p>
    <div class="profileRows">
      <div class="profileRow profileHeader"><span>Name</span><span>Target</span></div>
      {#each status.profiles as p, i}
        <div class="profileRow">
          {#if p.editable}
            <input maxlength="15" bind:value={names[i]} />
            <input
              inputmode="decimal"
              step="0.1"
              class:inputInvalid={showDecimalInvalid(targets[i], tempLimits)}
              value={targets[i]}
              oninput={(e) => {
                targets[i] = filterDecimalInput(e.currentTarget.value);
              }}
              onblur={() => {
                targets[i] = coerceDecimal(targets[i], tempLimits);
              }}
            />
          {:else}
            <span class="profileFixed">{names[i]}</span>
            <span class="profileFixed profileFixedTarget">{targets[i]}{deg}{status.unit}</span>
          {/if}
        </div>
      {/each}
    </div>
    <button class="primary" onclick={saveProfiles}>Save profiles</button>
    <div class="saveStatus">{profilesStatus}</div>
  </article>

  <article class="stackedRow">
    <div class="stackedRowTop">
      <div class="stackedRowTitle"><h3>Cold Crash</h3></div>
    </div>
    <p class="formHint">
      Selecting Crash on the Dial always prompts Direct or Gradual. Gradual steps the live setpoint
      down instead of jumping straight to the Crash target, to avoid shocking the yeast.
    </p>
    <label><input type="checkbox" bind:checked={gradualEnabled} />Gradual is currently active</label>
    <div class="thresholds">
      <label>Step size<input
        inputmode="decimal"
        step="0.1"
        min="0.1"
        class:inputInvalid={showDecimalInvalid(gradualStep, stepLimits)}
        value={gradualStep}
        oninput={(e) => { gradualStep = filterDecimalInput(e.currentTarget.value); }}
        onblur={() => { gradualStep = coerceDecimal(gradualStep, stepLimits); }}
      /></label>
      <label>Step interval (hours)<input
        inputmode="numeric"
        step="1"
        min="1"
        class:inputInvalid={showDecimalInvalid(gradualStepHours, INTERVAL_HOURS_LIMITS)}
        value={gradualStepHours}
        oninput={(e) => { gradualStepHours = filterIntegerInput(e.currentTarget.value); }}
        onblur={() => { gradualStepHours = coerceDecimal(gradualStepHours, INTERVAL_HOURS_LIMITS); }}
      /></label>
    </div>
    <button class="primary" onclick={saveCrashGradual}>Save cold crash</button>
    <div class="saveStatus">{crashStatus}</div>
  </article>

  <article class="stackedRow">
    <div class="stackedRowTop">
      <div class="stackedRowTitle"><h3>Diacetyl Rest</h3></div>
    </div>
    <p class="formHint">
      Defaults used by the dashboard and dial. Typical rests are 24-48 hours at 70-72 {deg}{status.unit}.
    </p>
    <div class="thresholds">
      <label>Rest temp<input
        inputmode="decimal"
        step="0.1"
        class:inputInvalid={showDecimalInvalid(dRestTarget, tempLimits)}
        value={dRestTarget}
        oninput={(e) => { dRestTarget = filterDecimalInput(e.currentTarget.value); }}
        onblur={() => { dRestTarget = coerceDecimal(dRestTarget, tempLimits); }}
      /></label>
      <label>Duration hours<input
        inputmode="numeric"
        step="24"
        min="24"
        max="96"
        class:inputInvalid={showDecimalInvalid(dRestHours, DREST_HOURS_LIMITS)}
        value={dRestHours}
        oninput={(e) => { dRestHours = filterIntegerInput(e.currentTarget.value); }}
        onblur={() => { dRestHours = coerceDecimal(dRestHours, DREST_HOURS_LIMITS); }}
      /></label>
    </div>
    <label>
      After rest
      <select bind:value={dRestReturn}>
        {#each status.profiles as p}
          <option value={p.index}>{p.name}</option>
        {/each}
      </select>
    </label>
    <button class="primary" onclick={saveDrestDefaults}>Save D-rest defaults</button>
    <div class="saveStatus">{dRestStatus}</div>
  </article>
</section>