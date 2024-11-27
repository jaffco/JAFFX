#include "../../Jaffx.hpp"
#include "../../Gimmel/include/gimmel.hpp"
#include <memory>

struct GimmelTests : Jaffx::Program {
	std::unique_ptr<giml::Tremolo<float>> t;
	//giml::Tremolo<float> t{ this->samplerate };
	std::unique_ptr<giml::Delay<float>> longDelay;
	// giml::Reverb<float> r{this->samplerate, 4, 25, 4};
	//giml::Delay<float> longDelay{ this->samplerate };
	unsigned int counter = 0;
  	bool trigger = false;
	// int* pInt, *pInt2;
	
	
	void init() override {
		hardware.StartLog();
		t = std::make_unique<giml::Tremolo<float>>(this->samplerate);
		t->setSpeed(150);
		t->setDepth(0.75f);
		t->enable();

		// r.setParams(0.02, 0.25, 0.5, 10, 0.5);
		// r.enable();

		longDelay = std::make_unique<giml::Delay<float>>(this->samplerate);
		longDelay->setDelayTime(350);
		longDelay->setFeedback(0.2);
		longDelay->setBlend(0.35);
		longDelay->setDamping(0.7);
		longDelay->enable();
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
	
		return longDelay->processSample(in);
		// return r.processSample(in);
		// return t->processSample(in);
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


