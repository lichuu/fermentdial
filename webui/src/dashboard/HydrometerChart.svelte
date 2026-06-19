<script>
  let { samples = [], unit = 'F', source = 'live' } = $props();
  let canvas;

  const deg = String.fromCharCode(176);

  function finiteNumber(value) {
    return typeof value === 'number' && Number.isFinite(value);
  }

  function displayTemp(tempC) {
    return unit === 'F' ? tempC * 9 / 5 + 32 : tempC;
  }

  function extent(values, minSpan) {
    let lo = Math.min(...values);
    let hi = Math.max(...values);
    if (hi - lo < minSpan) {
      const mid = (hi + lo) / 2;
      lo = mid - minSpan / 2;
      hi = mid + minSpan / 2;
    }
    return { lo, hi };
  }

  function drawLine(g, points, xFor, yFor, color, width = 2) {
    let started = false;
    g.beginPath();
    points.forEach((value, i) => {
      if (!finiteNumber(value)) {
        started = false;
        return;
      }
      const x = xFor(i);
      const y = yFor(value);
      if (started) {
        g.lineTo(x, y);
      } else {
        g.moveTo(x, y);
        started = true;
      }
    });
    g.strokeStyle = color;
    g.lineWidth = width;
    g.lineJoin = 'round';
    g.lineCap = 'round';
    g.stroke();
  }

  function drawLabel(g, text, x, y, color, align = 'left') {
    g.fillStyle = color;
    g.textAlign = align;
    g.textBaseline = 'middle';
    g.fillText(text, x, y);
  }

  function draw() {
    const c = canvas;
    if (!c) return;
    const w = c.clientWidth;
    const h = c.clientHeight;
    if (!w || !h) return;

    const dpr = window.devicePixelRatio || 1;
    const nextW = Math.round(w * dpr);
    const nextH = Math.round(h * dpr);
    if (c.width !== nextW) c.width = nextW;
    if (c.height !== nextH) c.height = nextH;

    const g = c.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    g.font = '12px "Trebuchet MS", "Avenir Next", Verdana, sans-serif';

    const data = samples || [];
    const gravity = data.map((p) => p.gravity);
    const controllerTemps = data.map((p) =>
      finiteNumber(p.tempC) ? displayTemp(p.tempC) : null
    );
    const hydroTemps = data.map((p) =>
      finiteNumber(p.hydroTempC) ? displayTemp(p.hydroTempC) : null
    );
    const gravityValues = gravity.filter(finiteNumber);
    const hydroTempValues = hydroTemps.filter(finiteNumber);
    const tempValues = controllerTemps.concat(hydroTemps).filter(finiteNumber);

    if (data.length < 2 || (!gravityValues.length && !hydroTempValues.length)) {
      g.fillStyle = 'rgba(176, 216, 248, 0.72)';
      g.textAlign = 'center';
      g.textBaseline = 'middle';
      g.font = '13px "Trebuchet MS", "Avenir Next", Verdana, sans-serif';
      g.fillText('Waiting for hydrometer history', w / 2, h / 2);
      return;
    }

    const padL = 52;
    const padR = 52;
    const padT = 16;
    const padB = 28;
    const plotW = Math.max(1, w - padL - padR);
    const plotH = Math.max(1, h - padT - padB);
    const tempScale = tempValues.length ? extent(tempValues, 2) : { lo: 0, hi: 1 };
    const gravityScale = gravityValues.length
      ? extent(gravityValues, 0.008)
      : { lo: 1, hi: 1.008 };
    const xFor = (i) => padL + (data.length < 2 ? 0 : (i / (data.length - 1)) * plotW);
    const yTemp = (v) =>
      padT + (1 - (v - tempScale.lo) / (tempScale.hi - tempScale.lo)) * plotH;
    const yGravity = (v) =>
      padT + (1 - (v - gravityScale.lo) / (gravityScale.hi - gravityScale.lo)) * plotH;

    g.strokeStyle = 'rgba(176, 216, 248, 0.14)';
    g.lineWidth = 1;
    for (let i = 0; i <= 4; i++) {
      const y = padT + (plotH / 4) * i;
      g.beginPath();
      g.moveTo(padL, y);
      g.lineTo(w - padR, y);
      g.stroke();
    }
    g.strokeStyle = 'rgba(176, 216, 248, 0.24)';
    g.strokeRect(padL, padT, plotW, plotH);

    drawLine(g, controllerTemps, xFor, yTemp, '#b0d8f8', 2);
    drawLine(g, hydroTemps, xFor, yTemp, '#36c87a', 2);
    drawLine(g, gravity, xFor, yGravity, '#e44840', 2.2);

    g.font = '11px "Trebuchet MS", "Avenir Next", Verdana, sans-serif';
    drawLabel(g, gravityScale.hi.toFixed(3), 8, padT, '#eaa0a0');
    drawLabel(g, gravityScale.lo.toFixed(3), 8, padT + plotH, '#eaa0a0');
    drawLabel(g, tempScale.hi.toFixed(1) + deg + unit, w - 8, padT, '#b0d8f8', 'right');
    drawLabel(g, tempScale.lo.toFixed(1) + deg + unit, w - 8, padT + plotH, '#b0d8f8', 'right');

    const label = source === 'csv' ? 'CSV history' : 'Live recent history';
    drawLabel(g, label, padL, h - 10, 'rgba(208, 232, 240, 0.62)');
  }

  $effect(() => {
    samples;
    unit;
    source;
    draw();
  });

  function onResize() {
    draw();
  }
</script>

<svelte:window onresize={onResize} />
<div class="chartWrap">
  <div class="chartLegend">
    <span><i class="sg"></i>SG</span>
    <span><i class="beer"></i>Controller temp</span>
    <span><i class="hydro"></i>Hydrometer temp</span>
  </div>
  <canvas class="hydroChart" bind:this={canvas}></canvas>
</div>
