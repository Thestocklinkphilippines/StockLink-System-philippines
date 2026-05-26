#ifndef SF_WIRELESS_CONSOLE_H
#define SF_WIRELESS_CONSOLE_H

#include <Arduino.h>
#include <WiFi.h>

class WirelessConsole : public Print {
public:
  WirelessConsole();

  void begin(unsigned long baudRate);
  int available();
  int read();
  int peek();
  void flush();
  void beginCommandResponse();
  void endCommandResponse();
  bool lastReadWasCommandPort() const;
  size_t write(uint8_t byte) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  int printf(const char* format, ...);
  void service();

private:
  void ensureServerStarted();
  void acceptClient();
  void dropClient();
  void sendBanner();

  HardwareSerial* usb_;
  WiFiServer server_;
  WiFiClient client_;
  bool started_;
  bool serverRunning_;
  unsigned long baudRate_;
};

extern WirelessConsole SFConsole;
void serviceWirelessConsole();

// Backward compatibility macro - only define if not already defined
#ifndef SF_WIRELESS_CONSOLE_IMPL
#ifndef Serial
#define Serial SFConsole
#endif
#endif

#endif