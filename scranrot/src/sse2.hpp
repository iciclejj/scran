#ifndef SCRANROT_SSE_H
#define SCRANROT_SSE_H


#include <emmintrin.h>

#include "./types.hpp"
#include "./backends.hpp"
#include "./util.hpp"


namespace scranrot::internal {

    static inline __m128i
    scranrot_sse2_rgba_shuffle_to_m128i(u32 rgba_shuffle_mask) {
        // TODO: Assert rgba_shuffle is valid (and let (0 => 0,1,2,3) ?)
        const __m128i _rgba_shuffle_mask_128_offsets = _mm_setr_epi8(0,0,0,0, 4,4,4,4, 8,8,8,8, 12,12,12,12);
        const __m128i _rgba_shuffle_mask_128 = _mm_set1_epi32(rgba_shuffle_mask);
        const __m128i rgba_shuffle_mask_128 = _mm_add_epi8(_rgba_shuffle_mask_128_offsets, _rgba_shuffle_mask_128);
        return rgba_shuffle_mask_128;
    }

    SCRANROT_ALWAYS_INLINE
    static inline __m128i
    scranrot_sse2_rotate_180_get_modified_rgba_shuffle(const __m128i original_rgba_shuffle_mask) {
        return _mm_shuffle_epi32(original_rgba_shuffle_mask, _MM_SHUFFLE(0,1,2,3));
    }

    template<>
    SCRANROT_ALWAYS_INLINE
    inline __m128i
    load_unaligned<__m128i>(const void *src) {
        return _mm_loadu_si128(static_cast<const __m128i_u *>(src));
    }

    template<>
    SCRANROT_ALWAYS_INLINE
    inline void
    store_unaligned<__m128i>(void *dst, const __m128i &val) {
        _mm_storeu_si128(static_cast<__m128i_u *>(dst), val);
    }

}


#endif
