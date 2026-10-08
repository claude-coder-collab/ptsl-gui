#pragma once

#include <ptslgui/catalog.hpp>
#include <ptslgui/schema.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ptslgui::test {

inline std::string readFile(const std::string& path) {
    const std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open " + path);
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

inline const ProtoSchema& fixtureSchema() {
    static const ProtoSchema schema = [] {
        auto loaded = ProtoSchema::fromProtoText(readFile(PTSLGUI_FIXTURE_PROTO), "fixture.proto");
        if (!loaded) {
            throw std::runtime_error(loaded.error());
        }
        return std::move(*loaded);
    }();
    return schema;
}

inline const CommandCatalog& fixtureCatalog() {
    static const CommandCatalog catalog = [] {
        auto loaded = CommandCatalog::fromJson(readFile(PTSLGUI_FIXTURE_CATALOG));
        if (!loaded) {
            throw std::runtime_error(loaded.error());
        }
        return std::move(*loaded);
    }();
    return catalog;
}

} // namespace ptslgui::test
