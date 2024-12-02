#include "../../Jaffx.hpp"
#include "../../Gimmel/include/gimmel.hpp"
#include <memory>

// 2024-12-02
// prototyped containerization of giml fx
// seems to be breaking delay-based effects

// Testing ground for Gimmel effects
// Biquad: working!
// Chorus: working!
// Compressor: working!
// Delay: working!
// Detune: working!
// Reverb: works... but fragile
// Tremolo: working!

struct GimmelTests : Jaffx::Program {
	std::unique_ptr<giml::Effect<float>> fxChain[3];
	
	void init() override {
		fxChain[0] = std::make_unique<giml::Reverb<float>>(this->samplerate);
		auto& mReverb = static_cast<giml::Reverb<float>&>(*fxChain[0]);
		mReverb.setParams(0.02f, 0.2f, 0.2f, 25.f, 0.3f);
		mReverb.enable();

		fxChain[1] = std::make_unique<giml::Delay<float>>(this->samplerate);
		auto& mDelay = static_cast<giml::Delay<float>&>(*fxChain[0]);
		mDelay.setDelayTime(398);
		mDelay.setDamping(0.5f);
		mDelay.setFeedback(0.3);
		mDelay.enable();

		fxChain[2] = std::make_unique<giml::Compressor<float>>(this->samplerate);
		auto& mCompressor = static_cast<giml::Compressor<float>&>(*fxChain[1]);
		mCompressor.setAttack(3.5f);
		mCompressor.setKnee(4.f);
		mCompressor.setMakeupGain(20.f);
		mCompressor.setRatio(14.f);
		mCompressor.setRelease(100.f);
		mCompressor.setThresh(-35.f);
		mCompressor.enable();
	}

	float processAudio(float in) override {
		float out = in;
		for (auto &e : fxChain) {out = e->processSample(out);}
		return out;
	}

};

int main() {
  GimmelTests mGimmelTests;
  mGimmelTests.start();
  return 0;
}