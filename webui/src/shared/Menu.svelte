<script>
  let { page, activeHash = '', showLogout = false } = $props();
  let open = $state(false);

  function toggle(e) {
    e.stopPropagation();
    open = !open;
  }
  function close() {
    open = false;
  }
  function onDocClick(e) {
    if (open && !e.target.closest('.menu') && !e.target.closest('.menuBtn')) {
      open = false;
    }
  }
</script>

<svelte:window onclick={onDocClick} />

<button class="menuBtn" type="button" onclick={toggle} aria-label="Menu">&#9776;</button>
<nav class="menu" class:open>
  <a href="/dashboard" class:current={page === 'dashboard'} onclick={close}>Dashboard</a>
  <a href="/settings" class:current={page === 'settings' && !activeHash} onclick={close}>Settings</a>
  <a href="/settings#profiles" class="sub" class:current={activeHash === '#profiles'} onclick={close}>Profiles</a>
  <a href="/settings#hydrometer" class="sub" class:current={activeHash === '#hydrometer'} onclick={close}>Hydrometer</a>
  <a href="/settings#controller" class="sub" class:current={activeHash === '#controller'} onclick={close}>Controller</a>
  <a href="/settings#system" class="sub" class:current={activeHash === '#system'} onclick={close}>System</a>
  <a href="/settings#monitoring" class="sub" class:current={activeHash === '#monitoring'} onclick={close}>Monitoring</a>
  <a href="/metrics" onclick={close}>Metrics</a>
  {#if showLogout}
    <a href="/logout" onclick={close}>Log out</a>
  {/if}
</nav>
