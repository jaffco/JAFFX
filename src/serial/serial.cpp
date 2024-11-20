#include "../../Jaffx.hpp"

// Program that demonstrates printing to serial bus
// Great utility for debugging
struct Serial : Jaffx::Program {
  const char message[7] = "Hello!"; // set a message
  unsigned int counter = 0;
  bool triggered = false;

  void init() override {
    this->hardware.StartLog(true);
  }

  float processAudio(float in) override {
    counter++;
    if (counter >= this->samplerate && !triggered) { // once per second
      counter = 0;
      triggered = true; // trigger a print
    }
    return 0.f;
  }

  void loop() override {
    if (triggered) { // print set by trigger
      this->hardware.PrintLine(message);
      triggered = false;
    }
    System::Delay(500); // Don't spam the serial!
  }
};

int main() {
  Serial mSerial;
  mSerial.start();
}
