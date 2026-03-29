#ifndef SF_GLOBALS_H
#define SF_GLOBALS_H

#include <Arduino.h>
#include <Preferences.h>

#include "sf_state.h"

extern Preferences prefs;
extern RuntimeState state;
extern String lastScheduleSlot;
extern String serialCmdBuffer;
extern String cachedCfgStr;

#endif
