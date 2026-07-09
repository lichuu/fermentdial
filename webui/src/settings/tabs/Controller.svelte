<script>
  import { postSettings } from '../../api.js';
  import {
    coerceDecimal,
    decimalError,
    deltaLimits,
    filterDecimalInput,
    showDecimalInvalid,
    offsetLimits,
    calibrationRefLimits,
  } from '../../validation.js';

  let { status, refresh } = $props();

  let coolOn = $state('0.5');
  let heatOn = $state('0.5');
  let hold = $state('0.3');
  let offset = $state('0.0');
  let controllerStatus = $state('');

  let calRef = $state('32.0');
  let calStatus = $state('');

  $effect(() => {
    coolOn = status.coolOn.toFixed(1);
    heatOn = status.heatOn.toFixed(1);
    hold = status.hold.toFixed(1);
    offset = status.tempOffset.toFixed(1);
    // Default ice point in the active unit when unit changes.
    if (status.unit === 'C') {
      if (calRef === '32.0' || calRef === '32') calRef = '0.0';
    } else if (calRef === '0.0' || calRef === '0') {
      calRef = '32.0';
    }
  });

  const bandLimits = $derived(deltaLimits(status.unit));
  const sensorOffsetLimits = $derived(offsetLimits(status.unit));
  const refLimits = $derived(calibrationRefLimits(status.unit));
  const deg = String.fromCharCode(176);

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

  async function applyCalibration() {
    calRef = coerceDecimal(calRef, refLimits);
    const err = decimalError(calRef, refLimits, 'Reference temp');
    if (err) {
      calStatus = err;
      return;
    }
    if (!status.tempValid) {
      calStatus = 'No valid sensor reading.';
      return;
    }
    calStatus = 'Calibrating...';
    const wanted = parseFloat(calRef);
    try {
      // Response is fresh status JSON (prop may lag until after refresh).
      const next = await postSettings({ calibrateRef: calRef });
      await refresh();
      const live = next.tempValid ? next.temperature : NaN;
      const unit = next.unit || status.unit;
      if (Number.isFinite(live) && Number.isFinite(wanted) && Math.abs(live - wanted) > 0.15) {
        calStatus =
          'Offset applied (clamped). Live is ' +
          live.toFixed(1) +
          deg +
          unit +
          ', not ' +
          wanted.toFixed(1) +
          deg +
          unit +
          '.';
      } else {
        calStatus =
          'Offset set so reading matches ' + calRef + deg + unit + '.';
      }
    } catch (e) {
      calStatus = 'Calibration failed.';
    }
  }

  function setIcePoint() {
    calRef = status.unit === 'C' ? '0.0' : '32.0';
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
      <label>Cool above<input
        inputmode="decimal"
        step="0.1"
        class:inputInvalid={showDecimalInvalid(coolOn, bandLimits)}
        value={coolOn}
        oninput={(e) => { coolOn = filterDecimalInput(e.currentTarget.value); }}
        onblur={() => { coolOn = coerceDecimal(coolOn, bandLimits); }}
      /></label>
      <label>Heat below<input
        inputmode="decimal"
        step="0.1"
        class:inputInvalid={showDecimalInvalid(heatOn, bandLimits)}
        value={heatOn}
        oninput={(e) => { heatOn = filterDecimalInput(e.currentTarget.value); }}
        onblur={() => { heatOn = coerceDecimal(heatOn, bandLimits); }}
      /></label>
      <label>In range<input
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
    <p class="formHint">Cool starts this far above the live setpoint; heat this far below. In-range is the hold band around target.</p>
    <button class="primary" onclick={saveControl}>Save controller settings</button>
    <div class="saveStatus">{controllerStatus}</div>
  </article>

  <article class="stackedRow">
    <div class="stackedRowTop">
      <div class="stackedRowTitle"><h3>Single-point calibration</h3></div>
    </div>
    <p class="formHint">
      Put the probe in a known bath (ice water is ~{status.unit === 'C' ? '0' : '32'}{deg}{status.unit}),
      wait for a stable reading, then set the reference. Offset becomes
      <code>reference − raw</code> so the live display matches the bath.
    </p>
    <p class="formHint">
      Live {status.tempValid ? status.temperature.toFixed(1) : '—'}{deg}{status.unit}
      · raw {status.rawTemperature != null && status.tempValid ? Number(status.rawTemperature).toFixed(1) : '—'}{deg}{status.unit}
      · offset {status.tempOffset.toFixed(1)}
    </p>
    <div class="thresholds">
      <label>Reference temp<input
        inputmode="decimal"
        step="0.1"
        class:inputInvalid={showDecimalInvalid(calRef, refLimits)}
        value={calRef}
        oninput={(e) => { calRef = filterDecimalInput(e.currentTarget.value, { allowNegative: true }); }}
        onblur={() => { calRef = coerceDecimal(calRef, refLimits); }}
      /></label>
    </div>
    <div class="modes" style="margin-top:8px">
      <button type="button" class="btnSecondary" onclick={setIcePoint}>Ice point</button>
      <button type="button" class="primary" onclick={applyCalibration}>Apply calibration</button>
    </div>
    <div class="saveStatus">{calStatus}</div>
  </article>
</section>
