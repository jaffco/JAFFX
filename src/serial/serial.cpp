#include "../../Jaffx.hpp"

struct Serial : Jaffx::Program {
  const char message[7] = "Hello!";
  unsigned int counter = 0;
  bool triggered = false;

  void init() override {
    this->hardware.StartLog(true);
  }

  float processAudio(float in) override {
    counter++;
    if (counter >= this->samplerate && !triggered) {
      counter = 0;
      triggered = true;
    }
    return 0.f;
  }

  void loop() override {
    if (triggered) {
      this->hardware.PrintLine(message);
      triggered = false;
    }
    System::Delay(500);
  }
};

int main() {
  Serial mSerial;
  mSerial.start();
}
