<script>
  let { samples = [], unit = 'F' } = $props();
  let canvas;
  let chartWrap;

  let hoverIndex = $state(-1);
  let tooltipX = $state(0);
  let tooltipY = $state(0);
  let seriesVisible = $state({ sg: true, controller: true, hydro: true });

  const deg = String.fromCharCode(176);
  let layout = null;

  // Single source of truth per series: how to read it from a sample (`get`),
  // which y-axis it uses, how it renders (`color`/`width`), and how it formats
  // in the tooltip (`tipLabel`/`fmt`). draw(), the legend, and the tooltip all
  // loop this list — there is no per-series branching anywhere else.
  const SERIES = [
    {
      key: 'sg', label: 'SG', tipLabel: 'SG', swatch: 'sg',
      color: '#e44840', width: 2.2, axis: 'gravity',
      get: (p) => p.gravity,
      fmt: (v) => v.toFixed(3),
    },
    {
      key: 'controller', label: 'Controller temp', tipLabel: 'Controller', swatch: 'beer',
      color: '#b0d8f8', width: 2, axis: 'temp',
      get: (p) => (finiteNumber(p.tempC) ? displayTemp(p.tempC) : null),
      fmt: (v) => v.toFixed(1) + deg + unit,
    },
    {
      key: 'hydro', label: 'Hydrometer temp', tipLabel: 'Hydrometer', swatch: 'hydro',
      color: '#36c87a', width: 2, axis: 'temp',
      get: (p) => (finiteNumber(p.hydroTempC) ? displayTemp(p.hydroTempC) : null),
      fmt: (v) => v.toFixed(1) + deg + unit,
    },
  ];

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

  function timeSpanMs(data) {
    if (!data.length) return 0;
    const first = data[0]?.atMs;
    const last = data[data.length - 1]?.atMs;
    if (!finiteNumber(first) || !finiteNumber(last)) return 0;
    return Math.abs(last - first);
  }

  function formatAxisTime(sample, spanMs) {
    if (!finiteNumber(sample?.atMs)) return '';
    if (sample.wallClock) {
      const d = new Date(sample.atMs);
      if (spanMs > 86400000) {
        return d.toLocaleString(undefined, {
          month: 'short',
          day: 'numeric',
          hour: 'numeric',
          minute: '2-digit',
        });
      }
      return d.toLocaleTimeString(undefined, { hour: 'numeric', minute: '2-digit' });
    }
    const sec = Math.floor(sample.atMs / 1000);
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    if (h > 0) return `${h}h${String(m).padStart(2, '0')}m`;
    return `${m}m`;
  }

  function formatTooltipTime(sample) {
    if (!finiteNumber(sample?.atMs)) return 'Time unavailable';
    if (sample.wallClock) {
      return new Date(sample.atMs).toLocaleString(undefined, {
        weekday: 'short',
        month: 'short',
        day: 'numeric',
        hour: 'numeric',
        minute: '2-digit',
        second: '2-digit',
      });
    }
    const sec = Math.floor(sample.atMs / 1000);
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = sec % 60;
    if (h > 0) return `Uptime ${h}h ${m}m ${s}s`;
    if (m > 0) return `Uptime ${m}m ${s}s`;
    return `Uptime ${s}s`;
  }

  function axisTicks(count, maxTicks = 5) {
    if (count <= 1) return [0];
    const ticks = Math.min(maxTicks, count);
    const indices = [];
    for (let t = 0; t < ticks; t++) {
      indices.push(Math.round((t / (ticks - 1)) * (count - 1)));
    }
    return [...new Set(indices)].sort((a, b) => a - b);
  }

  function toggleSeries(key) {
    const next = { ...seriesVisible, [key]: !seriesVisible[key] };
    if (!next.sg && !next.controller && !next.hydro) {
      return;
    }
    seriesVisible = next;
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

  function drawDot(g, x, y, color, radius = 3.5) {
    g.beginPath();
    g.fillStyle = color;
    g.arc(x, y, radius, 0, Math.PI * 2);
    g.fill();
    g.strokeStyle = 'rgba(7, 16, 21, 0.85)';
    g.lineWidth = 1;
    g.stroke();
  }

  function drawLabel(g, text, x, y, color, align = 'left', baseline = 'middle') {
    g.fillStyle = color;
    g.textAlign = align;
    g.textBaseline = baseline;
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
    // Materialize each series' display values once; everything below loops these.
    const series = SERIES.map((s) => ({ ...s, values: data.map(s.get) }));
    const visible = series.filter((s) => seriesVisible[s.key]);
    const axisValues = (axis) =>
      visible
        .filter((s) => s.axis === axis)
        .flatMap((s) => s.values.filter(finiteNumber));

    layout = null;
    if (data.length < 2 || !visible.some((s) => s.values.some(finiteNumber))) {
      g.fillStyle = 'rgba(176, 216, 248, 0.72)';
      g.textAlign = 'center';
      g.textBaseline = 'middle';
      g.font = '13px "Trebuchet MS", "Avenir Next", Verdana, sans-serif';
      g.fillText('Waiting for fermentation history', w / 2, h / 2);
      return;
    }

    const gravityValues = axisValues('gravity');
    const tempValues = axisValues('temp');

    const padL = gravityValues.length ? 52 : 16;
    const padR = tempValues.length ? 52 : 16;
    const padT = 16;
    const padB = 42;
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
    const yForAxis = { gravity: yGravity, temp: yTemp };
    // Gravity drawn last so it sits on top, matching the original z-order.
    const drawOrder = [...visible].sort(
      (a, b) => (a.axis === 'gravity' ? 1 : 0) - (b.axis === 'gravity' ? 1 : 0)
    );
    const spanMs = timeSpanMs(data);

    layout = { padL, padR, padT, padB, plotW, plotH, data, xFor };

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

    for (const s of drawOrder) {
      drawLine(g, s.values, xFor, yForAxis[s.axis], s.color, s.width);
    }

    if (hoverIndex >= 0 && hoverIndex < data.length) {
      const x = xFor(hoverIndex);
      g.strokeStyle = 'rgba(176, 216, 248, 0.45)';
      g.lineWidth = 1;
      g.setLineDash([4, 4]);
      g.beginPath();
      g.moveTo(x, padT);
      g.lineTo(x, padT + plotH);
      g.stroke();
      g.setLineDash([]);

      for (const s of drawOrder) {
        const v = s.values[hoverIndex];
        if (finiteNumber(v)) drawDot(g, x, yForAxis[s.axis](v), s.color);
      }
    }

    g.font = '11px system-ui, -apple-system, "Segoe UI", Roboto, sans-serif';
    if (gravityValues.length) {
      drawLabel(g, gravityScale.hi.toFixed(3), 8, padT, '#eaa0a0');
      drawLabel(g, gravityScale.lo.toFixed(3), 8, padT + plotH, '#eaa0a0');
    }
    if (tempValues.length) {
      drawLabel(g, tempScale.hi.toFixed(1) + deg + unit, w - 8, padT, '#b0d8f8', 'right');
      drawLabel(g, tempScale.lo.toFixed(1) + deg + unit, w - 8, padT + plotH, '#b0d8f8', 'right');
    }

    const ticks = axisTicks(data.length);
    g.font = '10px system-ui, -apple-system, "Segoe UI", Roboto, sans-serif';
    ticks.forEach((idx, tickNum) => {
      const label = formatAxisTime(data[idx], spanMs);
      if (!label) return;
      const x = xFor(idx);
      const align = tickNum === 0 ? 'left' : tickNum === ticks.length - 1 ? 'right' : 'center';
      drawLabel(g, label, x, h - 8, 'rgba(208, 232, 240, 0.72)', align, 'bottom');
    });

  }

  function indexAtX(x) {
    if (!layout || layout.data.length < 2) return -1;
    const { padL, plotW, data } = layout;
    if (x < padL || x > padL + plotW) return -1;
    const frac = (x - padL) / plotW;
    return Math.max(0, Math.min(data.length - 1, Math.round(frac * (data.length - 1))));
  }

  function onChartMove(event) {
    if (!canvas || !chartWrap) return;
    const rect = canvas.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const next = indexAtX(x);
    if (next !== hoverIndex) {
      hoverIndex = next;
    }
    const wrapRect = chartWrap.getBoundingClientRect();
    tooltipX = event.clientX - wrapRect.left + 12;
    tooltipY = event.clientY - wrapRect.top - 8;
  }

  function onChartLeave() {
    if (hoverIndex !== -1) {
      hoverIndex = -1;
    }
  }

  const hoverSample = $derived(hoverIndex >= 0 ? samples[hoverIndex] : null);
  const hoverRows = $derived(
    hoverSample
      ? SERIES.filter((s) => seriesVisible[s.key]).map((s) => {
          const v = s.get(hoverSample);
          return {
            key: s.key,
            swatch: s.swatch,
            label: s.tipLabel,
            value: finiteNumber(v) ? s.fmt(v) : '--',
          };
        })
      : []
  );

  $effect(() => {
    samples;
    unit;
    hoverIndex;
    seriesVisible;
    draw();
  });

  $effect(() => {
    const wrap = chartWrap;
    if (!wrap || typeof ResizeObserver === 'undefined') {
      return;
    }
    const observer = new ResizeObserver(() => {
      draw();
    });
    observer.observe(wrap);
    return () => observer.disconnect();
  });

  function onResize() {
    draw();
  }
</script>

<svelte:window onresize={onResize} />
<div
  class="chartWrap"
  bind:this={chartWrap}
  role="img"
  aria-label="Hydrometer history chart"
  onmousemove={onChartMove}
  onmouseleave={onChartLeave}
>
  <div class="chartLegend" role="group" aria-label="Chart series">
    {#each SERIES as series (series.key)}
      <button
        type="button"
        class="chartLegendBtn"
        class:off={!seriesVisible[series.key]}
        aria-pressed={seriesVisible[series.key]}
        onclick={() => toggleSeries(series.key)}
      >
        <i class={series.swatch}></i>{series.label}
      </button>
    {/each}
  </div>
  <canvas class="hydroChart" bind:this={canvas}></canvas>
  {#if hoverSample}
    <div
      class="chartTooltip"
      style:left="{tooltipX}px"
      style:top="{tooltipY}px"
      role="status"
    >
      <div class="chartTooltipTime">{formatTooltipTime(hoverSample)}</div>
      {#each hoverRows as row (row.key)}
        <div class="chartTooltipRow {row.swatch}">
          <span>{row.label}</span>
          <strong>{row.value}</strong>
        </div>
      {/each}
    </div>
  {/if}
</div>
