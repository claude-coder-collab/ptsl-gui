#include <ptslgui/sample.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <random>

namespace ptslgui {
namespace {

using Json = nlohmann::ordered_json;

constexpr int presenceOneIn = 2;
constexpr int integerRange = 1000;
constexpr int floatSteps = 64;

class Generator {
public:
    Generator(const ProtoSchema& schema, const SampleOptions& options)
        : schema_(&schema)
        , options_(options)
        , random_(options.seed) {}

    // NOLINTNEXTLINE(misc-no-recursion)
    Json message(std::string_view name, int depth) {
        Json object = Json::object();
        const auto spec = schema_->message(name);
        if (!spec) {
            return object;
        }
        for (const auto& oneof : spec->oneofs) {
            std::vector<const FieldSpec*> members;
            for (const auto& field : spec->fields) {
                if (field.oneof == oneof && canGenerate(field, depth)) {
                    members.push_back(&field);
                }
            }
            if (!members.empty() && chance()) {
                const FieldSpec* chosen = members[index(members.size())];
                object[chosen->name] = single(*chosen, depth);
            }
        }
        for (const auto& field : spec->fields) {
            if (!field.oneof.empty() || !canGenerate(field, depth) || !chance()) {
                continue;
            }
            if (field.isMap) {
                object[field.name] = map(field, depth);
            } else if (field.repeated) {
                Json array = Json::array();
                const int count = between(0, options_.maxElements);
                for (int i = 0; i < count; ++i) {
                    array.push_back(single(field, depth));
                }
                object[field.name] = std::move(array);
            } else {
                object[field.name] = single(field, depth);
            }
        }
        return object;
    }

private:
    [[nodiscard]] bool canGenerate(const FieldSpec& field, int depth) const {
        if (field.isMap) {
            return field.mapEntry.size() == 2 &&
                   (field.mapEntry[1].kind != FieldKind::Message || depth < options_.maxDepth);
        }
        return field.kind != FieldKind::Message || depth < options_.maxDepth;
    }

    // NOLINTNEXTLINE(misc-no-recursion)
    Json map(const FieldSpec& field, int depth) {
        Json object = Json::object();
        const int count = between(0, options_.maxElements);
        for (int i = 0; i < count; ++i) {
            const Json key = single(field.mapEntry[0], depth);
            object[key.is_string() ? key.get<std::string>() : key.dump()] = single(field.mapEntry[1], depth);
        }
        return object;
    }

    // NOLINTNEXTLINE(misc-no-recursion)
    Json single(const FieldSpec& field, int depth) {
        switch (field.kind) {
        case FieldKind::Bool:
            return chance();
        case FieldKind::Int32:
            return between(-integerRange, integerRange);
        case FieldKind::UInt32:
            return between(0, integerRange);
        case FieldKind::Int64:
            return std::to_string(between(-integerRange, integerRange));
        case FieldKind::UInt64:
            return std::to_string(between(0, integerRange));
        case FieldKind::Float:
        case FieldKind::Double:
            return static_cast<double>(between(-floatSteps, floatSteps)) / 4.0;
        case FieldKind::String:
            return text();
        case FieldKind::Bytes: {
            static constexpr std::array<std::string_view, 4> samples{"", "AA==", "AAEC", "aGVsbG8="};
            return std::string(samples.at(index(samples.size())));
        }
        case FieldKind::Enum:
            if (field.enumValues.empty()) {
                return 0;
            }
            return field.enumValues[index(field.enumValues.size())].name;
        case FieldKind::Message:
            return message(field.typeName, depth + 1);
        }
        return nullptr;
    }

    std::string text() {
        static constexpr std::array<std::string_view, 6> samples{
            "", "a", "Audio 1", "/tmp/x y.wav", "ünïcödé", "quote \" and \\ slash"};
        return std::string(samples.at(index(samples.size())));
    }

    bool chance() { return between(1, presenceOneIn) == 1; }

    int between(int low, int high) { return std::uniform_int_distribution<int>(low, high)(random_); }

    std::size_t index(std::size_t size) { return std::uniform_int_distribution<std::size_t>(0, size - 1)(random_); }

    const ProtoSchema* schema_;
    SampleOptions options_;
    std::mt19937 random_;
};

} // namespace

std::string sampleJson(const ProtoSchema& schema, std::string_view messageName, const SampleOptions& options) {
    Generator generator(schema, options);
    return generator.message(messageName, 0).dump();
}

} // namespace ptslgui
