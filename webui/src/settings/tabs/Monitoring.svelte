<script>
  import { postForm } from '../../api.js';

  let { config } = $props();

  let influx = $state({});
  let influxClearPassword = $state(false);
  let influxStatus = $state('');

  let brewfather = $state({});
  let brewfatherStatus = $state('');

  let mqtt = $state({});
  let mqttClearPassword = $state(false);
  let mqttStatus = $state('');

  $effect(() => {
    influx = { ...config.influx };
    brewfather = { ...config.brewfather };
    mqtt = { ...config.mqtt };
  });

  const metricsHost = $derived(config.wifiIp || config.apSsid);

  function integrationBadge(enabled, lastStatus) {
    if (!enabled) return { text: 'Off', class: 'panelBadge-off' };
    const status = (lastStatus || '').toLowerCase();
    if (status.startsWith('ok') || status === 'connected' || status === 'saved') {
      return { text: 'Active', class: 'panelBadge-on' };
    }
    if (status === 'disabled' || status === 'waiting') {
      return { text: 'Waiting', class: 'panelBadge-warn' };
    }
    return { text: 'On', class: 'panelBadge-info' };
  }

  const influxBadge = $derived(integrationBadge(influx.enabled, config.influx.lastStatus));
  const brewfatherBadge = $derived(integrationBadge(brewfather.enabled, config.brewfather.lastStatus));
  const mqttBadge = $derived(integrationBadge(mqtt.enabled, config.mqtt.lastStatus));

  async function saveInflux() {
    influxStatus = 'Saving...';
    await postForm('/settings/influx', {
      influxEnabled: influx.enabled ? 1 : 0,
      influxTarget: influx.target,
      influxMeasurement: influx.measurement,
      influxUrl: influx.url,
      influxDatabase: influx.database,
      influxRetentionPolicy: influx.retentionPolicy,
      influxUsername: influx.username,
      influxPassword: influx.passwordInput || '',
      clearInfluxPassword: influxClearPassword ? 1 : 0,
      influxOrg: influx.org,
      influxBucket: influx.bucket,
      influxToken: influx.tokenInput || '',
      clearInfluxToken: influxClearPassword ? 1 : 0,
      influxInterval: influx.intervalSeconds,
    });
    influxStatus = 'Saved.';
  }

  async function saveBrewfather() {
    brewfatherStatus = 'Saving...';
    await postForm('/settings/brewfather', {
      brewfatherEnabled: brewfather.enabled ? 1 : 0,
      brewfatherPayloadScope: brewfather.payloadScope,
      brewfatherLoggingId: brewfather.loggingId,
      brewfatherDeviceName: brewfather.deviceName,
      brewfatherUrl: brewfather.url,
      brewfatherInterval: brewfather.intervalSeconds,
    });
    brewfatherStatus = 'Saved.';
  }

  async function saveMqtt() {
    mqttStatus = 'Saving...';
    await postForm('/settings/mqtt', {
      mqttEnabled: mqtt.enabled ? 1 : 0,
      mqttPayloadScope: mqtt.payloadScope,
      mqttHaDiscovery: mqtt.haDiscovery ? 1 : 0,
      mqttDiscoveryPrefix: mqtt.discoveryPrefix,
      mqttHost: mqtt.host,
      mqttPort: mqtt.port,
      mqttUsername: mqtt.username,
      mqttPassword: mqtt.passwordInput || '',
      clearMqttPassword: mqttClearPassword ? 1 : 0,
      mqttTopic: mqtt.baseTopic,
      mqttInterval: mqtt.intervalSeconds,
    });
    mqttStatus = 'Saved.';
  }
</script>

