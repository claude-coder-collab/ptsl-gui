#include <ptslgui/schema.hpp>

#include <google/protobuf/compiler/parser.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/io/tokenizer.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/util/json_util.h>

#include <algorithm>
#include <format>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <ranges>
#include <vector>

namespace ptslgui {
namespace {

namespace pb = google::protobuf;

#if GOOGLE_PROTOBUF_VERSION >= 4022000
#define PTSLGUI_PB_RECORD_ERROR RecordError
using PbText = absl::string_view;
#else
#define PTSLGUI_PB_RECORD_ERROR AddError
using PbText = const std::string&;
#endif

class TokenizerErrors final : public pb::io::ErrorCollector {
public:
    void PTSLGUI_PB_RECORD_ERROR(int line, pb::io::ColumnNumber column, PbText message) override {
        errors.push_back(std::format("{}:{}: {}", line + 1, column + 1, std::string(message)));
    }

    std::vector<std::string> errors;
};

class PoolErrors final : public pb::DescriptorPool::ErrorCollector {
public:
    void PTSLGUI_PB_RECORD_ERROR(PbText filename, PbText elementName, const pb::Message* /*descriptor*/,
                                 ErrorLocation /*location*/, PbText message) override {
        errors.push_back(
            std::format("{}: {}: {}", std::string(filename), std::string(elementName), std::string(message)));
    }

    std::vector<std::string> errors;
};

#undef PTSLGUI_PB_RECORD_ERROR

std::string joinErrors(const std::vector<std::string>& errors) {
    std::string result;
    for (const auto& error : errors) {
        if (!result.empty()) {
            result += '\n';
        }
        result += error;
    }
    return result;
}

std::string trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

/// Removes Doxygen block decoration ("*" line prefixes) and surrounding blank lines from a proto comment.
std::string cleanComment(std::string_view comment) {
    std::string result;
    std::string pendingBlank;
    std::size_t start = 0;
    while (start <= comment.size()) {
        const auto end = std::min(comment.find('\n', start), comment.size());
        std::string line = trimmed(comment.substr(start, end - start));
        if (line.starts_with('*')) {
            line = trimmed(std::string_view(line).substr(1));
        }
        if (line.empty()) {
            pendingBlank += result.empty() ? "" : "\n";
        } else {
            if (!result.empty()) {
                result += pendingBlank.empty() ? "\n" : "\n\n";
            }
            pendingBlank.clear();
            result += line;
        }
        start = end + 1;
    }
    return result;
}

template <typename Descriptor>
std::string commentOf(const Descriptor& descriptor) {
    pb::SourceLocation location;
    if (!descriptor.GetSourceLocation(&location)) {
        return {};
    }
    const std::string leading = cleanComment(location.leading_comments);
    const std::string trailing = cleanComment(location.trailing_comments);
    if (leading.empty() || trailing.empty()) {
        return leading + trailing;
    }
    return leading + '\n' + trailing;
}

FieldKind kindOf(const pb::FieldDescriptor& field) {
    using Type = pb::FieldDescriptor::CppType;
    switch (field.cpp_type()) {
    case Type::CPPTYPE_BOOL:
        return FieldKind::Bool;
    case Type::CPPTYPE_INT32:
        return FieldKind::Int32;
    case Type::CPPTYPE_INT64:
        return FieldKind::Int64;
    case Type::CPPTYPE_UINT32:
        return FieldKind::UInt32;
    case Type::CPPTYPE_UINT64:
        return FieldKind::UInt64;
    case Type::CPPTYPE_FLOAT:
        return FieldKind::Float;
    case Type::CPPTYPE_DOUBLE:
        return FieldKind::Double;
    case Type::CPPTYPE_ENUM:
        return FieldKind::Enum;
    case Type::CPPTYPE_MESSAGE:
        return FieldKind::Message;
    case Type::CPPTYPE_STRING:
        return field.type() == pb::FieldDescriptor::TYPE_BYTES ? FieldKind::Bytes : FieldKind::String;
    }
    return FieldKind::String;
}

std::vector<EnumValue> enumValuesOf(const pb::EnumDescriptor& descriptor) {
    std::vector<EnumValue> values;
    values.reserve(static_cast<std::size_t>(descriptor.value_count()));
    for (int i = 0; i < descriptor.value_count(); ++i) {
        const auto* value = descriptor.value(i);
        if (const auto existing = std::ranges::find(values, value->number(), &EnumValue::number);
            existing != values.end()) {
            existing->aliases.emplace_back(value->name());
            continue;
        }
        values.push_back(EnumValue{.name = std::string(value->name()),
                                   .number = value->number(),
                                   .aliases = {},
                                   .comment = commentOf(*value)});
    }
    return values;
}

using SourcePath = std::vector<int>;

SourcePath childPath(SourcePath path, std::initializer_list<int> children) {
    path.insert(path.end(), children);
    return path;
}

/// Moves the first non-deprecated name of each aliased enum number to the front of its group, so that protobuf
/// (which prints the first declared name) and the form prefer current names. Source locations follow the values.
class AliasOrder {
public:
    explicit AliasOrder(pb::FileDescriptorProto& file) : file_(&file) {
        for (const auto& location : file_->source_code_info().location()) {
            comments_.emplace(SourcePath(location.path().begin(), location.path().end()),
                              location.leading_comments() + location.trailing_comments());
        }
    }

