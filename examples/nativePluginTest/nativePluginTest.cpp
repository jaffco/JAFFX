#include "../../Jaffx.hpp"

#include "./nativePlugin/lpa_abi.h" // Include the proper function bindings
#include "./nativePlugin/build/nativePlugin.h" // Include the actual binary as a C-style array


class NativePluginTest : public Jaffx::Firmware {
    const LPA_Entry* pluginInstance;
    void init() override {
        // Import plugin + call plugin's init func

        // Invalidate instruction cache
        SCB_InvalidateICache();
        __DSB();
        __ISB();

        // Typecast the pointer and access the code using this entry struct at the beginning of the program
        pluginInstance = reinterpret_cast<const LPA_Entry*>(build_nativePlugin_bin);

        if (pluginInstance->abi_version != 1) {
            // We should log error, we don't support this yet. Also something went wrong with file loading
        }
        if (!pluginInstance->initPlugin || !pluginInstance->processAudio) {
            // Log an error, these weren't brought into memory properly
        }

        pluginInstance->initPlugin();
    }
    
    float processAudio(float in) override {
        // // Call the plugin's process audio function (TODO: time it as well)
        // float out;
        // pluginInstance->processAudio(&in, &out, 1);
        // return out;
        return in;
    }
    
    void loop() override {
        // Print timings?? Not sure yet lmao
        float f1, f2, res;
        // TODO: Set f1 & f2 to random values
        pluginInstance->dummyAdd(&f1, &f2, &res);
        // TODO: Print f1 + f2 = res and the timings (in cycles)
    }
  
};

int main() {
  NativePluginTest mNativePluginTest;
  mNativePluginTest.start();
  return 0;
}


