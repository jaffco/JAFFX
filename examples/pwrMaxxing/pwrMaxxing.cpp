#include "../../Jaffx.hpp"
#include "../../Gimmel/include/gimmel.hpp"
#include <memory>

#include "../../MicroNAM/MicroNAM.h"
#include "../namTest/models/FenderModel.h"
#include "../namTest/models/MarshallModel.h"

#include "dev/oled_ssd1312.h"

using DisplayType = daisy::OledDisplay<daisy::SSD13124WireSpi128x32Driver>;

template <int NumDisplays>
class DisplayManager {
private:
  DisplayType mDisplay;
  GPIO mGPIOs[NumDisplays];
  unsigned registeredDisplays = 0;

  void deselectAll() {
    for (unsigned i = 0; i < registeredDisplays; i++) {
      mGPIOs[i].Write(true);
    }
  }

  void selectDisplay(unsigned displayNumber) {
    if (displayNumber < registeredDisplays) {
      deselectAll();
      mGPIOs[displayNumber].Write(false);
    }
  }

public:
  void registerDisplay(Pin chipSelectPin) {
    if (registeredDisplays < NumDisplays) {
      mGPIOs[registeredDisplays].Init(chipSelectPin, GPIO::Mode::OUTPUT);
      mGPIOs[registeredDisplays].Write(true);
      registeredDisplays++;
    }
  }

  // Broadcast init by selecting all displays before calling Init.
  void initDisplay(DisplayType::Config config) {
    for (unsigned i = 0; i < registeredDisplays; i++) {
      mGPIOs[i].Write(false);
    }
    mDisplay.Init(config);
  }

  void updateAll() {
    for (unsigned i = 0; i < registeredDisplays; i++) {
      selectDisplay(i);
      mDisplay.Update();
    }
    deselectAll();
  }

  void flush(bool fill = false) {
    mDisplay.Fill(fill);
    updateAll();
  }
};

// Add NAM compatibility to giml
namespace giml {
  class AmpModeler : public Effect<float> {
  private:
    MicroNAM::NanoNet<1> mFenderNet, mMarshallNet;

  public:
    void loadModels() {
      static_assert(FenderModelWeightsCount == 842, "NamWavenet expects 842 weights");
      mFenderNet.load_weights(FenderModelWeights);
      static_assert(MarshallModelWeightsCount == 842, "NamWavenet expects 842 weights");
      mMarshallNet.load_weights(MarshallModelWeights);
    }
    
    float processSample(const float& input) override {
      float inputBuffer[1];
      inputBuffer[0] = input;
      float output[1];
      if (!this->enabled) {
        mFenderNet.forward(inputBuffer, output);
        return output[0];
      }
      mMarshallNet.forward(inputBuffer, output);
      return output[0];
    }
  };
}

struct Settings {
  bool toggles[4] = { false, false, false, false };
  float params[5][3];

  bool operator!=(const Settings& a) const {
    for (int i = 0; i < 4; i++) {
      if (toggles[i] != a.toggles[i]) { return true; }
      for (int j = 0; j < 3; j++) {
        if (params[i][j] != a.params[i][j]) { return true; }
      }
    }
    return false;
  }
};

struct InterfaceManager {
  bool editMode = false;
  int select = 0;
  static const int numEffects = 4;
  static const int numParams = 1;
  GPIO leds[numEffects];
  Switch switches[numEffects];
  Encoder encoders[numParams + 1];
  Settings* localSettings = nullptr;
  PersistentStorage<Settings>* savedSettings = nullptr;

  void init(Settings& local, PersistentStorage<Settings>& saved) {
    localSettings = &local;
    savedSettings = &saved;
    loadSettings();

    // Footswitches (D6, D5, D4, D3) - all have internal pullups for active-low
    switches[0].Init(seed::D6, 0.f, Switch::Type::TYPE_MOMENTARY, Switch::Polarity::POLARITY_INVERTED);
    switches[1].Init(seed::D5, 0.f, Switch::Type::TYPE_MOMENTARY, Switch::Polarity::POLARITY_INVERTED);
    switches[2].Init(seed::D4, 0.f, Switch::Type::TYPE_MOMENTARY, Switch::Polarity::POLARITY_INVERTED);
    switches[3].Init(seed::D3, 0.f, Switch::Type::TYPE_MOMENTARY, Switch::Polarity::POLARITY_INVERTED);
    
    // LED indicators (D28, D26, D25, D24)
    leds[0].Init(seed::D28, GPIO::Mode::OUTPUT);
    // leds[1].Init(seed::D26, GPIO::Mode::OUTPUT);
    // leds[2].Init(seed::D25, GPIO::Mode::OUTPUT);
    // leds[3].Init(seed::D24, GPIO::Mode::OUTPUT);
    
    // Encoder 0: Clicking encoder for edit mode toggle (A=D2, B=D1, Click=D7)
    encoders[0].Init(seed::D2, seed::D1, seed::D7);
    // Encoder 1: Normal encoder for parameter adjustment (A=D14, B=D16)
    encoders[1].Init(seed::D14, seed::D16, seed::D22);
  }

