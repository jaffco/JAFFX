#include "../../Jaffx.hpp"
#include "dev/oled_ssd130x.h"
#include "util/oled_fonts.h"

using DisplayType = daisy::OledDisplay<daisy::SSD130x4WireSpi128x32Driver>;

class Display : public Jaffx::Firmware {
  DisplayType mDisplay;
  unsigned mPhase = 0; // Initialize phase counter
  bool screenState = false;
  bool lastScreenState = false;

  void init() override {
    DisplayType::Config disp_cfg;
    disp_cfg.driver_config.transport_config.pin_config.dc = seed::D9;
    disp_cfg.driver_config.transport_config.pin_config.reset = seed::D11;
    mDisplay.Init(disp_cfg);
    mDisplay.Fill(false); // Clear the display
    mDisplay.Update();
  }
  
  float processAudio(float in) override {
    mPhase++;
    if (mPhase >= this->samplerate * 0.5) { // Every 0.5 seconds...
      screenState = !screenState; // Toggle screen state
      mPhase = 0;
    }
    return in;
  }

  void loop() override {
    if (screenState != lastScreenState) {
      mDisplay.Fill(false); // Clear screen
      
      if (screenState) {
        // Show "JAFFX" title
        mDisplay.SetCursor(0, 0);
        mDisplay.WriteString("JAFFX", Font_11x18, true);
        
        // Show status
        mDisplay.SetCursor(0, 20);
        mDisplay.WriteString("Ready!", Font_7x10, true);
      } else {
        // Show alternate message
        mDisplay.SetCursor(0, 8);
        mDisplay.WriteString("Audio DSP", Font_7x10, true);
        mDisplay.SetCursor(0, 20);
        mDisplay.WriteString("Platform", Font_7x10, true);
      }
      
      mDisplay.Update();
      lastScreenState = screenState;
    }
  }
  
};

int main() {
  Display mDisplay;
  mDisplay.start();
  return 0;
}


