#define SF_MULTI_CONSOLE_IMPL
#include "sf_multi_console.h"

#include <stdarg.h>

namespace {
void drainInputWithHint(WiFiClient* clients, int maxClients, const char* hintMessage) {
  if (clients == nullptr || hintMessage == nullptr) return;
  for (int i = 0; i < maxClients; i++) {
    if (!clients[i] || !clients[i].connected()) continue;
    if (clients[i].available() <= 0) continue;

    while (clients[i].available() > 0) {
      clients[i].read();
    }
    clients[i].println(hintMessage);
  }
}
}  // namespace

MultiWirelessConsole SFMultiConsole;

MultiWirelessConsole::MultiWirelessConsole()
    : usb_(&::Serial),
      commandServer_(MULTI_CONSOLE_COMMAND_PORT),
      debugServer_(MULTI_CONSOLE_DEBUG_PORT),
    networkServer_(MULTI_CONSOLE_NETWORK_PORT),
      systemServer_(MULTI_CONSOLE_SYSTEM_PORT),
    keypadServer_(MULTI_CONSOLE_KEYPAD_PORT),
      commandClientCount_(0),
      debugClientCount_(0),
    networkClientCount_(0),
      systemClientCount_(0),
    keypadClientCount_(0),
      started_(false),
      serversRunning_(false),
      baudRate_(115200UL),
      lastReadSource_(InputSource::NONE),
      commandResponseActive_(false) {}

void MultiWirelessConsole::begin(unsigned long baudRate) {
  baudRate_ = baudRate;
  if (usb_ != nullptr) {
    usb_->begin(baudRate_);
  }
  started_ = true;
  service();
}

void MultiWirelessConsole::beginCommandResponse() {
  commandResponseActive_ = true;
}

void MultiWirelessConsole::endCommandResponse() {
  commandResponseActive_ = false;
}

bool MultiWirelessConsole::isCommandResponseActive() const {
  return commandResponseActive_;
}

bool MultiWirelessConsole::lastReadWasCommandPort() const {
  return lastReadSource_ == InputSource::COMMAND_PORT;
}

void MultiWirelessConsole::ensureServersStarted() {
  if (!started_ || serversRunning_) return;
  if (WiFi.status() != WL_CONNECTED) return;

  commandServer_.begin();
  commandServer_.setNoDelay(true);
  
  debugServer_.begin();
  debugServer_.setNoDelay(true);

  networkServer_.begin();
  networkServer_.setNoDelay(true);
  
  systemServer_.begin();
  systemServer_.setNoDelay(true);

  keypadServer_.begin();
  keypadServer_.setNoDelay(true);
  
  serversRunning_ = true;
}

void MultiWirelessConsole::sendCommandBanner() {
  for (int i = 0; i < kMaxClientsPerPort; i++) {
    if (commandClients_[i] && commandClients_[i].connected()) {
      commandClients_[i].println();
      commandClients_[i].println("=== ESP32 Command Console (Port 2323) ===");
      commandClients_[i].println("Send commands here. Responses appear on this port.");
      commandClients_[i].println("Type 'help' for available commands.");
      commandClients_[i].println();
    }
  }
}

void MultiWirelessConsole::sendDebugBanner() {
  for (int i = 0; i < kMaxClientsPerPort; i++) {
    if (debugClients_[i] && debugClients_[i].connected()) {
      debugClients_[i].println();
      debugClients_[i].println("=== ESP32 Debug Console (Port 2324) ===");
      debugClients_[i].println("Output-only stream. All debug logs and general output appear here.");
      debugClients_[i].println("Use port 2323 for commands.");
      debugClients_[i].println();
    }
  }
}

void MultiWirelessConsole::sendNetworkBanner() {
  for (int i = 0; i < kMaxClientsPerPort; i++) {
    if (networkClients_[i] && networkClients_[i].connected()) {
      networkClients_[i].println();
      networkClients_[i].println("=== ESP32 Network Console (Port 2326) ===");
      networkClients_[i].println("Output-only stream. Network-related logs and events appear here.");
      networkClients_[i].println("Use port 2324 for general debug output.");
      networkClients_[i].println();
    }
  }
}

