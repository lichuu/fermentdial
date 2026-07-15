<script>
  import Menu from '../shared/Menu.svelte';
  import { getStatus, getSettingsConfig } from '../api.js';
  import Profiles from './tabs/Profiles.svelte';
  import Programs from './tabs/Programs.svelte';
  import Hydrometer from './tabs/Hydrometer.svelte';
  import Controller from './tabs/Controller.svelte';
  import System from './tabs/System.svelte';
  import Monitoring from './tabs/Monitoring.svelte';
  import Setup from './tabs/Setup.svelte';

  const TABS = ['profiles', 'programs', 'hydrometer', 'controller', 'system', 'monitoring', 'setup'];

  let status = $state(null);
  let config = $state(null);
  let activeTab = $state((location.hash || '#profiles').slice(1));

  async function refresh() {
    try {
      status = await getStatus();
    } catch (e) {
      // ignore transient fetch failures
    }
  }

  async function loadConfig() {
    try {
      config = await getSettingsConfig();
    } catch (e) {
      // ignore transient fetch failures
    }
  }

  function showTab(id) {
    activeTab = TABS.includes(id) ? id : 'profiles';
    if (location.hash !== '#' + activeTab) {
      history.replaceState(null, '', '#' + activeTab);
    }
  }

  function onHashChange() {
    showTab((location.hash || '#profiles').slice(1));
  }

  $effect(() => {
    refresh();
    loadConfig();
    const id = setInterval(refresh, 3000);
    return () => clearInterval(id);
  });
</script>

<svelte:window onhashchange={onHashChange} />

<main>
  <div class="top">
    <h1><a href="/dashboard">FermentDial Settings</a></h1>
    <Menu page="settings" activeHash={'#' + activeTab} showLogout={true} />
  </div>

  <div class="tabs">
    {#each TABS as t}
      <button class="tab" class:active={activeTab === t} onclick={() => showTab(t)}>
        {t.charAt(0).toUpperCase() + t.slice(1)}
      </button>
    {/each}
  </div>

  <div class="tabpanel" class:tabpanel-shown={activeTab === 'profiles'}>
    {#if status}
      <Profiles {status} {refresh} />
    {/if}
  </div>
  <div class="tabpanel" class:tabpanel-shown={activeTab === 'programs'}>
    {#if status}
      <Programs {status} {refresh} />
    {/if}
  </div>
  <div class="tabpanel" class:tabpanel-shown={activeTab === 'hydrometer'}>
    {#if status}
      <Hydrometer {status} {refresh} />
    {/if}
  </div>
  <div class="tabpanel" class:tabpanel-shown={activeTab === 'controller'}>
    {#if status}
      <Controller {status} {refresh} />
    {/if}
  </div>
  <div class="tabpanel" class:tabpanel-shown={activeTab === 'system'}>
    {#if status && config}
      <System {status} {config} />
    {/if}
  </div>
  <div class="tabpanel" class:tabpanel-shown={activeTab === 'monitoring'}>
    {#if config}
      <Monitoring {config} />
    {/if}
  </div>
  <div class="tabpanel" class:tabpanel-shown={activeTab === 'setup'}>
    <Setup />
  </div>

  {#if config}
    <footer class="buildFoot">
      FermentDial v{config.firmwareVersion} ·
      <a
        href={'https://github.com/lichuu/fermentdial/commit/' + (config.firmwareGitSha || '')}
        target="_blank"
        rel="noreferrer"
      >{(config.firmwareGitSha || '').slice(0, 7)}</a>
    </footer>
  {/if}
</main>
