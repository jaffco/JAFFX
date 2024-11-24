#include "../../Jaffx.hpp"
#include "test.h"

struct WavFile : Jaffx::Program {
  int readHead = 0;

  void init() override {}

  float processAudio(float in) override {
    float output = wav_data[readHead]; 
    readHead++;                        
    if (readHead >= wav_data_len) {readHead = 0;}
    return output;
  }
  
};

int main() {
  WavFile mWavFile;
  mWavFile.start();
  return 0;
}