void MultiWirelessConsole::sendSystemBanner() {
  for (int i = 0; i < kMaxClientsPerPort; i++) {
    if (systemClients_[i] && systemClients_[i].connected()) {
      systemClients_[i].println();
      systemClients_[i].println("=== ESP32 System Console (Port 2325) ===");
      systemClients_[i].println("Output-only stream. Critical system events and alerts appear here.");
      systemClients_[i].println("Use port 2323 for commands.");
      systemClients_[i].println();
    }
  }
}

void MultiWirelessConsole::sendKeypadBanner() {
  for (int i = 0; i < kMaxClientsPerPort; i++) {
    if (keypadClients_[i] && keypadClients_[i].connected()) {
      keypadClients_[i].println();
      keypadClients_[i].println("=== ESP32 Keypad Console (Port 2327) ===");
      keypadClients_[i].println("Output-only stream. Keypad input and calibration events appear here.");
      keypadClients_[i].println("Use port 2324 for general debug output.");
      keypadClients_[i].println();
    }
  }
}

void MultiWirelessConsole::sendBanners() {
  sendCommandBanner();
  sendDebugBanner();
  sendNetworkBanner();
  sendSystemBanner();
  sendKeypadBanner();
}

void MultiWirelessConsole::acceptClients() {
  if (!serversRunning_) return;

  // Accept command port clients
  WiFiClient incoming = commandServer_.available();
  if (incoming) {
    // Find empty slot or replace oldest
    for (int i = 0; i < kMaxClientsPerPort; i++) {
      if (!commandClients_[i] || !commandClients_[i].connected()) {
        if (commandClients_[i]) commandClients_[i].stop();
        commandClients_[i] = incoming;
        commandClients_[i].setNoDelay(true);
        commandClientCount_++;
        sendCommandBanner();
        return;
      }
    }
    incoming.stop();
  }

  // Accept debug port clients
  incoming = debugServer_.available();
  if (incoming) {
    for (int i = 0; i < kMaxClientsPerPort; i++) {
      if (!debugClients_[i] || !debugClients_[i].connected()) {
        if (debugClients_[i]) debugClients_[i].stop();
        debugClients_[i] = incoming;
        debugClients_[i].setNoDelay(true);
        debugClientCount_++;
        sendDebugBanner();
        return;
      }
    }
    incoming.stop();
  }

  // Accept network port clients
  incoming = networkServer_.available();
  if (incoming) {
    for (int i = 0; i < kMaxClientsPerPort; i++) {
      if (!networkClients_[i] || !networkClients_[i].connected()) {
        if (networkClients_[i]) networkClients_[i].stop();
        networkClients_[i] = incoming;
        networkClients_[i].setNoDelay(true);
        networkClientCount_++;
        sendNetworkBanner();
        return;
      }
    }
    incoming.stop();
  }

  // Accept system port clients
  incoming = systemServer_.available();
  if (incoming) {
    for (int i = 0; i < kMaxClientsPerPort; i++) {
      if (!systemClients_[i] || !systemClients_[i].connected()) {
        if (systemClients_[i]) systemClients_[i].stop();
        systemClients_[i] = incoming;
        systemClients_[i].setNoDelay(true);
        systemClientCount_++;
        sendSystemBanner();
        return;
      }
    }
    incoming.stop();
  }

  // Accept keypad port clients
  incoming = keypadServer_.available();
  if (incoming) {
    for (int i = 0; i < kMaxClientsPerPort; i++) {
      if (!keypadClients_[i] || !keypadClients_[i].connected()) {
        if (keypadClients_[i]) keypadClients_[i].stop();
        keypadClients_[i] = incoming;
        keypadClients_[i].setNoDelay(true);
        keypadClientCount_++;
        sendKeypadBanner();
        return;
      }
    }
    incoming.stop();
  }
}

