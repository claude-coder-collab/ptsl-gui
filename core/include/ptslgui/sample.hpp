#pragma once

#include <ptslgui/schema.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace ptslgui {

struct SampleOptions {
    std::uint32_t seed = 0;
    /// Nested messages deeper than this are left out.
    int maxDepth = 3;
    /// Upper bound on generated repeated and map elements.
    int maxElements = 3;
};

/// Generates random JSON for a message that ProtoSchema::normalizeJson accepts. Deterministic for a given seed.
/// Exercises every field kind, oneofs, optional presence, repeated fields and maps. Returns "{}" for unknown types.
[[nodiscard]] std::string sampleJson(const ProtoSchema& schema, std::string_view messageName,
                                     const SampleOptions& options = {});

} // namespace ptslgui
