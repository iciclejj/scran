#ifndef SCRANROT_COMMON_HPP
#define SCRANROT_COMMON_HPP

#include <cstdint>
#include <cstddef>
#include <type_traits>


namespace scranrot::internal {
    using std::size_t;

    using i8  = std::int8_t;
    using i16 = std::int16_t;
    using i32 = std::int32_t;
    using i64 = std::int64_t;

    using u8  = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;

    static_assert(
           std::is_same_v<u8, unsigned char>
        && std::is_same_v<i8, signed char>,
        "scranrot requires 8-bit bytes, and (un)signed char == (u)int8_t to keep same strict aliasing assumptions"
    );
}


#endif