    void apply() {
        for (int i = 0; i < file_->enum_type_size(); ++i) {
            reorder(*file_->mutable_enum_type(i), {pb::FileDescriptorProto::kEnumTypeFieldNumber, i});
        }
        for (int i = 0; i < file_->message_type_size(); ++i) {
            visit(*file_->mutable_message_type(i), {pb::FileDescriptorProto::kMessageTypeFieldNumber, i});
        }
        remapLocations();
    }

private:
    void visit(pb::DescriptorProto& message, const SourcePath& path) { // NOLINT(misc-no-recursion)
        for (int i = 0; i < message.enum_type_size(); ++i) {
            reorder(*message.mutable_enum_type(i), childPath(path, {pb::DescriptorProto::kEnumTypeFieldNumber, i}));
        }
        for (int i = 0; i < message.nested_type_size(); ++i) {
            visit(*message.mutable_nested_type(i), childPath(path, {pb::DescriptorProto::kNestedTypeFieldNumber, i}));
        }
    }

    [[nodiscard]] bool isDeprecated(const SourcePath& enumPath, int index) const {
        const auto it = comments_.find(childPath(enumPath, {pb::EnumDescriptorProto::kValueFieldNumber, index}));
        return it != comments_.end() && it->second.contains("@deprecated");
    }

    void reorder(pb::EnumDescriptorProto& enumProto, const SourcePath& path) {
        std::map<int, std::vector<int>> byNumber;
        for (int i = 0; i < enumProto.value_size(); ++i) {
            byNumber[enumProto.value(i).number()].push_back(i);
        }
        std::vector<int> newIndex(static_cast<std::size_t>(enumProto.value_size()));
        std::ranges::iota(newIndex, 0);
        bool changed = false;
        for (const auto& indices : byNumber | std::views::values) {
            const auto preferred = std::ranges::find_if(indices, [&](int index) { return !isDeprecated(path, index); });
            if (preferred == indices.end() || *preferred == indices.front()) {
                continue;
            }
            enumProto.mutable_value()->SwapElements(indices.front(), *preferred);
            std::swap(newIndex[static_cast<std::size_t>(indices.front())],
                      newIndex[static_cast<std::size_t>(*preferred)]);
            changed = true;
        }
        if (changed) {
            renumbered_.emplace(path, std::move(newIndex));
        }
    }

    void remapLocations() {
        if (renumbered_.empty()) {
            return;
        }
        for (auto& location : *file_->mutable_source_code_info()->mutable_location()) {
            for (const auto& [enumPath, newIndex] : renumbered_) {
                const auto depth = static_cast<int>(enumPath.size());
                if (location.path_size() < depth + 2 ||
                    !std::equal(enumPath.begin(), enumPath.end(), location.path().begin()) ||
                    location.path(depth) != pb::EnumDescriptorProto::kValueFieldNumber) {
                    continue;
                }
                location.set_path(depth + 1, newIndex[static_cast<std::size_t>(location.path(depth + 1))]);
                break;
            }
        }
    }

    pb::FileDescriptorProto* file_;
    std::map<SourcePath, std::string> comments_;
    std::map<SourcePath, std::vector<int>> renumbered_;
};

FieldSpec fieldSpecOf(const pb::FieldDescriptor& field) { // NOLINT(misc-no-recursion)
    FieldSpec spec{
        .name = std::string(field.name()),
        .jsonName = std::string(field.json_name()),
        .number = field.number(),
        .kind = kindOf(field),
        .repeated = field.is_repeated(),
        .isMap = field.is_map(),
        .hasPresence = field.has_presence(),
        .oneof = {},
        .typeName = {},
        .enumValues = {},
        .mapEntry = {},
        .comment = commentOf(field),
    };
    if (const auto* oneof = field.real_containing_oneof()) {
        spec.oneof = std::string(oneof->name());
    }
    if (const auto* enumType = field.enum_type()) {
        spec.typeName = std::string(enumType->full_name());
        spec.enumValues = enumValuesOf(*enumType);
    }
    if (const auto* messageType = field.message_type()) {
        spec.typeName = std::string(messageType->full_name());
        if (spec.isMap) {
            spec.mapEntry.push_back(fieldSpecOf(*messageType->map_key()));
            spec.mapEntry.push_back(fieldSpecOf(*messageType->map_value()));
        }
    }
    return spec;
}

} // namespace

struct ProtoSchema::Impl {
    pb::DescriptorPool pool;
    const pb::FileDescriptor* file = nullptr;
    std::string package;
    mutable std::mutex factoryMutex;
    mutable pb::DynamicMessageFactory factory{&pool};

    [[nodiscard]] const pb::Descriptor* findMessage(std::string_view name) const {
        const std::string fullName(name);
        if (const auto* descriptor = pool.FindMessageTypeByName(fullName)) {
            return descriptor;
        }
        if (!package.empty()) {
            return pool.FindMessageTypeByName(package + "." + fullName);
        }
        return nullptr;
    }

