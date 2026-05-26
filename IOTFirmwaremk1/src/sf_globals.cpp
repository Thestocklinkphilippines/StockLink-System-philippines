#include "sf_globals.h"

Preferences prefs;
RuntimeState state = {0, 0, 0, 0, 0, 0, 0, 0, 0, false, true, 85.0f, 90.0f, false, false, 0, 0, 0, false, 0, 0, false, false, false, false, 0.0f, 0};
String lastScheduleSlot = "";
String serialCmdBuffer = "";
String cachedCfgStr = "{}";
bool gSerialConsoleExclusive = false;
