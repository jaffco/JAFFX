#include "../../Jaffx.hpp"

struct AdcRead : Jaffx::Program {

  void init() override {
    AdcChannelConfig config;
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
      this->hardware.PrintLine("Knob: %d", this->hardware.adc.Get(0));
    }
    return 0.f;
  }
};

int main(void) {
  AdcRead mAdcRead;
  mAdcRead.start();
}


