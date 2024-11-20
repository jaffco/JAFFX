#include "../../Jaffx.hpp"

// Simple blink program, the "Hello World!" of embedded systems
struct Blink : Jaffx::Program {
  bool ledState = true;
  unsigned int counter = 0;

  float processAudio(float in) override {
    counter++;
    if (counter >= this->samplerate/2) { // every 0.5 seconds...
      counter = 0;
      ledState = !ledState; // flip LED state
      this->hardware.SetLed(ledState); // To-Do: move to `loop()`
    }
    return 0.f;
  }
};

int main(void) {
  Blink mBlink;
  mBlink.start();
}
