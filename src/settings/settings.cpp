#include "../../Jaffx.hpp"

// Program that implements Persistent Storage, 
// allowing for the saving of presets between boot cycles

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
  bool trigger = false;
	PersistentStorage<Settings> savedSettings{this->hardware.qspi}; // PersistentStorage for Settings
	Settings localSettings = {false}; // local settings, constructed with a default
	
	// Get stored settings and write to local
	void loadSettings() {
		localSettings = savedSettings.GetSettings();
	}

	// Get local settings and write to storage
	void saveSettings() {
		this->savedSettings.GetSettings() = localSettings; // update with local
		this->savedSettings.Save(); // save to persistent memory 
	}

	void init() override {
		this->savedSettings.Init(localSettings); // init with default settings
		this->loadSettings(); // loadSettings from flash
		this->localSettings.ledState = !localSettings.ledState; // flip ledState
		this->saveSettings(); // saveSettings to flash
		this->trigger = true; // trigger led update
	}

	void loop() override {
		if (trigger) { // triggered once per boot cycle
			this->hardware.SetLed(localSettings.ledState);
			this->trigger = false;
		}
	}

};

int main(void) {
	SettingsTest mSettingsTest;
	mSettingsTest.start();
}