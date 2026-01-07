#include "../../Jaffx.hpp"

class NativePluginTest : public Jaffx::Firmware {
    void init() override {
        // TODO: Import plugin + call plugin's init func
    }
    
    float processAudio(float in) override {
        //TODO: Call the plugin's process audio function (time it as well)
        return in;
    }
    
    void loop() override {
        // Print timings?? Not sure yet lmao
    }
  
};

int main() {
  NativePluginTest mNativePluginTest;
  mNativePluginTest.start();
  return 0;
}


