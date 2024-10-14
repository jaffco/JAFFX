#include "libDaisy/src/daisy_seed.h"
using namespace daisy;

namespace Jaffx {
  static DaisySeed hardware;

  // overridable per-sample operation
  inline float processAudio(float in) {return in;}

  // basic mono->dual-mono callback
  static void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                            AudioHandle::InterleavingOutputBuffer out,
                            size_t                                size)
  {
    for(size_t i = 0; i < size; i += 2)
    {
      // left out
      out[i] = processAudio(in[i]);

      // right out
      out[i + 1] = out[i];
    }
  }

  int start(void)
  {
    // initialize hardware
    hardware.Init();
    hardware.SetAudioBlockSize(4);

    // start callback
    hardware.StartAudio(AudioCallback);

    // loop indefinitely 
    while(1) {}
  }
}