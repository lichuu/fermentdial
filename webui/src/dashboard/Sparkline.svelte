<script>
  let { data = [] } = $props();
  let canvas;

  function draw() {
    const c = canvas;
    if (!c) return;
    const w = c.clientWidth;
    const h = c.clientHeight;
    if (!w || !h) return;
    const dpr = window.devicePixelRatio || 1;
    if (c.width !== Math.round(w * dpr)) c.width = Math.round(w * dpr);
    if (c.height !== Math.round(h * dpr)) c.height = Math.round(h * dpr);
    const g = c.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const d = data;
    if (!d || d.length < 2) return;
    let lo = Math.min.apply(null, d);
    let hi = Math.max.apply(null, d);
    if (hi - lo < 0.5) {
      const m = (hi + lo) / 2;
      lo = m - 0.5;
      hi = m + 0.5;
    }
    const pad = 8;
    const top = h * 0.42; // keep the trace in the lower part, behind the text
    const X = (i) => (i / (d.length - 1)) * w;
    const Y = (v) => h - pad - ((v - lo) / (hi - lo)) * (h - pad - top);
    g.beginPath();
    d.forEach((v, i) => {
      const px = X(i);
      const py = Y(v);
      i ? g.lineTo(px, py) : g.moveTo(px, py);
    });
    const area = new Path2D();
    d.forEach((v, i) => {
      const px = X(i);
      const py = Y(v);
      i ? area.lineTo(px, py) : area.moveTo(px, py);
    });
    area.lineTo(w, h);
    area.lineTo(0, h);
    area.closePath();
    const fg = g.createLinearGradient(0, top, 0, h);
    fg.addColorStop(0, 'rgba(46,213,160,0.16)');
    fg.addColorStop(1, 'rgba(46,213,160,0)');
    g.fillStyle = fg;
    g.fill(area);
    const lg = g.createLinearGradient(0, 0, w, 0);
    lg.addColorStop(0, 'rgba(46,213,160,0.28)');
    lg.addColorStop(1, 'rgba(127,230,200,0.28)');
    g.strokeStyle = lg;
    g.lineWidth = 1.5;
    g.lineJoin = 'round';
    g.lineCap = 'round';
    g.stroke();
  }

  $effect(() => {
    data;
    draw();
  });

  function onResize() {
    draw();
  }
</script>

<svelte:window onresize={onResize} />
<canvas id="spark" bind:this={canvas}></canvas>
