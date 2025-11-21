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
  bool toggleState = false;

  // Overloading the != operator
  // This is necessary as this operator is used in the PersistentStorage source code
  bool operator!=(const Settings& a) const {
    return !(a.toggleState == toggleState);
  }

};

/**
 * @brief struct for managing the UI
 * @todo convert from struct to class
 */
struct InterfaceManager {
  GPIO mLed;
  Switch mFootswitch;
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
    mFootswitch.Init(seed::A0, 0.f, Switch::Type::TYPE_MOMENTARY, Switch::Polarity::POLARITY_NORMAL);
    mLed.Init(seed::A1, GPIO::Mode::OUTPUT);
    mLed.Write(localSettings->toggleState);
  }

  void processInput() {
    mFootswitch.Debounce();
    if (mFootswitch.FallingEdge()) { // invert toggle state
      localSettings->toggleState = !localSettings->toggleState;
      // saveSettings();
    }
  }

  void processOutput() {
    // set led based on toggle state
    mLed.Write(localSettings->toggleState);
  }

  // Get stored settings and write to local
	void loadSettings() { *localSettings = savedSettings->GetSettings(); }

  // Save local settings to persistent memory
  void saveSettings() {
    savedSettings->GetSettings() = *localSettings; 
    savedSettings->Save(); // save to persistent memory
  }
};

class JuTron : public Jaffx::Firmware {
  giml::EnvelopeFilter<float> mEnvelopeFilter{samplerate};
  InterfaceManager mInterfaceManager;
  Settings mSettings;
  PersistentStorage<Settings> mPersistentStorage{hardware.qspi};

  void init() override {
    mInterfaceManager.init(mSettings, mPersistentStorage);
    mEnvelopeFilter.setParams();
    mEnvelopeFilter.toggle(mSettings.toggleState);
  }

  float processAudio(float in) override {
    return mEnvelopeFilter.processSample(in);
  }

  void loop() override {
    mInterfaceManager.processInput();
    mEnvelopeFilter.toggle(mSettings.toggleState);
    mInterfaceManager.processOutput();
    System::Delay(5);
  }
  
};

int main() {
  JuTron mJuTron;
  mJuTron.start();
  return 0;
}


