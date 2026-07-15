#pragma once

#include <Arduino.h>

#include "config.h"

namespace ferm {

class NetworkManager;

// Improv Wi-Fi serial provisioning (https://www.improv-wifi.com/serial/).
// Browser flashers (ESP Web Tools) send Wi-Fi credentials over the USB CDC
// port right after flashing; they are stored through NetworkManager's normal
// credential path, so nothing new is persisted. Compiles to a no-op when
// FERM_ENABLE_NETWORK is 0.
class ImprovSerial {
public:
  void begin(NetworkManager *network);
  void loop(uint32_t nowMs);

private:
#if FERM_ENABLE_NETWORK
  static constexpr size_t MAX_FRAME = 192;

  void pushByte(uint8_t b, uint32_t nowMs);
  void handleRpc(const uint8_t *data, uint8_t len, uint32_t nowMs);
  void sendState(uint8_t state);
  void sendError(uint8_t error);
  void sendRpcResult(uint8_t cmd, const String *strings, uint8_t count);
  void sendFrame(uint8_t type, const uint8_t *data, uint8_t len);
  String deviceUrl() const;

  NetworkManager *_network = nullptr;
  uint8_t _rx[MAX_FRAME];
  size_t _rxLen = 0;
  bool _provisioning = false;
  uint32_t _provisionStartMs = 0;
#endif
};

} // namespace ferm
