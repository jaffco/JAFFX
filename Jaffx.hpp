#include "libDaisy/src/daisy_seed.h"
using namespace daisy;

namespace Jaffx {
  struct Program {
    // declare an instance of the hardware
    static DaisySeed hardware;
    static Program* instance; // Static pointer to the current instance of Program

    // overridable per-sample operation
    inline virtual float processAudio(float in) {return in;}

    // basic mono->dual-mono callback
    static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
      for (size_t i = 0; i < size; i++) {
        out[0][i] = instance->processAudio(in[0][i]); // format is in/out[channel][sample]
        out[1][i] = out[0][i];
      }
    }

    void start() {
      // initialize hardware
      hardware.Init();
      hardware.SetAudioBlockSize(4); // number of samples handled per callback (buffer size)
      hardware.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ); // sample rate

      // init instance and start callback
      instance = this;
      hardware.StartAudio(AudioCallback);

      // loop indefinitely
      while(1) {}
    }
  };

  // Global instancing of static members
  DaisySeed Program::hardware; 
  Program* Program::instance = nullptr;
} // namespace Jaffx