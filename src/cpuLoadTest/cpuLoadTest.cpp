// From my main man Takumi himself
// https://forum.electro-smith.com/t/how-many-audio-i-o-channels-can-daisy-handle/3937/5

#include "daisy_seed.h"
using namespace daisy;
DaisySeed hw;
CpuLoadMeter loadMeter;

void MyCallback(AudioHandle::InputBuffer in, 
                AudioHandle::OutputBuffer out, 
                size_t size) 
{
    loadMeter.OnBlockStart();
    for (size_t i = 0; i < size; i++)
    {
        // add your processing here
        out[0][i] = 0.0f;
        out[1][i] = 0.0f;
    }
    loadMeter.OnBlockEnd();
}
int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(128);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    // start logging to the serial connection
    hw.StartLog();

    // initialize the load meter so that it knows what time is available for the processing:
    loadMeter.Init(hw.AudioSampleRate(), hw.AudioBlockSize());

    // start the audio processing callback
    hw.StartAudio(MyCallback);

    while(1) {
        // get the current load (smoothed value and peak values)
        const float avgLoad = loadMeter.GetAvgCpuLoad();
        const float maxLoad = loadMeter.GetMaxCpuLoad();
        const float minLoad = loadMeter.GetMinCpuLoad();
        // print it to the serial connection (as percentages)
        hw.PrintLine("Processing Load %%:");
        hw.PrintLine("Max: " FLT_FMT3, FLT_VAR3(maxLoad * 100.0f));
        hw.PrintLine("Avg: " FLT_FMT3, FLT_VAR3(avgLoad * 100.0f));
        hw.PrintLine("Min: " FLT_FMT3, FLT_VAR3(minLoad * 100.0f));
        // don't spam the serial connection too much
        System::Delay(500);
    }
}


