#include "../../Jaffx.hpp"
#include "../../Gimmel/include/gimmel.hpp"
#include <cstdint>

/**
 * @brief PWM wrapper class for hardware timer PWM control
 *
 * Provides a clean interface for controlling PWM signals using STM32 timers.
 * Supports setting frequency in Hz and duty cycle as normalized values (0.0-1.0).
 */
class PwmWrapper {
private:
  PWMHandle pwmHandle;
  PWMHandle::Config pwmConfig;
  bool initialized;

public:
  /**
   * @brief Constructor - initializes with default TIM3 configuration
   */
  PwmWrapper() : initialized(false) {
    // Default configuration
    pwmConfig.periph = PWMHandle::Config::Peripheral::TIM_3;
    pwmConfig.period = 0xff; // 8-bit resolution
  }

  /**
   * @brief Initialize the PWM peripheral
   * @param timer The timer peripheral to use (default: TIM_3)
   * @param period The PWM period in timer ticks (affects resolution vs frequency)
   * @return true if initialization successful
   */
  bool init(PWMHandle::Config::Peripheral timer, unsigned int period) {
      pwmConfig.periph = timer;
      pwmConfig.period = period;

      PWMHandle::Result result = pwmHandle.Init(pwmConfig);
      if (result == PWMHandle::Result::OK) {
          initialized = true;
          return true;
      }
      return false;
  }

  /**
   * @brief Configure a PWM channel on a specific pin
   * @param channel The channel number (1-4)
   * @param pin The GPIO pin to use
   * @param polarity PWM polarity (HIGH or LOW)
   * @return true if configuration successful
   */
  bool configureChannel(uint8_t channel,
                        Pin pin,
                        PWMHandle::Channel::Config::Polarity polarity) {
    if (!initialized) return false;

    PWMHandle::Channel::Config channelConfig;
    channelConfig.pin = pin;
    channelConfig.polarity = polarity;

    PWMHandle::Result result;
    switch (channel) {
      case 1:
        result = pwmHandle.Channel1().Init(channelConfig);
        break;
      case 2:
        result = pwmHandle.Channel2().Init(channelConfig);
        break;
      case 3:
        result = pwmHandle.Channel3().Init(channelConfig);
        break;
      case 4:
        result = pwmHandle.Channel4().Init(channelConfig);
        break;
      default:
        return false;
    }

    return (result == PWMHandle::Result::OK);
  }

  /**
   * @brief Set PWM frequency in Hz
   * @param frequency Desired frequency in Hz
   * @note This affects all channels on the same timer
   */
  void setFrequency(float frequency) {
    if (!initialized || frequency <= 0) return;

    // STM32H7 timer clock is 200MHz for APB1/APB2 timers
    const unsigned int timerClock = 200000000; // 200 MHz
    
    // Formula from libDaisy: frequency = timerClock / (2 * (period + 1) * (prescaler + 1))
    // Rearranged: (prescaler + 1) * (period + 1) = timerClock / (2 * frequency)
    
    unsigned int divisor = timerClock / (2 * frequency);
    
    // Choose prescaler and period to achieve the divisor
    // Strategy: Use smaller prescaler for better resolution, larger period
    unsigned int prescaler = 0;
    unsigned int period = divisor - 1;
    
    // If period exceeds 16-bit limit for TIM3/TIM4, increase prescaler
    while (period > 0xFFFF && prescaler < 0xFFFF) {
      prescaler++;
      period = (divisor / (prescaler + 1)) - 1;
    }
    
    // Ensure values are within valid ranges
    if (prescaler > 0xFFFF) prescaler = 0xFFFF;
    if (period > 0xFFFF) period = 0xFFFF;
    if (period < 1) period = 1;
    
    // Apply new prescaler and period using libDaisy's runtime methods
    pwmHandle.SetPrescaler(prescaler);
    pwmHandle.SetPeriod(period);
    
    // Update config for consistency
    pwmConfig.prescaler = prescaler;
    pwmConfig.period = period;
  }

  /**
   * @brief Set duty cycle for a specific channel
   * @param channel The channel number (1-4)
   * @param dutyCycle Duty cycle as normalized value (0.0 = 0%, 1.0 = 100%)
   */
  void setDutyCycle(uint8_t channel, float dutyCycle) {
    if (!initialized) return;

    // Clamp duty cycle to valid range
    dutyCycle = giml::clip(dutyCycle, 0.0f, 1.0f);

    // Calculate raw value based on current period
    unsigned int rawValue = (unsigned int)(dutyCycle * pwmConfig.period);

    switch (channel) {
      case 1:
        pwmHandle.Channel1().SetRaw(rawValue);
        break;
      case 2:
        pwmHandle.Channel2().SetRaw(rawValue);
        break;
      case 3:
        pwmHandle.Channel3().SetRaw(rawValue);
        break;
      case 4:
        pwmHandle.Channel4().SetRaw(rawValue);
        break;
    }
  }

  /**
   * @brief Check if PWM is initialized
   * @return true if initialized and ready to use
   */
  bool isInitialized() const { return initialized; }

  /**
   * @brief Get the current duty cycle for a channel
   * @param channel The channel number (1-4)
   * @return Current duty cycle (0.0-1.0)
   */
  float getDutyCycle(uint8_t channel) const {
    // Note: libDaisy PWMHandle doesn't provide a direct way to read current duty cycle
    // This would require storing the values internally or accessing timer registers directly
    // For now, return 0.0 as a placeholder
    return 0.0f;
  }

  PWMHandle& getHandle() { return pwmHandle; }
};

// PWM Output demonstration using the JAFFX framework
// This example shows how to use hardware PWM to control LED brightness
// with a smooth sine wave pattern, similar to the libDaisy PWM_Output example
class PwmOutput : public Jaffx::Firmware {
  PwmWrapper pwm;

  void init() override {
    // Initialize PWM with default TIM3 and 8-bit resolution
    if (!pwm.init(PWMHandle::Config::Peripheral::TIM_3, 0xff)) {
      // Error handling - could be logged if debug is enabled
      return;
    }

    // Configure channel 2 for built-in LED (PC7)
    if (!pwm.configureChannel(2, {PORTC, 7}, PWMHandle::Channel::Config::Polarity::HIGH)) {
      // Error handling - could be logged if debug is enabled
      return;
    }

    // Configure channel 1 for D19 (PA6)
    if (!pwm.configureChannel(1, seed::D19, PWMHandle::Channel::Config::Polarity::HIGH)) {
      // Error handling - could be logged if debug is enabled
      return;
    }

    // Set initial frequency to 1 Hz
    pwm.setFrequency(0.5f);

    // Set brightness low on both channels
    pwm.setDutyCycle(1, 0.5f); // D19
    pwm.setDutyCycle(2, 0.5f); // Built-in LED
  }
};

int main(void) {
  PwmOutput mPwmOutput;
  mPwmOutput.start();
}