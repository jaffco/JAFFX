#include "../../Jaffx.hpp"

struct Template : Jaffx::Program {

  void init() override {}

  float processAudio(float in) override {}
  
};

int main() {
  Template mTemplate;
  mTemplate.start();
  return 0;
}