<section class="stackedCard">
  <header class="stackedCardHeader">
    <h2>Integrations</h2>
    <p class="stackedCardDesc">Scrape metrics locally or push telemetry to external services.</p>
  </header>

  <article class="stackedRow">
    <div class="stackedRowTop">
      <div class="stackedRowTitle">
        <h3>Prometheus</h3>
        <span class="panelBadge panelBadge-info">Pull</span>
      </div>
    </div>
    <p class="stackedRowDesc">
      Scrape endpoint for Prometheus. Values are emitted in Celsius with 0/1 output gauges.
      The measurement name from Influx Export is also used as the Prometheus metric prefix.
    </p>
    <div class="scrapeEndpoint">
      <div class="scrapeEndpointUrl">http://{metricsHost}/metrics</div>
    </div>
  </article>

  <article class="stackedRow">
    <form class="integrationForm" onsubmit={(e) => { e.preventDefault(); saveInflux(); }}>
      <div class="stackedRowTop">
        <div class="stackedRowTitle">
          <h3>Influx Export</h3>
          <span class="panelBadge {influxBadge.class}">{influxBadge.text}</span>
        </div>
        <div class="integrationBar">
          <div class="scanModes integrationToggle">
            <button type="button" class:active={!influx.enabled} onclick={() => { influx.enabled = false; }}>Off</button>
            <button type="button" class:active={influx.enabled} onclick={() => { influx.enabled = true; }}>On</button>
          </div>
          <button type="submit" class="btnCompact">Save</button>
        </div>
      </div>
      <p class="stackedRowDesc">
        Push line protocol to InfluxDB, VictoriaMetrics, or compatible backends.
        VictoriaMetrics accepts Influx line protocol at <code>/write</code> unless the URL already
        includes a write path.
      </p>
      {#if influx.enabled}
        <div class="integrationDetails">
          <label>
            Target
            <select bind:value={influx.target}>
              <option value="1">InfluxDB 1.x</option>
              <option value="2">InfluxDB 2.x</option>
              <option value="3">InfluxDB 3.x</option>
              <option value="vm">VictoriaMetrics</option>
            </select>
          </label>
          <label>Measurement / metric name<input bind:value={influx.measurement} /></label>
          <label>Base URL<input placeholder="http://host:8086" bind:value={influx.url} /></label>
          {#if influx.target === '2' || influx.target === '3'}
            <div class="row">
              <label>Org<input bind:value={influx.org} /></label>
              <label>Bucket<input bind:value={influx.bucket} /></label>
            </div>
            <label>V2/V3 token<input type="password" placeholder="leave blank to keep saved" bind:value={influx.tokenInput} /></label>
            <label><input type="checkbox" bind:checked={influxClearPassword} />Clear saved token</label>
          {:else}
            <div class="row">
              <label>Database / DB<input bind:value={influx.database} /></label>
              <label>Retention policy<input bind:value={influx.retentionPolicy} /></label>
            </div>
            <div class="row">
              <label>Username<input bind:value={influx.username} /></label>
              <label>Password / v1 token<input type="password" placeholder="leave blank to keep saved" bind:value={influx.passwordInput} /></label>
            </div>
            <label><input type="checkbox" bind:checked={influxClearPassword} />Clear saved password</label>
          {/if}
          <label>Interval seconds<input inputmode="numeric" bind:value={influx.intervalSeconds} /></label>
        </div>
      {/if}
      {#if influxStatus}<p class="formFeedback">{influxStatus}</p>{/if}
      <dl class="metaLine">
        <dt>Last export</dt>
        <dd>{config.influx.lastStatus}</dd>
      </dl>
    </form>
  </article>

  <article class="stackedRow">
    <form class="integrationForm" onsubmit={(e) => { e.preventDefault(); saveBrewfather(); }}>
      <div class="stackedRowTop">
        <div class="stackedRowTitle">
          <h3>Brewfather</h3>
          <span class="panelBadge {brewfatherBadge.class}">{brewfatherBadge.text}</span>
        </div>
        <div class="integrationBar">
          <div class="scanModes integrationToggle">
            <button type="button" class:active={!brewfather.enabled} onclick={() => { brewfather.enabled = false; }}>Off</button>
            <button type="button" class:active={brewfather.enabled} onclick={() => { brewfather.enabled = true; }}>On</button>
          </div>
          <button type="submit" class="btnCompact">Save</button>
        </div>
      </div>
      <p class="stackedRowDesc">
        Post Custom Stream JSON to Brewfather (up to once every 15 minutes per device).
        Use the logging ID from Brewfather Settings &gt; Power-ups &gt; Custom Stream.
      </p>
      {#if brewfather.enabled}
        <div class="integrationDetails">
          <label>
            Payload
            <select bind:value={brewfather.payloadScope}>
              <option value="all">Controller + hydrometer</option>
              <option value="hydrometer">Hydrometer only</option>
            </select>
          </label>
          <label>Logging ID<input placeholder="your-logging-id" bind:value={brewfather.loggingId} /></label>
          <label>Device name<input placeholder="leave blank for hostname" bind:value={brewfather.deviceName} /></label>
          <label>Endpoint URL<input placeholder="https://log.brewfather.net/stream" bind:value={brewfather.url} /></label>
          <label>Interval seconds<input inputmode="numeric" min="900" bind:value={brewfather.intervalSeconds} /></label>
        </div>
      {/if}
      {#if brewfatherStatus}<p class="formFeedback">{brewfatherStatus}</p>{/if}
      <dl class="metaLine">
        <dt>Status</dt>
        <dd>{config.brewfather.lastStatus}</dd>
      </dl>
    </form>
  </article>

  <article class="stackedRow">
    <form class="integrationForm" onsubmit={(e) => { e.preventDefault(); saveMqtt(); }}>
      <div class="stackedRowTop">
        <div class="stackedRowTitle">
          <h3>MQTT / Home Assistant</h3>
          <span class="panelBadge {mqttBadge.class}">{mqttBadge.text}</span>
        </div>
        <div class="integrationBar">
          <div class="scanModes integrationToggle">
            <button type="button" class:active={!mqtt.enabled} onclick={() => { mqtt.enabled = false; }}>Off</button>
            <button type="button" class:active={mqtt.enabled} onclick={() => { mqtt.enabled = true; }}>On</button>
          </div>
          <button type="submit" class="btnCompact">Save</button>
        </div>
      </div>
      <p class="stackedRowDesc">
        Publish state to an MQTT broker or Home Assistant.
        Topics: <code>{config.mqtt.computedBaseTopic}/state</code> and
        <code>{config.mqtt.computedBaseTopic}/availability</code>.
        Hydrometer-only mode publishes one retained topic per Tilt/RAPT under
        <code>{config.mqtt.computedBaseTopic}/hydrometer/&lt;id&gt;/state</code>. With discovery
        enabled, Home Assistant gets gravity, temperature, ABV, velocity, stability, battery, and
        signal entities per device.
      </p>
      {#if mqtt.enabled}
        <div class="integrationDetails">
          <label>
            Payload
            <select bind:value={mqtt.payloadScope}>
              <option value="all">Controller + hydrometer</option>
              <option value="hydrometer">Hydrometer only</option>
            </select>
          </label>
          <label><input type="checkbox" bind:checked={mqtt.haDiscovery} />Publish Home Assistant discovery (hydrometer only)</label>
          <label>Discovery prefix<input placeholder="homeassistant" bind:value={mqtt.discoveryPrefix} /></label>
          <div class="row">
            <label>Broker host<input placeholder="192.168.1.10" bind:value={mqtt.host} /></label>
            <label>Port<input inputmode="numeric" bind:value={mqtt.port} /></label>
          </div>
          <div class="row">
            <label>Username<input bind:value={mqtt.username} /></label>
            <label>Password<input type="password" placeholder="leave blank to keep saved" bind:value={mqtt.passwordInput} /></label>
          </div>
          <label><input type="checkbox" bind:checked={mqttClearPassword} />Clear saved password</label>
          <label>Base topic<input bind:value={mqtt.baseTopic} /></label>
          <label>Interval seconds<input inputmode="numeric" bind:value={mqtt.intervalSeconds} /></label>
        </div>
      {/if}
      {#if mqttStatus}<p class="formFeedback">{mqttStatus}</p>{/if}
      <dl class="metaLine">
        <dt>Status</dt>
        <dd>{config.mqtt.lastStatus}</dd>
      </dl>
    </form>
  </article>
</section>