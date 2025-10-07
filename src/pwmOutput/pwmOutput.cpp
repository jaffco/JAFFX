#include "../../Jaffx.hpp"
#include <cmath>

// PWM Output demonstration using the JAFFX framework
// This example shows how to use hardware PWM to control LED brightness
// with a smooth sine wave pattern, similar to the libDaisy PWM_Output example
class PwmOutput : public Jaffx::Firmware {
  PWMHandle pwm_tim3;
  float phase = 0.0f;
  const float TWO_PI = 6.2831853072f;
  const float PHASE_INCREMENT = 0.01f; // Controls PWM frequency (1 Hz)

  void init() override {
    // Configure the PWM peripheral
    PWMHandle::Config pwm_config;
    pwm_config.periph = PWMHandle::Config::Peripheral::TIM_3;
    pwm_config.period = 0xff; // 8-bit resolution for higher frequency

    // Initialize PWM handle
    if(pwm_tim3.Init(pwm_config) != PWMHandle::Result::OK) {
      // Error handling - could be logged if debug is enabled
    }

    // Configure PWM channel 2 for the built-in LED
    PWMHandle::Channel::Config channel_config;
    channel_config.pin = {PORTC, 7}; // Built-in LED pin
    channel_config.polarity = PWMHandle::Channel::Config::Polarity::HIGH;

    // Initialize the channel
    if(pwm_tim3.Channel2().Init(channel_config) != PWMHandle::Result::OK) {
      // Error handling - could be logged if debug is enabled
    }
  }

  float processAudio(float in) override {
    // Pass through audio unchanged
    return in;
  }

  void loop() override {
    // Generate a smooth 1 Hz sine wave for LED brightness
    float brightness = giml::cos(TWO_PI * phase) * 0.5f + 0.5f;
    
    // Apply cubic gamma correction for more linear LED brightness perception
    float gamma_corrected = brightness * brightness * brightness;
    
    // Set PWM duty cycle (0.0 to 1.0 maps to 0 to period)
    pwm_tim3.Channel2().Set(gamma_corrected);
    
    // Update phase for next iteration
    phase += PHASE_INCREMENT;
    if(phase > 1.0f) {
      phase -= 1.0f;
    }
    
    // Small delay to control update rate
    System::Delay(10);
  }
};

int main(void) {
  PwmOutput mPwmOutput;
  mPwmOutput.start();
}