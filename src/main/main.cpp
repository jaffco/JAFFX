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
- `MenuManager` struct [x]
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
  bool toggles[5] = {false, false, false, false, false}; // switches
  float params[5][3]; // params

  // Overloading the != operator
  // This is necessary as this operator is used in the PersistentStorage source code
  bool operator!=(const Settings& a) const {
    for (int i = 0; i < 5; i++) {
      if (toggles[i] != a.toggles[i]) {return true;} // check toggles
      for (int j = 0; j < 3; j++) { // check params
        if (params[i][j] != a.params[i][j]) {return true;} 
      }
    }
    return false; // No mismatches found
  }

};

/**
 * @brief struct for managing the UI
 */
struct MenuManager {
  bool editMode = false;
  int select = 0;
  static const int numEffects = 5;
  static const int numParams = 3;
  GPIO leds[numEffects];
  Switch switches[numEffects];
  //Encoder encoders[numParams + 1];
  //Encoder encoders[1]; // test
  Encoder mEncoder; // test
  Settings* localSettings = nullptr;

  /**
   * @brief init function based on specific hardware setup
   * 
   * Hardware setup 2024-12-03:
   * 
   * switches on pins A6-A10
   * 
   * leds on pins D14-10
   * 
   * @todo init rest of ctrls
   */
  void init(Settings& initSettings) {
    localSettings = &initSettings;
    switches[0].Init(seed::A6);
    switches[1].Init(seed::A7);
    switches[2].Init(seed::A8);
    switches[3].Init(seed::A9);
    switches[4].Init(seed::A10);
    leds[0].Init(seed::D14, GPIO::Mode::OUTPUT);
    leds[1].Init(seed::D13, GPIO::Mode::OUTPUT);
    leds[2].Init(seed::D12, GPIO::Mode::OUTPUT);
    leds[3].Init(seed::D11, GPIO::Mode::OUTPUT); 
    leds[4].Init(seed::D10, GPIO::Mode::OUTPUT);  
    mEncoder.Init(seed::A0, seed::A1, seed::A2);
    //encoders[0].Init(seed::A0, seed::A1, seed::A2); // init encoder1
    //encoders[1].Init(seed::A3, seed::A4, seed::A5); // init encoder2
    // init switches
  }

  void processInput() {
    // debounce encoders and switches
    //for (auto& e : encoders) {e.Debounce();}
    mEncoder.Debounce();
    for (auto& s : switches) {s.Debounce();}
    
    // check encoder1 for edit mode. `RisingEdge()` triggers at boot, `FallingEdge()` preferred
    //if (encoders[0].RisingEdge()) {editMode = !editMode;} // flip state
    if (mEncoder.FallingEdge()) {editMode = !editMode;} // flip state

    // if editmode, process selector and param knobs
    if (editMode) { 
      select = (select + numEffects + mEncoder.Increment()) % numEffects;
      auto& selection = localSettings->params[select];
      // for (int j = 0; j < numParams; j++) {
      //   selection[j] += encoders[j + 1].Increment() * 0.01f;
      //   selection[j] = giml::clip<float>(selection[j], 0.f, 1.f); 
      //   // TODO: encapsulate clamping in a param class^
      // }
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
        if (i == select && !leds[i].Read()) {leds[i].Write(true);} 
        else {leds[i].Write(false);}
      } else {leds[i].Write(localSettings->toggles[i]);} 
    } // if !editMode, show toggle state^
  }

};

/**
 * @brief firmware for the Jaffx pedal
 * 
 * @todo encapsulate `PersistentStorage` functions
 * @todo implement UI
 * @todo implement DSP
 */
struct Main : Jaffx::Program {
  PersistentStorage<Settings> savedSettings{hardware.qspi}; // PersistentStorage for Settings
	Settings mSettings; // local settings 
  MenuManager mMenuManager; 
  std::unique_ptr<giml::Detune<float>> mDetune;
  std::unique_ptr<giml::Delay<float>> mDelay;
  std::unique_ptr<giml::Compressor<float>> mCompressor;
  std::unique_ptr<giml::Reverb<float>> mReverb;
  

	// Get stored settings and write to local
	void loadSettings() {mSettings = savedSettings.GetSettings();}

	// Get local settings and write to storage
	void saveSettings() {
		savedSettings.GetSettings() = mSettings; 
		savedSettings.Save(); // save to persistent memory 
	}

  void init() override {
    hardware.StartLog();
    savedSettings.Init(mSettings);
    loadSettings();
    mMenuManager.init(mSettings);

    mDetune = std::make_unique<giml::Detune<float>>(this->samplerate);
		mDetune->setPitchRatio(0.995);

    mDelay = std::make_unique<giml::Delay<float>>(this->samplerate);
		mDelay->setDelayTime(398.f);
    mDelay->setDamping(0.7f);
    mDelay->setFeedback(0.2f);
    mDelay->setBlend(1.f);

    mCompressor = std::make_unique<giml::Compressor<float>>(this->samplerate);
		mCompressor->setAttack(3.5f);
    mCompressor->setKnee(5.f);
    mCompressor->setMakeupGain(10.f);
    mCompressor->setRatio(4.f);
    mCompressor->setRelease(100.f);
    mCompressor->setThresh(-20.f);

    mReverb = std::make_unique<giml::Reverb<float>>(this->samplerate);
		mReverb->setParams(0.03f, 0.2f, 0.5f, 50.f, 0.9f);
  }

  /**
   * @todo check for change in `Settings` from MenuManager,
   * and call `saveSettings()` if changed
   */
  void blockStart() override {
    Program::blockStart(); // for debug mode
    mMenuManager.processInput();
    mDetune->toggle(mSettings.toggles[0]);
    mDelay->toggle(mSettings.toggles[1]);
    mCompressor->toggle(mSettings.toggles[2]);
    mReverb->toggle(mSettings.toggles[3]);
  }

  bool trigger = false;
  int counter = 0;
  /**
   * @todo implement toggles
   */
  float processAudio(float in) override {
    counter++;
    if (counter >= samplerate && !trigger) {
      counter = 0;
      trigger = true;
    }

    float y_x = giml::powMix(in, mDelay->processSample(mDetune->processSample(in)));
    y_x = mCompressor->processSample(y_x);
    y_x = giml::powMix(y_x, mReverb->processSample(y_x));
    return y_x;
  }
  
  /**
   * @todo move save logic to when `editMode` is exited
   */
  void loop() override {
    System::Delay(10);
    mMenuManager.processOutput();
    if (trigger) {
      saveSettings();
      hardware.PrintLine("Saved!");
      trigger = false;
    }
  }

};

int main() {
  Main mMain;
  mMain.start();
  return 0;
}


