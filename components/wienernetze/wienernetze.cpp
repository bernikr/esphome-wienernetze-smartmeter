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

#define READ_TIMEOUT 100 // Time to wait after last byte before decoding

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
  return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
         ((uint32_t)bytes[2] << 8) | bytes[3];
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

void format_dlms_time(const uint8_t* buf, char* out_str, size_t max_len) {
  // 1. Extract date & time fields
  uint16_t year  = ((uint16_t)buf[0] << 8) | buf[1];
  uint8_t month  = buf[2];
  uint8_t day    = buf[3];
  uint8_t hour   = buf[5]; // byte 4 is day-of-week, skip it
  uint8_t minute = buf[6];
  uint8_t second = buf[7];

  // 2. Extract and invert the deviation to get the UTC offset
  int16_t dev_min = (int16_t)(((uint16_t)buf[9] << 8) | buf[10]);

  if (dev_min != (int16_t)0x8000) {
    // Invert because DLMS defines: Deviation = UTC - LocalTime
    int16_t offset_min = -dev_min;
    int tz_hours       = offset_min / 60;
    int tz_mins        = std::abs(offset_min % 60);

    // Full ISO 8601: "YYYY-MM-DDTHH:MM:SS+02:00"
    snprintf(out_str,
        max_len,
        "%04u-%02u-%02uT%02u:%02u:%02u%+03d:%02u",
        year,
        month,
        day,
        hour,
        minute,
        second,
        tz_hours,
        tz_mins);
  } else {
    // Fallback if meter sends 0x8000 (deviation not specified)
    snprintf(out_str,
        max_len,
        "%04u-%02u-%02u %02u:%02u:%02u",
        year,
        month,
        day,
        hour,
        minute,
        second);
  }
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
      currentTime - this->lastRead > READ_TIMEOUT) {
    ESP_LOGV(TAG,
        "raw received data: %s",
        format_hex_pretty(this->receiveBuffer).c_str());
    handle_message(this->receiveBuffer.data(), this->receiveBuffer.size());
    this->receiveBuffer.clear(); // Reset buffer
  }
}

