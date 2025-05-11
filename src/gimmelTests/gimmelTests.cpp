#include "../../Jaffx.hpp"
#include "../../Gimmel/include/gimmel.hpp"
#include <memory>
#include <cmath> // Required for isnan()

/**
 * @brief Testing ground for Gimmel effects
 * @note Makefile has arm_math for optimization of trig calls
 */
class GimmelTests : public Jaffx::Firmware {
	// std::unique_ptr<giml::EffectYouAreTesting<float>> mEffect;
	giml::EffectsLine<float> signalChain;
	Switch mReboot, mToggle;
	
	void init() override {
		this->debug = true;
		mReboot.Init(seed::D18, 0.f, Switch::Type::TYPE_MOMENTARY, Switch::Polarity::POLARITY_NORMAL);
		mToggle.Init(seed::D16, 0.f, Switch::Type::TYPE_MOMENTARY, Switch::Polarity::POLARITY_NORMAL);

		// mEffect = std::make_unique<giml::EffectYouAreTesting<float>>(this->samplerate);
		// mEffect->setParams();
		// mEffect->toggle(true);
		// signalChain.pushBack(mEffect.get());
	}

	float processAudio(float in) override {
		return signalChain.processSample(in);
	}

	/**
   * @todo callbacks for params/setters
   */
  void blockStart() override {
    Firmware::blockStart(); // for debug mode
		mReboot.Debounce();
    if (mReboot.TimeHeldMs() > 100.f) {
			this->hardware.system.ResetToBootloader(daisy::System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
		};

		mToggle.Debounce();
		if (mToggle.FallingEdge()) {
			signalChain[0]->toggle();
		}
  }

	void loop() override {
    this->hardware.PrintLine("Some Status Regarding Your Effect");
  }
    
};

int main() {
  GimmelTests mGimmelTests;
  mGimmelTests.start();
  return 0;
}