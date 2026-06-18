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

<div class="grid">
  <section class="panel">
    <h2>Prometheus</h2>
    <div class="status">/metrics</div>
    <p class="hint">
      Scrape <code class="url">http://{metricsHost}/metrics</code>. Values are emitted in Celsius
      and output states are 0/1 gauges. The measurement / metric name set under Influx Export is
      also used as the Prometheus metric prefix.
    </p>
  </section>

  <section class="panel">
    <h2>Influx Export</h2>
    <form onsubmit={(e) => { e.preventDefault(); saveInflux(); }}>
      <label><input type="checkbox" bind:checked={influx.enabled} />Enable push export</label>
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
      <button type="submit">Save export settings</button>
    </form>
    <div class="saveStatus">{influxStatus}</div>
    <p class="hint">
      Last export: {config.influx.lastStatus}. VictoriaMetrics uses Influx line protocol at
      <code>/write</code> unless the URL already includes an Influx write path.
    </p>
  </section>

  <section class="panel">
    <h2>Brewfather</h2>
    <form onsubmit={(e) => { e.preventDefault(); saveBrewfather(); }}>
      <label><input type="checkbox" bind:checked={brewfather.enabled} />Enable Custom Stream logging</label>
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
      <button type="submit">Save Brewfather settings</button>
    </form>
    <div class="saveStatus">{brewfatherStatus}</div>
    <p class="hint">
      Posts Custom Stream JSON no more than once every 15 minutes per device name. Hydrometer only
      sends each fresh discovered Tilt/RAPT without controller target or state. Use the ID from
      Settings &gt; Power-ups &gt; Custom Stream. Status: {config.brewfather.lastStatus}
    </p>
  </section>

  <section class="panel">
    <h2>MQTT / Home Assistant</h2>
    <form onsubmit={(e) => { e.preventDefault(); saveMqtt(); }}>
      <label><input type="checkbox" bind:checked={mqtt.enabled} />Enable MQTT publishing</label>
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
      <button type="submit">Save MQTT settings</button>
    </form>
    <div class="saveStatus">{mqttStatus}</div>
    <p class="hint">
      Publishes the status JSON to <code>{config.mqtt.computedBaseTopic}/state</code> (retained),
      with availability on <code>{config.mqtt.computedBaseTopic}/availability</code>. Hydrometer
      only publishes one retained state topic per discovered Tilt/RAPT under
      <code>{config.mqtt.computedBaseTopic}/hydrometer/&lt;id&gt;/state</code>, and (when
      discovery is on) auto-creates a Home Assistant device with gravity, temperature, ABV,
      velocity, stability, battery, and signal entities. Status: {config.mqtt.lastStatus}
    </p>
  </section>
</div>
