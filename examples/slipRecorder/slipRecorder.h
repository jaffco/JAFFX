/*
 * slipRecorder.h
 * Public header for the SlipRecorder example
 *
 * Provides an interface declaration for the SlipRecorder firmware class.
 * The full implementation lives in `slipRecorder.cpp`.
 */

#ifndef SLIPRECORDER_H
#define SLIPRECORDER_H

#include "../../Jaffx.hpp"

// Forward-declare the template writer used by the implementation.
template <size_t WriteBufferSize>
class SDCardWavWriter;

class SlipRecorder : public Jaffx::Firmware {
public:
  SlipRecorder() = default;
  ~SlipRecorder() override = default;

  // Firmware lifecycle (overrides provided by the implementation in .cpp)
  void init() override;
  float processAudio(float in) override;
  void loop() override;

private:
  // Implementation-defined members are in the .cpp file; keeping the
  // header minimal prevents ODR issues while exposing the public API.
};

#endif // SLIPRECORDER_H
