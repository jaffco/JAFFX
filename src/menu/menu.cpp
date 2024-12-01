#include "../../Jaffx.hpp"

// Hardware config:
// led1 on pin D15
// led2 on pin D16
// led3 on pin D17
// encoder A on pin A3
// encoder B on pin A4
// encoder btn on pin A5

struct Menu : Jaffx::Program {
  bool editMode = false; // encoder press
  int select = 0; // selector
  GPIO leds[3];
  Encoder mEncoder;

  void init() override {
    leds[0].Init(seed::D15, GPIO::Mode::OUTPUT); // init led1
    leds[1].Init(seed::D16, GPIO::Mode::OUTPUT); // init led2
    leds[2].Init(seed::D17, GPIO::Mode::OUTPUT); // init led3
    mEncoder.Init(seed::A3, seed::A4, seed::A5); // init encoder
    hardware.StartLog();
  }

  void blockStart() override {
    mEncoder.Debounce();
    if (mEncoder.RisingEdge()) {editMode = !editMode;} // flip state
    if (editMode) {
      select = (select + 3 + mEncoder.Increment()) % 3;
    }
  }

  bool trigger = false;
  int counter = 0;
  float processAudio(float in) override {
    counter++;
    if (counter >= samplerate && !trigger) {
      trigger = true;
      counter = 0;
    }
    return 0.f;
  }

  void loop() override {

    for (int i = 0; i < 3; i++) {
      if (editMode) {
        if (i == select && !leds[i].Read()) {leds[i].Write(true);} 
        else {leds[i].Write(false);}
      } else {leds[i].Write(true);}
    }

    if (trigger) {
      hardware.PrintLine("Select: %d", select);
      trigger = false;
    }

  }
  
};

int main() {
  Menu mMenu;
  mMenu.start();
  return 0;
}