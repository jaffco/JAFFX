#include "../../Jaffx.hpp"
#include "../../Gimmel/include/gimmel.hpp"
#include <memory> // for unique_ptr && make_unique

#include "../namTest/DumbleModel.h"
#include "../namTest/MarshallModel.h"

// Add NAM compatibility to giml
namespace giml {
  template <typename T, typename Layer1, typename Layer2>
  class AmpModeler : public Effect<T> {
  private:
    wavenet::RTWavenet<1, 1, Layer1, Layer2> clean, dirty;
    DumbleModelWeights cleanWeights;
    MarshallModelWeights dirtyWeights;

  public:
    void loadModels() {
      this->clean.loadModel(this->cleanWeights.weights);
      this->dirty.loadModel(this->dirtyWeights.weights);
    }
    
    T processSample(const T& input) override {
      if (!this->enabled) { return this->clean.model.forward(input); }
      return this->dirty.model.forward(input);
    }
  };
}

//=====================================================================
// ADD USER DEFINITIONS HERE
// class UserDefinedEffect : public Effect<float> {};
// ... 
//=====================================================================

/**
 * @brief Settings struct for writing and recalling settings
 * with persistent memory
 * 
 * @todo implement "version" member to call defaults when 
 * struct has changed
 */
struct Settings {
  bool toggles[5] = { false, false, false, false, false }; // switches
  float params[5][3]; // params

  // Overloading the != operator
  // This is necessary as this operator is used in the PersistentStorage source code
  bool operator!=(const Settings& a) const {
    for (int i = 0; i < 5; i++) {
      if (toggles[i] != a.toggles[i]) { return true; } // check toggles
      for (int j = 0; j < 3; j++) { // check params
        if (params[i][j] != a.params[i][j]) { return true; } 
      }
    }
    return false; // No mismatches found
  }

};

/**
 * @brief struct for managing the UI
 * @todo convert from struct to class
 */
struct InterfaceManager {
  bool editMode = false;
  int select = 0;
  static const int numEffects = 5;
  static const int numParams = 3;
  GPIO leds[numEffects];
  Switch switches[numEffects];
  Encoder encoders[numParams + 1];
  Settings* localSettings = nullptr;
  PersistentStorage<Settings>* savedSettings = nullptr;

  /**
   * @brief init function based on specific hardware setup
   * 
   * Hardware setup 2025-01-17:
   * 
   * switches 0-4 on pins D24-28
   * 
   * leds 0-4 on pins D14-10
   * 
   * encoder 0 on pins D15, D16, D17
   * 
   * encoders 1-3 on pins D18-22, D19-23, D17
   */
  void init(Settings& local, PersistentStorage<Settings>& saved) {
    // init settings
    localSettings = &local;
    savedSettings = &saved;
    loadSettings();

    // init ctrls
    switches[0].Init(seed::D18, 0.f, Switch::Type::TYPE_MOMENTARY, Switch::Polarity::POLARITY_NORMAL);
    switches[1].Init(seed::D16, 0.f, Switch::Type::TYPE_MOMENTARY, Switch::Polarity::POLARITY_NORMAL);
    switches[2].Init(seed::D2, 0.f, Switch::Type::TYPE_MOMENTARY, Switch::Polarity::POLARITY_NORMAL);
    switches[3].Init(seed::D5, 0.f, Switch::Type::TYPE_MOMENTARY, Switch::Polarity::POLARITY_NORMAL);
    switches[4].Init(seed::D3, 0.f, Switch::Type::TYPE_MOMENTARY, Switch::Polarity::POLARITY_NORMAL);
    leds[0].Init(seed::D19, GPIO::Mode::OUTPUT);
    leds[1].Init(seed::D17, GPIO::Mode::OUTPUT);
    leds[2].Init(seed::D1, GPIO::Mode::OUTPUT);
    leds[3].Init(seed::D6, GPIO::Mode::OUTPUT); 
    leds[4].Init(seed::D4, GPIO::Mode::OUTPUT);  
    encoders[0].Init(seed::D21, seed::D20, seed::D22);
    encoders[1].Init(seed::D23, seed::D24, seed::D22);
    encoders[2].Init(seed::D25, seed::D26, seed::D22);
    encoders[3].Init(seed::D27, seed::D28, seed::D22);
  }

