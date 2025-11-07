#include "../../Jaffx.hpp"
#include "../../Gimmel/include/gimmel.hpp"

class NoiseTest : public Jaffx::Firmware {
  giml::Compressor<float> mCompressor{this->samplerate};

  // use this to balance CPU load between audio callback and loop()
  void burnCycles(unsigned howMany = 1000) {
    static float angle = 0.f;
    for (unsigned i = 0; i < howMany; i++) {
      std::sin(angle);
      angle += 0.01f;
      if (angle > 2 * M_PI) {
        angle = 0.f;
      }
    }
  }

  void init() override {
    mCompressor.setParams(-20.f, 4.f, 10.f, 5.f, 3.5f, 100.f);
    mCompressor.enable();
    this->debug = true;
  }

  float processAudio(float in) override {
    this->burnCycles(400);
    return mCompressor.processSample(in);
  }

  void loop() override {
    //this->burnCycles();
  }
  
};

int main() {
  NoiseTest mNoiseTest;
  mNoiseTest.start();
  return 0;
}


