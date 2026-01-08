#include "../../Jaffx.hpp"

#include "./nativePlugin/lpa_abi.h" // Include the proper function bindings
#include "./nativePlugin/build/nativePlugin.h" // Include the actual binary as a C-style array

class JaffxTimer {
private:
  bool mDone = false;
  unsigned int mStartTime = 0;
  unsigned int mEndTime = 0;
  unsigned int mTickFreq = 0; 

public:
  void start() {  
    mTickFreq = System::GetTickFreq();
    this->mStartTime = System::GetTick();
  }

  void end() {
    this->mEndTime = System::GetTick();
    mDone = true;
  }

  unsigned int ticksElapsed() {
    if (!mDone) {
      return 0;
    }
    return mEndTime - mStartTime;
  }

  float usElapsed() {
    if (!mDone) {
      return 0.f;
    }
    float ticksElapsed = float(mEndTime - mStartTime);
    return (ticksElapsed * 1e6f) / mTickFreq;
  }
};

class NativePluginTest : public Jaffx::Firmware {
    const LPA_Entry* pluginInstance;
    void init() override {

        // Start logging and wait for serial connection
        this->hardware.StartLog(true);
        System::Delay(200);
        this->hardware.PrintLine("===========================================");
        this->hardware.PrintLine("    Native Plugin Test Program             ");
        this->hardware.PrintLine("===========================================");
        this->hardware.PrintLine(""); 


        // Import plugin + call plugin's init func

        this->hardware.PrintLine("Invalidating instruction cache...");
        // Invalidate instruction cache
        SCB_InvalidateICache();
        __DSB();
        __ISB();
        this->hardware.PrintLine("Instruction cache invalidated.");


        this->hardware.PrintLine("Loading plugin from embedded binary...");
        // Typecast the pointer and access the code using this entry struct at the beginning of the program
        pluginInstance = reinterpret_cast<const LPA_Entry*>(build_nativePlugin_bin);
        this->hardware.PrintLine("Plugin loaded at address: %p", pluginInstance);

        this->hardware.PrintLine("Verifying plugin ABI version and function pointers...");

        if (pluginInstance->abi_version != 1) {
            // We should log error, we don't support this yet. Also something went wrong with file loading
            this->hardware.PrintLine("[ERROR] Unsupported plugin ABI version: %d", pluginInstance->abi_version);
            while (true) {} // halt here
        }

        if (!pluginInstance->initPlugin || !pluginInstance->processAudio) {
            // Log an error, these weren't brought into memory properly
            this->hardware.PrintLine("[ERROR] Plugin function pointers are null!");
        }

        this->hardware.PrintLine("Plugin ABI version: %d", pluginInstance->abi_version);
        this->hardware.PrintLine("Plugin function pointers verified.");

        this->hardware.PrintLine("Initializing plugin...");
        pluginInstance->initPlugin();
        this->hardware.PrintLine("Plugin initialized successfully!");
    }
    
    void loop() override {
        this->hardware.PrintLine("[STATUS] loop() called");
        this->hardware.PrintLine("[STATUS] Stopping Audio...");
        this->hardware.StopAudio();
        this->hardware.PrintLine("[OK] Audio stopped");

        // Benchmark configuration
        const int WARMUP_RUNS = 10;
        const int BENCHMARK_RUNS = 100;


        // Warmup phase to stabilize caches and branch prediction
        this->hardware.PrintLine("[WARMUP] Running %d warmup iterations with random inputs...", WARMUP_RUNS);
        float warmup_result = 0.0f;  // Prevent optimization
        for (int i = 0; i < WARMUP_RUNS; i++) {
            // Use random inputs for warmup too
            float a = (float)(daisy::Random::GetValue() % 1000);
            float b = (float)(daisy::Random::GetValue() % 1000);
            pluginInstance->dummyAdd(&a, &b, &warmup_result);
        }
        this->hardware.PrintLine("[OK] Warmup complete (result: " FLT_FMT3 ")", FLT_VAR3(warmup_result));
        
        // Benchmark phase with random inputs
        this->hardware.PrintLine("");
        this->hardware.PrintLine("[BENCHMARK] Running %d iterations with random inputs...", BENCHMARK_RUNS);
        
        float total_us = 0.0f;
        float min_us = 1e9f;
        float max_us = 0.0f;
        float total_ticks = 0.0f;
        float min_ticks = 1e9f;
        float max_ticks = 0.0f;
        volatile float checksum = 0.0f;  // Prevent optimization
        
        for (int i = 0; i < BENCHMARK_RUNS; i++) {
            JaffxTimer timer;
            
            // Use true random numbers from libDaisy RNG peripheral
            // Constrain to reasonable range to avoid overflow issues
            float a = (float)(daisy::Random::GetValue() % 1000000);  // 0 to ~1M
            float b = (float)(daisy::Random::GetValue() % 1000000);  // 0 to ~1M
            float result = 0.f;

            timer.start();          
            pluginInstance->dummyAdd(&a, &b, &result);
            timer.end();
            
            float elapsed_us = timer.usElapsed();
            float elapsed_ticks = (float)timer.ticksElapsed();
            
            total_us += elapsed_us;
            total_ticks += elapsed_ticks;
            
            if (elapsed_us < min_us) min_us = elapsed_us;
            if (elapsed_us > max_us) max_us = elapsed_us;
            if (elapsed_ticks < min_ticks) min_ticks = elapsed_ticks;
            if (elapsed_ticks > max_ticks) max_ticks = elapsed_ticks;
            
            // Use result to prevent dead code elimination
            checksum += result;
        }
        
        float avg_us = total_us / BENCHMARK_RUNS;
        float avg_ticks = total_ticks / BENCHMARK_RUNS;
        
        this->hardware.PrintLine("");
        this->hardware.PrintLine("=== BENCHMARK RESULTS ===");
        this->hardware.PrintLine("Iterations: %d", BENCHMARK_RUNS);
        this->hardware.PrintLine("Average:    " FLT_FMT3 " us (%d ticks)", FLT_VAR3(avg_us), (int)avg_ticks);
        this->hardware.PrintLine("Minimum:    " FLT_FMT3 " us (%d ticks)", FLT_VAR3(min_us), (int)min_ticks);
        this->hardware.PrintLine("Maximum:    " FLT_FMT3 " us (%d ticks)", FLT_VAR3(max_us), (int)max_ticks);
        this->hardware.PrintLine("Checksum:   " FLT_FMT3 " (prevents optimization)", FLT_VAR3(checksum));
        this->hardware.PrintLine("We made it!");

        // Prepare for next test iteration
        System::Delay(200);
        System::ResetToBootloader(System::BootloaderMode::DAISY_INFINITE_TIMEOUT);        
    } 
  
};

int main() {
  NativePluginTest mNativePluginTest;
  mNativePluginTest.start();
  return 0;
}


