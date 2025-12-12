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
  float vol = 0.5f;
  float drive = 0.5f;
  float J = 0.5f;
  int mode = 0;

  // Overloading the != operator
  // This is necessary as this operator is used in the PersistentStorage source code
  bool operator!=(const Settings& a) const {
    return (toggleState != a.toggleState) ||
           (vol != a.vol) ||
           (drive != a.drive) ||
           (J != a.J) ||
           (mode != a.mode);
  }
};

/**
 * @brief struct for managing the UI
 * @todo convert from struct to class
 */
struct InterfaceManager {
  GPIO mLed;
  GPIO mLedLP;  // Lowpass mode LED on D25
  GPIO mLedBP;  // Bandpass mode LED on D26
  GPIO mLedHP;  // Highpass mode LED on D27
  Switch mFootswitch;
  AnalogControl mVolKnob;
  AnalogControl mDriveKnob;
  AnalogControl mJKnob;
  Encoder mModeEncoder;
  Settings* localSettings = nullptr;
  PersistentStorage<Settings>* savedSettings = nullptr;

  /**
   * @brief init function based on specific hardware setup
   */
  void init(DaisySeed& hw, Settings& local, PersistentStorage<Settings>& saved) {
    // init settings
    localSettings = &local;
    savedSettings = &saved;
    loadSettings();

    // Configure ADC channels for the three knobs
    AdcChannelConfig adcConfig[3];
    adcConfig[0].InitSingle(seed::A9);  // Vol knob
    adcConfig[1].InitSingle(seed::A11); // Drive knob
    adcConfig[2].InitSingle(seed::A5);  // J knob

    // Initialize ADC with the three channels
    hw.adc.Init(adcConfig, 3);
    hw.adc.Start();

    // init ctrls
    mLed.Init(seed::A0, GPIO::Mode::OUTPUT);
    mLedLP.Init(seed::D25, GPIO::Mode::OUTPUT);  // LP mode LED
    mLedBP.Init(seed::D26, GPIO::Mode::OUTPUT);  // BP mode LED
    mLedHP.Init(seed::D27, GPIO::Mode::OUTPUT);  // HP mode LED
    mFootswitch.Init(seed::A1, 0.f, Switch::Type::TYPE_TOGGLE, Switch::Polarity::POLARITY_NORMAL);
    mVolKnob.Init(hw.adc.GetPtr(0), hw.AudioSampleRate());   // Vol knob on ADC channel 0
    mDriveKnob.Init(hw.adc.GetPtr(1), hw.AudioSampleRate()); // Drive knob on ADC channel 1
    mJKnob.Init(hw.adc.GetPtr(2), hw.AudioSampleRate());     // J knob on ADC channel 2
    mModeEncoder.Init(seed::A3, seed::A4, seed::D28); // Encoder (button not connected, using unused pin)
    mLed.Write(localSettings->toggleState);
  }

  void processInput() {

    // Debounce discrete ctrls
    mFootswitch.Debounce();
    mModeEncoder.Debounce();

    if (mFootswitch.Pressed () && mFootswitch.TimeHeldMs () > 5000) { // 5 second hold in on position to reset to bootloader
      System:: ResetToBootloader (daisy:: System:: DAISY_INFINITE_TIMEOUT) ;
    }

    if (mFootswitch.FallingEdge()) { // invert toggle state
      localSettings->toggleState = !localSettings->toggleState;
    }

    int enc = mModeEncoder.Increment();
    if (enc != 0) {
      localSettings->mode -= enc;  // Inverted direction
      // Wrap around 0-2
      if (localSettings->mode > 2) { 
        localSettings->mode = 0; 
      } else if (localSettings->mode < 0) {
        localSettings->mode = 2;
      }
    }

    float vol = mVolKnob.Process();
    vol = giml::scale(vol, 0.f, 1.f, -12.f, 12.f);
    vol = giml::dBtoA(vol);
    localSettings->vol = vol;

    float drive = mDriveKnob.Process();
    drive = giml::scale(drive, 0.f, 1.f, 0.f, 30.f);
    drive = giml::dBtoA(drive);
    localSettings->drive = drive;

    localSettings->J = mJKnob.Process();

    // saveSettings();
  }

  void processOutput() {
    // set led based on toggle state
    mLed.Write(localSettings->toggleState);
    // set mode LEDs based on current mode (0=LP, 1=BP, 2=HP)
    mLedLP.Write(localSettings->mode == 0);
    mLedBP.Write(localSettings->mode == 1);
    mLedHP.Write(localSettings->mode == 2);

    // mLedLP.Write(true);
    // mLedBP.Write(true);
    // mLedHP.Write(true);
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

  void updateCtrls() {
    mEnvelopeFilter.setQ(giml::scale(mSettings.J, 0.f, 1.f, 0.f, 20.f));
    mEnvelopeFilter.toggle(mSettings.toggleState);
    mEnvelopeFilter.setFilterType(mSettings.mode);
  }

  void init() override {
    mInterfaceManager.init(hardware, mSettings, mPersistentStorage);
    updateCtrls();
  }

  float processAudio(float in) override {
    
    if (!mSettings.toggleState) {
      return in; // bypass if toggle is off
    }

    return mEnvelopeFilter.processSample(mSettings.drive * in) * mSettings.vol;
  }

  void loop() override {
    mInterfaceManager.processInput();
    updateCtrls();
    mInterfaceManager.processOutput();
    System::Delay(1);
  }
  
};

int main() {
  JuTron mJuTron;
  mJuTron.start();
  return 0;
}


