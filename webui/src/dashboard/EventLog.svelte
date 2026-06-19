<script>
  import { getEvents } from '../api.js';

  let events = $state([]);

  async function load() {
    try {
      events = await getEvents();
    } catch (e) {
      // ignore transient fetch failures
    }
  }

  $effect(() => {
    load();
    const id = setInterval(load, 5000);
    return () => clearInterval(id);
  });

  function when(e) {
    if (e.wallClock) {
      return new Date(e.ts * 1000).toLocaleString();
    }
    const h = Math.floor(e.ts / 3600);
    const m = Math.floor((e.ts % 3600) / 60);
    return '+' + (h > 0 ? h + 'h ' + m + 'm' : m + 'm') + ' uptime';
  }

  const FAULTY = new Set([
    'sensor-fault',
    'interlock-fault',
    'not-reaching-target',
    'hydrometer-stale',
    'long-runtime',
  ]);
</script>

<section class="panel eventPanel" hidden={events.length === 0}>
  <h2>Events</h2>
  <div class="eventScroll">
    <ul class="eventList">
      {#each events as e}
        <li class="ev" class:warn={FAULTY.has(e.typeName)}>
          <span class="evWhen">{when(e)}</span>
          <span class="evMsg">{e.message}</span>
        </li>
      {/each}
    </ul>
  </div>
</section>

<style>
  .eventScroll {
    max-height: 11rem;
    margin-top: 0;
    overflow-y: auto;
    overscroll-behavior: contain;
    -webkit-overflow-scrolling: touch;
    scrollbar-width: thin;
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) - 2px);
    background: var(--face);
    padding: 0 12px;
  }
  .eventList { list-style: none; margin: 0; padding: 0; }
  .ev {
    display: flex;
    gap: 10px;
    padding: 7px 0;
    border-bottom: 1px solid var(--border);
    font-size: 0.85rem;
  }
  .ev:last-child { border-bottom: none; }
  .evWhen { color: var(--muted); white-space: nowrap; min-width: 9em; font-variant-numeric: tabular-nums; }
  .evMsg { flex: 1; }
  .ev.warn .evMsg { color: #f7a; }
</style>
