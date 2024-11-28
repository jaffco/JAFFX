#include "../../Jaffx.hpp"

struct Bootloader : Jaffx::Program {

  bool ledState = true;
  bool trigger = false;
  unsigned int counter = 0;

  float processAudio(float in) override {
    counter++;
    if (counter >= this->samplerate/2 && !trigger) { // every 0.5 seconds...
      counter = 0;
      ledState = !ledState; // flip LED state
      trigger = true;
    }
    return 0.f;
  }

  void loop() override {
    if (trigger) {
      this->hardware.SetLed(ledState);
      trigger = false;
    }
    System::Delay(500); // Don't spam the LED!
  }
  
};

int main() {
  Bootloader mBootloader;
  mBootloader.start();
  return 0;
}

