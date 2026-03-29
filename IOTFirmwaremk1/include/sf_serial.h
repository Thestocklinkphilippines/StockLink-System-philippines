#ifndef SF_SERIAL_H
#define SF_SERIAL_H

#include <Arduino.h>

void printSerialHelp();
void dumpLocalConfigToSerial();
void dumpStateToSerial();
void dumpPrefsToSerial();
void executeSerialCommand(const String& rawCmd);
void handleSerialCommands();

#endif
