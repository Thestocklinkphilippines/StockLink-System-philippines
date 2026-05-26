#include "sf_adc.h"

#include <Arduino.h>

#include "sf_debug.h"
#include "sf_pins.h"

namespace {
// ADC sampling configuration
static const int kHighPrioritySamples = 32;   // Keypad and critical reads
static const int kNormalSamples = 16;         // General purpose
static const int kAdcReadTimeoutMs = 100;

// Per-pin state for anomaly detection and filtering
struct AdcPinState {
  int lastReading = 0;
  int minReading = 4095;
  int maxReading = 0;
  int sampleBuffer[16] = {0};  // Circular buffer for last 16 samples
  uint8_t bufferHead = 0;
  bool statsEnabled = false;
};

// Pinned I/O ADC ports on ESP32
static const int kNumTrackedPins = 4;
static AdcPinState gAdcPinState[kNumTrackedPins] = {};
static int gTrackedPins[kNumTrackedPins] = {PIN_KEYPAD_ADC, PIN_BATTERY_ADC, PIN_MAINS_SENSE_ADC, 35};

static bool gAdcSystemInitialized = false;
static bool gAdcStatsEnabled = false;

// Find or create state entry for a pin
static AdcPinState* getOrCreatePinState(int adcPin) {
  for (int i = 0; i < kNumTrackedPins; i++) {
    if (gTrackedPins[i] == adcPin || gTrackedPins[i] == 0) {
      if (gTrackedPins[i] == 0) gTrackedPins[i] = adcPin;
      return &gAdcPinState[i];
    }
  }
  return nullptr;
}

// Filter outliers from samples using median + IQR method
// Returns filtered average, or -1 if too many outliers
static int filterOutliers(int* samples, int count) {
  if (count <= 0) return -1;
  if (count == 1) return samples[0];

  // Simple outlier rejection: if any sample is more than 2% away from others, flag it
  int sum = 0;
  int validCount = 0;
  int avgGuess = 0;

  // First pass: rough average
  for (int i = 0; i < count; i++) avgGuess += samples[i];
  avgGuess /= count;

  // Second pass: reject samples > 3% deviation from average
  int threshold = (avgGuess > 0) ? (avgGuess * 3) / 100 : 100;
  for (int i = 0; i < count; i++) {
    int delta = abs(samples[i] - avgGuess);
    if (delta <= threshold) {
      sum += samples[i];
      validCount++;
    }
  }

  if (validCount == 0) return -1;
  if (validCount < (count / 2)) return -1;  // More than half rejected = bad read

  return sum / validCount;
}

// Sample ADC with consistent micro-timing
static int sampleAdcConsistent(int adcPin, int numSamples) {
  if (numSamples <= 0) return -1;

  int samples[32];
  if (numSamples > 32) numSamples = 32;

  // Discard first sample (may be stale from previous read)
  analogRead(adcPin);
  delayMicroseconds(100);

  // Collect samples with consistent timing
  for (int i = 0; i < numSamples; i++) {
    samples[i] = analogRead(adcPin);
    // Space samples 250us apart for stable multi-point averaging
    if (i < numSamples - 1) delayMicroseconds(250);
  }

  // Filter outliers and average
  return filterOutliers(samples, numSamples);
}

}  // namespace

void initAdcSystem() {
  if (gAdcSystemInitialized) return;

  pinMode(PIN_KEYPAD_ADC, INPUT);
  pinMode(PIN_BATTERY_ADC, INPUT);

  // ESP32 ADC resolution already set to 12-bit in main.cpp
  // Configure attenuation for 0-3.3V range on the keypad ADC path.
  analogSetAttenuation(ADC_11db);

  // Perform one dummy read to prime the ADC
  analogRead(PIN_KEYPAD_ADC);
  delayMicroseconds(100);

  gAdcSystemInitialized = true;
  LOG_INFO("ADC system initialized: resolution=12bit attenuation=11dB");
}

int readAdcReliable(int adcPin, AdcPriority priority) {
  if (!gAdcSystemInitialized) initAdcSystem();

  AdcPinState* state = getOrCreatePinState(adcPin);
  if (!state) return -1;

  // Determine number of samples based on priority
  int numSamples = (priority == ADC_PRIORITY_CRITICAL) ? kHighPrioritySamples : kHighPrioritySamples;
  int reading = sampleAdcConsistent(adcPin, numSamples);

  if (reading < 0) {
    LOG_WARN("ADC read failed pin=%d priority=%d", adcPin, (int)priority);
    return -1;
  }

  // Update statistics
  state->lastReading = reading;
  state->sampleBuffer[state->bufferHead] = reading;
  state->bufferHead = (state->bufferHead + 1) % 16;

  if (reading < state->minReading) state->minReading = reading;
  if (reading > state->maxReading) state->maxReading = reading;

  // Anomaly detection: if reading deviates >5% from recent average, log warning
  if (gAdcStatsEnabled && state->bufferHead > 4) {
    int recentSum = 0;
    for (int i = 0; i < 16; i++) recentSum += state->sampleBuffer[i];
    int recentAvg = recentSum / 16;
    int maxDeviation = (recentAvg * 5) / 100;

    if (abs(reading - recentAvg) > maxDeviation) {
      LOG_DEBUG("ADC anomaly pin=%d read=%d recent_avg=%d deviation=%d", adcPin, reading, recentAvg,
                abs(reading - recentAvg));
    }
  }

  return reading;
}

int readAdcFast(int adcPin) {
  if (!gAdcSystemInitialized) initAdcSystem();
  return analogRead(adcPin);
}

void pollAdcHighPriority() {
  if (!gAdcSystemInitialized) return;

  // Priority 1: Keypad input (user interaction)
  readAdcReliable(PIN_KEYPAD_ADC, ADC_PRIORITY_HIGH);
}

void getAdcStats(int adcPin, int& outAvg, int& outMin, int& outMax) {
  AdcPinState* state = getOrCreatePinState(adcPin);
  if (!state) {
    outAvg = outMin = outMax = -1;
    return;
  }

  int sum = 0;
  for (int i = 0; i < 16; i++) sum += state->sampleBuffer[i];

  outAvg = sum / 16;
  outMin = state->minReading;
  outMax = state->maxReading;
}

int getLastAdcReading(int adcPin) {
  AdcPinState* state = getOrCreatePinState(adcPin);
  if (!state) return -1;
  return state->lastReading;
}

void setAdcStatsEnabled(bool enabled) {
  gAdcStatsEnabled = enabled;
  LOG_INFO("ADC statistics %s", enabled ? "enabled" : "disabled");
}
