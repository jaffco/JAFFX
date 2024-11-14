#include "../../Jaffx.hpp"
#include <string>

struct Serial : Jaffx::Program {
  const char message[7] = "Hello!";
  unsigned int counter = 0;

  void init() override {
    this->hardware.StartLog(true);
  }

  float processAudio(float in) override {
    counter++;
    if (counter >= this->samplerate * 3) {
      counter = 0;
      this->hardware.PrintLine(message);
    }
    return 0.f;
  }
};

int main(void) {
  Serial mSerial;
  mSerial.start();
}
