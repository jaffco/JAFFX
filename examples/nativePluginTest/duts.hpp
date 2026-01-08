/**
 * @file duts.cpp
 * @brief This file contains the functions we want to test
 */

#ifndef DUTS_HPP
#define DUTS_HPP

#include "../../Gimmel/include/gimmel.hpp" // Include all the giml effects
/**
 * DUT #1
 * giml::SinOsc
 * 
 * Measures the performance of
 * - sin() (trig functions)
 * - repeated adds+mults (scaling + incremental phasor)
 * 
 */
giml::SinOsc<float> mySinOsc{48000};

extern "C" void initSinOsc() {
    mySinOsc.setFrequency(440);
}

extern "C" void processSinOsc(float* inBuffer, float* outBuffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        outBuffer[i] = mySinOsc.processSample();
    }
}

/**
 * DUT #2
 * giml::Phaser DUT
 * 
 * Measures the performance of
 * - per-sample tan() (trig functions)
 * - several divides
 * - repeated adds+mults (bank of SVFs which each have Traps)
 * 
 */
giml::Phaser<float> myPhaserEffect{48000};

extern "C" void initPhaser() {
    myPhaserEffect.setParams();
    myPhaserEffect.enable();
}

extern "C" void processPhaser(float* inBuffer, float* outBuffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        outBuffer[i] = myPhaserEffect.processSample(inBuffer[i]);
    }
}


/**
 * DUT #3
 * giml::Compressor
 * 
 * Measures the performance of
 * - pow() (for both doubles and floats, depending on type)
 * - branching (several thresholds to check per sample)
 * - log10()
 * - std::max()
 * - repeated adds + mults
 * 
 */
giml::Compressor<float> myCompressor{48000};

extern "C" void initCompressor() {
    myCompressor.setParams(); // TODO: Pick some good params
    myCompressor.enable();
}

extern "C" void processCompressor(float* inBuffer, float* outBuffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        outBuffer[i] = myCompressor.processSample(inBuffer[i]);
    }
}

/**
 * DUT #4
 * giml::Reverb
 * 
 * Measures performance of
 * - 
 */
giml::Reverb<float> myReverb{48000};

extern "C" void initReverb() {
    // TODO: myReverb.setParams()
    myReverb.enable();
}

extern "C" void processReverb(float* inBuffer, float* outBuffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        outBuffer[i] = myReverb.processSample(inBuffer[i]);
    }
}

#endif