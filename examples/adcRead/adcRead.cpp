#include "../../Jaffx.hpp"

// AdcReads allow us to sample continuous control values
class AdcRead : public Jaffx::Firmware {
  bool trigger = false;
  AnalogControl mKnob;

  void init() override {
    // Initialize ADC channel
    AdcChannelConfig config;
    config.InitSingle(seed::A0);
    this->hardware.adc.Init(&config, 1);
    this->hardware.adc.Start();

    // Initialize AnalogControl with ADC pointer from Jaffx hardware
    mKnob.Init(this->hardware.adc.GetPtr(0), this->samplerate);

    this->hardware.StartLog();
  }

  float processAudio(float in) override {
    static int counter;
    counter++;
    if (counter >= this->samplerate && !trigger) {
      counter = 0;
      trigger = true;
    }
    return 0.f;
  }

  void loop() override {
    if (trigger) {
      // Process the AnalogControl (applies smoothing and normalization)
      float knobValue = mKnob.Process();

      // Also show raw ADC value for comparison
      uint16_t rawValue = this->hardware.adc.Get(0);

      hardware.PrintLine("Raw ADC: %d", rawValue);
      hardware.PrintLine("AnalogControl: " FLT_FMT3, FLT_VAR3(knobValue));

      trigger = false;
    }
    System::Delay(500); // Don't spam the serial!
  }

};

int main(void) {
  AdcRead mAdcRead;
  mAdcRead.start();
}


