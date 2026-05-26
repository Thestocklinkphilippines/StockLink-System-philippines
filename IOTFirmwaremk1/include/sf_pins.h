#ifndef SF_PINS_H
#define SF_PINS_H

// Ultrasonic level sensors
static const int PIN_FEEDER_TRIG = 14;
static const int PIN_FEEDER_ECHO = 15;
static const int PIN_WATER_TRIG = 16;
static const int PIN_WATER_ECHO = 17;

// LCD I2C
static const int PIN_LCD_SDA = 21;
static const int PIN_LCD_SCL = 22;


// Analog keypad input
static const int PIN_KEYPAD_ADC = 32;
// Battery voltage sense input (ADC1, safe with WiFi)
static const int PIN_BATTERY_ADC = 33;

// Actuators
static const int PIN_FEED_MOTOR = 18;
static const int PIN_WATER_SOLENOID = 19;
static const int PIN_BATTERY_SHUTDOWN_RELAY = 25;

// Buzzer
static const int PIN_BUZZER = 23;

// Power failover sense is read as a digital input (HIGH = mains present, LOW = mains lost)
static const int PIN_MAINS_SENSE_ADC = 12;

#endif
