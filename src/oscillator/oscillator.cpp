#include "../../Jaffx.hpp"
#include "../../Gimmel/include/oscillator.hpp"

struct Oscillator : Jaffx::Program {
  // add member objects
  giml::SinOsc<float> osc;

  // constructor that inits members
  Oscillator() : osc(samplerate) {
    osc.setFrequency(220.f);
  }

  // override audio callback
  float processAudio(float in) override {
    return osc.processSample();
  }
};

int main(void) {
  Oscillator osc;
  osc.start();
}
