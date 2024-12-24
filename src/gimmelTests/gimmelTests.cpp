#include "../../Jaffx.hpp"
#include "../../Gimmel/include/gimmel.hpp"
#include <memory>

// 2024-12-02
// prototyped containerization of giml fx
// breaks delay-based effects
// see commit `5a921ca`

// Testing ground for Gimmel effects
// Biquad: working!
// Chorus: working!
// Compressor: working!
// Delay: working!
// Detune: working!
// Reverb: works... but fragile
// Tremolo: working!

struct GimmelTests : Jaffx::Program {
	std::unique_ptr<giml::Biquad<float>> mBiquad;
	std::unique_ptr<giml::Chorus<float>> mChorus;
	std::unique_ptr<giml::Compressor<float>> mCompressor;
	std::unique_ptr<giml::Delay<float>> mDelay;
	std::unique_ptr<giml::Detune<float>> mDetune;
	std::unique_ptr<giml::Reverb<float>> mReverb;
	std::unique_ptr<giml::Tremolo<float>> mTremolo;
	
	void init() override {
		mReverb = std::make_unique<giml::Reverb<float>>(this->samplerate);
		mReverb->setParams(0.02, 0.75, 0.5, 1000, 0.25, giml::Reverb<float>::RoomType::CUBE);
		mReverb->enable();

		mCompressor = std::make_unique<giml::Compressor<float>>(this->samplerate);
		mCompressor->setAttack(3.5f);
		mCompressor->setKnee(4.f);
		mCompressor->setMakeupGain(20.f);
		mCompressor->setRatio(14.f);
		mCompressor->setRelease(100.f);
		mCompressor->setThresh(-35.f);
		mCompressor->enable();
	}

	float processAudio(float in) override {
		counter++;
    if (counter >= this->samplerate * 5 && !trigger) { // once per second
      counter = 0;
      trigger = true; // trigger a print
    }
	
		// return longDelay->processSample(in);
		//return detuneeee->processSample(in);
		return r->processSample(in)*(0.25) + in*(1-0.25);
		//return t->processSample(in);
	}

	void loop() override {
    if (trigger) { // print set by trigger
      	// for (int j = 0; j < 5; j++) {
		// 	hardware.PrintLine("1: %d \n", pInt[j]);
		// 	hardware.PrintLine("2: %d \n", pInt2[j]);
		// 	//hardware.PrintLine("hello\n");
		// }
      trigger = false;
    }
    System::Delay(500); // Don't spam the serial!
  }

};

int main() {
  GimmelTests mGimmelTests;
  mGimmelTests.start();
  return 0;
}