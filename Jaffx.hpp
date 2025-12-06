#include "libDaisy/src/daisy_seed.h"
#include "include/SDRAM.hpp"
#include "arm_math.h"
using namespace daisy;

namespace giml {

	// Overwrite stdlib memory syscalls in giml space
	void* malloc(size_t size) { return Jaffx::mSDRAM.malloc(size); }
	void* calloc(size_t nelemb, size_t size) { return Jaffx::mSDRAM.calloc(nelemb, size); }
	void* realloc(void* ptr, size_t size) { return Jaffx::mSDRAM.realloc(ptr, size); }
	void free(void* ptr) { Jaffx::mSDRAM.free(ptr); }

	// Overwrite trig calls with optimized ARM versions
	inline float sin(float x) { 
		return arm_sin_f32(x); 
	}

	// // 
	inline float cos(float x) { 
		return arm_cos_f32(x); 
	}

}

namespace Jaffx {
class Firmware {
public:
	// declare an instance of the hardware
	static DaisySeed hardware;
	static Firmware* instance; // Static pointer to the current instance of Program

	// It's handy to have these numbers on tap
	const int samplerate = 48000;
	const int buffersize = 256;

	// loadMeter for debugging, bool for toggle
	CpuLoadMeter loadMeter;
	bool debug = false;

public:
	// overridable init function
	inline virtual void init() {}

	// debug init function
	inline void initDebug() {
		if (debug) {
			hardware.StartLog(true);
			// loadMeter.Init(hardware.AudioSampleRate(), hardware.AudioBlockSize());
		}
	}

	// overridable per-sample operation
	inline virtual float processAudio(float in) { return in; }

	// TODO: Better way of doing this
	// basic mono->dual-mono callback
	inline virtual void CustomAudioBlockCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
		if (instance->debug) { instance->loadMeter.OnBlockStart(); }
		instance->blockStart();
		for (size_t i = 0; i < size; i++) {
			out[0][i] = instance->processAudio(in[0][i]); // format is in/out[channel][sample]
			out[1][i] = out[0][i];
		}
		instance->blockEnd();
		if (instance->debug) { instance->loadMeter.OnBlockEnd(); }
	}

	// See: https://forum.electro-smith.com/t/what-is-the-difference-between-the-two-types-of-audio-callback/454/2
	inline virtual void CustomInterleavedAudioBlockCallback(AudioHandle::InterleavingInputBuffer in, AudioHandle::InterleavingOutputBuffer out, size_t size) {
		// Format for this is { L0, R0, L1, R1, . . . LN, RN }
		if (instance->debug) { instance->loadMeter.OnBlockStart(); }
		instance->blockStart();
		for (size_t i = 0; i < size; i++) {
			out[i] = in[i];
		}
		instance->blockEnd();
		if (instance->debug) { instance->loadMeter.OnBlockEnd(); }
	}

	// overridable audio block start/end operation
	inline virtual void blockStart() {}
	inline virtual void blockEnd() {}

	// overridable loop operation
	inline virtual void loop() {}

	// debug loop
	inline void debugLoop() {
		if (debug) {
			// as seen in https://electro-smith.github.io/libDaisy/md_doc_2md_2__a3___getting-_started-_audio.html
			const float avgLoad = loadMeter.GetAvgCpuLoad();
			const float maxLoad = loadMeter.GetMaxCpuLoad();
			const float minLoad = loadMeter.GetMinCpuLoad();
			// print it to the serial connection (as percentages)
			hardware.PrintLine("Processing Load:");
			hardware.PrintLine("Max: " FLT_FMT3 "%%", FLT_VAR3(maxLoad * 100.0f));
			hardware.PrintLine("Avg: " FLT_FMT3 "%%", FLT_VAR3(avgLoad * 100.0f));
			hardware.PrintLine("Min: " FLT_FMT3 "%%", FLT_VAR3(minLoad * 100.0f));
			System::Delay(1000); // Don't spam the serial!
		}
	}

	// TODO: Better way of doing this
	static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
		instance->CustomAudioBlockCallback(in, out, size);
	}

	static void InterleavingAudioCallback(AudioHandle::InterleavingInputBuffer in, AudioHandle::InterleavingOutputBuffer out, size_t size) {
		instance->CustomInterleavedAudioBlockCallback(in, out, size);
	}

	void start() {
		// initialize hardware
		hardware.Init();
		hardware.SetAudioBlockSize(buffersize); // number of samples handled per callback (buffer size)
		hardware.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ); // sample rate

		mSDRAM.init(); // Needs to be called AFTER hardware init, and not in the object's constructor

		// init instance and start callback
		instance = this;
		this->init();
		this->initDebug();
		
		hardware.StartAudio(AudioCallback);

		// loop indefinitely
		while (true) { 
			this->loop(); 
			// this->debugLoop(); Commenting out for SlipRecorder, I need hardware prints but not CPU Load
		}
	}
	
};

// Global instancing of static members
DaisySeed Firmware::hardware; 
Firmware* Firmware::instance = nullptr;

} // namespace Jaffx