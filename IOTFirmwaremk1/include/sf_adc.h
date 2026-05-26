#ifndef SF_ADC_H
#define SF_ADC_H

#include <Arduino.h>

// ADC priority levels - higher priority reads are performed first
enum AdcPriority {
  ADC_PRIORITY_CRITICAL = 0,   // Safety-critical (mains sensing)
  ADC_PRIORITY_HIGH = 1,        // User input (keypad)
  ADC_PRIORITY_NORMAL = 2,      // Other analog reads
};

// Initialize ADC system with optimized settings for reliability
// Must be called in setup() before using any ADC functions
void initAdcSystem();

// Perform a high-reliability ADC read with multiple samples and filtering
// Returns averaged ADC value with anomaly detection
// adcPin: GPIO pin number for ADC input
// priority: AdcPriority level (affects timing and reliability)
// Returns: Averaged ADC value (0-4095 for 12-bit), or -1 if read failed
int readAdcReliable(int adcPin, AdcPriority priority = ADC_PRIORITY_NORMAL);

// Fast ADC read (single sample) - used when speed is critical
// Returns: Raw ADC value (0-4095 for 12-bit), or -1 if failed
int readAdcFast(int adcPin);

// Perform priority-based ADC polling in main loop
// Must be called at the START of the main loop before all other I/O
// This ensures ADC reads capture the freshest data with minimal interference
void pollAdcHighPriority();

// Get the last cached ADC reading for a pin (from pollAdcHighPriority() call)
// This is faster than readAdcReliable and should be used in time-sensitive paths
// Returns: Last cached ADC value (0-4095 for 12-bit), or -1 if no reading available
int getLastAdcReading(int adcPin);

// Get ADC statistics for debugging (last 16 samples average/min/max)
void getAdcStats(int adcPin, int& outAvg, int& outMin, int& outMax);

// Enable/disable ADC statistics collection (small RAM/CPU overhead when enabled)
void setAdcStatsEnabled(bool enabled);

#endif

