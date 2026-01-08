#include "lpa_abi.h"
#include "../duts.hpp"

extern "C" {
// Use 4-byte alignment for now, as each field is 4 bytes anyways (no need to worry about extra padding where 8-byte alignment might have that)
__attribute__((used, section(".entry"), aligned(4)))
const LPA_Entry lpa_entry = {
    .abi_version = 1,
    .initPlugin        = initPhaser,
    .processAudio     = processPhaser,
    .dummyAdd           = 0
};

}