#include "lpa_abi.h"

struct MyPlugin {
    float gain;
};

extern "C" void lpa_init(void* instance) {
    MyPlugin* p = (MyPlugin*)instance;
    p->gain = 0.5f;
}

extern "C" void lpa_process(
    void* instance,
    float* in,
    float* out,
    uint32_t frames
) {
    MyPlugin* p = (MyPlugin*)instance;
    for(uint32_t i = 0; i < frames; i++) {
        out[i] = in[i] * p->gain;
    }
}

extern "C" {
// Use 4-byte alignment for now, as each field is 4 bytes anyways (no need to worry about extra padding where 8-byte alignment might have that)
__attribute__((used, section(".entry"), aligned(4)))
const LPA_Entry lpa_entry = {
    .abi_version = 1,
    .initPlugin        = lpa_init,
    .processAudio     = lpa_process,
};

}