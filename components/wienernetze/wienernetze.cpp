#include "wienernetze.h"

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#ifdef USE_ESP_IDF
  #ifndef MBEDTLS_CONFIG_FILE
    #define MBEDTLS_CONFIG_FILE "mbedtls/esp_config.h"
  #endif
  #include <mbedtls/aes.h>
#else
  #include <AES.h>
  #include <CTR.h>
#endif

namespace esphome {
namespace wienernetze {
namespace {
uint16_t calculate_crc16_x25(const uint8_t* data, size_t length) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0x8408;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc ^ 0xffff;
}

uint32_t bytes_to_int(const uint8_t* bytes, size_t offset, size_t len) {
  uint32_t result = 0;
  for (size_t i = offset; i < offset + len; i++) {
    result = (result << 8) | bytes[i];
  }
  return result;
}

void decrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t* data,
    size_t data_len) {
#ifdef USE_ESP_IDF
  // ESP-IDF: Native mbedTLS AES-128-CTR Decryption
  mbedtls_aes_context aes_ctx;
  mbedtls_aes_init(&aes_ctx);
  mbedtls_aes_setkey_enc(&aes_ctx, key, 128);
  size_t nc_off            = 0;
  uint8_t stream_block[16] = {0};
  uint8_t nonce_counter[16];
  memcpy(nonce_counter, iv, 16);
  mbedtls_aes_crypt_ctr(
      &aes_ctx, data_len, &nc_off, nonce_counter, stream_block, data, data);
  mbedtls_aes_free(&aes_ctx);
#else
  // Arduino: Fallback to rweather/Crypto library
  CTR<AES128> ctraes128;
  ctraes128.setKey(key, 16);
  ctraes128.setIV(iv, 16);
  ctraes128.decrypt(data, data, data_len);
#endif
}
} // namespace

void WienerNetze::dump_config() {
  ESP_LOGCONFIG(TAG, "WienerNetze Smartmeter:");
  ESP_LOGCONFIG(TAG, "  version: %s", WIENERNETZE_VERSION);
}

void WienerNetze::loop() {
  unsigned long currentTime = millis();

  while (available()) {
    uint8_t c = read();
    this->receiveBuffer.push_back(c);

    this->lastRead = currentTime;
  }

  if (!this->receiveBuffer.empty() &&
      currentTime - this->lastRead > this->readTimeout) {
    ESP_LOGV(TAG,
        "raw received data: %s",
        format_hex_pretty(this->receiveBuffer).c_str());
    handle_message(this->receiveBuffer);
    this->receiveBuffer.clear(); // Reset buffer
  }
}

