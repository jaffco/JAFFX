#include "../../Jaffx.hpp"
#include "../../Gimmel/include/utility.hpp"

// This app demonstrates 3 LEDs that are always on using raw GPIO
// Hardware config:
// LEDs on pins D0, D1, D2

class DBMeter : public Jaffx::Firmware {
  GPIO mLeds[3];
  float RmsReport = 0.f;

  void init() override {
    mLeds[0].Init(seed::D0, GPIO::Mode::OUTPUT);
    mLeds[1].Init(seed::D2, GPIO::Mode::OUTPUT);
    mLeds[2].Init(seed::D4, GPIO::Mode::OUTPUT);
  }

  float processAudio(float in) override {
    static unsigned counter = 0;
    static float RMS = 0.f;
    counter++;
    RMS += in * in;
    if (counter >= buffersize){
      RmsReport = sqrt(RMS); // update report
      counter = 0; // reset counter
      RMS = 0.f; // reset RMS
    }
    return in; // output throughput 
  }

  void loop() override {
    // Convert RMS to dB                // Add small offset to avoid log(0)   
    float dB = 20.0f * log10f(RmsReport + 1e-12f); 
    
    // LED logic based on dB levels
    if (dB >= 0.0f) {
      // >= 0dB: all three LEDs on
      mLeds[0].Write(true);
      mLeds[1].Write(true);
      mLeds[2].Write(true);
    } else if (dB >= -6.0f) {
      // >= -6dB: LEDs 1 and 2 on, LED 0 off
      mLeds[0].Write(false);
      mLeds[1].Write(true);
      mLeds[2].Write(true);
    } else if (dB >= -20.0f) {
      // >= -20dB: LED 2 on, LEDs 0 and 1 off
      mLeds[0].Write(false);
      mLeds[1].Write(false);
      mLeds[2].Write(true);
    } else {
      // < -20dB: all LEDs off
      mLeds[0].Write(false);
      mLeds[1].Write(false);
      mLeds[2].Write(false);
    }
    
    System::Delay(1);
  }
  
};

int main() {
  DBMeter mDBMeter;
  mDBMeter.start();
  return 0;
}


