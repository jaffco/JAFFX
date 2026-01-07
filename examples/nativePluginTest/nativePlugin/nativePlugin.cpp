#include "lpa_abi.h"

float gain;

extern "C" void lpa_init() {
    gain = 0.5f;
}

extern "C" void lpa_process(
    float* in,
    float* out,
    uint32_t frames
) {
    for(uint32_t i = 0; i < frames; i++) {
        out[i] = in[i] * gain;
    }
}

extern "C" void dummyAdd(float* in1, float* in2, float* out) {
    *out = *in1 + *in2;
}

extern "C" {
// Use 4-byte alignment for now, as each field is 4 bytes anyways (no need to worry about extra padding where 8-byte alignment might have that)
__attribute__((used, section(".entry"), aligned(4)))
const LPA_Entry lpa_entry = {
    .abi_version = 1,
    .initPlugin        = lpa_init,
    .processAudio     = lpa_process,
    .dummyAdd           = dummyAdd
};

}