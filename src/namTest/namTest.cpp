#include "../../RTNeural/RTNeural/RTNeural.h"
#include "wavenet/wavenet_model.hpp"
#include "model.h"
#include "../../Jaffx.hpp"

struct NAMMathsProvider
{
#if RTNEURAL_USE_EIGEN
    template <typename Matrix>
    static auto tanh (const Matrix& x)
    {
        // See: math_approx::tanh<3>
        const auto x_poly = x.array() * (1.0f + 0.183428244899f * x.array().square());
        return x_poly.array() * (x_poly.array().square() + 1.0f).array().rsqrt();
        //return x.array().tanh(); // Tried using Eigen's built in tanh(), also works, failed on the same larger models as above custom tanh
    }
#elif RTNEURAL_USE_XSIMD
    template <typename T>
    static T tanh (const T& x)
    {
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

  void init() override {
    loadWeights(model);
    loadModel(model);
  }

  float processAudio(float in) override {
    return rtneural_wavenet.forward(in);
  }

  void loop() override {}
  
};

int main() {
  NamTest mNamTest;
  mNamTest.start();
  return 0;
}


