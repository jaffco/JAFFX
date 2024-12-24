#include "../../Jaffx.hpp"
#include "../../Gimmel/include/gimmel.hpp"
#include <memory>

struct GimmelTests : Jaffx::Program {
	std::unique_ptr<giml::Tremolo<float>> t;
	//giml::Tremolo<float> t{ this->samplerate };
	std::unique_ptr<giml::Delay<float>> longDelay;
	// giml::Reverb<float> r{this->samplerate, 4, 25, 4};
	std::unique_ptr<giml::Detune<float>> detuneeee;
	std::unique_ptr<giml::Reverb<float>> r;
	unsigned int counter = 0;
  	bool trigger = false;
	// int* pInt, *pInt2;
	
	
	void init() override {
		hardware.StartLog();
		t = std::make_unique<giml::Tremolo<float>>(this->samplerate);
		t->setSpeed(150);
		t->setDepth(0.75f);
		t->enable();

		r = std::make_unique<giml::Reverb<float>>(this->samplerate);
		r->setParams(0.02, 0.75, 0.5, 1000, 0.25, giml::Reverb<float>::RoomType::CUBE);
		r->enable();

		// detuneeee = std::make_unique<giml::Detune<float>>(this->samplerate);
		// detuneeee->setPitchRatio(2);
		// detuneeee->setWindowSize(25);
		// detuneeee->enable();

		// longDelay = std::make_unique<giml::Delay<float>>(this->samplerate);
		// longDelay->setDelayTime(350);
		// longDelay->setFeedback(0.2);
		// longDelay->setBlend(0.35);
		// longDelay->setDamping(0.7);
		// longDelay->enable();
		// pInt = (int*)m.malloc(sizeof(int) * 5);
		// for (int i = 0; i < 5; i++) {
		// 	pInt[i] = 2 * i;
		// }
		// m.free(pInt);
		// pInt2 = (int*)m.calloc(5, sizeof(int));
		// for (int i = 0; i < 5; i++) {
		// 	pInt2[i] = 5 * i;
		// }
	}

	float processAudio(float in) override {
		counter++;
    if (counter >= this->samplerate * 5 && !trigger) { // once per second
      counter = 0;
      trigger = true; // trigger a print
    }
	
		// return longDelay->processSample(in);
		//return detuneeee->processSample(in);
		return r->processSample(in)*(0.75) + in*(0.25);
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
  GimmelTests g;
  g.start();
  return 0;
}


