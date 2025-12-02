#include "state.h"
#include "fsm.h"
#include <cstdio>

// Minimal concrete implementations for the state machine used by the
// slipRecorder example. Implementations are intentionally light-weight
// so they can be adapted to hardware-specific logic later.

// Sleep state: idle/low-power waiting state
void Sleep::enter(FSM* recorder) {
	(void)recorder; // silence unused parameter warning
}

void Sleep::exit(FSM* recorder) {
	(void)recorder;
}

void Sleep::handle(FSM* recorder) {
	(void)recorder;
	// 
}

// SD Card check state: verify SD card present and ready
void SD_Check::enter(FSM* recorder) {
	(void)recorder;
}

void SD_Check::exit(FSM* recorder) {
	(void)recorder;
}

void SD_Check::handle(FSM* recorder) {
	(void)recorder;
	
}

// Record state: active recording state
void Record::enter(FSM* recorder) {
	(void)recorder;
}

void Record::exit(FSM* recorder) {
	(void)recorder;
}

void Record::handle(FSM* recorder) {
	(void)recorder;
	// show whether we're recording with onboard LED
}

