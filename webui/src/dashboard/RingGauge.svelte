<script>
  let { pct = null, label = '--', stroke = 'var(--gold)' } = $props();

  const RING_C = 2 * Math.PI * 40;
  const fill = $derived(pct != null ? Math.min(Math.max(pct, 0), 100) : 0);
</script>

<div class="ringGauge">
  <svg viewBox="0 0 96 96" aria-hidden="true">
    <circle class="ringTrack" cx="48" cy="48" r="40" />
    {#if fill > 0}
      <circle
        class="ringFill"
        cx="48"
        cy="48"
        r="40"
        style:stroke
        stroke-dasharray="{(RING_C * fill) / 100} {RING_C}"
        transform="rotate(-90 48 48)"
      />
    {/if}
  </svg>
  <span class="ringLabel">{label}</span>
</div>
