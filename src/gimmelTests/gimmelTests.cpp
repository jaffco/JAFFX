#include "../../Jaffx.hpp"
#include "../../Gimmel/include/gimmel.hpp"
#include <memory>

/**
 * @brief Testing ground for Gimmel effects
 * @note Makefile has arm_math turned on 
 * for optimization of trig calls
 */
class GimmelTests : public Jaffx::Firmware {
	// std::unique_ptr<giml::EffectYouAreTesting<float>> mEffect;
	giml::EffectsLine<float> signalChain;
	
	void init() override {
		// mEffect = std::make_unique<giml::EffectYouAreTesting<float>>(this->samplerate);
		// mEffect->setParams();
		// mEffect->toggle(true);
		// signalChain.pushBack(mEffect.get());
	}

	float processAudio(float in) override {
		// return signalChain.processSample(in);
		return in;
	}

	void loop() override {};
    
};

int main() {
  GimmelTests mGimmelTests;
  mGimmelTests.start();
  return 0;
}