void WienerNetze::handle_message(std::vector<uint8_t> msg) {
  uint8_t msg_len = msg.size();

  // HDLC Frame Format Type 3
  // +------+---------------+---------------+-------------+---------+-
  // | Flag | Frame Format  | Dest Address  | Src Address | Control |
  // |  7E  |   (2 bytes)   |  (1-4 bytes)  | (1-4 bytes) | (1 byte)|
  // +------+---------------+---------------+-------------+---------+-
  // --------+-------------+---------+------+
  //   HCS   | Information |   FCS   | Flag |
  // (2 byte)|  (APDU data)| (2 byte)|  7E  |
  // --------+-------------+---------+------+

  if (msg[0] != 0x7e) {
    ESP_LOGW(TAG, "wrong opening byte: %02x, expected 7e", msg[0]);
    return;
  }

  if (msg[msg_len - 1] != 0x7e) {
    ESP_LOGW(TAG, "wrong closing byte: %02x, expected 7e", msg[msg_len - 1]);
    return;
  }

  // CRC Check (FCS)
  int crc          = calculate_crc16_x25(msg.data() + 1, msg_len - 4);
  int expected_crc = msg[msg_len - 2] * 256 + msg[msg_len - 3];
  if (crc != expected_crc) {
    ESP_LOGW(
        TAG, "crc mismatch: calculated %04x, expected %04x", crc, expected_crc);
    return;
  }

  // HDLC Frame Format Bytes
  // 4 bit Format Type | 1 Segmentation Bit | 11 bit length
  if ((msg[1] & 0xf0) != 0xa0) {
    ESP_LOGE(TAG, "wrong format type: %02x, expected a0", msg[1] & 0xf0);
    return;
  }

  if (msg[1] & 0x09) {
    ESP_LOGE(TAG, "Segmented HDLC messages are not supported");
    return;
  }

  if (msg_len - 2 != (((msg[1] & 0x07) << 8) | msg[2])) {
    ESP_LOGE(TAG,
        "wrong msg length: %i, expected %i",
        msg_len,
        (((msg[1] & 0x0007) << 8) | msg[2]) + 2);
    return;
  }

  // The HDLC header in front of the payload differs between meters: its two
  // address fields are 1, 2 or 4 bytes long (Siemens: 1 + 4, Iskraemeco:
  // 1 + 2, Landis+Gyr: 2 + 1). Only the last byte of an address field has the
  // lowest bit set (IEC 62056-46).
  size_t pos     = 3;
  bool header_ok = true;
  for (int field = 0; field < 2; field++) {
    size_t start = pos;
    while (pos < msg_len && (msg[pos] & 0x01) == 0) {
      pos++;
    }
    pos++;
    size_t length = pos - start;
    header_ok &= length == 1 || length == 2 || length == 4;
  }
  pos += 3; // control field and header check sequence

  // LLC header, then the system title (tag DB, length 8). Its first three
  // bytes are the manufacturer ID, e.g. SMS, LGZ or ISK.
  if (!header_ok || pos + 5 + 8 > msg_len || msg[pos] != 0xe6 ||
      msg[pos + 1] != 0xe7 || msg[pos + 2] != 0x00 || msg[pos + 3] != 0xdb ||
      msg[pos + 4] != 0x08) {
    ESP_LOGW(TAG,
        "unexpected frame header: %s",
        format_hex_pretty(std::vector<uint8_t>(msg.begin(),
                              msg.begin() + std::min<size_t>(msg_len, 20)))
            .c_str());
    return;
  }
  const uint8_t* system_title = &msg[pos + 5];
  ESP_LOGV(TAG,
      "system title: %.3s %s",
      reinterpret_cast<const char*>(system_title),
      format_hex_pretty(
          std::vector<uint8_t>(system_title + 3, system_title + 8))
          .c_str());

  // The positions below are those of the Siemens layout, where the system
  // title starts at byte 16.
  int offset = static_cast<int>(pos + 5) - 16;
  // The decrypted payload starts with 0x0f and ends with eight 5-byte values,
  // so it needs at least 41 bytes (see the checks after decryption).
  if (msg_len - 33 - offset < 41) {
    ESP_LOGW(TAG, "data too short: %i bytes", msg_len);
    return;
  }

  // Decrypt
  uint8_t data_len       = msg_len - 33 - offset;
  uint8_t data[data_len] = {0};
  memcpy(data, msg.data() + 30 + offset, data_len);
  uint8_t nonce[16] = {0};
  memcpy(nonce, msg.data() + 16 + offset, 8);
  memcpy(nonce + 8, msg.data() + 26 + offset, 4);
  nonce[15] = 0x02;

  decrypt(this->key, nonce, data, data_len);

  ESP_LOGV(TAG,
      "decrypted data: %s",
      format_hex_pretty(std::vector<uint8_t>(data, data + data_len)).c_str());

  if (data[0] != 0x0f || data[data_len - 5] != 0x06 ||
      data[data_len - 5 * 2] != 0x06 || data[data_len - 5 * 3] != 0x06 ||
      data[data_len - 5 * 4] != 0x06 || data[data_len - 5 * 5] != 0x06 ||
      data[data_len - 5 * 6] != 0x06 || data[data_len - 5 * 7] != 0x06 ||
      data[data_len - 5 * 8] != 0x06) {
    ESP_LOGE(TAG, "decryption error, please check if your key is correct");
    return;
  }

  uint32_t active_energy_pos_raw = bytes_to_int(data, data_len - 4 - 5 * 7, 4);
  uint32_t active_energy_neg_raw = bytes_to_int(data, data_len - 4 - 5 * 6, 4);
  uint32_t reactive_energy_pos_raw =
      bytes_to_int(data, data_len - 4 - 5 * 5, 4);
  uint32_t reactive_energy_neg_raw =
      bytes_to_int(data, data_len - 4 - 5 * 4, 4);

  // use modulo 1000kwh for the energy sensors, because esphome sensors are only
  // 32bit floats values larger than that would suffer from precision errors
  // because the sensors are defined as total_increasing, home assistant will
  // still correctly display consumption
  float active_energy_pos   = (active_energy_pos_raw % 1000000) / 1000.0;
  float active_energy_neg   = (active_energy_neg_raw % 1000000) / 1000.0;
  float reactive_energy_pos = (reactive_energy_pos_raw % 1000000) / 1000.0;
  float reactive_energy_neg = (reactive_energy_neg_raw % 1000000) / 1000.0;
  float active_power_pos    = bytes_to_int(data, data_len - 4 - 5 * 3, 4);
  float active_power_neg    = bytes_to_int(data, data_len - 4 - 5 * 2, 4);
  float reactive_power_pos  = bytes_to_int(data, data_len - 4 - 5 * 1, 4);
  float reactive_power_neg  = bytes_to_int(data, data_len - 4 - 5 * 0, 4);

  if (this->active_energy_pos != nullptr &&
      this->active_energy_pos->state != active_energy_pos)
    this->active_energy_pos->publish_state(active_energy_pos);
  if (this->active_energy_neg != nullptr &&
      this->active_energy_neg->state != active_energy_neg)
    this->active_energy_neg->publish_state(active_energy_neg);
  if (this->reactive_energy_pos != nullptr &&
      this->reactive_energy_pos->state != reactive_energy_pos)
    this->reactive_energy_pos->publish_state(reactive_energy_pos);
  if (this->reactive_energy_neg != nullptr &&
      this->reactive_energy_neg->state != reactive_energy_neg)
    this->reactive_energy_neg->publish_state(reactive_energy_neg);
  if (this->active_power_pos != nullptr &&
      this->active_power_pos->state != active_power_pos)
    this->active_power_pos->publish_state(active_power_pos);
  if (this->active_power_neg != nullptr &&
      this->active_power_neg->state != active_power_neg)
    this->active_power_neg->publish_state(active_power_neg);
  if (this->reactive_power_pos != nullptr &&
      this->reactive_power_pos->state != reactive_power_pos)
    this->reactive_power_pos->publish_state(reactive_power_pos);
  if (this->reactive_power_neg != nullptr &&
      this->reactive_power_neg->state != reactive_power_neg)
    this->reactive_power_neg->publish_state(reactive_power_neg);

  char buffer[16];
  if (this->active_energy_pos_raw != nullptr) {
    itoa(active_energy_pos_raw, buffer, 10);
    if (this->active_energy_pos_raw->state != buffer)
      this->active_energy_pos_raw->publish_state(buffer);
  }
  if (this->active_energy_neg_raw != nullptr) {
    itoa(active_energy_neg_raw, buffer, 10);
    if (this->active_energy_neg_raw->state != buffer)
      this->active_energy_neg_raw->publish_state(buffer);
  }
  if (this->reactive_energy_pos_raw != nullptr) {
    itoa(reactive_energy_pos_raw, buffer, 10);
    if (this->reactive_energy_pos_raw->state != buffer)
      this->reactive_energy_pos_raw->publish_state(buffer);
  }
  if (this->reactive_energy_neg_raw != nullptr) {
    itoa(reactive_energy_neg_raw, buffer, 10);
    if (this->reactive_energy_neg_raw->state != buffer)
      this->reactive_energy_neg_raw->publish_state(buffer);
  }
}
} // namespace wienernetze
} // namespace esphome
