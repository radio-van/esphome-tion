#include <cinttypes>
#include "esphome/core/log.h"
#include "esphome/core/defines.h"

#include "tion_o2_climate.h"

#ifdef TION_ENABLE_ANTIFRIZE
#define TION_OPTION_STR_ANTIFRIZE "enabled"
#else
#define TION_OPTION_STR_ANTIFRIZE "disabled"
#endif

#define DUMP_UNK(field) \
  if (state.field == 0 || state.field == 1) \
    ESP_LOGD(TAG, "%-12s: %u", #field, state.field); \
  else if (static_cast<int8_t>(state.field) > 0) \
    ESP_LOGD(TAG, "%-12s: 0x%02X, %s, %u", #field, state.field, bits_str(state.field), state.field); \
  else \
    ESP_LOGD(TAG, "%-12s: 0x%02X, %s, %u, %d", #field, state.field, bits_str(state.field), state.field, \
             static_cast<int8_t>(state.field));

namespace esphome {
namespace tion {

static const char *const TAG = "tion_o2";

void TionO2Climate::dump_config() {
  this->dump_settings(TAG, "Tion O2");
  // LOG_SELECT("  ", "Air Intake", this->air_intake_);
  ESP_LOGCONFIG("  ", "Antifrize: %s", TION_OPTION_STR_ANTIFRIZE);
  this->dump_presets(TAG);
}

void TionO2Climate::update_state(const tiono2_state_t &state) {
  this->dump_state(state);

  if (!state.flags.power_state) {
    this->mode = climate::CLIMATE_MODE_OFF;
    this->action = climate::CLIMATE_ACTION_OFF;
  } else if (state.flags.heater_state) {
    this->mode = climate::CLIMATE_MODE_HEAT;
    this->action = this->mode == state.is_heating() ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_FAN;
  } else {
    this->mode = climate::CLIMATE_MODE_FAN_ONLY;
    this->action = climate::CLIMATE_ACTION_FAN;
  }

  this->current_temperature = state.current_temperature;
  this->target_temperature = state.target_temperature;
  this->set_fan_speed_(state.fan_speed);
  this->publish_state();

  if (this->productivity_) {
    this->productivity_->publish_state(state.productivity);
  }
}

static char bits_str_buf[9]{};
const char *bits_str(uint8_t v) {
  for (int i = 0; i < 8; i++) {
    bits_str_buf[7 - i] = ((v >> i) & 1) + '0';
  }
  return bits_str_buf;
}

void TionO2Climate::dump_state(const tiono2_state_t &state) const {
  ESP_LOGD(TAG, "flags       : %s", bits_str(*reinterpret_cast<const uint8_t *>(&state.flags)));
  ESP_LOGD(TAG, "outdoor_temp: %d", state.outdoor_temperature);
  ESP_LOGD(TAG, "current_temp: %d", state.current_temperature);
  ESP_LOGD(TAG, "target_temp : %d", state.current_temperature);
  ESP_LOGD(TAG, "fan_speed   : %d", state.fan_speed);
  ESP_LOGD(TAG, "productivity: %d", state.productivity);
  DUMP_UNK(unknown7);
  DUMP_UNK(unknown8);
  DUMP_UNK(unknown9);
  ESP_LOGD(TAG, "work_time   : %" PRIu32, state.counters.work_time_days());
  DUMP_UNK(unknown14);
  DUMP_UNK(unknown15);
  DUMP_UNK(unknown16);
  DUMP_UNK(unknown17);
}

void TionO2Climate::control_climate_state(climate::ClimateMode mode, uint8_t fan_speed, float target_temperature,
                                          TionClimateGatePosition gate_position) {
  ControlState control{};
  control.fan_speed = fan_speed;
  if (!std::isnan(target_temperature)) {
    control.target_temperature = target_temperature;
  }

  if (mode == climate::CLIMATE_MODE_OFF) {
    control.power_state = false;
  } else if (mode == climate::CLIMATE_MODE_HEAT_COOL) {
    control.power_state = true;
  } else {
    control.power_state = true;
    control.heater_state = mode == climate::CLIMATE_MODE_HEAT;
  }

  this->control_state_(control);
}

void TionO2Climate::control_state_(const ControlState &state) {
  tiono2_state_t st = this->state_;

  if (state.power_state.has_value()) {
    st.flags.power_state = *state.power_state;
    if (this->state_.flags.power_state != st.flags.power_state) {
      ESP_LOGD(TAG, "New power state %s -> %s", ONOFF(this->state_.flags.power_state), ONOFF(st.flags.power_state));
    }
  }

  if (state.heater_state.has_value()) {
    st.flags.heater_state = *state.heater_state;
    if (this->state_.flags.heater_state != st.flags.heater_state) {
      ESP_LOGD(TAG, "New heater state %s -> %s", ONOFF(this->state_.flags.heater_state), ONOFF(st.flags.heater_state));
    }
  }

  if (state.fan_speed.has_value()) {
    st.fan_speed = *state.fan_speed;
    if (this->state_.fan_speed != st.fan_speed) {
      ESP_LOGD(TAG, "New fan speed %u -> %u", this->state_.fan_speed, st.fan_speed);
    }
  }

  if (state.target_temperature.has_value()) {
    st.target_temperature = *state.target_temperature;
    if (this->state_.target_temperature != st.target_temperature) {
      ESP_LOGD(TAG, "New target temperature %d -> %d", this->state_.target_temperature, st.target_temperature);
    }
  }

#ifdef TION_ENABLE_ANTIFRIZE
  if (st.flags.power_state && !st.flags.heater_state && this->outdoor_temperature_) {
    auto outdoor_temperature = this->outdoor_temperature_->state;
    if (!std::isnan(outdoor_temperature) && outdoor_temperature < 0.001) {
      ESP_LOGW(TAG, "Antifrize protection has worked. Heater now enabled.");
      st.flags.heater_state = true;
    }
  }
#endif

  this->write_api_state_(st);
}

}  // namespace tion
}  // namespace esphome