void WienerNetze::handle_message(const uint8_t* msg, size_t msg_len) {
  // HDLC Frame Format Type 3
  //  0      1               3               3+?           3+x
  //  +------+---------------+---------------+-------------+---------+-
  //  | Flag | Frame Format  | Dest Address  | Src Address | Control |
  //  |  7E  |   (2 bytes)   |  (1-4 bytes)  | (1-4 bytes) | (1 byte)|
  //  +------+---------------+---------------+-------------+---------+-
  //  4+x      6+x           len-3     len-1
  // -+--------+-------------+---------+------+
  //  |  HCS   | Information |   FCS   | Flag |
  //  |(2 byte)|  (APDU data)| (2 byte)|  7E  |
  // -+--------+-------------+---------+------+

  if (msg[0] != 0x7e) {
    ESP_LOGW(TAG, "wrong opening byte: %02x, expected 7e", msg[0]);
    return;
  }

  if (msg[msg_len - 1] != 0x7e) {
    ESP_LOGW(TAG, "wrong closing byte: %02x, expected 7e", msg[msg_len - 1]);
    return;
  }

  // CRC Check (FCS)
  uint16_t crc = calculate_crc16_x25(msg + 1, msg_len - 4);
  uint16_t expected_crc =
      ((uint16_t)(msg[msg_len - 2] << 8) | msg[msg_len - 3]);
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
    ESP_LOGE(TAG, "segmented HDLC messages are not supported");
    return;
  }

  size_t msg_len_read = (((uint16_t)(msg[1] & 0x07) << 8) | msg[2]) + 2;
  if (msg_len != msg_len_read) {
    ESP_LOGE(TAG, "wrong msg length: %i, expected %i", msg_len, msg_len_read);
    return;
  }

  // Dest and Src addresses are variable length (1, 2 or 4 bytes).
  // The least significant bit is only set for the last byte of each address.
  // Iterate until the second byte with a non-zero least significant bit is
  // found.
  size_t pos = 3;
  for (int i = 0; i < 2; i++) { // 2 iterations: Dest and Src
    while (pos < msg_len && (msg[pos] & 0x01) == 0) { // Skip to last byte
      pos++;
    }
    pos++;
  }
  size_t addr_len = pos - 3;

  if (msg[3 + addr_len] != 0x03 && msg[3 + addr_len] != 0x13) {
    ESP_LOGE(TAG,
        "wrong control field: %02x, expected 0x03 or 0x13",
        msg[3 + addr_len]);
    return;
  }

  const uint8_t* information_field = &msg[6 + addr_len];
  size_t information_field_len     = msg_len - 6 - addr_len - 3;

  ESP_LOGV(TAG,
      "information field data: %s",
      format_hex_pretty(std::vector<uint8_t>(information_field,
                            information_field + information_field_len))
          .c_str());

  // DLMS/COSEM Information Field (glo-general-ciphering / Suite 0)
  //  0        1         2          3          4           5
  //  +--------+---------+----------+----------+-----------+--------------+-
  //  | D-LSAP | S-LSAP  | LLC Ctrl | APDU Tag | Title Len | System Title |
  //  |   E6   |  E6/E7  |    00    |    DB    |    08     |  (8 bytes)   |
  //  +--------+---------+----------+----------+-----------+--------------+-
  //  13           13+x       14+x          18+x
  // -+------------+----------+-------------+-------------------+
  //  | Cipher Len | Sec Ctrl | Frame Count | Encrypted Payload |
  //  | (1-3 bytes)|    20    |  (4 bytes)  |    (variable)     |
  // -+------------+----------+-------------+-------------------+

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
      system_title,
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
    cypher_len = ((uint16_t)information_field[14] << 8) | information_field[15];
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
  uint8_t iv[16] = {0};
  memcpy(iv, &information_field[5], 8);
  memcpy(iv + 8, &information_field[14 + cypher_len_len], 4);
  iv[15] = 0x02;

  decrypt(this->key, iv, data, data_len);

  ESP_LOGV(TAG,
      "decrypted data: %s",
      format_hex_pretty(std::vector<uint8_t>(data, data + data_len)).c_str());

  // Decrypted DLMS/COSEM Payload (Data-Notification APDU: 0F)
  //  0          1            2                5             6
  //  +----------+------------+----------------+-------------+----------------+-
  //  | APDU Tag | Prio/Flags | Long-Invoke-Id | Date-Time L | APDU Timestamp |
  //  |    0F    |     00     |   (3 bytes)    |     0C      |   (12 bytes)   |
  //  +----------+------------+----------------+-------------+----------------+-
  //  18           19           20              22
  // -+------------+------------+---------------+--------------------+-
  //  | Struct Tag | Struct Qty | Str Tag & Len |  Element 1: Serial |
  //  |     02     |     0A     |     09 10     |     (16 bytes)     |
  // -+------------+------------+---------------+--------------------+-
  //  38           40                   52         53
  // -+------------+--------------------+----------+--------------------+-
  //  | Str Tag & L|  Element 2: Time   | Type Tag |   Element 3: +A    |
  //  |    09 0C   |     (12 bytes)     |    06    |    (uint32 Wh)     |
  // -+------------+--------------------+----------+--------------------+-
  //  57           58                   62         63
  // -+------------+--------------------+----------+--------------------+-
  //  | Type Tag   |   Element 4: -A    | Type Tag |   Element 5: +R    |
  //  |    06      |    (uint32 Wh)     |    06    |   (uint32 varh)    |
  // -+------------+--------------------+----------+--------------------+-
  //  67           68                   72         73
  // -+------------+--------------------+----------+--------------------+-
  //  | Type Tag   |   Element 6: -R    | Type Tag |   Element 7: +P    |
  //  |    06      |   (uint32 varh)    |    06    |     (uint32 W)     |
  // -+------------+--------------------+----------+--------------------+-
  //  77           78                   82         83
  // -+------------+--------------------+----------+--------------------+-
  //  | Type Tag   |   Element 8: -P    | Type Tag |   Element 9: +Q    |
  //  |    06      |     (uint32 W)     |    06    |    (uint32 var)    |
  // -+------------+--------------------+----------+--------------------+-
  //  87           88
  // -+------------+--------------------+
  //  | Type Tag   |   Element 10: -Q   |
  //  |    06      |    (uint32 var)    |
  // -+------------+--------------------+

  // Check some well known bytes in the decrypted data
  if (data[0] != 0x0f || data[1] != 0x00 || data[5] != 0x0c ||
      data[18] != 0x02) {
    ESP_LOGE(TAG, "decryption error, please check if your key is correct");
    return;
  }

  // Not all meters send the serial number. Wiener Netze documents it as the
  // first element for Siemens meters (10 elements), Landis+Gyr and Iskraemeco
  // meters start directly with the time (9 elements), and so do some Siemens
  // meters (e.g. IM351 with system title SMSgp). All following offsets are
  // relative to the start of the time element.
  // https://www.wienernetze.at/smart-meter-kundenschnittstelle
  size_t time_pos;
  if (data[19] == 0x0a) {
    // Meter Serial Number
    if (data[20] != 0x09 || data[21] != 0x10) {
      ESP_LOGE(TAG,
          "unexpected meter serial number tag: %02x %02x, expected 09 10",
          data[20],
          data[21]);
      return;
    }
    ESP_LOGV(TAG, "meter serial number: %.16s", &data[22]);
    time_pos = 38;
  } else if (data[19] == 0x09) {
    time_pos = 20;
  } else {
    ESP_LOGE(TAG,
        "unexpected number of elements: %02x, expected 0a or 09",
        data[19]);
    return;
  }

  if (data_len < time_pos + 2 + 12 + 8 * 5) {
    ESP_LOGE(TAG,
        "decrypted data too short: %i bytes, expected at least %i",
        data_len,
        time_pos + 2 + 12 + 8 * 5);
    return;
  }

  if (data[time_pos] != 0x09 || data[time_pos + 1] != 0x0c) {
    ESP_LOGE(TAG,
        "unexpected measurement time tag: %02x %02x, expected 09 0c",
        data[time_pos],
        data[time_pos + 1]);
    return;
  }
  const uint8_t* measurement_time = &data[time_pos + 2];

  // Elements 3 to 10 (or 2 to 9 without serial number), each a Type Tag 06
  // followed by a uint32
  const uint8_t* values = &data[time_pos + 14];
  for (int i = 0; i < 8; i++) {
    if (values[5 * i] != 0x06) {
      ESP_LOGE(TAG,
          "unexpected type tag of value %i: %02x, expected 06",
          i + 1,
          values[5 * i]);
      return;
    }
  }

  // Measurement Time
  char time_buf[35];
  format_dlms_time(measurement_time, time_buf, sizeof(time_buf));
  ESP_LOGV(TAG, "meter measurement time: %s", time_buf);

  if (memcmp(measurement_time, &data[6], 12)) {
    ESP_LOGW(TAG, "difference between measurement time and meter time");
    format_dlms_time(&data[6], time_buf, sizeof(time_buf));
    ESP_LOGW(TAG, "meter time: %s", time_buf);
  }

  uint32_t active_energy_pos_raw   = read_uint32(&values[1 + 5 * 0]);
  uint32_t active_energy_neg_raw   = read_uint32(&values[1 + 5 * 1]);
  uint32_t reactive_energy_pos_raw = read_uint32(&values[1 + 5 * 2]);
  uint32_t reactive_energy_neg_raw = read_uint32(&values[1 + 5 * 3]);

  // use modulo 1000kwh for the energy sensors, because esphome sensors are only
  // 32bit floats values larger than that would suffer from precision errors
  // because the sensors are defined as total_increasing, home assistant will
  // still correctly display consumption
  float active_energy_pos   = (active_energy_pos_raw % 1000000) / 1000.0;
  float active_energy_neg   = (active_energy_neg_raw % 1000000) / 1000.0;
  float reactive_energy_pos = (reactive_energy_pos_raw % 1000000) / 1000.0;
  float reactive_energy_neg = (reactive_energy_neg_raw % 1000000) / 1000.0;
  float active_power_pos    = read_uint32(&values[1 + 5 * 4]);
  float active_power_neg    = read_uint32(&values[1 + 5 * 5]);
  float reactive_power_pos  = read_uint32(&values[1 + 5 * 6]);
  float reactive_power_neg  = read_uint32(&values[1 + 5 * 7]);

#define WRITE_SENSOR(name)                                  \
  if (this->name != nullptr && this->name->state != name) { \
    this->name->publish_state(name);                        \
  }

  WRITE_SENSOR(active_energy_pos);
  WRITE_SENSOR(active_energy_neg);
  WRITE_SENSOR(reactive_energy_pos);
  WRITE_SENSOR(reactive_energy_neg);
  WRITE_SENSOR(active_power_pos);
  WRITE_SENSOR(active_power_neg);
  WRITE_SENSOR(reactive_power_pos);
  WRITE_SENSOR(reactive_power_neg);

  char buffer[16];

#define WRITE_TEXT_SENSOR(name)          \
  if (this->name != nullptr) {           \
    itoa(name, buffer, 10);              \
    if (this->name->state != buffer) {   \
      this->name->publish_state(buffer); \
    }                                    \
  }

  WRITE_TEXT_SENSOR(active_energy_pos_raw);
  WRITE_TEXT_SENSOR(active_energy_neg_raw);
  WRITE_TEXT_SENSOR(reactive_energy_pos_raw);
  WRITE_TEXT_SENSOR(reactive_energy_neg_raw);
}
} // namespace wienernetze
} // namespace esphome
