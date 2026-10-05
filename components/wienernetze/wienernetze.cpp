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

uint32_t read_uint32(const uint8_t* bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24) |
         (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
}

void decrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t* data,
    size_t data_len) {
#ifdef USE_ESP_IDF
  // ESP-IDF: use mbedTLS AES-128-CTR Decryption
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
  // Arduino: use rweather/Crypto library
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
    handle_message(this->receiveBuffer.data(), this->receiveBuffer.size());
    this->receiveBuffer.clear(); // Reset buffer
  }
}

void WienerNetze::handle_message(const uint8_t* msg, size_t msg_len) {
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
  uint16_t crc          = calculate_crc16_x25(msg + 1, msg_len - 4);
  uint16_t expected_crc = (msg[msg_len - 2] << 8) | msg[msg_len - 3];
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

  if (msg[1] & 0x08) {
    ESP_LOGE(TAG, "Segmented HDLC messages are not supported");
    return;
  }

  if (msg_len - 2 != (((msg[1] & 0x07) << 8) | msg[2])) {
    ESP_LOGE(TAG,
        "wrong msg length: %i, expected %i",
        msg_len,
        (((msg[1] & 0x07) << 8) | msg[2]) + 2);
    return;
  }

  // Dest and Src addresses are variable length (1, 2 or 4 bytes).
  // The least significant bit is only set for the last byte of each address.
  // Iterate until the second byte with a non-zero least significant bit is
  // found.
  size_t pos = 3;
  for (int i = 0; i < 2; i++) { // 2 iterations: Dest and Src
    while (pos < msg_len && (msg[pos] & 0x01) == 0) { // Skip leading zero bytes
      pos++;
    }
    pos++;
  }
  size_t control_field_pos = pos; // addresses are followed by the control field

  if (msg[control_field_pos] != 0x03 && msg[control_field_pos] != 0x13) {
    ESP_LOGE(TAG, "wrong control field: %02x, expected 0x03 or 0x13", msg[pos]);
    return;
  }

  // DLMS/COSEM Information Field (glo-general-ciphering / Suite 0)
  // +--------+---------+----------+----------+-----------+--------------+-
  // | D-LSAP | S-LSAP  | LLC Ctrl | APDU Tag | Title Len | System Title |
  // |   E6   |  E6/E7  |    00    |    DB    |    08     |  (8 bytes)   |
  // +--------+---------+----------+----------+-----------+--------------+-
  // ------------+----------+-------------+-------------------+
  //  Cipher Len | Sec Ctrl | Frame Count | Encrypted Payload |
  //  (1-3 bytes)|    20    |  (4 bytes)  |    (variable)     |
  // ------------+----------+-------------+-------------------+
  const uint8_t* information_field = &msg[control_field_pos + 3];
  uint8_t information_field_len    = msg_len - control_field_pos - 6;

  ESP_LOGV(TAG,
      "information field data: %s",
      format_hex_pretty(std::vector<uint8_t>(information_field,
                            information_field + information_field_len))
          .c_str());

  if (information_field[0] != 0xe6) {
    ESP_LOGE(TAG, "unexpected D-LSAP: %02x, expected e6", information_field[0]);
    return;
  }

  if (information_field[1] != 0xe6 && information_field[1] != 0xe7) {
    ESP_LOGE(TAG,
        "unexpected S-LSAP: %02x, expected e6 or e7",
        information_field[1]);
    return;
  }

  if (information_field[2] != 0x00) {
    ESP_LOGE(
        TAG, "unexpected LLC Ctrl: %02x, expected 00", information_field[2]);
    return;
  }

  if (information_field[3] != 0xdb) {
    ESP_LOGE(TAG,
        "unexpected APDU Tag: %02x, only db is supported",
        information_field[3]);
    return;
  }

  if (information_field[4] != 0x08) {
    ESP_LOGE(
        TAG, "unexpected Title Len: %02x, expected 08", information_field[4]);
    return;
  }

  const uint8_t* system_title = &information_field[5];
  ESP_LOGV(TAG,
      "system title: %.3s %s",
      reinterpret_cast<const char*>(system_title),
      format_hex_pretty(
          std::vector<uint8_t>(system_title + 3, system_title + 8))
          .c_str());

  // The Cypher/Data Length field can be 1-3 bytes, its A-XDR encoded, so 0x00
  // to 0x7F are encoded as the same value, for bigger values, it starts with a
  // byte having the MSB set giving the amount of bytes to follow, then the
  // value in the following bytes.
  uint8_t cypher_len;
  size_t cypher_len_len;
  if (!(information_field[13] & 0x80)) {
    cypher_len_len = 1;
    cypher_len     = information_field[13];
  } else if (information_field[13] == 0x81) {
    cypher_len_len = 2;
    cypher_len     = information_field[14];
  } else if (information_field[13] == 0x82) {
    cypher_len_len = 3;
    cypher_len     = information_field[14] << 8 | information_field[15];
  } else {
    ESP_LOGE(TAG, "unexpected cypher len field: %02x", information_field[13]);
    return;
  }

  // Subtract length of Sec Ctrl and Frame Count fields to get the length of the
  // data/cyphertext
  size_t data_len = cypher_len - 5;

  if (data_len != information_field_len - 18 - cypher_len_len) {
    ESP_LOGE(TAG,
        "cypher len field does not match the length of the data: %i != %i",
        data_len,
        information_field_len - 18 - cypher_len_len);
    return;
  }

  // Decrypt
  uint8_t data[data_len] = {0};
  memcpy(data, &information_field[18 + cypher_len_len], data_len);
  uint8_t nonce[16] = {0};
  memcpy(nonce, &information_field[5], 8);
  memcpy(nonce + 8, &information_field[14 + cypher_len_len], 4);
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

  uint32_t active_energy_pos_raw   = read_uint32(data + data_len - 4 - 5 * 7);
  uint32_t active_energy_neg_raw   = read_uint32(data + data_len - 4 - 5 * 6);
  uint32_t reactive_energy_pos_raw = read_uint32(data + data_len - 4 - 5 * 5);
  uint32_t reactive_energy_neg_raw = read_uint32(data + data_len - 4 - 5 * 4);

  // use modulo 1000kwh for the energy sensors, because esphome sensors are only
  // 32bit floats values larger than that would suffer from precision errors
  // because the sensors are defined as total_increasing, home assistant will
  // still correctly display consumption
  float active_energy_pos   = (active_energy_pos_raw % 1000000) / 1000.0;
  float active_energy_neg   = (active_energy_neg_raw % 1000000) / 1000.0;
  float reactive_energy_pos = (reactive_energy_pos_raw % 1000000) / 1000.0;
  float reactive_energy_neg = (reactive_energy_neg_raw % 1000000) / 1000.0;
  float active_power_pos    = read_uint32(data + data_len - 4 - 5 * 3);
  float active_power_neg    = read_uint32(data + data_len - 4 - 5 * 2);
  float reactive_power_pos  = read_uint32(data + data_len - 4 - 5 * 1);
  float reactive_power_neg  = read_uint32(data + data_len - 4 - 5 * 0);

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
