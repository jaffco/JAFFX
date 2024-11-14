#include "daisy_seed.h"
using namespace daisy;

DaisySeed hw;

//Setting Struct containing parameters we want to save to flash
struct Settings {
	bool ledState;

	//Overloading the != operator
	//This is necessary as this operator is used in the PersistentStorage source code
	bool operator!=(const Settings& a) const {
    return !(a.ledState==ledState);
  }
};

//Persistent Storage Declaration. Using type Settings and passed the devices qspi handle
PersistentStorage<Settings> SavedSettings(hw.qspi);
bool ledState = false;

void Load() {
	//Reference to local copy of settings stored in flash
	Settings &LocalSettings = SavedSettings.GetSettings();
	ledState = LocalSettings.ledState;
}

void Save() {
	//Reference to local copy of settings stored in flash
	Settings &LocalSettings = SavedSettings.GetSettings();
	LocalSettings.ledState = !LocalSettings.ledState;
}

void ProcessControls() {
	hw.SetLed(ledState);
}

int main(void) {
	hw.Init();
	
	//Initilize the PersistentStorage Object with default values.
	//Defaults will be the first values stored in flash when the device is first turned on. They can also be restored at a later date using the RestoreDefaults method
	Settings DefaultSettings = {false};
	SavedSettings.Init(DefaultSettings);

	Load();
	Save();
	SavedSettings.Save();
	hw.SetLed(ledState);

	while(1) {}
}
