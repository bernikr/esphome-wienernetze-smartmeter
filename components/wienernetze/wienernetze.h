#pragma once

#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

#include <cstdint>
#include <vector>

#define WIENERNETZE_SENSOR(name) \
protected:                       \
  sensor::Sensor* name{};        \
                                 \
public:                          \
  void set_##name(sensor::Sensor* name) { this->name = name; }

#define WIENERNETZE_TEXT_SENSOR(name) \
protected:                            \
  text_sensor::TextSensor* name{};    \
                                      \
public:                               \
  void set_##name(text_sensor::TextSensor* name) { this->name = name; }

namespace esphome {
// Forward declarations to minimize header dependencies
namespace sensor { class Sensor; }
namespace text_sensor { class TextSensor; }

namespace wienernetze {
static const char* WIENERNETZE_VERSION = "1.3.0-beta.1";
static const char* TAG                 = "wienernetze";

class WienerNetze : public Component, public uart::UARTDevice {
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
    void set_key(const uint8_t* key) { this->key = key; }

  private:
    std::vector<uint8_t> receiveBuffer; // Stores the raw package data
    unsigned long lastRead = 0;         // Timestamp when data was last read
    int readTimeout = 100;       // Time to wait after last byte before decoding
    const uint8_t* key{nullptr}; // Stores the decryption key

    void handle_message(std::vector<uint8_t> msg);
};
} // namespace wienernetze
} // namespace esphome
