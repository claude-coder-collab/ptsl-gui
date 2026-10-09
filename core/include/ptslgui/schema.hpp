#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ptslgui {

enum class FieldKind { Bool, Int32, Int64, UInt32, UInt64, Float, Double, String, Bytes, Enum, Message };

/// One enum number. Aliased names (allow_alias) are folded into the preferred, non-deprecated name.
struct EnumValue {
    std::string name;
    int number = 0;
    std::vector<std::string> aliases;
    std::string comment;
};

/// Description of one field of a protobuf message, enough to build an editor for it.
struct FieldSpec { // NOLINT(misc-no-recursion)
    std::string name;
    std::string jsonName;
    int number = 0;
    FieldKind kind = FieldKind::String;
    bool repeated = false;
    bool isMap = false;
    bool hasPresence = false;
    /// Name of the containing oneof (synthetic proto3 "optional" oneofs are not reported).
    std::string oneof;
    /// Fully qualified message or enum type name for Message and Enum fields.
    std::string typeName;
    std::vector<EnumValue> enumValues;
    /// For map fields: the key and value fields of the map entry.
    std::vector<FieldSpec> mapEntry;
    std::string comment;
};

struct MessageSpec {
    std::string fullName;
    std::string name;
    std::vector<FieldSpec> fields;
    std::vector<std::string> oneofs;
    std::string comment;
};

struct JsonFormat {
    bool pretty = false;
    /// Also print fields without presence at their default values, as the PTSL SDK does when sending.
    bool includeDefaults = false;
};

/// A protobuf schema parsed at runtime from .proto source text. Move-only.
class ProtoSchema {
public:
    [[nodiscard]] static std::expected<ProtoSchema, std::string>
    fromProtoText(std::string_view text, std::string_view fileName = "PTSL.proto");

    ProtoSchema(ProtoSchema&&) noexcept;
    ProtoSchema& operator=(ProtoSchema&&) noexcept;
    ProtoSchema(const ProtoSchema&) = delete;
    ProtoSchema& operator=(const ProtoSchema&) = delete;
    ~ProtoSchema();

    [[nodiscard]] const std::string& packageName() const;

    /// Accepts a fully qualified name or a name relative to the file's package.
    [[nodiscard]] bool hasMessage(std::string_view name) const;
    [[nodiscard]] std::optional<MessageSpec> message(std::string_view name) const;
    [[nodiscard]] std::optional<std::vector<EnumValue>> enumValues(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> messageNames() const;

    /// Parses JSON as the given message and re-serialises it with proto field names, omitting default values
    /// unless format.includeDefaults is set. Enum values are printed by their preferred name.
    /// An empty or whitespace-only input is treated as "{}".
    [[nodiscard]] std::expected<std::string, std::string>
    normalizeJson(std::string_view messageName, std::string_view json, JsonFormat format = {}) const;

private:
    struct Impl;
    explicit ProtoSchema(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace ptslgui
