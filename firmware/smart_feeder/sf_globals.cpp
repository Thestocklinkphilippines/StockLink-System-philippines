#include "sf_globals.h"

Preferences prefs;
RuntimeState state = {0, 0, 0, 0, 0, 0, 0, 0, false, true, 85.0f, 90.0f};
String lastScheduleSlot = "";
String serialCmdBuffer = "";
String cachedCfgStr = "{}";
