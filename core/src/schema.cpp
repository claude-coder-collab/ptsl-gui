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
#include <mutex>

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

template <typename Descriptor>
std::string commentOf(const Descriptor& descriptor) {
    pb::SourceLocation location;
    if (!descriptor.GetSourceLocation(&location)) {
        return {};
    }
    const std::string leading = trimmed(location.leading_comments);
    const std::string trailing = trimmed(location.trailing_comments);
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
        values.push_back(EnumValue{.name = std::string(value->name()), .number = value->number()});
    }
    return values;
}

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
