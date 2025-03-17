#include "../../Jaffx.hpp"

// This app allows for easy testing and effect's impulse response
class IrTest : public Jaffx::Firmware {
  bool impulse = false;
  int counter = 0;

  void init() override {}

  float processAudio(float in) override {
    impulse = false;
    counter++;
    if (counter >= samplerate) {
      impulse = true; // send an impulse once per second
      counter = 0;
    }
    return impulse;
  }
  
};

int main() {
  IrTest mIrTest;
  mIrTest.start();
  return 0;
}


