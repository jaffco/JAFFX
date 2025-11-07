#include "../../Jaffx.hpp"

class EncoderRead : public Jaffx::Firmware {
  Encoder mEncoder;
  int encoderState = 0;

  void init() override {
    mEncoder.Init(seed::A2, seed::A3, seed::A4);
    this->hardware.StartLog();
  }

  float processAudio(float in) override {
    mEncoder.Debounce(); // debounce encoder regularly
    encoderState += mEncoder.Increment(); // increment/decrement on turn
    if (mEncoder.RisingEdge()) {encoderState = 0;} // reset encoder state on press
    return 0.f;
  }

  void loop() override {
    hardware.PrintLine("Encoder State: %d", encoderState);
    System::Delay(500); // Don't spam the serial!
  }
  
};

int main() {
  EncoderRead mEncoderRead;
  mEncoderRead.start();
  return 0;
}


