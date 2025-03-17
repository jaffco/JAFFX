#include "../../Jaffx.hpp"

class Template : public Jaffx::Firmware {

  void init() override {}

  float processAudio(float in) override {
    return in;
  }

  void loop() override {}
  
};

int main() {
  Template mTemplate;
  mTemplate.start();
  return 0;
}


