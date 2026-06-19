#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#ifndef PI
#define PI 3.14159265358979323846
#endif
#ifndef TWO_PI
#define TWO_PI (2.0 * PI)
#endif

using std::isnan;
#ifndef NAN
#define NAN (static_cast<float>(INFINITY) - static_cast<float>(INFINITY))
#endif

constexpr uint8_t OUTPUT = 1;
constexpr uint8_t INPUT = 0;
constexpr uint8_t HIGH = 1;
constexpr uint8_t LOW = 0;

constexpr int MOCK_GPIO_SIZE = 256;
inline int mock_gpio[MOCK_GPIO_SIZE] = {};
inline int mock_gpio_mode[MOCK_GPIO_SIZE] = {};

inline void mock_gpio_reset() {
  for (int i = 0; i < MOCK_GPIO_SIZE; ++i) {
    mock_gpio[i] = LOW;
    mock_gpio_mode[i] = INPUT;
  }
}

inline void pinMode(uint8_t pin, uint8_t mode) {
  if (pin < MOCK_GPIO_SIZE) {
    mock_gpio_mode[pin] = mode;
  }
}

inline void digitalWrite(uint8_t pin, uint8_t level) {
  if (pin < MOCK_GPIO_SIZE) {
    mock_gpio[pin] = level;
  }
}

class String {
public:
  String() = default;

  String(const char *value) : _value(value != nullptr ? value : "") {}

  String(const String &other) : _value(other._value) {}

  String &operator=(const String &other) {
    _value = other._value;
    return *this;
  }

  String &operator=(const char *value) {
    _value = value != nullptr ? value : "";
    return *this;
  }

  bool operator==(const char *value) const {
    return _value == (value != nullptr ? value : "");
  }

  size_t length() const { return _value.length(); }

  String &trim() {
    const auto start = _value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
      _value.clear();
      return *this;
    }
    const auto end = _value.find_last_not_of(" \t\r\n");
    _value = _value.substr(start, end - start + 1);
    return *this;
  }

  String &toUpperCase() {
    for (char &ch : _value) {
      if (ch >= 'a' && ch <= 'z') {
        ch = static_cast<char>(ch - 'a' + 'A');
      }
    }
    return *this;
  }

  String substring(size_t start, size_t end) const {
    if (start >= _value.length()) {
      return String("");
    }
    if (end > _value.length()) {
      end = _value.length();
    }
    if (end < start) {
      return String("");
    }
    return String(_value.substr(start, end - start).c_str());
  }

  bool startsWith(const char *prefix) const {
    if (prefix == nullptr) {
      return false;
    }
    return _value.rfind(prefix, 0) == 0;
  }

  const char *c_str() const { return _value.c_str(); }

  operator const char *() const { return _value.c_str(); }

private:
  std::string _value;
};