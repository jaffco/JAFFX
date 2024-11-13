#include "../../Jaffx.hpp"

struct Blink : Jaffx::Program {
  bool ledState = true;
  unsigned int counter = 0;

  float processAudio(float in) override {
    counter++;
    if (counter >= this->samplerate/2) {
      counter = 0;
      ledState = !ledState;
      this->hardware.SetLed(ledState);
    }
    return 0.f;
  }
};

int main(void) {
  Blink mBlink;
  mBlink.start();
}
