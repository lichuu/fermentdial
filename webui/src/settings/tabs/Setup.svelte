<script>
  import { getSelfCheck } from '../../api.js';

  let checks = $state([]);
  let loading = $state(true);

  async function run() {
    loading = true;
    try {
      checks = await getSelfCheck();
    } catch (e) {
      checks = [];
    }
    loading = false;
  }

  $effect(() => {
    run();
  });

  const ICON = { ok: '✓', warn: '!', fail: '✕' };
</script>

<section class="panel">
  <h2>Setup &amp; Self-Check</h2>
  <p class="hint">
    A quick pass over the sensor, outputs, hydrometer, network, integrations, and
    program config. Re-run after changing wiring or settings.
  </p>
  <button class="primary" onclick={run} disabled={loading}>
    {loading ? 'Checking...' : 'Re-run checks'}
  </button>
  <ul class="checkList">
    {#each checks as c}
      <li class="check check-{c.status}">
        <span class="badge">{ICON[c.status] || '?'}</span>
        <div>
          <div class="checkLabel">{c.label}</div>
          <div class="checkDetail">{c.detail}</div>
        </div>
      </li>
    {/each}
    {#if !loading && checks.length === 0}
      <li class="hint">No checks returned.</li>
    {/if}
  </ul>
</section>

<style>
  .checkList {
    list-style: none;
    padding: 0;
    margin: 14px 0 0;
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
  .check {
    display: flex;
    gap: 10px;
    align-items: flex-start;
    padding: 8px 10px;
    border-radius: 8px;
    background: rgba(255, 255, 255, 0.04);
  }
  .badge {
    width: 22px;
    height: 22px;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    font-weight: bold;
    flex-shrink: 0;
    color: #fff;
  }
  .check-ok .badge { background: #2a6; }
  .check-warn .badge { background: #c93; }
  .check-fail .badge { background: #c33; }
  .checkLabel { font-weight: 600; }
  .checkDetail { font-size: 0.85rem; opacity: 0.75; }
</style>