    [[nodiscard]] const pb::EnumDescriptor* findEnum(std::string_view name) const {
        const std::string fullName(name);
        if (const auto* descriptor = pool.FindEnumTypeByName(fullName)) {
            return descriptor;
        }
        if (!package.empty()) {
            return pool.FindEnumTypeByName(package + "." + fullName);
        }
        return nullptr;
    }
};

ProtoSchema::ProtoSchema(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

ProtoSchema::ProtoSchema(ProtoSchema&&) noexcept = default;

ProtoSchema& ProtoSchema::operator=(ProtoSchema&&) noexcept = default;

ProtoSchema::~ProtoSchema() = default;

std::expected<ProtoSchema, std::string> ProtoSchema::fromProtoText(std::string_view text, std::string_view fileName) {
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected("proto text is too large");
    }
    pb::io::ArrayInputStream input(text.data(), static_cast<int>(text.size()));
    TokenizerErrors tokenizerErrors;
    pb::io::Tokenizer tokenizer(&input, &tokenizerErrors);
    pb::compiler::Parser parser;
    parser.RecordErrorsTo(&tokenizerErrors);
    pb::FileDescriptorProto fileProto;
    if (!parser.Parse(&tokenizer, &fileProto) || !tokenizerErrors.errors.empty()) {
        return std::unexpected(std::format("cannot parse {}:\n{}", fileName, joinErrors(tokenizerErrors.errors)));
    }
    fileProto.set_name(std::string(fileName));
    AliasOrder(fileProto).apply();

    auto impl = std::make_unique<Impl>();
    PoolErrors poolErrors;
    impl->file = impl->pool.BuildFileCollectingErrors(fileProto, &poolErrors);
    if (impl->file == nullptr) {
        return std::unexpected(std::format("cannot build {}:\n{}", fileName, joinErrors(poolErrors.errors)));
    }
    impl->package = std::string(impl->file->package());
    return ProtoSchema(std::move(impl));
}

const std::string& ProtoSchema::packageName() const {
    return impl_->package;
}

bool ProtoSchema::hasMessage(std::string_view name) const {
    return impl_->findMessage(name) != nullptr;
}

std::optional<MessageSpec> ProtoSchema::message(std::string_view name) const {
    const auto* descriptor = impl_->findMessage(name);
    if (descriptor == nullptr) {
        return std::nullopt;
    }
    MessageSpec spec{
        .fullName = std::string(descriptor->full_name()),
        .name = std::string(descriptor->name()),
        .fields = {},
        .oneofs = {},
        .comment = commentOf(*descriptor),
    };
    spec.fields.reserve(static_cast<std::size_t>(descriptor->field_count()));
    for (int i = 0; i < descriptor->field_count(); ++i) {
        spec.fields.push_back(fieldSpecOf(*descriptor->field(i)));
    }
    for (int i = 0; i < descriptor->real_oneof_decl_count(); ++i) {
        spec.oneofs.emplace_back(descriptor->oneof_decl(i)->name());
    }
    return spec;
}

std::optional<std::vector<EnumValue>> ProtoSchema::enumValues(std::string_view name) const {
    const auto* descriptor = impl_->findEnum(name);
    if (descriptor == nullptr) {
        return std::nullopt;
    }
    return enumValuesOf(*descriptor);
}

std::vector<std::string> ProtoSchema::messageNames() const {
    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(impl_->file->message_type_count()));
    for (int i = 0; i < impl_->file->message_type_count(); ++i) {
        names.emplace_back(impl_->file->message_type(i)->full_name());
    }
    std::ranges::sort(names);
    return names;
}

std::expected<std::string, std::string> ProtoSchema::normalizeJson(std::string_view messageName, std::string_view json,
                                                                   JsonFormat format) const {
    const auto* descriptor = impl_->findMessage(messageName);
    if (descriptor == nullptr) {
        return std::unexpected(std::format("unknown message type {}", messageName));
    }
    const std::string input = trimmed(json).empty() ? std::string("{}") : std::string(json);

    std::unique_ptr<pb::Message> message;
    {
        const std::scoped_lock lock(impl_->factoryMutex);
        message.reset(impl_->factory.GetPrototype(descriptor)->New());
    }

    const pb::util::JsonParseOptions parseOptions;
    if (const auto status = pb::util::JsonStringToMessage(input, message.get(), parseOptions); !status.ok()) {
        return std::unexpected(std::string(status.message()));
    }

    pb::util::JsonPrintOptions printOptions;
    printOptions.preserve_proto_field_names = true;
    printOptions.add_whitespace = format.pretty;
#if GOOGLE_PROTOBUF_VERSION >= 5026000
    printOptions.always_print_fields_with_no_presence = format.includeDefaults;
#else
    printOptions.always_print_primitive_fields = format.includeDefaults;
#endif
    std::string output;
    if (const auto status = pb::util::MessageToJsonString(*message, &output, printOptions); !status.ok()) {
        return std::unexpected(std::string(status.message()));
    }
    if (format.pretty) {
        return trimmed(output);
    }
    return output;
}

} // namespace ptslgui
