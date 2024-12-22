#include "../../Jaffx.hpp"

namespace nam {
	// Overwrite stdlib calls in nam space
	void* malloc(size_t size) { return m.malloc(size); }
	void* calloc(size_t nelemb, size_t size) { return m.calloc(nelemb, size); }
	void* realloc(void* ptr, size_t size) { return m.realloc(ptr, size); }
	void free(void* ptr) { m.free(ptr); }
}

#define NAM_SAMPLE_FLOAT
#include "../../microNam/NAM/NAM.h"
#include "../../microNam/example_models/microMarshall.h" // microMarshall 

struct NamTest : Jaffx::Program {
  std::unique_ptr<nam::DSP> mModel; 

  void init() override {
    this->debug = true;
    mModel = nam::get_dsp(microMarshall); // currently breaking
  }

  float processAudio(float in) override {
    // this->mModel->process(&in, &in, 1); // how much cpu will this take?
    return in;
  }
  
};

int main() {
  NamTest mNamTest;
  mNamTest.start();
  return 0;
}