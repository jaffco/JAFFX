#include "../../Jaffx.hpp"
#include "dev/oled_ssd1312.h"
#include "util/oled_fonts.h"

using DisplayType = daisy::OledDisplay<daisy::SSD13124WireSpi128x32Driver>;

class DisplayCalibration : public Jaffx::Firmware {
  DisplayType mDisplay;
  unsigned mPhase = 0;
  int testState = 0; // 0=corners, 1=edges, 2=grid, 3=text positioning
  bool lastUpdate = false;

  void init() override {
    DisplayType::Config disp_cfg;
    disp_cfg.driver_config.transport_config.pin_config.dc = seed::D9;
    disp_cfg.driver_config.transport_config.pin_config.reset = seed::D11;
    mDisplay.Init(disp_cfg);
    mDisplay.Fill(false);
    mDisplay.Update();
  }
  
  float processAudio(float in) override {
    mPhase++;
    if (mPhase >= this->samplerate * 2.0) { // Every 2 seconds...
      testState = (testState + 1) % 4; // Cycle through 4 test states
      mPhase = 0;
      lastUpdate = true;
    }
    return in;
  }

  void loop() override {
    if (lastUpdate) {
      mDisplay.Fill(false); // Clear screen
      
      switch(testState) {
        case 0: // Test corner pixels
          mDisplay.DrawPixel(0, 0, true);      // Top-left
          mDisplay.DrawPixel(127, 0, true);    // Top-right  
          mDisplay.DrawPixel(0, 31, true);     // Bottom-left
          mDisplay.DrawPixel(127, 31, true);   // Bottom-right
          
          // Add some nearby pixels to make corners more visible
          mDisplay.DrawPixel(1, 0, true);
          mDisplay.DrawPixel(0, 1, true);
          mDisplay.DrawPixel(126, 0, true);
          mDisplay.DrawPixel(127, 1, true);
          mDisplay.DrawPixel(1, 31, true);
          mDisplay.DrawPixel(0, 30, true);
          mDisplay.DrawPixel(126, 31, true);
          mDisplay.DrawPixel(127, 30, true);
          break;
          
        case 1: // Test edge lines
          // Top edge
          for(int x = 0; x < 128; x += 4) {
            mDisplay.DrawPixel(x, 0, true);
          }
          // Bottom edge  
          for(int x = 0; x < 128; x += 4) {
            mDisplay.DrawPixel(x, 31, true);
          }
          // Left edge
          for(int y = 0; y < 32; y += 2) {
            mDisplay.DrawPixel(0, y, true);
          }
          // Right edge
          for(int y = 0; y < 32; y += 2) {
            mDisplay.DrawPixel(127, y, true);
          }
          break;
          
        case 2: // Test grid pattern
          for(int x = 0; x < 128; x += 16) {
            for(int y = 0; y < 32; y += 8) {
              mDisplay.DrawPixel(x, y, true);
              mDisplay.DrawPixel(x+1, y, true);
              mDisplay.DrawPixel(x, y+1, true);
              mDisplay.DrawPixel(x+1, y+1, true);
            }
          }
          break;
          
        case 3: // Test text positioning at different coordinates
          // Top-left text
          mDisplay.SetCursor(0, 0);
          mDisplay.WriteString("TL", Font_7x10, true);
          
          // Top-right text (estimate position)
          mDisplay.SetCursor(114, 0); // 128 - (2 chars * 7 pixels)
          mDisplay.WriteString("TR", Font_7x10, true);
          
          // Bottom-left text
          mDisplay.SetCursor(0, 22); // 32 - 10 pixels font height
          mDisplay.WriteString("BL", Font_7x10, true);
          
          // Bottom-right text
          mDisplay.SetCursor(114, 22);
          mDisplay.WriteString("BR", Font_7x10, true);
          
          // Center text
          mDisplay.SetCursor(57, 11); // Rough center: (128-14)/2, (32-10)/2
          mDisplay.WriteString("C", Font_7x10, true);
          
          // Test coordinate (0,0) specifically
          mDisplay.DrawPixel(0, 0, true);  // Mark where (0,0) actually is
          break;
      }
      
      mDisplay.Update();
      lastUpdate = false;
    }
  }
  
};

int main() {
  DisplayCalibration mDisplayCalibration;
  mDisplayCalibration.start();
  return 0;
}


