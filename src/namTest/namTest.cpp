#include "../../RTNeural/RTNeural/RTNeural.h"
#include "../../RTNeural/modules/rt-nam/rt-nam.hpp"
#include "model.h"
#include "../../Jaffx.hpp"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif
#ifndef M_E
#define M_E (2.71828182845904523536)
#endif
#ifndef M_PI_2
#define M_PI_2 (M_PI / 2)
#endif
#include "../../Gimmel/include/detune.hpp"
#include "../../Gimmel/include/delay.hpp"
#include <memory> // for unique_ptr && make_unique

using Layer1 =
wavenet::Layer_Array<float, 
                     1, // input_size
                     1, // condition_size
                     2, // head_size
                     2, // channels
                     3, // kernel_size
                     wavenet::Dilations<1, 2, 4, 8, 16, 32, 64>, // dilations
                     false, // head_bias
                     wavenet::NAMMathsProvider>; // maths provider

using Layer2 = 
wavenet::Layer_Array<float, 
                     2, // input_size
                     1, // condition_size
                     1, // head_size
                     2, // channels
                     3, // kernel_size
                     wavenet::Dilations<128, 256, 512, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512>, // dilations
                     true, // head_bias
                     wavenet::NAMMathsProvider>; // maths provider

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


