#ifndef LPA_ABI_H
#define LPA_ABI_H

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t abi_version;

    void (*initPlugin)(void* instance);
    void (*processAudio)(
        void* instance,
        float* in,
        float* out,
        uint32_t frames
    );

} LPA_Entry;

#ifdef __cplusplus
}
#endif
#endif