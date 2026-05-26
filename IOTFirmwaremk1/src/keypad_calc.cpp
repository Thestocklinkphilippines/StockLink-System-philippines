#include "keypad_calc.h"

#include <limits.h>

namespace {
static const char kKeypadKeysLocal[16] = {
    '1', '2', '3', 'A',
    '4', '5', '6', 'B',
    '7', '8', '9', 'C',
    '*', '0', '#', 'D'};

int gHeldKeyIndex = -1;
}

static int sampleAdcAverageRuntime(int (*adcRead)(), int samples) {
  const int safeSamples = (samples <= 0) ? 1 : samples;
  long total = 0;
  int minSample = 4095;
  int maxSample = 0;

  for (int i = 0; i < safeSamples; i++) {
    int sample = adcRead();
    if (sample < 0) sample = 0;
    if (sample > 4095) sample = 4095;
    total += sample;
    if (sample < minSample) minSample = sample;
    if (sample > maxSample) maxSample = sample;
    delayMicroseconds(500);
  }

  // Lightweight trimmed mean to suppress single-sample spikes.
  if (safeSamples >= 3) {
    total -= minSample;
    total -= maxSample;
    return (int)(total / (safeSamples - 2));
  }

  return (int)(total / safeSamples);
}

static int clampAdc(int value) {
  if (value < 0) return 0;
  if (value > 4095) return 4095;
  return value;
}

static bool hasValidCenters(const int centers[16]) {
  if (!centers) return false;
  int trend = 0;
  for (int i = 0; i < 16; i++) {
    if (centers[i] <= 0 || centers[i] >= 4095) return false;
    if (i == 0) continue;
    int delta = centers[i] - centers[i - 1];
    if (delta == 0) return false;
    int stepTrend = (delta > 0) ? 1 : -1;
    if (trend == 0) trend = stepTrend;
    if (stepTrend != trend) return false;
  }
  return true;
}

static int computeKeyRadius(const int centers[16], int keyIndex, int toleranceAdc, int edgePadAdc) {
  int leftGap = (keyIndex > 0) ? abs(centers[keyIndex] - centers[keyIndex - 1]) : INT_MAX;
  int rightGap = (keyIndex < 15) ? abs(centers[keyIndex + 1] - centers[keyIndex]) : INT_MAX;
  int nearestGap = (leftGap < rightGap) ? leftGap : rightGap;

  int adaptiveRadius = nearestGap / 2;
  if (adaptiveRadius == INT_MAX / 2 || adaptiveRadius == INT_MAX) adaptiveRadius = 2047;

  int pad = (edgePadAdc < 0) ? 0 : edgePadAdc;
  adaptiveRadius -= pad;
  if (adaptiveRadius < 0) adaptiveRadius = 0;

  int tolerance = (toleranceAdc < 0) ? 0 : toleranceAdc;
  if (tolerance == 0) return adaptiveRadius;
  if (adaptiveRadius == 0) return tolerance;
  return (adaptiveRadius < tolerance) ? adaptiveRadius : tolerance;
}

static int findClosestCenter(const int centers[16], int adc) {
  int bestIndex = -1;
  int bestDelta = 4096;
  for (int i = 0; i < 16; i++) {
    int delta = abs(adc - centers[i]);
    if (delta < bestDelta) {
      bestDelta = delta;
      bestIndex = i;
    }
  }
  return bestIndex;
}

struct SampleWindowStats {
  int averageAdc = 0;
  int minAdc = 4095;
  int maxAdc = 0;
};

static SampleWindowStats sampleAdcWindowStats(int (*adcRead)(), int samples) {
  SampleWindowStats stats;
  const int safeSamples = (samples <= 0) ? 1 : samples;
  long total = 0;

  for (int i = 0; i < safeSamples; i++) {
    int sample = adcRead();
    if (sample < 0) sample = 0;
    if (sample > 4095) sample = 4095;
    total += sample;
    if (sample < stats.minAdc) stats.minAdc = sample;
    if (sample > stats.maxAdc) stats.maxAdc = sample;
    delayMicroseconds(500);
  }

  if (safeSamples >= 3) {
    total -= stats.minAdc;
    total -= stats.maxAdc;
    stats.averageAdc = (int)(total / (safeSamples - 2));
  } else {
    stats.averageAdc = (int)(total / safeSamples);
  }

  return stats;
}