  void processInput() {
    // debounce encoders and switches
    for (auto& e : encoders) { e.Debounce(); }
    for (auto& s : switches) { s.Debounce(); }

    // hold leftmost switch to enter bootloader
    if (switches[0].TimeHeldMs() > 500.f) { 
      System::ResetToBootloader(System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
    }
    
    // check encoder[0] for edit mode. 
    // `RisingEdge()` triggers at boot, `FallingEdge()` preferred
    if (encoders[0].FallingEdge()) {
      editMode = !editMode; // flip state
      if (!editMode) { saveSettings(); } // if exiting edit mode, save settings
    } 

    // if editmode, process selector and param knobs
    if (editMode) { 
      select = (select + numEffects + encoders[0].Increment()) % numEffects;
      auto& selection = localSettings->params[select];
      for (int i = 0; i < numParams; i++) {
        selection[i] += encoders[i + 1].Increment() * 0.05f;
        selection[i] = giml::clip<float>(selection[i], 0.f, 1.f); 
        // TODO: encapsulate clamping in a param class^
      }
    } else { // if !editMode
      for (int i = 0; i < numEffects; i++) {
        if (switches[i].RisingEdge()) { // Toggle switches... Pressed() or RisingEdge()?
          localSettings->toggles[i] = !localSettings->toggles[i];
        }
      }
    }
  }

  void processOutput() {
    for (int i = 0; i < numEffects; i++) { // for each led
      if (editMode) { // if editMode, light selected
        if (i == select && !leds[i].Read()) { leds[i].Write(true); } 
        else { leds[i].Write(false); }
      } else { leds[i].Write(localSettings->toggles[i]); } // if !editMode, show toggle state
    } 
  }

  // Get stored settings and write to local
	void loadSettings() { *localSettings = savedSettings->GetSettings(); }

  // Save local settings to persistent memory
  void saveSettings() {
    savedSettings->GetSettings() = *localSettings; 
    savedSettings->Save(); // save to persistent memory
  }
};

/**
 * @brief firmware for the Jaffx pedal
 * @todo implement param callbacks
 */
class MultiFxTemplate : public Jaffx::Firmware {
  PersistentStorage<Settings> mPersistentStorage{hardware.qspi}; // PersistentStorage for settings
	Settings mSettings; // local settings 
  InterfaceManager mInterfaceManager; 

  //=====================================================================
  // ADD GIMMEL EFFECTS HERE
  // std::unique_ptr<giml::Effect<float>> mEffect;
  // ... 
  //=====================================================================
  giml::EffectsLine<float> mFxChain;


  void init() override {
    mPersistentStorage.Init(mSettings);
    mInterfaceManager.init(mSettings, mPersistentStorage);

    //=====================================================================
    // CONSTRUCT EFFECTS HERE
    // mEffect = std::make_unique<giml::Effect<float>>(this->samplerate);
    // mEffect->setParams();
    // mFxChain.pushBack(mEffect.get());
    // ...
    //====================================================================
  }

  /**
   * @todo callbacks for params/setters
   */
  void blockStart() override {
    Firmware::blockStart(); // for debug mode
    mInterfaceManager.processInput();

    for (int i = 0; i < mFxChain.size(); i++) {
      mFxChain[i]->toggle(mSettings.toggles[i]);
    }

    // prototyping setters. TODO: Callbacks (for efficiency)
    auto& s = mSettings;
    if (mInterfaceManager.editMode) {
      //========================================================================
      // Set params here
      // mFxChain[0]->setParams(s.params[0][0], s.params[0][1], s.params[0][2]);
      // ... 
      //========================================================================
    }
  }

  float processAudio(float in) override {
    return mFxChain.processSample(in);
  }
  
  void loop() override {
    mInterfaceManager.processOutput();
    System::Delay(25); // what's a good value?
  }

};

int main() {
  MultiFxTemplate mMultiFxTemplate;
  mMultiFxTemplate.start();
  return 0;
}


