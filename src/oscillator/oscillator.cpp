#include "../../Jaffx.hpp"
#include "../../Gimmel/include/oscillator.hpp"

struct Oscillator : Jaffx::Program {
  // add member objects
  giml::SinOsc<float> osc;
  Switch mSwitch;

  // constructor that inits members
  Oscillator() : osc(samplerate) {
    osc.setFrequency(220.f);
    mSwitch.Init(this->hardware.GetPin(2), 1000);
  }

  // override audio callback
  float processAudio(float in) override {
    mSwitch.Debounce();
    bool amp = false;
    if (mSwitch.Pressed()) { amp = !amp;}
    return osc.processSample() * amp;
  }
};

int main(void) {
  Oscillator osc;
  osc.start();
}
