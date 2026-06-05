#pragma once

#ifdef USE_ESP32

#include "esphome/core/component.h"
#include "esphome/components/button/button.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/helpers.h"

#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

namespace esphome::delta_wallbox {

enum class DeltaState {
  STATE_IDLE,
  STATE_GET_MODEL,
  STATE_GET_BT_FW_VERSION,
  STATE_CHECK_PAIRING,
  STATE_GET_LOGIN_USER,
  STATE_GET_LOGIN_RESULT0,
  STATE_SEND_LOGIN_CREDENTIALS,
  STATE_GET_LOGIN_RESULT,
  STATE_GET_LOGIN_ROLE,
  STATE_TIME_SYNC,
  STATE_GET_CHARGER_FW_VERSION,
  STATE_CONNECTED,
};

class DeltaWallbox : public esphome::ble_client::BLEClientNode, public PollingComponent {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;
  void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) override;
  void dump_config() override;

  void set_user(const std::string &user) { user_ = user; }
  void set_password(const std::string &password) { password_ = password; }
  void set_pin(uint32_t pin) {
    pin_ = pin;
    has_pin_ = true;
  }
#ifdef USE_TIME
  void set_time_id(time::RealTimeClock *time_id) { time_id_ = time_id; }
#endif

  // Sensors
  void set_power_sensor(sensor::Sensor *s) { power_sensor_ = s; }
  void set_energy_sensor(sensor::Sensor *s) { energy_sensor_ = s; }
  void set_charging_time_sensor(sensor::Sensor *s) { charging_time_sensor_ = s; }
  void set_charger_state_sensor(sensor::Sensor *s) { charger_state_sensor_ = s; }
  void set_max_current_sensor(sensor::Sensor *s) { max_current_sensor_ = s; }
  void set_max_current_number(number::Number *n) { max_current_number_ = n; }

  // Text Sensors
  void set_serial_number_sensor(text_sensor::TextSensor *s) { serial_number_sensor_ = s; }
  void set_fw_version_sensor(text_sensor::TextSensor *s) { fw_version_sensor_ = s; }
  void set_bt_fw_version_sensor(text_sensor::TextSensor *s) { bt_fw_version_sensor_ = s; }
  void set_model_name_sensor(text_sensor::TextSensor *s) { model_name_sensor_ = s; }
  void set_error_code_sensor(text_sensor::TextSensor *s) { error_code_sensor_ = s; }

  // Switch Interface
  void set_charging_state(bool state);
  void set_max_current(float current);
  void start_charging();
  void stop_charging();
  bool is_charging();

  bool is_connected() { return this->node_state == esp32_ble_tracker::ClientState::ESTABLISHED && this->state_ == DeltaState::STATE_CONNECTED; }

 protected:
  void write_frame(const std::vector<uint8_t> &frame);
  void send_next_state_command();
  void handle_frame(const std::vector<uint8_t> &frame);
  void reset_state();

  std::string user_;
  std::string password_;
  uint32_t pin_{0};
  bool has_pin_{false};
#ifdef USE_TIME
  time::RealTimeClock *time_id_{nullptr};
#endif

  uint16_t rx_char_handle_{0};
  uint16_t tx_char_handle_{0};
  DeltaState state_{DeltaState::STATE_IDLE};
  std::vector<uint8_t> rx_buffer_;
  uint32_t last_tx_time_{0};
  uint8_t pairing_attempts_{0};
  bool pairing_in_progress_{false};
  bool pending_charging_state_{false};
  bool has_pending_charging_action_{false};
  bool is_charging_{false};
  uint8_t min_current_limit_{0};
  uint8_t max_current_limit_{255};

  // Sensors
  sensor::Sensor *power_sensor_{nullptr};
  sensor::Sensor *energy_sensor_{nullptr};
  sensor::Sensor *charging_time_sensor_{nullptr};
  sensor::Sensor *charger_state_sensor_{nullptr};
  sensor::Sensor *max_current_sensor_{nullptr};
  number::Number *max_current_number_{nullptr};

  // Text Sensors
  text_sensor::TextSensor *serial_number_sensor_{nullptr};
  text_sensor::TextSensor *fw_version_sensor_{nullptr};
  text_sensor::TextSensor *bt_fw_version_sensor_{nullptr};
  text_sensor::TextSensor *model_name_sensor_{nullptr};
  text_sensor::TextSensor *error_code_sensor_{nullptr};
};

class DeltaWallboxStartChargingButton : public button::Button, public Parented<DeltaWallbox> {
 protected:
  void press_action() override { this->parent_->start_charging(); }
};

class DeltaWallboxStopChargingButton : public button::Button, public Parented<DeltaWallbox> {
 protected:
  void press_action() override { this->parent_->stop_charging(); }
};

class DeltaWallboxMaxCurrentNumber : public number::Number, public Parented<DeltaWallbox> {
 protected:
  void control(float value) override { this->parent_->set_max_current(value); }
};

}  // namespace esphome::delta_wallbox

#endif
