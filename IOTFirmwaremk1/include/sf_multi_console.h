#ifndef SF_MULTI_CONSOLE_H
#define SF_MULTI_CONSOLE_H

#include <Arduino.h>
#include <WiFi.h>

// Port assignments
#define MULTI_CONSOLE_COMMAND_PORT 2323  // Commands (bi-directional)
#define MULTI_CONSOLE_DEBUG_PORT 2324    // Debug logs and general output
#define MULTI_CONSOLE_NETWORK_PORT 2326  // Network-related logs and events
#define MULTI_CONSOLE_SYSTEM_PORT 2325   // System alerts and critical events
#define MULTI_CONSOLE_KEYPAD_PORT 2327   // Keypad-related events

enum class MessageType {
  DEBUG = 0,      // Regular debug/info logs
  SYSTEM = 1,     // System events and alerts
  COMMAND_RESP = 2  // Command responses
};

class MultiWirelessConsole : public Print {
public:
  MultiWirelessConsole();

  void begin(unsigned long baudRate);
  void service();
  void beginCommandResponse();
  void endCommandResponse();
  bool isCommandResponseActive() const;
  bool lastReadWasCommandPort() const;
  
  // Serial interface for compatibility
  int available();
  int read();
  int peek();
  void flush();
  size_t write(uint8_t byte) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  int printf(const char* format, ...);
  
  // Port-specific output
  size_t writeDebug(const uint8_t* buffer, size_t size);
  size_t writeNetwork(const uint8_t* buffer, size_t size);
  size_t writeSystem(const uint8_t* buffer, size_t size);
  size_t writeKeypad(const uint8_t* buffer, size_t size);
  size_t writeCommand(const uint8_t* buffer, size_t size);
  
  // Formatted output to specific ports
  int debugPrintf(const char* format, ...);
  int networkPrintf(const char* format, ...);
  int systemPrintf(const char* format, ...);
  int keypadPrintf(const char* format, ...);
  int commandPrintf(const char* format, ...);

private:
  void ensureServersStarted();
  void acceptClients();
  void dropDisconnectedClients();
  void sendBanners();
  void sendCommandBanner();
  void sendDebugBanner();
  void sendNetworkBanner();
  void sendSystemBanner();
  void sendKeypadBanner();
  void broadcastToPort(WiFiServer& server, WiFiClient* clients, int maxClients, 
                       const uint8_t* buffer, size_t size);

  HardwareSerial* usb_;
  
  // Three servers for three ports
  WiFiServer commandServer_;
  WiFiServer debugServer_;
  WiFiServer networkServer_;
  WiFiServer systemServer_;
  WiFiServer keypadServer_;
  
  // Client arrays for each port (typically 1 client per port)
  static const int kMaxClientsPerPort = 2;
  WiFiClient commandClients_[kMaxClientsPerPort];
  WiFiClient debugClients_[kMaxClientsPerPort];
  WiFiClient networkClients_[kMaxClientsPerPort];
  WiFiClient systemClients_[kMaxClientsPerPort];
  WiFiClient keypadClients_[kMaxClientsPerPort];
  
  int commandClientCount_;
  int debugClientCount_;
  int networkClientCount_;
  int systemClientCount_;
  int keypadClientCount_;
  
  bool started_;
  bool serversRunning_;
  unsigned long baudRate_;
  enum class InputSource {
    NONE,
    USB,
    COMMAND_PORT,
  };

  InputSource lastReadSource_;
  bool commandResponseActive_;
};

extern MultiWirelessConsole SFMultiConsole;
void serviceMultiWirelessConsole();

// Backward compatibility macro - only define if not already defined
#ifndef SF_MULTI_CONSOLE_IMPL
#ifndef Serial
#define Serial SFMultiConsole
#endif
#endif

#endif
