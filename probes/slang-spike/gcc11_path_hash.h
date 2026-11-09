// Workaround for GCC 11.2 (devtoolset-11, the newest compiler for CentOS 7):
// libstdc++ only gained std::hash<std::filesystem::path> in GCC 11.3/12, so
// slang's `hash<T>` fallback (include/slang/util/Hash.h) fails to compile with
// "no member named 'operator()'". The repository's own slang build carries the
// same fix as a source patch (build/slang/patches/0001-gcc11-libstdcxx-path-hash.patch);
// here it is delivered with `-include` instead, so the vendored submodule stays
// pristine and the workaround applies to every translation unit.
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

namespace std {
template<>
struct hash<std::filesystem::path> {
    size_t operator()(const std::filesystem::path& value) const noexcept {
        return hash<std::string>{}(value.native());
    }
};
}  // namespace std
