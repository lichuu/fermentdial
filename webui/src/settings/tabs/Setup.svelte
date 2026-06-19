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

<section class="stackedCard">
  <header class="stackedCardHeader">
    <h2>Setup</h2>
    <p class="stackedCardDesc">
      A quick pass over the sensor, outputs, hydrometer, network, and integrations.
      Re-run after changing wiring or settings.
    </p>
  </header>

  <article class="stackedRow">
    <div class="stackedRowTop">
      <div class="stackedRowTitle"><h3>Self-check</h3></div>
      <button type="button" class="btnCompact primary" onclick={run} disabled={loading}>
        {loading ? 'Checking...' : 'Re-run'}
      </button>
    </div>
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
        <li class="formHint">No checks returned.</li>
      {/if}
    </ul>
  </article>
</section>

<style>
  .checkList {
    list-style: none;
    padding: 0;
    margin: 0;
    display: flex;
    flex-direction: column;
    gap: 6px;
  }
  .check {
    display: flex;
    gap: 10px;
    align-items: flex-start;
    padding: 9px 11px;
    border-radius: calc(var(--radius) - 3px);
    border: 1px solid var(--border);
    background: var(--face);
  }
  .badge {
    width: 22px;
    height: 22px;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    font-weight: 700;
    flex-shrink: 0;
    color: #fff;
  }
  .check-ok .badge { background: #2a6; }
  .check-warn .badge { background: #c93; }
  .check-fail .badge { background: #c33; }
  .checkLabel { font-weight: 600; }
  .checkDetail { font-size: 0.9em; color: var(--muted); margin-top: 1px; }
</style>