void MultiWirelessConsole::dropDisconnectedClients() {
  for (int i = 0; i < kMaxClientsPerPort; i++) {
    if (commandClients_[i] && !commandClients_[i].connected()) {
      commandClients_[i].stop();
      commandClientCount_--;
    }
    if (debugClients_[i] && !debugClients_[i].connected()) {
      debugClients_[i].stop();
      debugClientCount_--;
    }
    if (networkClients_[i] && !networkClients_[i].connected()) {
      networkClients_[i].stop();
      networkClientCount_--;
    }
    if (systemClients_[i] && !systemClients_[i].connected()) {
      systemClients_[i].stop();
      systemClientCount_--;
    }
    if (keypadClients_[i] && !keypadClients_[i].connected()) {
      keypadClients_[i].stop();
      keypadClientCount_--;
    }
  }
}

void MultiWirelessConsole::broadcastToPort(WiFiServer& server, WiFiClient* clients, 
                                          int maxClients, const uint8_t* buffer, size_t size) {
  for (int i = 0; i < maxClients; i++) {
    if (clients[i] && clients[i].connected()) {
      clients[i].write(buffer, size);
    }
  }
}

void MultiWirelessConsole::service() {
  ensureServersStarted();
  if (!serversRunning_) return;

  if (WiFi.status() != WL_CONNECTED) {
    dropDisconnectedClients();
    return;
  }

  dropDisconnectedClients();
  acceptClients();
  drainInputWithHint(debugClients_,
                     kMaxClientsPerPort,
                     "[INFO] Port 2324 is output-only. Use port 2323 for commands.");
  drainInputWithHint(systemClients_,
                     kMaxClientsPerPort,
                     "[INFO] Port 2325 is output-only. Use port 2323 for commands.");
}

int MultiWirelessConsole::available() {
  service();
  // Check command port for input (bi-directional)
  for (int i = 0; i < kMaxClientsPerPort; i++) {
    if (commandClients_[i] && commandClients_[i].connected() && commandClients_[i].available() > 0) {
      lastReadSource_ = InputSource::COMMAND_PORT;
      return commandClients_[i].available();
    }
  }
  lastReadSource_ = InputSource::USB;
  return usb_ != nullptr ? usb_->available() : 0;
}

int MultiWirelessConsole::read() {
  service();
  // Read from command port (bi-directional)
  for (int i = 0; i < kMaxClientsPerPort; i++) {
    if (commandClients_[i] && commandClients_[i].connected() && commandClients_[i].available() > 0) {
      lastReadSource_ = InputSource::COMMAND_PORT;
      return commandClients_[i].read();
    }
  }
  lastReadSource_ = InputSource::USB;
  return usb_ != nullptr ? usb_->read() : -1;
}

int MultiWirelessConsole::peek() {
  service();
  for (int i = 0; i < kMaxClientsPerPort; i++) {
    if (commandClients_[i] && commandClients_[i].connected() && commandClients_[i].available() > 0) {
      lastReadSource_ = InputSource::COMMAND_PORT;
      return commandClients_[i].peek();
    }
  }
  lastReadSource_ = InputSource::USB;
  return usb_ != nullptr ? usb_->peek() : -1;
}

void MultiWirelessConsole::flush() {
  if (usb_ != nullptr) {
    usb_->flush();
  }
  for (int i = 0; i < kMaxClientsPerPort; i++) {
    if (commandClients_[i] && commandClients_[i].connected()) {
      commandClients_[i].flush();
    }
    if (debugClients_[i] && debugClients_[i].connected()) {
      debugClients_[i].flush();
    }
    if (systemClients_[i] && systemClients_[i].connected()) {
      systemClients_[i].flush();
    }
  }
}

size_t MultiWirelessConsole::write(uint8_t byte) {
  if (commandResponseActive_) {
    return writeCommand(&byte, 1);
  }
  // Default write goes to debug port (for backward compatibility with Serial.print)
  return writeDebug(&byte, 1);
}

