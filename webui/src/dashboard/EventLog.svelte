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

<section class="panel" hidden={events.length === 0}>
  <h2>Events</h2>
  <ul class="eventList">
    {#each events as e}
      <li class="ev" class:warn={FAULTY.has(e.typeName)}>
        <span class="evWhen">{when(e)}</span>
        <span class="evMsg">{e.message}</span>
      </li>
    {/each}
  </ul>
</section>

<style>
  .eventList { list-style: none; margin: 8px 0 0; padding: 0; }
  .ev {
    display: flex;
    gap: 10px;
    padding: 5px 0;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
    font-size: 0.85rem;
  }
  .ev:last-child { border-bottom: none; }
  .evWhen { opacity: 0.55; white-space: nowrap; min-width: 9em; }
  .evMsg { flex: 1; }
  .ev.warn .evMsg { color: #f7a; }
</style>