  void processInput() {
    for (auto& e : encoders) { e.Debounce(); }
    for (auto& s : switches) { s.Debounce(); }

    if (switches[0].TimeHeldMs() > 500.f) {
      // System::ResetToBootloader(System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
    }

    if (encoders[0].FallingEdge()) {
      editMode = !editMode;
      if (!editMode) { saveSettings(); }
    }

    if (editMode) {
      select = (select + numEffects + encoders[0].Increment()) % numEffects;
      auto& selection = localSettings->params[select];
      for (int i = 0; i < numParams; i++) {
        selection[i] += encoders[i + 1].Increment() * 0.05f;
        selection[i] = giml::clip<float>(selection[i], 0.f, 1.f);
      }
    } else {
      for (int i = 0; i < numEffects; i++) {
        if (switches[i].FallingEdge()) {
          localSettings->toggles[i] = !localSettings->toggles[i];
        }
      }
    }
  }

  void processOutput() {
    for (int i = 0; i < numEffects; i++) {
      if (editMode) {
        if (i == select && !leds[i].Read()) { leds[i].Write(true); }
        else { leds[i].Write(false); }
      } else {
        leds[i].Write(localSettings->toggles[i]);
      }
    }
  }

  void loadSettings() { *localSettings = savedSettings->GetSettings(); }

  void saveSettings() {
    savedSettings->GetSettings() = *localSettings;
    savedSettings->Save();
  }
};

class PwrMaxxing : public Jaffx::Firmware {
  PersistentStorage<Settings> mPersistentStorage{hardware.qspi};
  Settings mSettings;
  InterfaceManager mInterfaceManager;

  std::unique_ptr<giml::Phaser<float>> mPhaser;
  giml::AmpModeler mAmpModeler;
  std::unique_ptr<giml::Expander<float>> mExpander;
  std::unique_ptr<giml::Chorus<float>> mChorus;
  std::unique_ptr<giml::Delay<float>> mDelay;
  std::unique_ptr<giml::Compressor<float>> mCompressor;
  giml::EffectsLine<float> mFxChain{6};

  DisplayManager<4> mDisplays;

  void init() override {
    mPersistentStorage.Init(mSettings);
    mInterfaceManager.init(mSettings, mPersistentStorage);

    mPhaser = std::make_unique<giml::Phaser<float>>(this->samplerate);
    mPhaser->setParams();
    mPhaser->enable();
    mFxChain.pushBack(mPhaser.get());

    mAmpModeler.loadModels();
    mFxChain.pushBack(&mAmpModeler);

    mExpander = std::make_unique<giml::Expander<float>>(this->samplerate);
    mExpander->setParams(-50.f, 4.f, 5.f);
    mExpander->enable();
    mExpander->toggleSideChain(true);
    mFxChain.pushBack(mExpander.get());

    mChorus = std::make_unique<giml::Chorus<float>>(this->samplerate);
    mChorus->setParams(0.2, 10.f);
    mChorus->enable();
    mFxChain.pushBack(mChorus.get());

    mDelay = std::make_unique<giml::Delay<float>>(this->samplerate);
    mDelay->setParams(398.f, 0.3f, 0.7f, 0.24f);
    mDelay->enable();
    mFxChain.pushBack(mDelay.get());

    mCompressor = std::make_unique<giml::Compressor<float>>(this->samplerate);
    mCompressor->setParams(-20.f, 4.f, 10.f, 5.f, 3.5f, 100.f);
    mCompressor->enable();
    mFxChain.pushBack(mCompressor.get());

    mDisplays.registerDisplay(seed::D19);
    mDisplays.registerDisplay(seed::D20);
    mDisplays.registerDisplay(seed::D18);
    mDisplays.registerDisplay(seed::D17);

    DisplayType::Config disp_cfg;
    disp_cfg.driver_config.transport_config.pin_config.dc = seed::D9;
    disp_cfg.driver_config.transport_config.pin_config.reset = seed::D13;
    mDisplays.initDisplay(disp_cfg);
    mDisplays.flush(true);
  }

  void blockStart() override {
    Firmware::blockStart();
    mInterfaceManager.processInput();

    mFxChain[0]->toggle(mSettings.toggles[0]);
    mFxChain[1]->toggle(mSettings.toggles[1]);
    mFxChain[3]->toggle(mSettings.toggles[2]);
    mFxChain[4]->toggle(mSettings.toggles[3]);

    if (mSettings.toggles[1]) {
      mCompressor->disable();
      mExpander->toggle(mSettings.toggles[4]);
    } else {
      mExpander->disable();
      mCompressor->toggle(mSettings.toggles[4]);
    }
  }

  float processAudio(float in) override {
    return in;
    mExpander->feedSideChain(in);
    return mFxChain.processSample(in);
  }

  void loop() override {
    mInterfaceManager.processOutput();
    System::Delay(50);
  }
};

int main() {
  PwrMaxxing mPwrMaxxing;
  mPwrMaxxing.start();
  return 0;
}


