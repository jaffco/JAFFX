#include "../../Jaffx.hpp"

// This app demonstrates libDaisy's Led class,
// which can adjust the brightness of an led with PWM

// Hardware config:
// mLed on pin D15

class LedCtrl : public Jaffx::Firmware {
  Led mLed;
  float phase;

  void init() override {
    mLed.Init(seed::D15, false);
  }

  float processAudio(float in) override {
    phase += 1.f / samplerate; // 1 Hz
    phase -= floor(phase);
    mLed.Set(phase);
    return in;
  }

  void loop() override {
    mLed.Update();
    System::Delay(1);
  }

};

int main() {
  LedCtrl mLedCtrl;
  mLedCtrl.start();
  return 0;
}


