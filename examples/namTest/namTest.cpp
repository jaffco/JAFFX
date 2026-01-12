#include "../../Jaffx.hpp"
#include "../../Gimmel/include/gimmel.hpp"
#include <memory> // for unique_ptr && make_unique

#include "DumbleModel.h"
#include "MarshallModel.h"

// Add NAM compatibility to giml
namespace giml {
  template <typename T, typename Layer1, typename Layer2>
  class AmpModeler : public Effect<T> {
  private:
    wavenet::RTWavenet<1, 1, Layer1, Layer2> clean, dirty;
    DumbleModelWeights cleanWeights;
    MarshallModelWeights dirtyWeights;

  public:
    void loadModels() {
      this->clean.loadModel(this->cleanWeights.weights);
      this->dirty.loadModel(this->dirtyWeights.weights);
    }
    
    T processSample(const T& input) override {
      if (!this->enabled) { return this->clean.model.forward(input); }
      return this->dirty.model.forward(input);
    }
  };
}
	
class NamTest : public Jaffx::Firmware {
  giml::AmpModeler<float, Layer1, Layer2> model;
  std::unique_ptr<giml::Detune<float>> mDetune;
  std::unique_ptr<giml::Delay<float>> mDelay;
  std::unique_ptr<giml::Delay<float>> mDelay2;
  giml::EffectsLine<float> mFxChain;

  void init() override {
    this->debug = true;
    model.loadModels();
    model.enable();
    mFxChain.pushBack(&model);

    mDetune = std::make_unique<giml::Detune<float>>(this->samplerate);
    mDetune->setParams(0.995f);
    //mDetune->enable();
    mFxChain.pushBack(mDetune.get());

    mDelay = std::make_unique<giml::Delay<float>>(this->samplerate);
    mDelay->setParams(398.f, 0.3f, 0.7f, 0.24f);
    mDelay->enable();

    mDelay2 = std::make_unique<giml::Delay<float>>(this->samplerate);
    mDelay2->setParams(798.f, 0.2f, 0.7f, 0.24f);
    mDelay2->enable();
  }

  float processAudio(float in) override {
    float dry = mFxChain.processSample(in);
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


