#ifndef CASSIA_CASSIA_H
#define CASSIA_CASSIA_H

#include <stddef.h>
#include <stdint.h>

#if defined(CASSIA_SHARED_LIBRARY)
#    if defined(_WIN32)
#        if defined(CASSIA_IMPLEMENTATION)
#            define CASSIA_EXPORT __declspec(dllexport)
#        else
#            define CASSIA_EXPORT __declspec(dllimport)
#        endif
#    else  // defined(_WIN32)
#        if defined(CASSIA_IMPLEMENTATION)
#            define CASSIA_EXPORT __attribute__((visibility("default")))
#        else
#            define CASSIA_EXPORT
#        endif
#    endif  // defined(_WIN32)
#else       // defined(CASSIA_SHARED_LIBRARY)
#    define CASSIA_EXPORT
#endif  // defined(CASSIA_SHARED_LIBRARY)

typedef struct CassiaStyling {
    float fill[4];
    uint32_t fillRule;
    uint32_t blendMode;
    uint32_t _padding[2];
} CassiaStyling;

extern "C" {
    CASSIA_EXPORT void cassia_init(uint32_t width, uint32_t height);
    CASSIA_EXPORT void cassia_render(
        const uint64_t* psegments,
        size_t psegmentCount,
        const CassiaStyling* stylings,
        size_t stylingCount
    );
    CASSIA_EXPORT void cassia_shutdown();
}

#endif // CASSIA_CASSIA_H
