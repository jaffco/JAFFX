#include "../../Jaffx.hpp"

struct Template : Jaffx::Program {

  void init() override {}

  float processAudio(float in) override {return in;}
  
};

int main() {
  Template mTemplate;
  mTemplate.start();
  return 0;
}


