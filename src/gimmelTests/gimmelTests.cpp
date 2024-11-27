#include "../../Jaffx.hpp"
#include "../../Gimmel/include/gimmel.hpp"

struct GimmelTests : Jaffx::Program {
	giml::Tremolo<float> t{this->samplerate};
	unsigned int counter = 0;
  bool trigger = false;
	int* pInt, *pInt2;
	
	
	void init() override {
		hardware.StartLog();
		t.setSpeed(150);
		t.setDepth(0.75f);
		t.enable();
		pInt = (int*)m.malloc(sizeof(int) * 5);
		for (int i = 0; i < 5; i++) {
			pInt[i] = 2 * i;
		}
		m.free(pInt);
		pInt2 = (int*)m.calloc(5, sizeof(int));
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
	
		return t.processSample(in);
	}

	void loop() override {
    if (trigger) { // print set by trigger
      	for (int j = 0; j < 5; j++) {
			hardware.PrintLine("1: %d \n", pInt[j]);
			hardware.PrintLine("2: %d \n", pInt2[j]);
			//hardware.PrintLine("hello\n");
		}
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


