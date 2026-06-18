<script>
  import { postSettings } from '../../api.js';

  let { status, refresh } = $props();

  let coolOn = $state('0.5');
  let heatOn = $state('0.5');
  let hold = $state('0.3');
  let offset = $state('0.0');
  let controllerStatus = $state('');

  $effect(() => {
    coolOn = status.coolOn.toFixed(1);
    heatOn = status.heatOn.toFixed(1);
    hold = status.hold.toFixed(1);
    offset = status.tempOffset.toFixed(1);
  });

  async function saveControl() {
    controllerStatus = 'Saving...';
    await postSettings({ coolOn, heatOn, hold, tempOffset: offset });
    await refresh();
    controllerStatus = 'Saved.';
  }
</script>

<section class="panel">
  <h2>Regulation</h2>
  <p class="hint">How tightly the controller holds the setpoint. Values are in {status.unit}.</p>
  <div class="thresholds" style="margin-top:10px">
    <label>Cool on<input inputmode="decimal" step="0.1" bind:value={coolOn} /></label>
    <label>Heat on<input inputmode="decimal" step="0.1" bind:value={heatOn} /></label>
    <label>Hold band<input inputmode="decimal" step="0.1" bind:value={hold} /></label>
    <label>Sensor offset<input inputmode="decimal" step="0.1" bind:value={offset} /></label>
  </div>
  <button class="primary" onclick={saveControl}>Save controller settings</button>
  <div class="saveStatus">{controllerStatus}</div>
</section>
