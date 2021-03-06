// -----------------------------------------------------------------------------
// BME280/BMP280 Sensor over I2C
// Uses SparkFun BME280 library
// Copyright (C) 2017-2018 by Xose Pérez <xose dot perez at gmail dot com>
// -----------------------------------------------------------------------------

#if SENSOR_SUPPORT && BMX280_SUPPORT

#pragma once

#include "Arduino.h"
#include "I2CSensor.h"
#include <SparkFunBME280.h>

#define BMX280_CHIP_BMP280          0x58
#define BMX280_CHIP_BME280          0x60

class BMX280Sensor : public I2CSensor {

    public:

        static unsigned char addresses[2];

        // ---------------------------------------------------------------------
        // Public
        // ---------------------------------------------------------------------

        BMX280Sensor(): I2CSensor() {
            _sensor_id = SENSOR_BMX280_ID;
            _bme = new BME280();
        }

        ~BMX280Sensor() {
            delete _bme;
        }

        // ---------------------------------------------------------------------
        // Sensor API
        // ---------------------------------------------------------------------

        // Initialization method, must be idempotent
        void begin() {

            if (!_dirty) return;
            _dirty = false;
            _chip = 0;

            // I2C auto-discover
            _address = _begin_i2c(_address, sizeof(BMX280Sensor::addresses), BMX280Sensor::addresses);
            if (_address == 0) return;

            // Init
            _init();

        }

        // Descriptive name of the sensor
        String description() {
            char buffer[20];
            snprintf(buffer, sizeof(buffer), "%s @ I2C (0x%02X)", _chip == BMX280_CHIP_BME280 ? "BME280" : "BMP280",  _address);
            return String(buffer);
        }

        // Type for slot # index
        unsigned char type(unsigned char index) {
            if (index < _count) {
                _error = SENSOR_ERROR_OK;
                unsigned char i = 0;
                #if BMX280_TEMPERATURE > 0
                    if (index == i++) return MAGNITUDE_TEMPERATURE;
                #endif
                #if BMX280_PRESSURE > 0
                    if (index == i++) return MAGNITUDE_PRESSURE;
                #endif
                #if BMX280_HUMIDITY > 0
                    if (_chip == BMX280_CHIP_BME280) {
                        if (index == i) return MAGNITUDE_HUMIDITY;
                    }
                #endif
            }
            _error = SENSOR_ERROR_OUT_OF_RANGE;
            return MAGNITUDE_NONE;
        }

        // Pre-read hook (usually to populate registers with up-to-date data)
        virtual void pre() {

            if (_chip == 0) {
                _error = SENSOR_ERROR_UNKNOWN_ID;
                return;
            }

            #if BMX280_MODE == 1
                forceRead();
            #endif

        }

        // Current value for slot # index
        double value(unsigned char index) {

            if (index < _count) {
                _error = SENSOR_ERROR_OK;
                unsigned char i = 0;
                #if BMX280_TEMPERATURE > 0
                    if (index == i++) return _bme->readTempC();
                #endif
                #if BMX280_PRESSURE > 0
                    if (index == i++) return _bme->readFloatPressure() / 100;
                #endif
                #if BMX280_HUMIDITY > 0
                    if (_chip == BMX280_CHIP_BME280) {
                        if (index == i) return _bme->readFloatHumidity();
                    }
                #endif
            }
            _error = SENSOR_ERROR_OUT_OF_RANGE;
            return 0;
        }

        // Load the configuration manifest
        static void manifest(JsonArray& sensors) {

            char buffer[10];

            JsonObject& sensor = sensors.createNestedObject();
            sensor["sensor_id"] = SENSOR_BMX280_ID;
            JsonArray& fields = sensor.createNestedArray("fields");

            {
                JsonObject& field = fields.createNestedObject();
                field["tag"] = UI_TAG_SELECT;
                field["name"] = "address";
                field["label"] = "Address";
                JsonArray& options = field.createNestedArray("options");
                {
                    JsonObject& option = options.createNestedObject();
                    option["name"] = "auto";
                    option["value"] = 0;
                }
                for (unsigned char i=0; i< sizeof(BMX280Sensor::addresses); i++) {
                    JsonObject& option = options.createNestedObject();
                    snprintf(buffer, sizeof(buffer), "0x%02X", BMX280Sensor::addresses[i]);
                    option["name"] = String(buffer);
                    option["value"] = BMX280Sensor::addresses[i];
                }
            }

        };

        void getConfig(JsonObject& root) {
            root["sensor_id"] = _sensor_id;
            root["address"] = _address;
        };

        void setConfig(JsonObject& root) {
            if (root.containsKey("address")) setAddress(root["address"]);
        };

    protected:

        void _init() {

            _bme->settings.commInterface = I2C_MODE;
            _bme->settings.I2CAddress = _address;
            _bme->settings.runMode = BMX280_MODE;
            _bme->settings.tStandby = 0;
            _bme->settings.filter = 0;
            _bme->settings.tempOverSample = BMX280_TEMPERATURE;
            _bme->settings.pressOverSample = BMX280_PRESSURE;
            _bme->settings.humidOverSample = BMX280_HUMIDITY;

            // Fix when not measuring temperature, t_fine should have a sensible value
            if (BMX280_TEMPERATURE == 0) _bme->t_fine = 100000; // aprox 20ºC

            // Make sure sensor had enough time to turn on. BMX280 requires 2ms to start up
            delay(10);

            // Check sensor correctly initialized
            _chip = _bme->begin();
            if ((_chip != BMX280_CHIP_BME280) && (_chip != BMX280_CHIP_BMP280)) {
                _chip = 0;
                i2cReleaseLock(_address);
                _error = SENSOR_ERROR_UNKNOWN_ID;
            }

            #if BMX280_TEMPERATURE > 0
                ++_count;
            #endif
            #if BMX280_PRESSURE > 0
                ++_count;
            #endif
            #if BMX280_HUMIDITY > 0
                if (_chip == BMX280_CHIP_BME280) ++_count;
            #endif

            _measurement_delay = measurementTime();

        }

        unsigned long measurementTime() {

            // Measurement Time (as per BMX280 datasheet section 9.1)
            // T_max(ms) = 1.25
            //  + (2.3 * T_oversampling)
            //  + (2.3 * P_oversampling + 0.575)
            //  + (2.4 * H_oversampling + 0.575)
            //  ~ 9.3ms for current settings

            double t = 1.25;
            #if BMX280_TEMPERATURE > 0
                t += (2.3 * BMX280_TEMPERATURE);
            #endif
            #if BMX280_PRESSURE > 0
                t += (2.3 * BMX280_PRESSURE + 0.575);
            #endif
            #if BMX280_HUMIDITY > 0
                if (_chip == BMX280_CHIP_BME280) {
                    t += (2.4 * BMX280_HUMIDITY + 0.575);
                }
            #endif

            return round(t + 1); // round up

        }

        void forceRead() {

            // We set the sensor in "forced mode" to force a reading.
            // After the reading the sensor will go back to sleep mode.
            uint8_t value = _bme->readRegister(BME280_CTRL_MEAS_REG);
            value = (value & 0xFC) + 0x01;
            _bme->writeRegister(BME280_CTRL_MEAS_REG, value);

            delay(_measurement_delay);

        }

        // ---------------------------------------------------------------------

        BME280 * _bme = NULL;
        unsigned char _chip;
        unsigned long _measurement_delay;

};

// Static inizializations

unsigned char BMX280Sensor::addresses[2] = {0x76, 0x77};

#endif // SENSOR_SUPPORT && BMX280_SUPPORT
