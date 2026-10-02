#include "hardware/pwm.hpp"

#include <driver/ledc.h>

// PWM
#define PWM_CHANNEL 0
#define PWM_FREQ 10000
#define PWM_RESOLUTION 8

namespace {

constexpr ledc_channel_t pwm_channel = LEDC_CHANNEL_0;
constexpr ledc_timer_t pwm_timer = LEDC_TIMER_0;
constexpr ledc_mode_t pwm_speed_mode = LEDC_LOW_SPEED_MODE;

}  // namespace

void initPwm() {
#if defined(PWM_PIN)
  ledc_timer_config_t timer{};
  timer.speed_mode = pwm_speed_mode;
  timer.timer_num = pwm_timer;
  timer.duty_resolution = LEDC_TIMER_8_BIT;
  timer.freq_hz = PWM_FREQ;
  timer.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer);

  ledc_channel_config_t channel{};
  channel.speed_mode = pwm_speed_mode;
  channel.channel = pwm_channel;
  channel.timer_sel = pwm_timer;
  channel.gpio_num = PWM_PIN;
  channel.duty = 0;
  channel.hpoint = 0;
  ledc_channel_config(&channel);
#endif
}

void set_pwm(uint8_t value) {
#if defined(PWM_PIN)
  ledc_set_duty(pwm_speed_mode, pwm_channel, value);
  ledc_update_duty(pwm_speed_mode, pwm_channel);
#else
  (void)value;
#endif
}

uint32_t get_pwm() {
#if defined(PWM_PIN)
  return ledc_get_duty(pwm_speed_mode, pwm_channel);
#else
  return 0;
#endif
}
