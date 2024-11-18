#include "../../Jaffx.hpp"
#include "../../Gimmel/include/oscillator.hpp"

struct Oscillator : Jaffx::Program {
  // add member objects
  giml::SinOsc<float> osc{samplerate};

  void init() override {
    this->osc.setFrequency(220.f);
  }

  // override audio callback
  float processAudio(float in) override {
    return osc.processSample();
  }
};

int main(void) {
  Oscillator mOscillator;
  mOscillator.start();
}
