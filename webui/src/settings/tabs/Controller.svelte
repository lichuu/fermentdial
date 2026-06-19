<script>
  import { postSettings } from '../../api.js';
  import {
    coerceDecimal,
    decimalError,
    deltaLimits,
    filterDecimalInput,
    showDecimalInvalid,
    offsetLimits,
  } from '../../validation.js';

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

  const bandLimits = $derived(deltaLimits(status.unit));
  const sensorOffsetLimits = $derived(offsetLimits(status.unit));

  async function saveControl() {
    coolOn = coerceDecimal(coolOn, bandLimits);
    heatOn = coerceDecimal(heatOn, bandLimits);
    hold = coerceDecimal(hold, bandLimits);
    offset = coerceDecimal(offset, sensorOffsetLimits);
    const err =
      decimalError(coolOn, bandLimits, 'Cool on') ||
      decimalError(heatOn, bandLimits, 'Heat on') ||
      decimalError(hold, bandLimits, 'Hold band') ||
      decimalError(offset, sensorOffsetLimits, 'Sensor offset');
    if (err) {
      controllerStatus = err;
      return;
    }
    controllerStatus = 'Saving...';
    await postSettings({ coolOn, heatOn, hold, tempOffset: offset });
    await refresh();
    controllerStatus = 'Saved.';
  }
</script>

<section class="stackedCard">
  <header class="stackedCardHeader">
    <h2>Controller</h2>
    <p class="stackedCardDesc">How tightly the controller holds the setpoint. Values are in {status.unit}.</p>
  </header>

  <article class="stackedRow">
    <div class="stackedRowTop">
      <div class="stackedRowTitle"><h3>Regulation</h3></div>
    </div>
    <div class="thresholds">
      <label>Cool on<input
        inputmode="decimal"
        step="0.1"
        class:inputInvalid={showDecimalInvalid(coolOn, bandLimits)}
        value={coolOn}
        oninput={(e) => { coolOn = filterDecimalInput(e.currentTarget.value); }}
        onblur={() => { coolOn = coerceDecimal(coolOn, bandLimits); }}
      /></label>
      <label>Heat on<input
        inputmode="decimal"
        step="0.1"
        class:inputInvalid={showDecimalInvalid(heatOn, bandLimits)}
        value={heatOn}
        oninput={(e) => { heatOn = filterDecimalInput(e.currentTarget.value); }}
        onblur={() => { heatOn = coerceDecimal(heatOn, bandLimits); }}
      /></label>
      <label>Hold band<input
        inputmode="decimal"
        step="0.1"
        class:inputInvalid={showDecimalInvalid(hold, bandLimits)}
        value={hold}
        oninput={(e) => { hold = filterDecimalInput(e.currentTarget.value); }}
        onblur={() => { hold = coerceDecimal(hold, bandLimits); }}
      /></label>
      <label>Sensor offset<input
        inputmode="decimal"
        step="0.1"
        class:inputInvalid={showDecimalInvalid(offset, sensorOffsetLimits)}
        value={offset}
        oninput={(e) => { offset = filterDecimalInput(e.currentTarget.value, { allowNegative: true }); }}
        onblur={() => { offset = coerceDecimal(offset, sensorOffsetLimits); }}
      /></label>
    </div>
    <button class="primary" onclick={saveControl}>Save controller settings</button>
    <div class="saveStatus">{controllerStatus}</div>
  </article>
</section>