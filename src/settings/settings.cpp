#include "../../Jaffx.hpp"

// inspired by:
// https://forum.electro-smith.com/t/saving-values-to-flash-memory-using-persistentstorage-class-on-daisy-pod/4306

// Settings Struct containing parameters we want to save to flash
struct Settings {
	bool ledState;

	// Overloading the != operator
	// This is necessary as this operator is used in the PersistentStorage source code
	bool operator!=(const Settings& a) const {return !(a.ledState==ledState);}
};

struct SettingsTest : Jaffx::Program {
  bool ledState = false;
  bool triggered = false;
	PersistentStorage<Settings> SavedSettings{this->hardware.qspi};
	Settings DefaultSettings = {false};
  
	void loadSettings() {
		// Reference to local copy of settings stored in flash
		Settings &LocalSettings = this->SavedSettings.GetSettings();
		this->ledState = LocalSettings.ledState; // update local var
	}

	void saveSettings() {
		// Reference to local copy of settings stored in flash
		Settings &LocalSettings = this->SavedSettings.GetSettings();
		LocalSettings.ledState = !LocalSettings.ledState; // update saved var
		this->SavedSettings.Save(); // save to flash
	}

	void init() override {
		this->SavedSettings.Init(DefaultSettings); // init with default settings
		this->loadSettings(); // loadSettings from flash
		this->saveSettings(); // saveSettings to flash
		this->triggered = true; // trigger led update
	}

	void loop() override {
		if (triggered) { // trigger once per boot cycle
			this->hardware.SetLed(this->ledState);
			this->triggered = false;
		}
	}
};

int main(void) {
	SettingsTest mSettingsTest;
	mSettingsTest.start();
}
