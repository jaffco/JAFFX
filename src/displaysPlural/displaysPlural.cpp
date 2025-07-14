#include "../../Jaffx.hpp"
#include "dev/oled_ssd130x.h"
#include "util/oled_fonts.h"

using DisplayType = daisy::OledDisplay<daisy::SSD130x4WireSpi128x32Driver>;

template <int NumDisplays>
class DisplayManager {
private:
  DisplayType mDisplay;
  GPIO mGPIOs[NumDisplays];
  unsigned activeDisplay = 0;
  unsigned registeredDisplays = 0;

  void selectDisplay(unsigned displayNumber) {
    if (displayNumber < registeredDisplays) {
      this->deselectAll();
      mGPIOs[displayNumber].Write(false); // CS LOW = active
      activeDisplay = displayNumber;
    }
  }

  void deselectAll() {
    for (unsigned i = 0; i < registeredDisplays; i++) {
      mGPIOs[i].Write(true); // CS HIGH = inactive
    }
  }  

public:

  // access to display
  DisplayType& operator()() {
    return mDisplay;
  }

  // call in Firmware::init()
  void initDisplay(DisplayType::Config config) {
    this->mDisplay.Init(config);
  }

  // registers Chip Select (CS) pins for displays
  void registerDisplay(Pin chipSelectPin) {
    if (registeredDisplays < NumDisplays) {
      mGPIOs[registeredDisplays].Init(chipSelectPin, GPIO::Mode::OUTPUT);
      mGPIOs[registeredDisplays].Write(true); // CS HIGH = inactive
      registeredDisplays++;
    }
  }

  // writes to specific display
  void update(unsigned displayNumber) {
    this->deselectAll();
    this->selectDisplay(displayNumber);
    mDisplay.Update();
  }

  // writes to all displays
  void updateAll() {
    for (auto i = 0; i < registeredDisplays; i++) {
      this->deselectAll();
      this->selectDisplay(i);
      mDisplay.Update();
    }
  }

  // clears/fills all displays
  void flushDisplays(bool fill = false) {
    this->mDisplay.Fill(fill);
    this->updateAll();
  }

};

class DisplaysPlural : public Jaffx::Firmware {
  DisplayManager<5> mDisplayManager;
  unsigned mPhase = 0; // Initialize phase counter
  bool screenState = false;
  bool lastScreenState = false;

  void init() override {
    DisplayType::Config disp_cfg;
    disp_cfg.driver_config.transport_config.pin_config.dc = seed::D9;
    disp_cfg.driver_config.transport_config.pin_config.reset = seed::D11;
    mDisplayManager.initDisplay(disp_cfg);
    mDisplayManager.registerDisplay(seed::D7);
    mDisplayManager.registerDisplay(seed::D12);
    mDisplayManager.flushDisplays();
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
      mDisplayManager.flushDisplays();
      if (screenState) {
        // Show "JAFFX" title
        mDisplayManager().SetCursor(0, 0);
        mDisplayManager().WriteString("JAFFX", Font_11x18, true);
        
        // Show status
        mDisplayManager().SetCursor(0, 20);
        mDisplayManager().WriteString("Ready!", Font_7x10, true);
        mDisplayManager.update(0);
      } else {
        // Show alternate message
        mDisplayManager().SetCursor(0, 8);
        mDisplayManager().WriteString("Audio DSP", Font_7x10, true);
        mDisplayManager().SetCursor(0, 20);
        mDisplayManager().WriteString("Platform", Font_7x10, true);
        mDisplayManager.update(1);
      }
      lastScreenState = screenState;
    }
  }
  
};

int main() {
  DisplaysPlural mDisplaysPlural;
  mDisplaysPlural.start();
  return 0;
}


