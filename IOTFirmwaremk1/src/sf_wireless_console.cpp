#define SF_WIRELESS_CONSOLE_IMPL
#include "sf_wireless_console.h"

#include <stdarg.h>

// Keep reference to actual hardware serial before it gets redefined by macro
static HardwareSerial& getHardwareSerial() {
  return ::Serial;
}

#include "sf_multi_console.h"

// WirelessConsole is now deprecated and delegates to MultiWirelessConsole
// for backward compatibility. New code should use SFMultiConsole directly.

WirelessConsole SFConsole;

WirelessConsole::WirelessConsole()
    : usb_(&getHardwareSerial()), server_(2323), client_(), started_(false), serverRunning_(false), baudRate_(115200UL) {}

void WirelessConsole::begin(unsigned long baudRate) {
  // Delegate to multi-console
  SFMultiConsole.begin(baudRate);
  baudRate_ = baudRate;
  started_ = true;
}

void WirelessConsole::ensureServerStarted() {
  // Multi-console handles server startup
}

void WirelessConsole::dropClient() {
  // No-op: multi-console manages clients
}

void WirelessConsole::sendBanner() {
  // No-op: multi-console sends banners
}

void WirelessConsole::acceptClient() {
  // No-op: multi-console accepts clients
}

void WirelessConsole::service() {
  SFMultiConsole.service();
}

int WirelessConsole::available() {
  return SFMultiConsole.available();
}

int WirelessConsole::read() {
  return SFMultiConsole.read();
}

int WirelessConsole::peek() {
  return SFMultiConsole.peek();
}

void WirelessConsole::flush() {
  SFMultiConsole.flush();
}

void WirelessConsole::beginCommandResponse() {
  SFMultiConsole.beginCommandResponse();
}

void WirelessConsole::endCommandResponse() {
  SFMultiConsole.endCommandResponse();
}

bool WirelessConsole::lastReadWasCommandPort() const {
  return SFMultiConsole.lastReadWasCommandPort();
}

size_t WirelessConsole::write(uint8_t byte) {
  return SFMultiConsole.write(byte);
}

size_t WirelessConsole::write(const uint8_t* buffer, size_t size) {
  return SFMultiConsole.write(buffer, size);
}

int WirelessConsole::printf(const char* format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (len < 0) return len;

  size_t toWrite = (size_t)len;
  if (toWrite > sizeof(buffer) - 1) {
    toWrite = sizeof(buffer) - 1;
  }
  write(reinterpret_cast<const uint8_t*>(buffer), toWrite);
  return len;
}

void serviceWirelessConsole() {
  serviceMultiWirelessConsole();
}