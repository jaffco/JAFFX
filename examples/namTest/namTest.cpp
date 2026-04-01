#include "../../Jaffx.hpp"
#include "../../Gimmel/include/gimmel.hpp"
#include <memory> // for unique_ptr && make_unique

#include "../../MicroNAM/MicroNAM.h"
#include "models/FenderModel.h"
#include "models/MarshallModel.h"

// Add NAM compatibility to giml
namespace giml {
  class AmpModeler : public Effect<float> {
  private:
    MicroNAM::NanoNet<1> mFenderNet, mMarshallNet;

  public:
    void loadModels() {
      static_assert(FenderModelWeightsCount == 842, "NamWavenet expects 842 weights");
      mFenderNet.load_weights(FenderModelWeights);
      static_assert(MarshallModelWeightsCount == 842, "NamWavenet expects 842 weights");
      mMarshallNet.load_weights(MarshallModelWeights);
    }
    
    float processSample(const float& input) override {
      float inputBuffer[1];
      inputBuffer[0] = input;
      float output[1];
      if (!this->enabled) {
        mFenderNet.forward(inputBuffer, output);
        return output[0];
      } 
      mMarshallNet.forward(inputBuffer, output);
      return output[0];
    }
  };
}
	
class NamTest : public Jaffx::Firmware {
  giml::AmpModeler mAmpModeler;
  std::unique_ptr<giml::Delay<float>> mDelay;
  std::unique_ptr<giml::Delay<float>> mDelay2;

  void init() override {
    this->debug = true;
    mAmpModeler.loadModels();
    mAmpModeler.enable();

    mDelay = std::make_unique<giml::Delay<float>>(this->samplerate);
    mDelay->setParams(398.f, 0.3f, 0.7f, 0.24f);
    mDelay->enable();

    mDelay2 = std::make_unique<giml::Delay<float>>(this->samplerate);
    mDelay2->setParams(798.f, 0.2f, 0.7f, 0.24f);
    mDelay2->enable();
  }

  float processAudio(float in) override {
    float dry = mAmpModeler.processSample(in);
    float delay1 = mDelay->processSample(dry);
    float delay2 = mDelay2->processSample(dry);
    float output = giml::linMix(delay1, delay2, 0.5f);
    return output;
  }

  void loop() override {}
  
};

int main() {
  NamTest mNamTest;
  mNamTest.start();
  return 0;
}