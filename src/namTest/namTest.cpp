#include "../../Jaffx.hpp"
#include "../../Gimmel/include/gimmel.hpp"
#include "model.h"
#include <memory> // for unique_ptr && make_unique

class NamTest : public Jaffx::Firmware {
  ModelWeights weights;
  wavenet::RTWavenet<1, 1, Layer1, Layer2> model;
  std::unique_ptr<giml::Detune<float>> mDetune;
  std::unique_ptr<giml::Delay<float>> mDelay;
  std::unique_ptr<giml::Delay<float>> mDelay2;
  giml::EffectsLine<float> mFxChain;

  void init() override {
    this->debug = true;
    model.loadModel(weights.weights);

    mDetune = std::make_unique<giml::Detune<float>>(this->samplerate);
    mDetune->setParams(0.995f);
    //mDetune->enable();
    mFxChain.pushBack(mDetune.get());

    mDelay = std::make_unique<giml::Delay<float>>(this->samplerate);
    mDelay->setParams(398.f, 0.3f, 0.7f, 0.24f);
    mDelay->enable();
    mFxChain.pushBack(mDelay.get());

    mDelay2 = std::make_unique<giml::Delay<float>>(this->samplerate);
    mDelay2->setParams(798.f, 0.2f, 0.7f, 0.24f);
    mDelay2->enable();
  }

  float processAudio(float in) override {
    float dry = model.model.forward(in);
    float wet = mDetune->processSample(dry);
    float delay1 = mDelay->processSample(wet);
    float delay2 = mDelay2->processSample(wet);
    wet = giml::linMix(delay1, delay2, 0.5f);
    return wet;
  }

  void loop() override {}
  
};

int main() {
  NamTest mNamTest;
  mNamTest.start();
  return 0;
}


