#pragma once

#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

#include <FastCRC.h>
#include <Crypto.h>
#include <AES.h>
#include <CTR.h>

#define WIENERNETZE_SENSOR(name) \
protected:                       \
    sensor::Sensor *name{};      \
                                 \
public:                          \
    void set_##name(sensor::Sensor *name) { this->name = name; }

#define WIENERNETZE_TEXT_SENSOR(name) \
protected:                            \
    text_sensor::TextSensor *name{};  \
                                      \
public:                               \
    void set_##name(text_sensor::TextSensor *name) { this->name = name; }

namespace esphome
{
    namespace wienernetze
    {
        static const char *WIENERNETZE_VERSION = "1.2.0";
        static const char *TAG = "wienernetze";

        class WienerNetze : public Component, public uart::UARTDevice
        {
            WIENERNETZE_SENSOR(active_energy_pos)
            WIENERNETZE_SENSOR(active_energy_neg)
            WIENERNETZE_SENSOR(reactive_energy_pos)
            WIENERNETZE_SENSOR(reactive_energy_neg)
            WIENERNETZE_SENSOR(active_power_pos)
            WIENERNETZE_SENSOR(active_power_neg)
            WIENERNETZE_SENSOR(reactive_power_pos)
            WIENERNETZE_SENSOR(reactive_power_neg)

            WIENERNETZE_TEXT_SENSOR(active_energy_pos_raw)
            WIENERNETZE_TEXT_SENSOR(active_energy_neg_raw)
            WIENERNETZE_TEXT_SENSOR(reactive_energy_pos_raw)
            WIENERNETZE_TEXT_SENSOR(reactive_energy_neg_raw)

        public:
            void dump_config() override;
            void loop() override;

            void set_key(const uint8_t *key) { this->key = key; }

        private:
            std::vector<uint8_t> receiveBuffer; // Stores the packet currently being received
            unsigned long lastRead = 0;         // Timestamp when data was last read
            int readTimeout = 100;              // Time to wait after last byte before considering data complete

            FastCRC16 CRC16;
            CTR<AES128> ctraes128;

            const uint8_t *key; // Stores the decryption key

            int bytes_to_int(uint8_t bytes[], int left, int right);
            void handle_message(std::vector<uint8_t> msg);
        };
    }
}