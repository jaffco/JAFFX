#include "../../RTNeural/RTNeural/RTNeural.h"
#include "wavenet/wavenet_model.hpp"
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

struct NAMMathsProvider
{
#if RTNEURAL_USE_EIGEN
  template <typename Matrix>
  static auto tanh (const Matrix& x) {
    // See: math_approx::tanh<3>
    const auto x_poly = x.array() * (1.0f + 0.183428244899f * x.array().square());
    return x_poly.array() * (x_poly.array().square() + 1.0f).array().rsqrt();
    //return x.array().tanh(); 
    // Tried using Eigen's built in tanh(), also works, failed on the same larger models as above custom tanh
  }
#elif RTNEURAL_USE_XSIMD
  template <typename T>
  static T tanh (const T& x) {
    return math_approx::tanh<3> (x);
  }
#endif
};

using Dilations = wavenet::Dilations<1, 2, 4, 8, 16, 32, 64>;
using Dilations2 = wavenet::Dilations<128, 256, 512, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512>;
wavenet::Wavenet_Model<float,
                       1,
                       wavenet::Layer_Array<float, 1, 1, 2, 2, 3, Dilations, false, NAMMathsProvider>,
                       wavenet::Layer_Array<float, 2, 1, 1, 2, 3, Dilations2, true, NAMMathsProvider>>
    rtneural_wavenet; 

void loadModel(ModelWeights& model) {
  rtneural_wavenet.load_weights(model.weights);
  static constexpr size_t N = 1; // number of samples sent through model at once
  rtneural_wavenet.prepare (N); // This is needed, including this allowed the led to come on before freezing
  rtneural_wavenet.prewarm();  // Note: looks like this just sends some 0's through the model
}

class NamTest : public Jaffx::Firmware {
  ModelWeights model;
  std::unique_ptr<giml::Detune<float>> mDetune;
  std::unique_ptr<giml::Delay<float>> mDelay;
  std::unique_ptr<giml::Delay<float>> mDelay2;
  giml::EffectsLine<float> mFxChain;

  void init() override {
    loadWeights(model);
    loadModel(model);

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
    float dry = rtneural_wavenet.forward(in);
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


