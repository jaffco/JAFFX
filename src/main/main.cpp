#include "../../Jaffx.hpp"
#include "../../Gimmel/include/gimmel.hpp"
#include <memory> // for unique_ptr && make_unique

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
    switches[0].Init(seed::D24);
    switches[1].Init(seed::D25);
    switches[2].Init(seed::D26);
    switches[3].Init(seed::D27);
    switches[4].Init(seed::D28);
    leds[0].Init(seed::D14, GPIO::Mode::OUTPUT);
    leds[1].Init(seed::D13, GPIO::Mode::OUTPUT);
    leds[2].Init(seed::D12, GPIO::Mode::OUTPUT);
    leds[3].Init(seed::D11, GPIO::Mode::OUTPUT); 
    leds[4].Init(seed::D10, GPIO::Mode::OUTPUT);  
    encoders[0].Init(seed::D15, seed::D16, seed::D17);
    encoders[1].Init(seed::D18, seed::D19, seed::D17);
    encoders[2].Init(seed::D20, seed::D21, seed::D17);
    encoders[3].Init(seed::D22, seed::D23, seed::D17);
  }

  void processInput() {
    // debounce encoders and switches
    for (auto& e : encoders) { e.Debounce(); }
    for (auto& s : switches) { s.Debounce(); }
    
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
class Main : public Jaffx::Firmware {
  PersistentStorage<Settings> mPersistentStorage{hardware.qspi}; // PersistentStorage for settings
	Settings mSettings; // local settings 
  InterfaceManager mInterfaceManager; 

  // effects 
  std::unique_ptr<giml::Detune<float>> mDetune;
  std::unique_ptr<giml::Phaser<float>> mPhaser;
  std::unique_ptr<giml::Delay<float>> mDelay;
  std::unique_ptr<giml::Compressor<float>> mCompressor;
  std::unique_ptr<giml::Reverb<float>> mReverb;
  giml::EffectsLine<float> mFxChain;


  void init() override {
    hardware.StartLog();
    this->debug = true;
    mPersistentStorage.Init(mSettings);
    mInterfaceManager.init(mSettings, mPersistentStorage);

    mDetune = std::make_unique<giml::Detune<float>>(this->samplerate);
    mDetune->setParams(0.995f);
    mFxChain.pushBack(mDetune.get());

    mPhaser = std::make_unique<giml::Phaser<float>>(this->samplerate);
    mPhaser->setParams();
    mFxChain.pushBack(mPhaser.get());

    mDelay = std::make_unique<giml::Delay<float>>(this->samplerate);
    mDelay->setParams(398.f, 0.2f, 0.7f, 0.5f);
    mFxChain.pushBack(mDelay.get());

    mCompressor = std::make_unique<giml::Compressor<float>>(this->samplerate);
    mCompressor->setParams(-20.f, 4.f, 10.f, 5.f, 3.5f, 100.f);
    mFxChain.pushBack(mCompressor.get());

    mReverb = std::make_unique<giml::Reverb<float>>(this->samplerate);
		mReverb->setParams(0.03f, 0.3f, 0.5f, 0.5f, 50.f, 0.9f);
    mFxChain.pushBack(mReverb.get());
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
      mDetune->setParams(s.params[0][0] * 0.1 + 0.9, giml::clip<float>(10.f + s.params[0][1] * 40.f, 10.f, 50.f), s.params[0][2]);
      mPhaser->setParams(giml::clip<float>(s.params[1][0] * 20.f, 0.f, 20.f), giml::clip<float>(s.params[1][1] * 2 - 1, -1, 1));
      mDelay->setParams(1000.0 * s.params[2][0], s.params[2][1], 0.7f, s.params[2][3]);
      mCompressor->setParams(-s.params[3][0] * 60, s.params[3][1] * 10.f, s.params[3][2] * 20, 5.f, 3.5f, 100.f); // TODO: defaults in giml
      mReverb->setParams(s.params[4][0] * 0.1, s.params[4][1] * 0.3, 0.5f, s.params[4][2], 50.f, 0.9f);
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
  Main mMain;
  mMain.start();
  return 0;
}


