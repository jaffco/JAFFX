#include "../../Jaffx.hpp"

// Hardware config:
// led1 on pin D14
// led2 on pin D13
// led3 on pin D12
// encoder1 A on pin A0
// encoder1 B on pin A1
// encoder1 btn on pin A2
// encoder2 A on pin A3
// encoder2 B on pin A4
// encoder2 btn on pin A5

struct MenuManager {
  bool editMode = false; // encoder press
  int select = 0; // selector
  bool toggles[3]; // led on/off when !editMode
  float params[3]; // params (will only modify params[0] in this app)
  GPIO leds[3]; 
  Encoder encoders[2]; 

  void init() {
    for (auto &t : toggles) {t = true;} // init all toggles true for now
    for (auto & p: params) {p = 0.f;} // init all params 0.f for now
    leds[0].Init(seed::D14, GPIO::Mode::OUTPUT); // init led1
    leds[1].Init(seed::D13, GPIO::Mode::OUTPUT); // init led2
    leds[2].Init(seed::D12, GPIO::Mode::OUTPUT); // init led3
    encoders[0].Init(seed::A0, seed::A1, seed::A2); // init encoder1
    encoders[1].Init(seed::A3, seed::A4, seed::A5); // init encoder2
  }

  void processInput() {
    // debounce encoders
    for (int i = 0; i < 2; i++) {
      encoders[i].Debounce();
    }

    // check encoder1 for edit mode, encoder2 for toggle[0] state
    if (encoders[0].RisingEdge()) {editMode = !editMode;} // flip state
    if (encoders[1].RisingEdge()) {toggles[0] = !toggles[0];}

    // if editmode, process selector and param knob
    if (editMode) { // if edit mode
      select = (select + 3 + encoders[0].Increment()) % 3; // process selector with modulus logic
      for (int i = 0; i < 3; i++) {
        if (select == i) { // modify params based on which is selected
          params[i] += encoders[1].Increment() * 0.01f;
          params[i] = (params[i] < 0.f) ? 0.f : (params[i] > 1.f ? 1.f : params[i]); 
          // ^TODO: handle clamping in a param class 
        }
      } 
    } 

  }

  void processOutput() {
    for (int i = 0; i < 3; i++) { // for each led
      if (editMode) { // if editMode, light selected
        if (i == select && !leds[i].Read()) {leds[i].Write(true);} 
        else {leds[i].Write(false);}
      } else { // if !editMode, show toggle state
        for (int i = 0; i < 3; i++) {
          leds[i].Write(toggles[i]);
        }
      }
    }
  }

};

class Menu : public Jaffx::Firmware {
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

  // should this loop have a `System::Delay()` in it to reduce CPU?
  void loop() override {

    mMenuManager.processOutput();

    // print param vals once per second
    if (trigger) {
      hardware.PrintLine("Params:");
      hardware.PrintLine("params[0]: " FLT_FMT3, FLT_VAR3(mMenuManager.params[0]));
      hardware.PrintLine("params[1]: " FLT_FMT3, FLT_VAR3(mMenuManager.params[1]));
      hardware.PrintLine("params[2]: " FLT_FMT3, FLT_VAR3(mMenuManager.params[2]));
      trigger = false;
    }

  }
  
};

int main() {
  Menu mMenu;
  mMenu.start();
  return 0;
}