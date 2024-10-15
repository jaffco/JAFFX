#include "../../Jaffx.hpp"

struct Test : Jaffx::Program {
  float processAudio(float in) override {
    return 0.1;
  }
};

int main(void) {
  Test test;
  test.start();
}