size_t MultiWirelessConsole::write(const uint8_t* buffer, size_t size) {
  if (commandResponseActive_) {
    return writeCommand(buffer, size);
  }
  // Default write goes to debug port
  return writeDebug(buffer, size);
}

int MultiWirelessConsole::printf(const char* format, ...) {
  if (commandResponseActive_) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (len < 0) return len;

    size_t toWrite = (size_t)len;
    if (toWrite > sizeof(buffer) - 1) {
      toWrite = sizeof(buffer) - 1;
    }
    writeCommand(reinterpret_cast<const uint8_t*>(buffer), toWrite);
    return len;
  }

  // Default printf goes to debug port
  return debugPrintf(format);
}

size_t MultiWirelessConsole::writeDebug(const uint8_t* buffer, size_t size) {
  if (usb_ != nullptr) {
    usb_->write(buffer, size);
  }
  broadcastToPort(debugServer_, debugClients_, kMaxClientsPerPort, buffer, size);
  return size;
}

size_t MultiWirelessConsole::writeNetwork(const uint8_t* buffer, size_t size) {
  if (usb_ != nullptr) {
    usb_->write(buffer, size);
  }
  broadcastToPort(networkServer_, networkClients_, kMaxClientsPerPort, buffer, size);
  return size;
}

size_t MultiWirelessConsole::writeSystem(const uint8_t* buffer, size_t size) {
  if (usb_ != nullptr) {
    usb_->write(buffer, size);
  }
  broadcastToPort(systemServer_, systemClients_, kMaxClientsPerPort, buffer, size);
  return size;
}

size_t MultiWirelessConsole::writeKeypad(const uint8_t* buffer, size_t size) {
  if (usb_ != nullptr) {
    usb_->write(buffer, size);
  }
  broadcastToPort(keypadServer_, keypadClients_, kMaxClientsPerPort, buffer, size);
  return size;
}

size_t MultiWirelessConsole::writeCommand(const uint8_t* buffer, size_t size) {
  if (usb_ != nullptr) {
    usb_->write(buffer, size);
  }
  broadcastToPort(commandServer_, commandClients_, kMaxClientsPerPort, buffer, size);
  return size;
}

int MultiWirelessConsole::debugPrintf(const char* format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (len < 0) return len;

  size_t toWrite = (size_t)len;
  if (toWrite > sizeof(buffer) - 1) {
    toWrite = sizeof(buffer) - 1;
  }
  writeDebug(reinterpret_cast<const uint8_t*>(buffer), toWrite);
  return len;
}

int MultiWirelessConsole::networkPrintf(const char* format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (len < 0) return len;

  size_t toWrite = (size_t)len;
  if (toWrite > sizeof(buffer) - 1) {
    toWrite = sizeof(buffer) - 1;
  }
  writeNetwork(reinterpret_cast<const uint8_t*>(buffer), toWrite);
  return len;
}

int MultiWirelessConsole::systemPrintf(const char* format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (len < 0) return len;

  size_t toWrite = (size_t)len;
  if (toWrite > sizeof(buffer) - 1) {
    toWrite = sizeof(buffer) - 1;
  }
  writeSystem(reinterpret_cast<const uint8_t*>(buffer), toWrite);
  return len;
}

int MultiWirelessConsole::keypadPrintf(const char* format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (len < 0) return len;

  size_t toWrite = (size_t)len;
  if (toWrite > sizeof(buffer) - 1) {
    toWrite = sizeof(buffer) - 1;
  }
  writeKeypad(reinterpret_cast<const uint8_t*>(buffer), toWrite);
  return len;
}

int MultiWirelessConsole::commandPrintf(const char* format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (len < 0) return len;

  size_t toWrite = (size_t)len;
  if (toWrite > sizeof(buffer) - 1) {
    toWrite = sizeof(buffer) - 1;
  }
  writeCommand(reinterpret_cast<const uint8_t*>(buffer), toWrite);
  return len;
}

void serviceMultiWirelessConsole() {
  SFMultiConsole.service();
}
