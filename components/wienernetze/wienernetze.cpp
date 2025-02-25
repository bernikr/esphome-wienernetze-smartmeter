#include "wienernetze.h"

namespace esphome
{
    namespace wienernetze
    {
        void WienerNetze::dump_config()
        {
            ESP_LOGCONFIG(TAG, "WienerNetze Smartmeter:");
            ESP_LOGCONFIG(TAG, "  version: %s", WIENERNETZE_VERSION);
        }

        void WienerNetze::loop()
        {
            unsigned long currentTime = millis();

            while (available())
            {
                uint8_t c = read();
                this->receiveBuffer.push_back(c);

                this->lastRead = currentTime;
            }

            if (!this->receiveBuffer.empty() && currentTime - this->lastRead > this->readTimeout)
            {
                ESP_LOGV(TAG, "raw recieved data: %s", format_hex_pretty(this->receiveBuffer).c_str());
                handle_message(this->receiveBuffer);
                this->receiveBuffer.clear(); // Reset buffer
            }
        }

        int WienerNetze::bytes_to_int(uint8_t bytes[], int left, int len)
        {
            int result = 0;
            for (unsigned int i = left; i < left + len; i++)
            {
                result = result * 256 + bytes[i];
            }
            return result;
        }

        void WienerNetze::handle_message(std::vector<uint8_t> msg)
        {
            uint8_t datalen = msg.size();

            if (msg[0] != 0x7e || msg[1] != 0xA0)
            {
                ESP_LOGW(TAG, "wrong opening bytes: %02x %02x, expected 7e a0", msg[0], msg[1]);
                return;
            }

            if (datalen != msg[2] + 2)
            {
                ESP_LOGW(TAG, "wrong msg length: %i, expected %i", datalen, msg[2] + 2);
                return;
            }

            if (msg[datalen - 1] != 0x7e)
            {
                ESP_LOGW(TAG, "wrong closing byte: %02x, expected 7e", msg[124]);
                return;
            }

            // Detect smartmeter type and make adjustments
            int offset = 0;
            if (memcmp(&msg[16], "SMSfp", 5) == 0)
            {
                ESP_LOGV(TAG, "Detected Siemens IM150/IM151/IM350/IM351");
            }
            else if (memcmp(&msg[14], "LGZgs", 5) == 0)
            {
                ESP_LOGV(TAG, "Detected Landis+Gyr E450/E570");
                offset = -2;
            }
            else if (memcmp(&msg[14], "ISKhu", 5) == 0)
            {
                ESP_LOGW(TAG, "Detected Iskraemeco AM550-TD0/AM550-ED0 (?)"); // change to LOGV when supported
                offset = -2;
                ESP_LOGW(TAG, "Support for this smartmeter is untested.");
                ESP_LOGW(TAG, "Please open a GitHub issue to report success or failure:");
                ESP_LOGW(TAG, "https://github.com/bernikr/esphome-wienernetze-smartmeter/issues/new");
            }
            else if (memcmp(&msg[14], "ISKit", 5) == 0)
            {
                ESP_LOGV(TAG, "Detected Iskraemeco AM550-TD0/AM550-ED0 (?)");
                offset = -2;
            }
            else
            {
                ESP_LOGW(TAG, "Unknown smartmeter model, support is untested.");
                ESP_LOGW(TAG, "Please open a GitHub issue and include the model of your smartmeter and the following data: %s", format_hex_pretty(std::vector<uint8_t>(&msg[14], &msg[14 + 7])).c_str());
                ESP_LOGW(TAG, "https://github.com/bernikr/esphome-wienernetze-smartmeter/issues/new");
            }

            // CRC Check
            int crc = this->CRC16.x25(msg.data() + 1, datalen - 4);
            int expected_crc = msg[datalen - 2] * 256 + msg[datalen - 3];
            if (crc != expected_crc)
            {
                ESP_LOGW(TAG, "crc mismatch: calculated %04x, expected %04x", crc, expected_crc);
                return;
            }

            // Decrypt
            uint8_t msglen = datalen - 33 - offset;
            uint8_t message[msglen] = {0};
            memcpy(message, msg.data() + 30 + offset, msglen);
            uint8_t nonce[16] = {0};
            memcpy(nonce, msg.data() + 16 + offset, 8);
            memcpy(nonce + 8, msg.data() + 26 + offset, 4);
            nonce[15] = 0x02;
            this->ctraes128.setKey(this->key, 16);
            this->ctraes128.setIV(nonce, 16);
            this->ctraes128.decrypt(message, message, msglen);
            ESP_LOGV(TAG, "decrypted data: %s", format_hex_pretty(std::vector<uint8_t>(message, message + msglen)).c_str());

            if (message[0] != 0x0f || message[msglen - 5] != 0x06 || message[msglen - 5 * 2] != 0x06 || message[msglen - 5 * 3] != 0x06 || message[msglen - 5 * 4] != 0x06 || message[msglen - 5 * 5] != 0x06 || message[msglen - 5 * 6] != 0x06 || message[msglen - 5 * 7] != 0x06 || message[msglen - 5 * 8] != 0x06)
            {
                ESP_LOGW(TAG, "decryption error, please check if your key is correct");
                return;
            }

            uint32_t active_energy_pos_raw = bytes_to_int(message, msglen - 4 - 5 * 7, 4);
            uint32_t active_energy_neg_raw = bytes_to_int(message, msglen - 4 - 5 * 6, 4);
            uint32_t reactive_energy_pos_raw = bytes_to_int(message, msglen - 4 - 5 * 5, 4);
            uint32_t reactive_energy_neg_raw = bytes_to_int(message, msglen - 4 - 5 * 4, 4);

            // use modulo 1000kwh for the energy sensors, because esphome sensors are only 32bit floats
            // values larger than that would suffer from precision errors
            // because the sensors are defined as total_increasing, home assistant will still correctly display consumption
            float active_energy_pos = (active_energy_pos_raw % 1000000) / 1000.0;
            float active_energy_neg = (active_energy_neg_raw % 1000000) / 1000.0;
            float reactive_energy_pos = (reactive_energy_pos_raw % 1000000) / 1000.0;
            float reactive_energy_neg = (reactive_energy_neg_raw % 1000000) / 1000.0;
            float active_power_pos = bytes_to_int(message, msglen - 4 - 5 * 3, 4);
            float active_power_neg = bytes_to_int(message, msglen - 4 - 5 * 2, 4);
            float reactive_power_pos = bytes_to_int(message, msglen - 4 - 5 * 1, 4);
            float reactive_power_neg = bytes_to_int(message, msglen - 4 - 5 * 0, 4);

            if (this->active_energy_pos != nullptr && this->active_energy_pos->state != active_energy_pos)
                this->active_energy_pos->publish_state(active_energy_pos);
            if (this->active_energy_neg != nullptr && this->active_energy_neg->state != active_energy_neg)
                this->active_energy_neg->publish_state(active_energy_neg);
            if (this->reactive_energy_pos != nullptr && this->reactive_energy_pos->state != reactive_energy_pos)
                this->reactive_energy_pos->publish_state(reactive_energy_pos);
            if (this->reactive_energy_neg != nullptr && this->reactive_energy_neg->state != reactive_energy_neg)
                this->reactive_energy_neg->publish_state(reactive_energy_neg);
            if (this->active_power_pos != nullptr && this->active_power_pos->state != active_power_pos)
                this->active_power_pos->publish_state(active_power_pos);
            if (this->active_power_neg != nullptr && this->active_power_neg->state != active_power_neg)
                this->active_power_neg->publish_state(active_power_neg);
            if (this->reactive_power_pos != nullptr && this->reactive_power_pos->state != reactive_power_pos)
                this->reactive_power_pos->publish_state(reactive_power_pos);
            if (this->reactive_power_neg != nullptr && this->reactive_power_neg->state != reactive_power_neg)
                this->reactive_power_neg->publish_state(reactive_power_neg);

            char buffer[16];
            if (this->active_energy_pos_raw != nullptr)
            {
                itoa(active_energy_pos_raw, buffer, 10);
                if (this->active_energy_pos_raw->state != buffer)
                    this->active_energy_pos_raw->publish_state(buffer);
            }
            if (this->active_energy_neg_raw != nullptr)
            {
                itoa(active_energy_neg_raw, buffer, 10);
                if (this->active_energy_neg_raw->state != buffer)
                    this->active_energy_neg_raw->publish_state(buffer);
            }
            if (this->reactive_energy_pos_raw != nullptr)
            {
                itoa(reactive_energy_pos_raw, buffer, 10);
                if (this->reactive_energy_pos_raw->state != buffer)
                    this->reactive_energy_pos_raw->publish_state(buffer);
            }
            if (this->reactive_energy_neg_raw != nullptr)
            {
                itoa(reactive_energy_neg_raw, buffer, 10);
                if (this->reactive_energy_neg_raw->state != buffer)
                    this->reactive_energy_neg_raw->publish_state(buffer);
            }
        }
    }
}