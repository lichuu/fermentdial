<script>
  import { getEvents } from '../api.js';

  let { clock = null, uptimeSeconds = null } = $props();

  let events = $state([]);

  const TIME_FMT = {
    month: 'short',
    day: 'numeric',
    hour: 'numeric',
    minute: '2-digit',
    second: '2-digit',
  };

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

  function epochForEvent(e) {
    if (e.wallClock) {
      return e.ts;
    }
    if (clock?.wallClock && uptimeSeconds != null && e.ts <= uptimeSeconds) {
      return clock.seconds - (uptimeSeconds - e.ts);
    }
    return null;
  }

  function formatEpoch(sec) {
    return new Date(sec * 1000).toLocaleString(undefined, TIME_FMT);
  }

  function formatUptime(sec) {
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = sec % 60;
    if (h > 0) {
      return m > 0 || s > 0 ? `${h}h ${m}m ${s}s` : `${h}h`;
    }
    if (m > 0) {
      return s > 0 ? `${m}m ${s}s` : `${m}m`;
    }
    return `${s}s`;
  }

  function when(e) {
    const epoch = epochForEvent(e);
    if (epoch != null) {
      return formatEpoch(epoch);
    }
    if (!e.wallClock && uptimeSeconds != null && e.ts > uptimeSeconds) {
      return 'before last reboot';
    }
    return formatUptime(e.ts) + ' after boot';
  }

  const FAULTY = new Set([
    'sensor-fault',
    'interlock-fault',
    'not-reaching-target',
    'hydrometer-stale',
    'long-runtime',
  ]);
</script>

<section class="panel eventPanel">
  <h2>Events</h2>
  {#if events.length === 0}
    <p class="sub" style="font-size:12px;margin:0">No events yet</p>
  {:else}
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
  {/if}
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
  .evWhen {
    color: var(--muted);
    white-space: nowrap;
    flex-shrink: 0;
    min-width: 10.5em;
    font-variant-numeric: tabular-nums;
  }
  .evMsg { flex: 1; min-width: 0; }
  .ev.warn .evMsg { color: #f7a; }
</style>