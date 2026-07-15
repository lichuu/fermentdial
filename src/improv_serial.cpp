#include "improv_serial.h"

#include "network.h"

#if FERM_ENABLE_NETWORK
#include <WiFi.h>
#endif

namespace ferm {

#if FERM_ENABLE_NETWORK

namespace {

constexpr uint8_t IMPROV_VERSION = 0x01;

constexpr uint8_t TYPE_CURRENT_STATE = 0x01;
constexpr uint8_t TYPE_ERROR_STATE = 0x02;
constexpr uint8_t TYPE_RPC_COMMAND = 0x03;
constexpr uint8_t TYPE_RPC_RESULT = 0x04;

constexpr uint8_t STATE_READY = 0x02;
constexpr uint8_t STATE_PROVISIONING = 0x03;
constexpr uint8_t STATE_PROVISIONED = 0x04;

constexpr uint8_t ERROR_INVALID_RPC = 0x01;
constexpr uint8_t ERROR_UNKNOWN_RPC = 0x02;
constexpr uint8_t ERROR_UNABLE_TO_CONNECT = 0x03;

constexpr uint8_t CMD_WIFI_SETTINGS = 0x01;
constexpr uint8_t CMD_GET_CURRENT_STATE = 0x02;
constexpr uint8_t CMD_GET_DEVICE_INFO = 0x03;
constexpr uint8_t CMD_GET_WIFI_NETWORKS = 0x04;

// Two startWifi attempts (NetworkManager retries every 15 s) plus margin.
constexpr uint32_t PROVISION_TIMEOUT_MS = 35000;

constexpr char HEADER[6] = {'I', 'M', 'P', 'R', 'O', 'V'};

// Frame layout: HEADER[6] version[1] type[1] len[1] data[len] checksum[1].
constexpr size_t IDX_VERSION = 6;
constexpr size_t IDX_TYPE = 7;
constexpr size_t IDX_LEN = 8;
constexpr size_t IDX_DATA = 9;

} // namespace

void ImprovSerial::begin(NetworkManager *network) { _network = network; }

void ImprovSerial::loop(uint32_t nowMs) {
  if (_network == nullptr) {
    return;
  }
  while (Serial.available() > 0) {
    pushByte(static_cast<uint8_t>(Serial.read()), nowMs);
  }

  if (_provisioning) {
    if (_network->snapshot().wifiConnected) {
      _provisioning = false;
      sendState(STATE_PROVISIONED);
      const String url = deviceUrl();
      sendRpcResult(CMD_WIFI_SETTINGS, &url, 1);
    } else if (nowMs - _provisionStartMs >= PROVISION_TIMEOUT_MS) {
      _provisioning = false;
      sendError(ERROR_UNABLE_TO_CONNECT);
      sendState(STATE_READY);
    }
  }
}

void ImprovSerial::pushByte(uint8_t b, uint32_t nowMs) {
  // Resynchronize on the header so serial log noise around frames is ignored.
  if (_rxLen < sizeof(HEADER)) {
    if (b == static_cast<uint8_t>(HEADER[_rxLen])) {
      _rx[_rxLen++] = b;
    } else {
      _rxLen = (b == static_cast<uint8_t>(HEADER[0])) ? 1 : 0;
      _rx[0] = b;
    }
    return;
  }

  _rx[_rxLen++] = b;
  if (_rxLen <= IDX_DATA) {
    return;
  }
  const uint8_t dataLen = _rx[IDX_LEN];
  const size_t total = IDX_DATA + dataLen + 1;
  if (total > MAX_FRAME) {
    _rxLen = 0;
    return;
  }
  if (_rxLen < total) {
    return;
  }

  uint8_t sum = 0;
  for (size_t i = 0; i < total - 1; ++i) {
    sum += _rx[i];
  }
  if (sum == _rx[total - 1] && _rx[IDX_VERSION] == IMPROV_VERSION &&
      _rx[IDX_TYPE] == TYPE_RPC_COMMAND) {
    handleRpc(_rx + IDX_DATA, dataLen, nowMs);
  }
  _rxLen = 0;
}

void ImprovSerial::handleRpc(const uint8_t *data, uint8_t len, uint32_t nowMs) {
  if (len < 2 || data[1] != len - 2) {
    sendError(ERROR_INVALID_RPC);
    return;
  }
  const uint8_t cmd = data[0];
  const uint8_t *payload = data + 2;
  const uint8_t payloadLen = data[1];

  switch (cmd) {
  case CMD_WIFI_SETTINGS: {
    if (payloadLen < 2) {
      sendError(ERROR_INVALID_RPC);
      return;
    }
    const uint8_t ssidLen = payload[0];
    if (ssidLen == 0 || 1 + ssidLen + 1 > payloadLen) {
      sendError(ERROR_INVALID_RPC);
      return;
    }
    const uint8_t passLen = payload[1 + ssidLen];
    if (1 + ssidLen + 1 + passLen > payloadLen) {
      sendError(ERROR_INVALID_RPC);
      return;
    }
    String ssid;
    String pass;
    ssid.reserve(ssidLen);
    pass.reserve(passLen);
    for (uint8_t i = 0; i < ssidLen; ++i) {
      ssid += static_cast<char>(payload[1 + i]);
    }
    for (uint8_t i = 0; i < passLen; ++i) {
      pass += static_cast<char>(payload[2 + ssidLen + i]);
    }
    sendState(STATE_PROVISIONING);
    if (!_network->improvJoin(ssid, pass, nowMs)) {
      sendError(ERROR_UNABLE_TO_CONNECT);
      sendState(STATE_READY);
      return;
    }
    _provisioning = true;
    _provisionStartMs = nowMs;
    break;
  }
  case CMD_GET_CURRENT_STATE: {
    const bool connected = _network->snapshot().wifiConnected;
    sendState(connected ? STATE_PROVISIONED : STATE_READY);
    if (connected) {
      const String url = deviceUrl();
      sendRpcResult(CMD_GET_CURRENT_STATE, &url, 1);
    }
    break;
  }
  case CMD_GET_DEVICE_INFO: {
    const String info[4] = {FIRMWARE_NAME, FIRMWARE_VERSION, "ESP32-S3",
                            _network->snapshot().hostname};
    sendRpcResult(CMD_GET_DEVICE_INFO, info, 4);
    break;
  }
  case CMD_GET_WIFI_NETWORKS: {
    const int count = WiFi.scanNetworks(false, true);
    for (int i = 0; i < count; ++i) {
      const String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) {
        continue;
      }
      const String net[3] = {
          ssid, String(WiFi.RSSI(i)),
          WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES"};
      sendRpcResult(CMD_GET_WIFI_NETWORKS, net, 3);
    }
    WiFi.scanDelete();
    sendRpcResult(CMD_GET_WIFI_NETWORKS, nullptr, 0);
    break;
  }
  default:
    sendError(ERROR_UNKNOWN_RPC);
    break;
  }
}

void ImprovSerial::sendState(uint8_t state) {
  sendFrame(TYPE_CURRENT_STATE, &state, 1);
}

void ImprovSerial::sendError(uint8_t error) {
  sendFrame(TYPE_ERROR_STATE, &error, 1);
}

void ImprovSerial::sendRpcResult(uint8_t cmd, const String *strings,
                                 uint8_t count) {
  uint8_t data[MAX_FRAME];
  size_t len = 0;
  data[len++] = cmd;
  data[len++] = 0; // payload length, patched below
  for (uint8_t i = 0; i < count; ++i) {
    const size_t strLen = strings[i].length();
    if (len + 1 + strLen + 1 > sizeof(data) || strLen > 255) {
      return;
    }
    data[len++] = static_cast<uint8_t>(strLen);
    memcpy(data + len, strings[i].c_str(), strLen);
    len += strLen;
  }
  data[1] = static_cast<uint8_t>(len - 2);
  sendFrame(TYPE_RPC_RESULT, data, static_cast<uint8_t>(len));
}

void ImprovSerial::sendFrame(uint8_t type, const uint8_t *data, uint8_t len) {
  uint8_t frame[MAX_FRAME + 16];
  size_t n = 0;
  memcpy(frame, HEADER, sizeof(HEADER));
  n += sizeof(HEADER);
  frame[n++] = IMPROV_VERSION;
  frame[n++] = type;
  frame[n++] = len;
  memcpy(frame + n, data, len);
  n += len;
  uint8_t sum = 0;
  for (size_t i = 0; i < n; ++i) {
    sum += frame[i];
  }
  frame[n++] = sum;
  Serial.write(frame, n);
}

String ImprovSerial::deviceUrl() const {
  return "http://" + WiFi.localIP().toString() + "/";
}

#else

void ImprovSerial::begin(NetworkManager *network) { (void)network; }
void ImprovSerial::loop(uint32_t nowMs) { (void)nowMs; }

#endif

} // namespace ferm
