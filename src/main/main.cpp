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
        if (switches[i].FallingEdge()) { // Toggle switches
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
class Main : public Jaffx::Firmware {
  PersistentStorage<Settings> mPersistentStorage{hardware.qspi}; // PersistentStorage for settings
	Settings mSettings; // local settings 
  InterfaceManager mInterfaceManager; 

  // effects 
  std::unique_ptr<giml::Phaser<float>> mPhaser;
  giml::AmpModeler<float, Layer1, Layer2> mAmpModeler{};
  std::unique_ptr<giml::Expander<float>> mExpander;
  std::unique_ptr<giml::Chorus<float>> mChorus;
  std::unique_ptr<giml::Delay<float>> mDelay;
  std::unique_ptr<giml::Compressor<float>> mCompressor;
  giml::EffectsLine<float> mFxChain{6};

  void init() override {
    hardware.StartLog();
    // this->debug = true;
    mPersistentStorage.Init(mSettings);
    mInterfaceManager.init(mSettings, mPersistentStorage);

    // ~15% CPU load
    mPhaser = std::make_unique<giml::Phaser<float>>(this->samplerate);
    mPhaser->setParams();
    mPhaser->enable();
    mFxChain.pushBack(mPhaser.get());

    // ~71% CPU load`
    mAmpModeler.loadModels();
    mFxChain.pushBack(&mAmpModeler);

    mExpander = std::make_unique<giml::Expander<float>>(this->samplerate);
    mExpander->setParams(-50.f, 4.f, 5.f);
    mExpander->enable();
    mExpander->toggleSideChain(true);
    mFxChain.pushBack(mExpander.get());

    // ~3% CPU load
    mChorus = std::make_unique<giml::Chorus<float>>(this->samplerate);
    mChorus->setParams(0.2, 10.f);
    mChorus->enable();
    mFxChain.pushBack(mChorus.get());

    // ~2% CPU load  
    mDelay = std::make_unique<giml::Delay<float>>(this->samplerate);
    mDelay->setParams(398.f, 0.3f, 0.7f, 0.24f);
    mDelay->enable();
    mFxChain.pushBack(mDelay.get());

    // ~3% CPU load
    mCompressor = std::make_unique<giml::Compressor<float>>(this->samplerate);
    mCompressor->setParams(-20.f, 4.f, 10.f, 5.f, 3.5f, 100.f);
    mCompressor->enable();
    mFxChain.pushBack(mCompressor.get());

    // Crashes the system
    // mReverb = std::make_unique<giml::Reverb<float>>(this->samplerate);
		// mReverb->setParams(0.02f, 0.5f, 0.5f, 0.24f, 5.f, 0.9f); // matches AlloFx
    // mReverb->enable();
    // mFxChain.pushBack(mReverb.get());
  }

  /**
   * @todo callbacks for params/setters
   */
  void blockStart() override {
    Firmware::blockStart(); // for debug mode
    mInterfaceManager.processInput();

    // toggle fx
    // unsigned int numToggles = std::min(int(mFxChain.size()), mInterfaceManager.numEffects);
    // for (unsigned int i = 0; i < numToggles; i++) {
    //   mFxChain[i]->toggle(mSettings.toggles[i]);
    // }
    mFxChain[0]->toggle(mSettings.toggles[0]);
    mFxChain[1]->toggle(mSettings.toggles[1]);
    // skip expander
    mFxChain[3]->toggle(mSettings.toggles[2]);
    mFxChain[4]->toggle(mSettings.toggles[3]);
    // skip compressor

    if (mSettings.toggles[1]) { // if amp modeler is enabled
      mCompressor->disable(); // disable compressor
      mExpander->toggle(mSettings.toggles[4]);
    } else {
      mExpander->disable();
      mCompressor->toggle(mSettings.toggles[4]);
    }

    // prototyping setters. TODO: Interrupt Callbacks (for efficiency)
    // auto& s = mSettings;
    if (mInterfaceManager.editMode) {
      // mDetune->setParams(s.params[0][0] * 0.1 + 0.9, giml::clip<float>(10.f + s.params[0][1] * 40.f, 10.f, 50.f), s.params[0][2]);
      // mPhaser->setParams(giml::clip<float>(s.params[1][0] * 20.f, 0.f, 20.f), giml::clip<float>(s.params[1][1] * 2 - 1, -1, 1));
      // mDelay->setParams(1000.0 * s.params[2][0], s.params[2][1], 0.7f, s.params[2][3]);
      // mCompressor->setParams(-s.params[3][0] * 60, s.params[3][1] * 10.f, s.params[3][2] * 20, 5.f, 3.5f, 100.f); // TODO: defaults in giml
      // mReverb->setParams(s.params[4][0] * 0.1, s.params[4][1] * 0.3, 0.5f, s.params[4][2], 50.f, 0.9f);
    }
  }

  float processAudio(float in) override {
    mExpander->feedSideChain(in);
    return mFxChain.processSample(in);
  }
  
  void loop() override {
    mInterfaceManager.processOutput();
    System::Delay(50); // what's a good value?
  }

};

int main() {
  Main mMain;
  mMain.start();
  return 0;
}


