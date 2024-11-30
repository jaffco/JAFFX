#include "../../Jaffx.hpp"

// This app demonstrates libDaisy's Led class,
// which can adjust the brightness of an led with PWM
// TODO: investigate flickering when LED is set < 1
struct LedCtrl : Jaffx::Program {
  Led mLed;
  bool flag = false;
  float phase;

  void init() override {
    mLed.Init(seed::D16, false, samplerate/buffersize);
  }

  float processAudio(float in) override {
    phase += 0.2f / samplerate; // 0.2 Hz
    phase -= floor(phase);
    mLed.Set(phase);
    return in;
  }

  void blockEnd() override {
    if (!flag) {
      flag = true;
    }
  }

  void loop() override {
    if (flag) {
      mLed.Update();
      flag = false;
    }
  }

};

int main() {
  LedCtrl mLedCtrl;
  mLedCtrl.start();
  return 0;
}


