#include "../../Jaffx.hpp"

// AdcReads allow us to sample continuous control values
class AdcRead : public Jaffx::Firmware {
  bool trigger = false;

  void init() override {
    AdcChannelConfig config; // if multiple, set up as an array: `config[numAdcs]`
    config.InitSingle(seed::A0);
    this->hardware.adc.Init(&config, 1);
    this->hardware.adc.Start();
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
      hardware.PrintLine("Knob: %d", this->hardware.adc.Get(0));
      trigger = false;
    }
    System::Delay(500); // Don't spam the serial!
  }

};

int main(void) {
  AdcRead mAdcRead;
  mAdcRead.start();
}


