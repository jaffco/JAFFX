#include "../../Jaffx.hpp"
#include <cmath>

// PWM Output demonstration using the JAFFX framework
// This example shows how to use hardware PWM to control LED brightness
// with a smooth sine wave pattern, similar to the libDaisy PWM_Output example
class PwmOutput : public Jaffx::Firmware {
  PWMHandle pwm_tim3;

  void init() override {
    // Configure the PWM peripheral
    PWMHandle::Config pwm_config;
    pwm_config.periph = PWMHandle::Config::Peripheral::TIM_3;
    pwm_config.period = 0xff; // 8-bit resolution for higher frequency

    // Initialize PWM handle
    if(pwm_tim3.Init(pwm_config) != PWMHandle::Result::OK) {
      // Error handling - could be logged if debug is enabled
    }

    // Configure PWM Handle with onboard LED
    PWMHandle::Channel::Config channel_config;
    channel_config.pin = {PORTC, 7}; // Built-in LED pin
    channel_config.polarity = PWMHandle::Channel::Config::Polarity::HIGH;

    // Configure TIM3 to write to onboard LED
    if(pwm_tim3.Channel2().Init(channel_config) != PWMHandle::Result::OK) {
      // Error handling - could be logged if debug is enabled
    }

    // Configure PWM Handle with GPIO Pin
    PWMHandle::Channel::Config channel_config2;
    channel_config2.pin = seed::D19; // seed::D19 (PA6) - works with TIM3_CH1
    channel_config2.polarity = PWMHandle::Channel::Config::Polarity::HIGH;

    // Configure TIM3 Channel 1 to write to D19
    if(pwm_tim3.Channel1().Init(channel_config2) != PWMHandle::Result::OK) {
      // Error handling - could be logged if debug is enabled
    }

    // Set brightness low on both
    pwm_tim3.Channel2().Set(0.1); 
    pwm_tim3.Channel1().Set(0.1);
  }
};

int main(void) {
  PwmOutput mPwmOutput;
  mPwmOutput.start();
}