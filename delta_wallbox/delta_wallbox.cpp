#include "delta_wallbox.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cstring>

#ifdef USE_ESP32

namespace esphome::delta_wallbox {

static const char *const TAG = "delta_wallbox";

static const char *delta_state_to_string(DeltaState state) {
  switch (state) {
    case DeltaState::STATE_IDLE:
      return "IDLE";
    case DeltaState::STATE_GET_MODEL:
      return "GET_MODEL";
    case DeltaState::STATE_GET_BT_FW_VERSION:
      return "GET_BT_FW_VERSION";
    case DeltaState::STATE_CHECK_PAIRING:
      return "CHECK_PAIRING";
    case DeltaState::STATE_GET_LOGIN_USER:
      return "GET_LOGIN_USER";
    case DeltaState::STATE_GET_LOGIN_RESULT0:
      return "GET_LOGIN_RESULT0";
    case DeltaState::STATE_SEND_LOGIN_CREDENTIALS:
      return "SEND_LOGIN_CREDENTIALS";
    case DeltaState::STATE_GET_LOGIN_RESULT:
      return "GET_LOGIN_RESULT";
    case DeltaState::STATE_GET_LOGIN_ROLE:
      return "GET_LOGIN_ROLE";
    case DeltaState::STATE_TIME_SYNC:
      return "TIME_SYNC";
    case DeltaState::STATE_GET_CHARGER_FW_VERSION:
      return "GET_CHARGER_FW_VERSION";
    case DeltaState::STATE_CONNECTED:
      return "CONNECTED";
    default:
      return "UNKNOWN";
  }
}

// UUID references
static const esp32_ble_tracker::ESPBTUUID SERVICE_UUID = esp32_ble_tracker::ESPBTUUID::from_raw("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
static const esp32_ble_tracker::ESPBTUUID TX_UUID = esp32_ble_tracker::ESPBTUUID::from_raw("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
static const esp32_ble_tracker::ESPBTUUID RX_UUID = esp32_ble_tracker::ESPBTUUID::from_raw("6e400003-b5a3-f393-e0a9-e50e24dcca9e");

// Modbus CRC-16 calculations
uint16_t crc16_modbus(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x01) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

void append_crc(std::vector<uint8_t> &frame) {
  uint16_t crc = crc16_modbus(frame.data(), frame.size());
  frame.push_back(crc & 0xFF);
  frame.push_back((crc >> 8) & 0xFF);
}

bool verify_crc(const uint8_t *data, size_t len) {
  if (len < 5) return false;
  uint16_t calculated = crc16_modbus(data, len - 2);
  uint16_t received = data[len - 2] | (data[len - 1] << 8);
  return calculated == received;
}

// Build Modbus structures mirroring delta_wallbox.py
std::vector<uint8_t> build_modbus_frame(const std::string &hex_str) {
  std::vector<uint8_t> frame;
  for (size_t i = 0; i < hex_str.length(); i += 2) {
    std::string byteString = hex_str.substr(i, 2);
    uint8_t byte = (uint8_t) strtol(byteString.c_str(), nullptr, 16);
    frame.push_back(byte);
  }
  append_crc(frame);
  return frame;
}

std::vector<uint8_t> build_login_command(const std::string &user, const std::string &password) {
  std::vector<uint8_t> frame;
  frame.push_back(0x01); // Unit ID
  frame.push_back(0x10); // FC16 Write Multiple Registers
  frame.push_back(0x01); // Reg Hi
  frame.push_back(0x72); // Reg Lo (0x0172)
  frame.push_back(0x00); // Registers Qty Hi
  frame.push_back(0x0E); // Registers Qty Lo (14 registers)
  frame.push_back(0x1C); // Byte count (28 bytes)

  // Password chunk: 22 bytes (44 hex chars), left-padded
  std::vector<uint8_t> pass_bytes(22, 0);
  size_t pass_len = password.length();
  if (pass_len > 22) pass_len = 22;
  std::copy(password.begin(), password.begin() + pass_len, pass_bytes.end() - pass_len);

  // User chunk: 6 bytes (12 hex chars), left-padded
  std::vector<uint8_t> user_bytes(6, 0);
  bool is_digit = true;
  for (char c : user) {
    if (!isdigit(c)) {
      is_digit = false;
      break;
    }
  }
  if (is_digit && !user.empty()) {
    uint64_t val = strtoull(user.c_str(), nullptr, 10);
    for (int i = 0; i < 6; i++) {
      user_bytes[5 - i] = (val >> (8 * i)) & 0xFF;
    }
  } else {
    size_t user_len = user.length();
    if (user_len > 6) user_len = 6;
    std::copy(user.begin(), user.begin() + user_len, user_bytes.end() - user_len);
  }

  frame.insert(frame.end(), pass_bytes.begin(), pass_bytes.end());
  frame.insert(frame.end(), user_bytes.begin(), user_bytes.end());

  append_crc(frame);
  return frame;
}

std::vector<uint8_t> build_time_sync_command(int year, int month, int day, int hour, int minute, int second, int weekday) {
  std::vector<uint8_t> frame;
  frame.push_back(0x01); // Unit ID
  frame.push_back(0x10); // FC16 Write Multiple Registers
  frame.push_back(0x01); // Address High
  frame.push_back(0x14); // Address Low (0x0114)
  frame.push_back(0x00); // Qty Hi
  frame.push_back(0x04); // Qty Lo (4 registers)
  frame.push_back(0x08); // Byte Count

  int year_lo = year % 100;
  uint8_t weekday_bitmap = 1 << weekday; // Sunday=bit0, Mon=bit1...

  frame.push_back(year_lo & 0xFF);
  frame.push_back(month & 0xFF);
  frame.push_back(weekday_bitmap & 0xFF);
  frame.push_back(day & 0xFF);
  frame.push_back(hour & 0xFF);
  frame.push_back(minute & 0xFF);
  frame.push_back(second & 0xFF);
  frame.push_back(0x00);

  append_crc(frame);
  return frame;
}

void DeltaWallbox::setup() {
  this->state_ = DeltaState::STATE_IDLE;
  this->rx_buffer_.clear();
}

void DeltaWallbox::dump_config() {
  ESP_LOGCONFIG(TAG, "Delta Wallbox:");
  ESP_LOGCONFIG(TAG, "  User: %s", this->user_.c_str());
  ESP_LOGCONFIG(TAG, "  BLE PIN configured: %s", YESNO(this->has_pin_));
  LOG_UPDATE_INTERVAL(this);
}

void DeltaWallbox::loop() {
  if (this->node_state == esp32_ble_tracker::ClientState::ESTABLISHED) {
    if (this->state_ == DeltaState::STATE_IDLE) {
      ESP_LOGI(TAG, "GATT connection established. Starting authentication state machine.");
      this->state_ = DeltaState::STATE_GET_MODEL;
      this->send_next_state_command();
    } else if (this->state_ != DeltaState::STATE_CONNECTED) {
      // Re-transmit state-based command on timeout
      uint32_t now = millis();
      if (now - this->last_tx_time_ > 4000) {
        ESP_LOGW(TAG, "Timeout waiting in login state %s for %u ms. Retransmitting command...",
                 delta_state_to_string(this->state_), (unsigned int) (now - this->last_tx_time_));
        this->send_next_state_command();
      }
    }
  }
}

void DeltaWallbox::update() {
  if (this->is_connected()) {
    // Start poll flow sequence
    ESP_LOGD(TAG, "Polling charging info...");
    std::vector<uint8_t> cmd = build_modbus_frame("0103011B0009");
    this->write_frame(cmd);
    this->last_tx_time_ = millis();
  }
}

void DeltaWallbox::write_frame(const std::vector<uint8_t> &frame) {
  if (this->tx_char_handle_ == 0) {
    ESP_LOGW(TAG, "TX characteristic handle is not ready while in state %s. Dropping %u-byte frame.",
             delta_state_to_string(this->state_), (unsigned int) frame.size());
    return;
  }
  ESP_LOGD(TAG, "Sending %u-byte frame in state %s (conn_id=%u, tx_handle=0x%04X).", (unsigned int) frame.size(),
           delta_state_to_string(this->state_), this->parent_->get_conn_id(), this->tx_char_handle_);
  auto status = esp_ble_gattc_write_char(this->parent_->get_gattc_if(), this->parent_->get_conn_id(),
                                         this->tx_char_handle_, frame.size(), const_cast<uint8_t*>(frame.data()),
                                         ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (status != ESP_GATT_OK) {
    ESP_LOGW(TAG, "esp_ble_gattc_write_char failed, status=%d", status);
  }
}

void DeltaWallbox::send_next_state_command() {
  std::vector<uint8_t> cmd;
  ESP_LOGD(TAG, "Preparing command for state %s.", delta_state_to_string(this->state_));
  switch (this->state_) {
    case DeltaState::STATE_GET_MODEL:
      ESP_LOGI(TAG, "Requesting charger model name...");
      cmd = build_modbus_frame("01030185000E");
      break;
    case DeltaState::STATE_GET_BT_FW_VERSION:
      ESP_LOGI(TAG, "Requesting BT firmware version...");
      cmd = build_modbus_frame("020301000001");
      break;
    case DeltaState::STATE_CHECK_PAIRING:
      ESP_LOGI(TAG, "Checking pairing status...");
      cmd = build_modbus_frame("020301000003");
      break;
    case DeltaState::STATE_GET_LOGIN_USER:
      ESP_LOGI(TAG, "Querying user profile...");
      cmd = build_modbus_frame("0103017D0003");
      break;
    case DeltaState::STATE_GET_LOGIN_RESULT0:
      ESP_LOGI(TAG, "Checking pre-login status register...");
      cmd = build_modbus_frame("010301800001");
      break;
    case DeltaState::STATE_SEND_LOGIN_CREDENTIALS:
      ESP_LOGI(TAG, "Transmitting login credentials...");
      cmd = build_login_command(this->user_, this->password_);
      break;
    case DeltaState::STATE_GET_LOGIN_RESULT:
      ESP_LOGI(TAG, "Verifying login result...");
      cmd = build_modbus_frame("010301800001");
      break;
    case DeltaState::STATE_GET_LOGIN_ROLE:
      ESP_LOGI(TAG, "Checking role privileges...");
      cmd = build_modbus_frame("010301810001");
      break;
    case DeltaState::STATE_TIME_SYNC: {
      ESP_LOGI(TAG, "Synchronizing wallbox clock...");
      int year = 2026, month = 1, day = 1, hour = 0, minute = 0, second = 0, weekday = 0;
#ifdef USE_TIME
      if (this->time_id_ != nullptr && this->time_id_->now().is_valid()) {
        auto tm = this->time_id_->now();
        year = tm.year;
        month = tm.month;
        day = tm.day_of_month;
        hour = tm.hour;
        minute = tm.minute;
        second = tm.second;
        // ESPTime day_of_week is 1 (Sunday) to 7 (Saturday) -> map to 0 (Sunday) to 6 (Saturday)
        weekday = tm.day_of_week - 1;
      }
#endif
      cmd = build_time_sync_command(year, month, day, hour, minute, second, weekday);
      break;
    }
    case DeltaState::STATE_GET_CHARGER_FW_VERSION:
      ESP_LOGI(TAG, "Requesting charger main firmware version...");
      cmd = build_modbus_frame("010301100002");
      break;
    default:
      return;
  }
  this->write_frame(cmd);
  this->last_tx_time_ = millis();
}

void DeltaWallbox::handle_frame(const std::vector<uint8_t> &frame) {
  ESP_LOGD(TAG, "Received valid frame in state %s: func=0x%02X len=%u", delta_state_to_string(this->state_), frame[1],
           (unsigned int) frame.size());
  if (frame[1] & 0x80) {
    ESP_LOGE(TAG, "Modbus exception received in state %d! Error code: 0x%02X", (int)this->state_, frame[2]);
    return;
  }

  switch (this->state_) {
    case DeltaState::STATE_GET_MODEL: {
      // 28 parse bytes
      std::string model(reinterpret_cast<const char*>(frame.data() + 3), frame[2]);
      model.erase(std::remove(model.begin(), model.end(), '\0'), model.end());
      ESP_LOGI(TAG, "Discovered Wallbox Model Name: %s", model.c_str());
      if (this->model_name_sensor_ != nullptr) {
        this->model_name_sensor_->publish_state(model);
      }
      ESP_LOGD(TAG, "State transition: %s -> %s", delta_state_to_string(this->state_),
               delta_state_to_string(DeltaState::STATE_GET_BT_FW_VERSION));
      this->state_ = DeltaState::STATE_GET_BT_FW_VERSION;
      this->send_next_state_command();
      break;
    }
    case DeltaState::STATE_GET_BT_FW_VERSION: {
      if (frame[2] >= 2) {
        uint8_t major = frame[3] % 16;
        uint8_t minor = frame[4];
        char ver[16];
        sprintf(ver, "%d.%d", major, minor);
        ESP_LOGI(TAG, "Discovered Wallbox BT FW: %s", ver);
        if (this->bt_fw_version_sensor_ != nullptr) {
          this->bt_fw_version_sensor_->publish_state(ver);
        }
      }
      ESP_LOGD(TAG, "State transition: %s -> %s", delta_state_to_string(this->state_),
               delta_state_to_string(DeltaState::STATE_CHECK_PAIRING));
      this->state_ = DeltaState::STATE_CHECK_PAIRING;
      this->send_next_state_command();
      break;
    }
    case DeltaState::STATE_CHECK_PAIRING: {
      if (frame[2] >= 2) {
        uint16_t status = (frame[3] << 8) | frame[4];
        ESP_LOGI(TAG, "Pairing Status Register: 0x%04X", status);
        if (status == 0x0007) {
          this->pairing_in_progress_ = false;
          this->pairing_attempts_ = 0;
          ESP_LOGI(TAG, "Pairing ready and confirmed!");
          ESP_LOGD(TAG, "State transition: %s -> %s", delta_state_to_string(this->state_),
                   delta_state_to_string(DeltaState::STATE_GET_LOGIN_USER));
          this->state_ = DeltaState::STATE_GET_LOGIN_USER;
          this->send_next_state_command();
        } else if (status == 0x0008) {
          ESP_LOGW(TAG, "Wallbox is waiting to be paired by the system controller (attempt %u, in_progress=%s).",
                   this->pairing_attempts_ + 1, YESNO(this->pairing_in_progress_));
          this->pairing_attempts_++;
          if (!this->pairing_in_progress_) {
            ESP_LOGI(TAG, "Initiating BLE pairing with wallbox.");
            auto err = this->parent_->pair();
            if (err != ESP_OK) {
              ESP_LOGW(TAG, "Failed to initiate BLE pairing, err=%d", err);
            } else {
              this->pairing_in_progress_ = true;
            }
          }
          if (this->pairing_attempts_ > 5) {
            ESP_LOGE(TAG, "Too many failed/waiting pairing checks. Resetting status.");
            this->reset_state();
          }
        }
      } else {
        ESP_LOGW(TAG, "Pairing status response too short: byte_count=%u frame_len=%u", frame[2],
                 (unsigned int) frame.size());
      }
      break;
    }
    case DeltaState::STATE_GET_LOGIN_USER: {
      ESP_LOGD(TAG, "State transition: %s -> %s", delta_state_to_string(this->state_),
               delta_state_to_string(DeltaState::STATE_GET_LOGIN_RESULT0));
      this->state_ = DeltaState::STATE_GET_LOGIN_RESULT0;
      this->send_next_state_command();
      break;
    }
    case DeltaState::STATE_GET_LOGIN_RESULT0: {
      ESP_LOGD(TAG, "State transition: %s -> %s", delta_state_to_string(this->state_),
               delta_state_to_string(DeltaState::STATE_SEND_LOGIN_CREDENTIALS));
      this->state_ = DeltaState::STATE_SEND_LOGIN_CREDENTIALS;
      this->send_next_state_command();
      break;
    }
    case DeltaState::STATE_SEND_LOGIN_CREDENTIALS: {
      ESP_LOGD(TAG, "State transition: %s -> %s", delta_state_to_string(this->state_),
               delta_state_to_string(DeltaState::STATE_GET_LOGIN_RESULT));
      this->state_ = DeltaState::STATE_GET_LOGIN_RESULT;
      this->send_next_state_command();
      break;
    }
    case DeltaState::STATE_GET_LOGIN_RESULT: {
      if (frame[2] >= 2) {
        uint8_t login_val = frame[4]; // respondent data_lo
        if (login_val == 0x01) {
          ESP_LOGI(TAG, "Credentials accepted! Logged in as Admin.");
          ESP_LOGD(TAG, "State transition: %s -> %s", delta_state_to_string(this->state_),
                   delta_state_to_string(DeltaState::STATE_GET_LOGIN_ROLE));
          this->state_ = DeltaState::STATE_GET_LOGIN_ROLE;
          this->send_next_state_command();
        } else {
          ESP_LOGE(TAG, "Login validation failed (0x%02X). Check password.", login_val);
          this->reset_state();
        }
      }
      break;
    }
    case DeltaState::STATE_GET_LOGIN_ROLE: {
      ESP_LOGD(TAG, "State transition: %s -> %s", delta_state_to_string(this->state_),
               delta_state_to_string(DeltaState::STATE_TIME_SYNC));
      this->state_ = DeltaState::STATE_TIME_SYNC;
      this->send_next_state_command();
      break;
    }
    case DeltaState::STATE_TIME_SYNC: {
      ESP_LOGD(TAG, "State transition: %s -> %s", delta_state_to_string(this->state_),
               delta_state_to_string(DeltaState::STATE_GET_CHARGER_FW_VERSION));
      this->state_ = DeltaState::STATE_GET_CHARGER_FW_VERSION;
      this->send_next_state_command();
      break;
    }
    case DeltaState::STATE_GET_CHARGER_FW_VERSION: {
      if (frame[2] >= 4) {
        char ver[16];
        sprintf(ver, "%d.%d", frame[3], frame[4]);
        ESP_LOGI(TAG, "Discovered Charger Main FW: %s", ver);
        if (this->fw_version_sensor_ != nullptr) {
          this->fw_version_sensor_->publish_state(ver);
        }
      }
      ESP_LOGD(TAG, "State transition: %s -> %s", delta_state_to_string(this->state_),
               delta_state_to_string(DeltaState::STATE_CONNECTED));
      this->state_ = DeltaState::STATE_CONNECTED;
      ESP_LOGI(TAG, "Authentication complete. Core parameters loaded.");
      this->status_clear_warning();
      this->update(); // perform first poll update immediately
      break;
    }
    case DeltaState::STATE_CONNECTED: {
      // Connected polling responses
      if (frame[1] == 0x03) {
        uint8_t byte_count = frame[2];
        if (byte_count == 18) {
          // Parsing 0x011B charging info (9 registers)
          uint32_t power_raw = (frame[3] << 24) | (frame[4] << 16) | (frame[5] << 8) | frame[6];
          uint32_t energy_raw = (frame[7] << 24) | (frame[8] << 16) | (frame[9] << 8) | frame[10];
          uint8_t minutes = frame[15];
          uint8_t hours = frame[16];
          uint8_t state_byte = frame[17];

          float power_kw = power_raw / 1000.0f;
          float energy_kwh = energy_raw / 1000.0f;
          float charging_minutes = hours * 60 + minutes;

          this->is_charging_ = (state_byte == 0x03);

          if (this->power_sensor_ != nullptr) this->power_sensor_->publish_state(power_kw);
          if (this->energy_sensor_ != nullptr) this->energy_sensor_->publish_state(energy_kwh);
          if (this->charging_time_sensor_ != nullptr) this->charging_time_sensor_->publish_state(charging_minutes);
          if (this->charger_state_sensor_ != nullptr) this->charger_state_sensor_->publish_state(state_byte);

          // Retrieve maximum current output setting next
          std::vector<uint8_t> cmd = build_modbus_frame("010301120002");
          this->write_frame(cmd);
        } else if (byte_count == 4) {
          // Parsing 0x0112 max output setting (2 registers)
          uint32_t current_raw = (frame[3] << 24) | (frame[4] << 16) | (frame[5] << 8) | frame[6];
          float amps = current_raw / 10.0f;
          if (this->max_current_sensor_ != nullptr) {
            this->max_current_sensor_->publish_state(amps);
          }

          // Query system serial number next
          std::vector<uint8_t> cmd = build_modbus_frame("010301090001");
          this->write_frame(cmd);
        } else if (byte_count == 2) {
          // Parsing single register responses: serial SN (0x0109) or error status (0x01B5)
          uint16_t reg_val = (frame[3] << 8) | frame[4];
          char hex_str[16];
          sprintf(hex_str, "%04X", reg_val);

          // We check which registry poll expects this value by verifying which sensor needs update
          if (this->serial_number_sensor_ != nullptr && !this->serial_number_sensor_->has_state()) {
            this->serial_number_sensor_->publish_state(hex_str);
          } else if (this->error_code_sensor_ != nullptr) {
            this->error_code_sensor_->publish_state(hex_str);
          }

          // If there's no state updated for error code yet, poll error code register
          if (this->error_code_sensor_ != nullptr && this->error_code_sensor_->state == "unknown") {
            std::vector<uint8_t> cmd = build_modbus_frame("010301B50001");
            this->write_frame(cmd);
          }
        }
      } else if (frame[1] == 0x06) {
        // Confirmation frame of Start/Stop write actions
        ESP_LOGI(TAG, "Wallbox charging command successfully acknowledged.");
        this->is_charging_ = this->pending_charging_state_;
        this->has_pending_charging_action_ = false;
      }
      break;
    }
  }
}

void DeltaWallbox::reset_state() {
  ESP_LOGW(TAG, "Resetting component state from %s. pairing_attempts=%u, pairing_in_progress=%s, rx_buffer=%u bytes",
           delta_state_to_string(this->state_), this->pairing_attempts_, YESNO(this->pairing_in_progress_),
           (unsigned int) this->rx_buffer_.size());
  this->rx_buffer_.clear();
  this->state_ = DeltaState::STATE_IDLE;
  this->pairing_attempts_ = 0;
  this->pairing_in_progress_ = false;
  this->status_set_warning();
}

void DeltaWallbox::set_charging_state(bool state) {
  if (!this->is_connected()) {
    ESP_LOGW(TAG, "Cannot send charging command while wallbox is not fully connected.");
    return;
  }

  this->pending_charging_state_ = state;
  this->has_pending_charging_action_ = true;

  std::vector<uint8_t> cmd;
  if (state) {
    ESP_LOGI(TAG, "Sending remote START charging command...");
    cmd = build_modbus_frame("010601020001");
  } else {
    ESP_LOGI(TAG, "Sending remote STOP charging command...");
    cmd = build_modbus_frame("010601030001");
  }

  this->write_frame(cmd);
  this->last_tx_time_ = millis();
}

void DeltaWallbox::start_charging() { this->set_charging_state(true); }

void DeltaWallbox::stop_charging() { this->set_charging_state(false); }

bool DeltaWallbox::is_charging() {
  return this->is_charging_;
}

void DeltaWallbox::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                       esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_DISCONNECT_EVT: {
      ESP_LOGW(TAG, "Disconnected from Bluetooth wallbox client (conn_id=%u, reason=%d). Resetting state.",
               param->disconnect.conn_id, param->disconnect.reason);
      this->reset_state();
      this->node_state = esp32_ble_tracker::ClientState::IDLE;
      break;
    }
    case ESP_GATTC_OPEN_EVT: {
      ESP_LOGI(TAG, "GATT open event received: status=%d conn_id=%u", param->open.status, param->open.conn_id);
      break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      ESP_LOGI(TAG, "GATT service discovery completed: status=%d", param->search_cmpl.status);
      auto *tx_char = this->parent_->get_characteristic(SERVICE_UUID, TX_UUID);
      auto *rx_char = this->parent_->get_characteristic(SERVICE_UUID, RX_UUID);
      if (tx_char == nullptr || rx_char == nullptr) {
        ESP_LOGE(TAG, "Could not discover matching NUS TX/RX service on this BLE device.");
        this->reset_state();
        break;
      }
      this->tx_char_handle_ = tx_char->handle;
      this->rx_char_handle_ = rx_char->handle;

      auto status = esp_ble_gattc_register_for_notify(this->parent_->get_gattc_if(), this->parent_->get_remote_bda(),
                                                      this->rx_char_handle_);
      if (status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "esp_ble_gattc_register_for_notify failed, status=%d", status);
      }
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      ESP_LOGI(TAG, "Register-for-notify event: status=%d handle=0x%04X", param->reg_for_notify.status,
               param->reg_for_notify.handle);
      if (param->reg_for_notify.handle == this->rx_char_handle_) {
        ESP_LOGI(TAG, "Subscribed to RX notification handle 0x%04X successfully.", this->rx_char_handle_);
        this->node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
      }
      break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
      if (param->notify.handle != this->rx_char_handle_) break;

      ESP_LOGD(TAG, "Notification received on handle 0x%04X with %u bytes. Buffer before append=%u bytes.",
               param->notify.handle, param->notify.value_len, (unsigned int) this->rx_buffer_.size());

      this->rx_buffer_.insert(this->rx_buffer_.end(), param->notify.value, param->notify.value + param->notify.value_len);

      bool progress = true;
      while (progress && this->rx_buffer_.size() >= 3) {
        progress = false;
        uint8_t func_code = this->rx_buffer_[1];
        size_t expected_len = 0;

        if (func_code == 0x03) {
          expected_len = this->rx_buffer_[2] + 5;
        } else if (func_code == 0x10 || func_code == 0x06) {
          expected_len = 8;
        } else if (func_code == (0x80 | 0x03) || func_code == (0x80 | 0x10) || func_code == (0x80 | 0x06)) {
          expected_len = 5;
        }

        if (expected_len > 0) {
          if (this->rx_buffer_.size() >= expected_len) {
            std::vector<uint8_t> frame(this->rx_buffer_.begin(), this->rx_buffer_.begin() + expected_len);
            if (verify_crc(frame.data(), expected_len)) {
              this->handle_frame(frame);
              this->rx_buffer_.erase(this->rx_buffer_.begin(), this->rx_buffer_.begin() + expected_len);
              progress = true;
            } else {
              ESP_LOGW(TAG, "CRC mismatch while parsing notification data in state %s. Dropping one byte from buffer.",
                       delta_state_to_string(this->state_));
              // CRC mismatch: shift buffer by 1 to align with next potential modbus frame start
              this->rx_buffer_.erase(this->rx_buffer_.begin());
              progress = true;
            }
          }
        } else {
          ESP_LOGW(TAG, "Unsupported function code 0x%02X while parsing notification data in state %s. Advancing buffer.",
                   func_code, delta_state_to_string(this->state_));
          // Unsupported command byte headers, advance single byte
          this->rx_buffer_.erase(this->rx_buffer_.begin());
          progress = true;
        }
      }
      break;
    }
    default:
      break;
  }
}

void DeltaWallbox::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  switch (event) {
    case ESP_GAP_BLE_PASSKEY_REQ_EVT: {
      if (!this->parent_->check_addr(param->ble_security.ble_req.bd_addr))
        return;

      if (!this->has_pin_) {
        ESP_LOGW(TAG, "Wallbox requested a BLE passkey, but no pin is configured. Set pin: under delta_wallbox:.");
        return;
      }

      ESP_LOGI(TAG, "Wallbox requested a BLE passkey. Replying with configured YAML pin.");
      esp_err_t err = esp_ble_passkey_reply(param->ble_security.ble_req.bd_addr, true, this->pin_);
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send BLE passkey reply: %s", esp_err_to_name(err));
      }
      break;
    }
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
      if (!this->parent_->check_addr(param->ble_security.auth_cmpl.bd_addr))
        return;

      this->pairing_in_progress_ = false;
      if (param->ble_security.auth_cmpl.success) {
        ESP_LOGI(TAG, "BLE pairing completed successfully. auth_mode=%d addr_type=%d. Re-checking wallbox pairing status.",
                 param->ble_security.auth_cmpl.auth_mode, param->ble_security.auth_cmpl.addr_type);
        this->pairing_attempts_ = 0;
        if (this->node_state == esp32_ble_tracker::ClientState::ESTABLISHED) {
          ESP_LOGD(TAG, "State transition: %s -> %s after BLE auth completion.", delta_state_to_string(this->state_),
                   delta_state_to_string(DeltaState::STATE_CHECK_PAIRING));
          this->state_ = DeltaState::STATE_CHECK_PAIRING;
          this->send_next_state_command();
        }
      } else {
        ESP_LOGW(TAG, "BLE pairing failed, reason=%d", param->ble_security.auth_cmpl.fail_reason);
      }
      break;
    default:
      break;
  }
}

}  // namespace esphome::delta_wallbox

#endif
