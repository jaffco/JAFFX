#include "../../Jaffx.hpp"
#include "../../Gimmel/include/gimmel.hpp"
#include <memory>

/*

What does main need?

- run from SRAM 
- save to & recall from QSPI
- do DSP
- manage UI

What does it need to do these things?

- Modified Makefile [x]
- `Settings` struct [x]
- `InterfaceManager` struct [x]
- parameterizable giml effects []

*/

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
   * Hardware setup 2024-12-08:
   * 
   * switches 0-5 on pins D24-28
   * 
   * leds 0-5 on pins D14-10
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
 * @todo implement params
 * @todo containerize fx
 * @todo improve DSP
 */
struct Main : Jaffx::Program {
  PersistentStorage<Settings> mPersistentStorage{hardware.qspi}; // PersistentStorage for settings
	Settings mSettings; // local settings 
  InterfaceManager mInterfaceManager; 

  // effects 
  std::unique_ptr<giml::Detune<float>> mDetune;
  std::unique_ptr<giml::Delay<float>> mDelay;
  std::unique_ptr<giml::Compressor<float>> mCompressor;
  std::unique_ptr<giml::Reverb<float>> mReverb;
  std::unique_ptr<giml::Effect<float>> mFxChain[4];


  void init() override {
    hardware.StartLog();
    mPersistentStorage.Init(mSettings);
    mInterfaceManager.init(mSettings, mPersistentStorage);

    mDetune = std::make_unique<giml::Detune<float>>(this->samplerate);
		mDetune->setPitchRatio(0.995);
    mFxChain[0] = mDetune;

    mDelay = std::make_unique<giml::Delay<float>>(this->samplerate);
		mDelay->setDelayTime(398.f);
    mDelay->setDamping(0.7f);
    mDelay->setFeedback(0.2f);
    mDelay->setBlend(1.f);
    mFxChain[1] = mDelay;

    mCompressor = std::make_unique<giml::Compressor<float>>(this->samplerate);
		mCompressor->setAttack(3.5f);
    mCompressor->setKnee(5.f);
    mCompressor->setMakeupGain(10.f);
    mCompressor->setRatio(4.f);
    mCompressor->setRelease(100.f);
    mCompressor->setThresh(-20.f);
    mFxChain[2] = mCompressor;

    mReverb = std::make_unique<giml::Reverb<float>>(this->samplerate);
		mReverb->setParams(0.03f, 0.2f, 0.5f, 0.5f, 50.f, 0.9f);
    mFxChain[3] = mReverb;
  }

  /**
   * @todo `for` loop for toggles (depends on containerization for effects)
   * @todo callbacks for setters
   */
  void blockStart() override {
    Program::blockStart(); // for debug mode
    mInterfaceManager.processInput();
    mDetune->toggle(mSettings.toggles[0]);
    mDelay->toggle(mSettings.toggles[1]);
    mCompressor->toggle(mSettings.toggles[2]);
    mReverb->toggle(mSettings.toggles[3]);

    // prototyping setters
    auto& s = mSettings;
    if (mInterfaceManager.editMode) {
      mDetune->setParams(s.params[0][0] * 0.1 + 0.9);
      mDelay->setParams(1000.0 * s.params[1][0], s.params[1][1], 0.7f, s.params[1][2]);
      mReverb->setParams(0.02f, 0.2f, s.params[3][1], s.params[3][0], s.params[3][2] * 50.f);
    }
  }

  /**
   * @todo implement params
   * @todo containerize effects
   */
  float processAudio(float in) override {
    float out = in;
    //for (auto& e : mFxChain) { out = e->processSample(out); }
    out = mDelay->processSample(mDetune->processSample(out));
    out = mCompressor->processSample(out);
    out = mReverb->processSample(out);
    if (!mSettings.toggles[4]) {
      return out;
    } else {
      return in;
    }
  }
  
  void loop() override {
    System::Delay(25); // what's a good value?
    mInterfaceManager.processOutput();
  }

};

int main() {
  Main mMain;
  mMain.start();
  return 0;
}


