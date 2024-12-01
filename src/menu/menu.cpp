#include "../../Jaffx.hpp"

// Hardware config:
// led1 on pin D15
// led2 on pin D16
// led3 on pin D17
// encoder A on pin A3
// encoder B on pin A4
// encoder btn on pin A5

struct MenuManager {
  bool editMode = false; // encoder press
  int select = 0; // selector
  GPIO leds[3];
  Encoder mEncoder;

  void init() {
    leds[0].Init(seed::D15, GPIO::Mode::OUTPUT); // init led1
    leds[1].Init(seed::D16, GPIO::Mode::OUTPUT); // init led2
    leds[2].Init(seed::D17, GPIO::Mode::OUTPUT); // init led3
    mEncoder.Init(seed::A3, seed::A4, seed::A5); // init encoder
  }

  void processInput() {
    mEncoder.Debounce();
    if (mEncoder.RisingEdge()) {editMode = !editMode;} // flip state
    if (editMode) { // if edit mode
      select = (select + 3 + mEncoder.Increment()) % 3; 
    } // ^process selector with modulus logic
  }

  void processOutput() {
    for (int i = 0; i < 3; i++) { // for each led
      if (editMode) { // if editMode, light selected
        if (i == select && !leds[i].Read()) {leds[i].Write(true);} 
        else {leds[i].Write(false);}
      } else {leds[i].Write(true);} // else, light all
    }
  }

};

struct Menu : Jaffx::Program {
  MenuManager mMenuManager;

  void init() override {
    mMenuManager.init();
    hardware.StartLog();
  }

  void blockStart() override {
    mMenuManager.processInput();
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

    mMenuManager.processOutput();

    if (trigger) {
      hardware.PrintLine("Select: %d", mMenuManager.select);
      trigger = false;
    }

  }
  
};

int main() {
  Menu mMenu;
  mMenu.start();
  return 0;
}