#include "../../Jaffx.hpp"

// AdcReads allow us to sample continuous control values
struct AdcRead : Jaffx::Program {

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
    if (counter >= this->samplerate) {
      counter = 0;
      this->hardware.PrintLine("Knob: %d", this->hardware.adc.Get(0)); // To-Do: Move to `loop()`
    }
    return 0.f;
  }
};

int main(void) {
  AdcRead mAdcRead;
  mAdcRead.start();
}


