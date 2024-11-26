#include "../../Jaffx.hpp"

// CpuLoadMeter is essential for benchmarking our code
// Great utility when debugging and/or optimizing
// So useful that we baked it into the `Jaffx` header
struct LoadMeter : Jaffx::Program {

  void init() override {
    this->debug = true; // set debug flag
  }

};

int main() {
  LoadMeter mLoadMeter;
  mLoadMeter.start();
  return 0;
}