void resetKeypadCalcState() {
  gHeldKeyIndex = -1;
}

char calculateKeypadKey(int (*adcRead)(),
                        const int centers[16],
                        int noKeyMin,
                        bool idleIsLow,
                        const KeypadAdcTuning& tuning,
                        int* outRawAdc,
                        int* outAdjustedAdc,
                        int* outAppliedOffset) {
  if (!adcRead || !centers || !hasValidCenters(centers)) {
    gHeldKeyIndex = -1;
    return '\0';
  }

  SampleWindowStats window = sampleAdcWindowStats(adcRead, tuning.samples);
  int rawAdc = window.averageAdc;
  int adjustedAdc = clampAdc(rawAdc + tuning.adcOffset);
  if (outRawAdc) *outRawAdc = rawAdc;
  if (outAdjustedAdc) *outAdjustedAdc = adjustedAdc;
  if (outAppliedOffset) *outAppliedOffset = tuning.adcOffset;

  int idleThreshold = clampAdc(noKeyMin + tuning.adcOffset);
  int idleBand = (tuning.idleBandAdc < 0) ? 0 : tuning.idleBandAdc;

  bool idle = false;
  if (idleIsLow) {
    idle = adjustedAdc <= (idleThreshold + idleBand);
  } else {
    idle = adjustedAdc >= (idleThreshold - idleBand);
  }

  if (idle) {
    gHeldKeyIndex = -1;
    return '\0';
  }

  // Make the lowest keys deterministic when the no-key state is a clean 0 V idle.
  // Key 1 only wins when the whole sample window stays below 900.
  // Key 2 wins when the sample window straddles the 900 boundary or sits in the narrow handoff band above it.
  if (centers[0] > 0 && centers[1] > centers[0] && centers[2] > centers[1]) {
    const int key1UpperBoundary = 900;
    const int key2UpperBoundary = 920;

    if (window.maxAdc < key1UpperBoundary) {
      gHeldKeyIndex = 0;
      return kKeypadKeysLocal[0];
    }

    if (window.minAdc < key1UpperBoundary && window.maxAdc >= key1UpperBoundary) {
      gHeldKeyIndex = 1;
      return kKeypadKeysLocal[1];
    }

    if (adjustedAdc >= key1UpperBoundary && adjustedAdc < key2UpperBoundary) {
      gHeldKeyIndex = 1;
      return kKeypadKeysLocal[1];
    }
  }

  if (gHeldKeyIndex >= 0 && gHeldKeyIndex < 16) {
    int heldRadius = computeKeyRadius(centers, gHeldKeyIndex, tuning.toleranceAdc, tuning.windowEdgePadAdc);
    int releaseHysteresis = (tuning.releaseHysteresisAdc < 0) ? 0 : tuning.releaseHysteresisAdc;
    int heldDelta = abs(adjustedAdc - centers[gHeldKeyIndex]);
    if (heldDelta <= (heldRadius + releaseHysteresis)) {
      return kKeypadKeysLocal[gHeldKeyIndex];
    }
    gHeldKeyIndex = -1;
  }

  int candidateIndex = findClosestCenter(centers, adjustedAdc);
  if (candidateIndex < 0) return '\0';

  int candidateRadius = computeKeyRadius(centers, candidateIndex, tuning.toleranceAdc, tuning.windowEdgePadAdc);
  int candidateDelta = abs(adjustedAdc - centers[candidateIndex]);
  if (candidateDelta <= candidateRadius) {
    gHeldKeyIndex = candidateIndex;
    return kKeypadKeysLocal[candidateIndex];
  }

  return '\0';
}
