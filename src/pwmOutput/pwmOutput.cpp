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

    // Configure PWM Handle with desired pin
    PWMHandle::Channel::Config channel_config;
    channel_config.pin = {PORTC, 7}; // Built-in LED pin
    channel_config.polarity = PWMHandle::Channel::Config::Polarity::HIGH;

    // Configure TIM3 to write to desired pin
    if(pwm_tim3.Channel2().Init(channel_config) != PWMHandle::Result::OK) {
      // Error handling - could be logged if debug is enabled
    }

    pwm_tim3.Channel2().Set(0.1); // Set brightness low
  }
};

int main(void) {
  PwmOutput mPwmOutput;
  mPwmOutput.start();
}