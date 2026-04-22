#ifndef SF_SIMULATION_H
#define SF_SIMULATION_H

#include "sf_config.h"

// Per-component simulation toggles.
// Keep SF_SIMULATION_MODE enabled while hardware is incomplete, then switch
// specific components to physical behavior as they become available.
#if SF_SIMULATION_MODE
#define SF_SIMULATE_FEEDER_LEVEL_SENSOR 1
#define SF_SIMULATE_WATER_LEVEL_SENSOR 0
#define SF_SIMULATE_FEED_MOTOR 0
#define SF_SIMULATE_WATER_REFILL 1
#define SF_SIMULATE_MAINS_INPUT 0
#else
#define SF_SIMULATE_FEEDER_LEVEL_SENSOR 0
#define SF_SIMULATE_WATER_LEVEL_SENSOR 0
#define SF_SIMULATE_FEED_MOTOR 0
#define SF_SIMULATE_WATER_REFILL 0
#define SF_SIMULATE_MAINS_INPUT 0
#endif

// Backward-compatible alias used by feeder logic.
#define SF_SIMULATE_LEVEL_SENSORS SF_SIMULATE_FEEDER_LEVEL_SENSOR

#endif