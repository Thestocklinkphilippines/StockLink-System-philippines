#ifndef KEYPAD_CALC_H
#define KEYPAD_CALC_H

#include <Arduino.h>

struct KeypadAdcTuning {
    int toleranceAdc = 30;
    int releaseHysteresisAdc = 20;
    int idleBandAdc = 30;
    int windowEdgePadAdc = 4;
    int adcOffset = 0;
    int samples = 5;
};

// Standalone keypad calculation API.
// `adcRead` should be a function that returns a single ADC sample (0..4095).
// `centers` is an array of 16 ADC center values for keys (monotonic increasing/decreasing).
// Returns the decoded keypad character ('0'..'9','A'..'D','*','#') or '\0' if no key.
// If provided, `outRawAdc` receives the sampled/averaged ADC before tuning,
// `outAdjustedAdc` receives the value used for decoding after tuning,
// and `outAppliedOffset` receives the runtime offset that was applied.
// Tuning values are provided in `tuning` (tolerance, hysteresis, idle band, offset, sampling).
char calculateKeypadKey(int (*adcRead)(),
                                                const int centers[16],
                                                int noKeyMin,
                                                bool idleIsLow,
                                                const KeypadAdcTuning& tuning,
                                                int* outRawAdc = nullptr,
                                                int* outAdjustedAdc = nullptr,
                                                int* outAppliedOffset = nullptr);

// Reset stateful key hold/hysteresis state (call after calibration changes).
void resetKeypadCalcState();

#endif
