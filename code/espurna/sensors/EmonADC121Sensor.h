// -----------------------------------------------------------------------------
// ADS121-based Energy Monitor Sensor over I2C
// Copyright (C) 2017-2018 by Xose Pérez <xose dot perez at gmail dot com>
// -----------------------------------------------------------------------------

#if SENSOR_SUPPORT && EMON_ADC121_SUPPORT

#pragma once

#include "Arduino.h"
#include "EmonSensor.h"

#if I2C_USE_BRZO
#include <brzo_i2c.h>
#else
#include <Wire.h>
#endif

// ADC121 Registers
#define ADC121_REG_RESULT       0x00
#define ADC121_REG_ALERT        0x01
#define ADC121_REG_CONFIG       0x02
#define ADC121_REG_LIMITL       0x03
#define ADC121_REG_LIMITH       0x04
#define ADC121_REG_HYST         0x05
#define ADC121_REG_CONVL        0x06
#define ADC121_REG_CONVH        0x07

#define ADC121_RESOLUTION       12
#define ADC121_CHANNELS         1

class EmonADC121Sensor : public EmonSensor {

    public:

        // ---------------------------------------------------------------------
        // Public
        // ---------------------------------------------------------------------

        EmonADC121Sensor(): EmonSensor() {
            _channels = ADC121_CHANNELS;
            _sensor_id = SENSOR_EMON_ADC121_ID;
            init();
        }

        // ---------------------------------------------------------------------
        // Sensor API
        // ---------------------------------------------------------------------

        // Initialization method, must be idempotent
        void begin() {

            if (!_dirty) return;
            _dirty = false;

            // Discover
            unsigned char addresses[] = {0x50, 0x51, 0x52, 0x54, 0x55, 0x56, 0x58, 0x59, 0x5A};
            _address = _begin_i2c(_address, sizeof(addresses), addresses);
            if (_address == 0) return;

            // Init sensor
            #if I2C_USE_BRZO
                uint8_t buffer[2];
                buffer[0] = ADC121_REG_CONFIG;
                buffer[1] = 0x00;
                brzo_i2c_start_transaction(_address, I2C_SCL_FREQUENCY);
                brzo_i2c_write(buffer, 2, false);
                brzo_i2c_end_transaction();
            #else
                Wire.beginTransmission(_address);
                Wire.write(ADC121_REG_CONFIG);
                Wire.write(0x00);
                Wire.endTransmission();
            #endif

            // Just one channel
            _count = _magnitudes;

            // Bit depth
            _resolution = ADC121_RESOLUTION;

            // Call the parent class method
            EmonSensor::begin();

            // warmup channel 0 (the only one)
            read(0);

        }

        // Descriptive name of the sensor
        String description() {
            char buffer[30];
            snprintf(buffer, sizeof(buffer), "EMON @ ADC121 @ I2C (0x%02X)", _address);
            return String(buffer);
        }

        // Pre-read hook (usually to populate registers with up-to-date data)
        void pre() {

            if (_address == 0) {
                _error = SENSOR_ERROR_UNKNOWN_ID;
                return;
            }

            _current[0] = read(0);

            #if EMON_REPORT_ENERGY
                static unsigned long last = 0;
                if (last > 0) {
                    _energy[0] += (_current[0] * _voltage * (millis() - last) / 1000);
                }
                last = millis();
            #endif

        }

        // Type for slot # index
        unsigned char type(unsigned char index) {
            _error = SENSOR_ERROR_OK;
            unsigned char i=0;
            #if EMON_REPORT_CURRENT
                if (index == i++) return MAGNITUDE_CURRENT;
            #endif
            #if EMON_REPORT_POWER
                if (index == i++) return MAGNITUDE_POWER_APPARENT;
            #endif
            #if EMON_REPORT_ENERGY
                if (index == i) return MAGNITUDE_ENERGY;
            #endif
            _error = SENSOR_ERROR_OUT_OF_RANGE;
            return MAGNITUDE_NONE;
        }

        // Current value for slot # index
        double value(unsigned char index) {

            _error = SENSOR_ERROR_OK;
            unsigned char channel = index / _magnitudes;

            unsigned char i=0;
            #if EMON_REPORT_CURRENT
                if (index == i++) return _current[channel];
            #endif
            #if EMON_REPORT_POWER
                if (index == i++) return _current[channel] * _voltage;
            #endif
            #if EMON_REPORT_ENERGY
                if (index == i) return _energy[channel];
            #endif

            _error = SENSOR_ERROR_OUT_OF_RANGE;
            return 0;

        }

    protected:

        // ---------------------------------------------------------------------
        // Protected
        // ---------------------------------------------------------------------

        unsigned int readADC(unsigned char channel) {

            (void) channel;

            unsigned int value;

            #if I2C_USE_BRZO
                uint8_t buffer[2];
                buffer[0] = ADC121_REG_RESULT;
                brzo_i2c_start_transaction(_address, I2C_SCL_FREQUENCY);
                brzo_i2c_write(buffer, 1, false);
                brzo_i2c_read(buffer, 2, false);
                brzo_i2c_end_transaction();
                value = (buffer[0] & 0x0F) << 8;
                value |= buffer[1];
            #else
                Wire.beginTransmission(_address);
                Wire.write(ADC121_REG_RESULT);
                Wire.endTransmission();
                Wire.requestFrom(_address, (unsigned char) 2);
                value = (Wire.read() & 0x0F) << 8;
                value = value + Wire.read();
            #endif

            return value;

        }

};

#endif // SENSOR_SUPPORT && EMON_ADC121_SUPPORT
