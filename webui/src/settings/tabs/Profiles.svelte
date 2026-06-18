<script>
  import { postSettings } from '../../api.js';

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

  function resetProfile(i) {
    const def = status.profiles[i]?.default;
    if (def != null) targets[i] = def.toFixed(1);
  }

  async function saveProfiles() {
    profilesStatus = 'Saving...';
    const fields = {};
    status.profiles.forEach((p, i) => {
      fields['profile' + i + 'Name'] = names[i];
      fields['profile' + i + 'Target'] = targets[i];
    });
    await postSettings(fields);
    await refresh();
    profilesStatus = 'Saved.';
  }

  async function saveCrashGradual() {
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

<section class="panel">
  <h2>Fermentation Profiles</h2>
  <p class="hint">Each profile holds a name and a target temperature until you switch profiles.</p>
  <div class="profileRows" style="margin-top:10px">
    <div class="profileRow profileHeader"><span>Name</span><span>Target</span><span></span></div>
    {#each status.profiles as p, i}
      <div class="profileRow">
        <input maxlength="15" bind:value={names[i]} />
        <input inputmode="decimal" step="0.1" bind:value={targets[i]} />
        <button class="reset" title="Reset to default" onclick={() => resetProfile(i)}>&#8634;</button>
      </div>
    {/each}
  </div>
  <button class="primary" onclick={saveProfiles}>Save profiles</button>
  <div class="saveStatus">{profilesStatus}</div>
</section>

<section class="panel">
  <h2>Cold Crash</h2>
  <p class="hint">
    Selecting Crash on the Dial always prompts Direct or Gradual. Gradual steps the live setpoint
    down instead of jumping straight to the Crash target, to avoid shocking the yeast.
  </p>
  <label><input type="checkbox" bind:checked={gradualEnabled} />Gradual is currently active</label>
  <div class="thresholds" style="margin-top:10px">
    <label>Step size<input inputmode="decimal" step="0.1" min="0.1" bind:value={gradualStep} /></label>
    <label>Step interval (hours)<input inputmode="numeric" step="1" min="1" bind:value={gradualStepHours} /></label>
  </div>
  <button class="primary" onclick={saveCrashGradual}>Save cold crash</button>
  <div class="saveStatus">{crashStatus}</div>
</section>

<section class="panel">
  <h2>Diacetyl Rest</h2>
  <p class="hint">
    Defaults used by the dashboard and dial. Typical rests are 24-48 hours at 70-72 {deg}{status.unit}.
  </p>
  <div class="thresholds" style="margin-top:10px">
    <label>Rest temp<input inputmode="decimal" step="0.1" bind:value={dRestTarget} /></label>
    <label>Duration hours<input inputmode="numeric" step="24" min="24" max="96" bind:value={dRestHours} /></label>
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
</section>
