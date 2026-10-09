#include <ptslgui/sequence.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <limits>

namespace ptslgui {
namespace {

using Json = nlohmann::ordered_json;

constexpr int formatVersion = 1;
constexpr std::string_view variablesPrefix = "vars";

std::string_view trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

bool isLabelStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool isLabelChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

Json parseOrString(const std::string& text) {
    if (Json::accept(text)) {
        return Json::parse(text);
    }
    return text;
}

/// Applies a chain of .field and [index] accessors.
std::expected<Json, std::string> applyPath(Json value, std::string_view path, std::string_view expression) {
    std::size_t pos = 0;
    while (pos < path.size()) {
        if (path[pos] == '.') {
            std::size_t end = pos + 1;
            while (end < path.size() && path[end] != '.' && path[end] != '[') {
                ++end;
            }
            const std::string field(path.substr(pos + 1, end - pos - 1));
            if (field.empty()) {
                return std::unexpected(std::format("{{{{{}}}}}: empty field name", expression));
            }
            if (!value.is_object() || !value.contains(field)) {
                return std::unexpected(std::format("{{{{{}}}}}: no field '{}'", expression, field));
            }
            value = Json(value[field]);
            pos = end;
        } else if (path[pos] == '[') {
            const auto close = path.find(']', pos);
            if (close == std::string_view::npos) {
                return std::unexpected(std::format("{{{{{}}}}}: missing ']'", expression));
            }
            const std::string_view digits = path.substr(pos + 1, close - pos - 1);
            if (digits.empty() || !std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; })) {
                return std::unexpected(std::format("{{{{{}}}}}: index must be a number", expression));
            }
            std::size_t index = 0;
            for (const char c : digits) {
                index = (index * 10) + static_cast<std::size_t>(c - '0');
            }
            if (!value.is_array() || index >= value.size()) {
                return std::unexpected(std::format("{{{{{}}}}}: no item [{}]", expression, index));
            }
            value = Json(value[index]);
            pos = close + 1;
        } else {
            return std::unexpected(std::format("{{{{{}}}}}: expected '.' or '['", expression));
        }
    }
    return value;
}

std::expected<Json, std::string> resolve(std::string_view rawExpression, const SubstitutionContext& context) {
    const std::string_view expression = trimmed(rawExpression);
    std::size_t end = 0;
    while (end < expression.size() && isLabelChar(expression[end])) {
        ++end;
    }
    const std::string_view head = expression.substr(0, end);
    if (head.empty()) {
        return std::unexpected(std::format("{{{{{}}}}}: expected a step label or vars.<name>", expression));
    }
    if (head == variablesPrefix) {
        if (end >= expression.size() || expression[end] != '.') {
            return std::unexpected(std::format("{{{{{}}}}}: expected vars.<name>", expression));
        }
        std::size_t nameEnd = end + 1;
        while (nameEnd < expression.size() && isLabelChar(expression[nameEnd])) {
            ++nameEnd;
        }
        const std::string_view name = expression.substr(end + 1, nameEnd - end - 1);
        const auto variable = std::ranges::find(context.variables, name, &std::pair<std::string, std::string>::first);
        if (variable == context.variables.end()) {
            return std::unexpected(std::format("{{{{{}}}}}: unknown variable '{}'", expression, name));
        }
        return applyPath(parseOrString(variable->second), expression.substr(nameEnd), expression);
    }
    const auto response = context.responses.find(head);
    if (response == context.responses.end()) {
        return std::unexpected(std::format(
            "{{{{{}}}}}: no response from step '{}' (only earlier, completed steps can be used)", expression, head));
    }
    const std::string body = trimmed(response->second).empty() ? std::string("{}") : response->second;
    if (!Json::accept(body)) {
        return std::unexpected(std::format("{{{{{}}}}}: the response of '{}' is not JSON", expression, head));
    }
    return applyPath(Json::parse(body), expression.substr(end), expression);
}

std::string escapedForString(const Json& value) {
    const std::string text = value.is_string() ? value.get<std::string>() : value.dump();
    const std::string quoted = Json(text).dump();
    return quoted.substr(1, quoted.size() - 2);
}

} // namespace

std::string sequenceToJson(const Sequence& sequence) {
    Json variables = Json::object();
    for (const auto& [name, value] : sequence.variables) {
        variables[name] = value;
    }
    Json steps = Json::array();
    for (const auto& step : sequence.steps) {
        steps.push_back(Json{{"label", step.label},
                             {"command", step.commandName},
                             {"request", step.requestTemplate},
                             {"enabled", step.enabled},
                             {"continue_on_error", step.continueOnError}});
    }
    const Json document{
        {"version", formatVersion}, {"name", sequence.name}, {"variables", variables}, {"steps", steps}};
    return document.dump(2);
}

std::expected<Sequence, std::string> sequenceFromJson(std::string_view text) {
    const Json document = Json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return std::unexpected("not a JSON object");
    }
    if (!document.contains("version") || document["version"] != formatVersion) {
        return std::unexpected(std::format("unsupported sequence format (expected version {})", formatVersion));
    }
    Sequence sequence;
    if (const auto name = document.find("name"); name != document.end()) {
        if (!name->is_string()) {
            return std::unexpected("name must be a string");
        }
        sequence.name = name->get<std::string>();
    }
    if (const auto variables = document.find("variables"); variables != document.end()) {
        if (!variables->is_object()) {
            return std::unexpected("variables must be an object");
        }
        for (const auto& [name, value] : variables->items()) {
            if (!isValidLabel(name) || !value.is_string()) {
                return std::unexpected(std::format("invalid variable '{}'", name));
            }
            sequence.variables.emplace_back(name, value.get<std::string>());
        }
    }
    const auto steps = document.find("steps");
    if (steps == document.end() || !steps->is_array()) {
        return std::unexpected("steps must be an array");
    }
    for (const auto& item : *steps) {
        const auto stepNumber = sequence.steps.size() + 1;
        if (!item.is_object() || !item.contains("label") || !item["label"].is_string() || !item.contains("command") ||
            !item["command"].is_string() || !item.contains("request") || !item["request"].is_string()) {
            return std::unexpected(std::format("step {} needs string label, command and request", stepNumber));
        }
        SequenceStep step{.label = item["label"].get<std::string>(),
                          .commandName = item["command"].get<std::string>(),
                          .requestTemplate = item["request"].get<std::string>(),
                          .enabled = item.value("enabled", true),
                          .continueOnError = item.value("continue_on_error", false)};
        if (!isValidLabel(step.label)) {
            return std::unexpected(std::format("step {} has an invalid label '{}'", stepNumber, step.label));
        }
        if (std::ranges::contains(sequence.steps, step.label, &SequenceStep::label)) {
            return std::unexpected(std::format("duplicate step label '{}'", step.label));
        }
        sequence.steps.push_back(std::move(step));
    }
    return sequence;
}

bool isValidLabel(std::string_view label) {
    return !label.empty() && isLabelStart(label.front()) && std::ranges::all_of(label, isLabelChar) &&
           label != variablesPrefix;
}

std::string uniqueStepLabel(const Sequence& sequence, std::string_view base) {
    std::string label;
    for (const char c : base) {
        label += isLabelChar(c) ? c : '_';
    }
    if (label.empty() || !isLabelStart(label.front()) || label == variablesPrefix) {
        label = "step_" + label;
    }
    const auto taken = [&sequence](const std::string& candidate) {
        return std::ranges::contains(sequence.steps, candidate, &SequenceStep::label);
    };
    if (!taken(label)) {
        return label;
    }
    for (std::size_t suffix = 2; suffix < std::numeric_limits<std::size_t>::max(); ++suffix) {
        std::string candidate = std::format("{}_{}", label, suffix);
        if (!taken(candidate)) {
            return candidate;
        }
    }
    return label;
}

bool hasPlaceholders(std::string_view requestTemplate) {
    const auto open = requestTemplate.find("{{");
    return open != std::string_view::npos && requestTemplate.find("}}", open + 2) != std::string_view::npos;
}

std::expected<std::string, std::string> substitute(std::string_view requestTemplate,
                                                   const SubstitutionContext& context) {
    std::string output;
    output.reserve(requestTemplate.size());
    bool inString = false;
    std::size_t pos = 0;
    while (pos < requestTemplate.size()) {
        const char c = requestTemplate[pos];
        if (inString && c == '\\' && pos + 1 < requestTemplate.size()) {
            output += requestTemplate.substr(pos, 2);
            pos += 2;
            continue;
        }
        if (requestTemplate.substr(pos).starts_with("{{")) {
            const auto close = requestTemplate.find("}}", pos + 2);
            if (close == std::string_view::npos) {
                return std::unexpected("unterminated {{ placeholder");
            }
            const auto value = resolve(requestTemplate.substr(pos + 2, close - pos - 2), context);
            if (!value) {
                return std::unexpected(value.error());
            }
            output += inString ? escapedForString(*value) : value->dump();
            pos = close + 2;
            continue;
        }
        if (c == '"') {
            if (!inString && requestTemplate.substr(pos + 1).starts_with("{{")) {
                const auto close = requestTemplate.find("}}", pos + 3);
                if (close != std::string_view::npos && close + 2 < requestTemplate.size() &&
                    requestTemplate[close + 2] == '"') {
                    const auto value = resolve(requestTemplate.substr(pos + 3, close - pos - 3), context);
                    if (!value) {
                        return std::unexpected(value.error());
                    }
                    output += value->dump();
                    pos = close + 3;
                    continue;
                }
            }
            inString = !inString;
        }
        output += c;
        ++pos;
    }
    return output;
}

std::string_view toString(StepStatus status) {
    switch (status) {
    case StepStatus::Waiting:
        return "waiting";
    case StepStatus::Skipped:
        return "skipped";
    case StepStatus::Running:
        return "running";
    case StepStatus::Completed:
        return "completed";
    case StepStatus::Failed:
        return "failed";
    case StepStatus::Cancelled:
        return "cancelled";
    }
    return "unknown";
}

struct SequenceRunner::Alive {};

SequenceRunner::SequenceRunner(const CommandCatalog& catalog, RequestController& controller, Dispatcher dispatcher)
    : catalog_(catalog)
    , controller_(controller)
    , dispatcher_(dispatcher ? std::move(dispatcher) : Dispatcher([](const std::function<void()>& task) { task(); }))
    , alive_(std::make_shared<Alive>()) {}

SequenceRunner::~SequenceRunner() = default;

bool SequenceRunner::start(Sequence sequence, StepCallback onStep, FinishedCallback onFinished) {
    if (running_) {
        return false;
    }
    sequence_ = std::move(sequence);
    context_ = SubstitutionContext{.responses = {}, .variables = sequence_.variables};
    onStep_ = std::move(onStep);
    onFinished_ = std::move(onFinished);
    running_ = true;
    stopRequested_ = false;
    ++generation_;
    post([this] { runStep(0); });
    return true;
}

void SequenceRunner::stop() {
    stopRequested_ = true;
}

void SequenceRunner::post(std::function<void()> task) {
    const std::weak_ptr<Alive> alive = alive_;
    dispatcher_([this, alive, generation = generation_, task = std::move(task)] {
        if (!alive.expired() && running_ && generation == generation_) {
            task();
        }
    });
}

void SequenceRunner::report(std::size_t index, const StepResult& result, const HistoryEntry* entry) {
    if (onStep_) {
        onStep_(index, result, entry);
    }
}

void SequenceRunner::finish(SequenceOutcome outcome) {
    running_ = false;
    if (onFinished_) {
        onFinished_(outcome);
    }
}

void SequenceRunner::runStep(std::size_t index) {
    if (stopRequested_) {
        finish(SequenceOutcome::Stopped);
        return;
    }
    if (index >= sequence_.steps.size()) {
        finish(SequenceOutcome::Completed);
        return;
    }
    const SequenceStep& step = sequence_.steps[index];
    if (!step.enabled) {
        report(index, StepResult{.status = StepStatus::Skipped, .historySequence = {}, .error = {}}, nullptr);
        post([this, index] { runStep(index + 1); });
        return;
    }
    const auto fail = [this, index, &step](std::string error) {
        report(index, StepResult{.status = StepStatus::Failed, .historySequence = {}, .error = std::move(error)},
               nullptr);
        if (step.continueOnError) {
            post([this, index] { runStep(index + 1); });
        } else {
            finish(SequenceOutcome::Failed);
        }
    };
    const CommandInfo* command = catalog_.findByName(step.commandName);
    if (command == nullptr) {
        fail(std::format("unknown command {}", step.commandName));
        return;
    }
    auto request = substitute(step.requestTemplate, context_);
    if (!request) {
        fail(request.error());
        return;
    }
    report(index, StepResult{.status = StepStatus::Running, .historySequence = {}, .error = {}}, nullptr);
    answered_ = false;
    const std::weak_ptr<Alive> alive = alive_;
    const auto sent = controller_.send(command->id, *request,
                                       [this, alive, generation = generation_, index](const HistoryEntry& entry) {
                                           if (!alive.expired() && running_ && generation == generation_) {
                                               onResponse(index, entry);
                                           }
                                       });
    if (!sent) {
        fail(sent.error());
    } else if (!answered_) {
        report(index, StepResult{.status = StepStatus::Running, .historySequence = *sent, .error = {}}, nullptr);
    }
}

void SequenceRunner::onResponse(std::size_t index, const HistoryEntry& entry) {
    answered_ = true;
    const SequenceStep& step = sequence_.steps[index];
    StepResult result{.status = StepStatus::Running, .historySequence = entry.sequence, .error = {}};
    switch (entry.outcome) {
    case Outcome::Pending:
    case Outcome::InProgress:
        report(index, result, &entry);
        return;
    case Outcome::Completed:
        result.status = StepStatus::Completed;
        context_.responses.insert_or_assign(step.label, entry.responseJson);
        report(index, result, &entry);
        post([this, index] { runStep(index + 1); });
        return;
    case Outcome::Cancelled:
        result.status = StepStatus::Cancelled;
        report(index, result, &entry);
        finish(SequenceOutcome::Stopped);
        return;
    case Outcome::Failed:
        result.status = StepStatus::Failed;
        report(index, result, &entry);
        if (step.continueOnError) {
            post([this, index] { runStep(index + 1); });
        } else {
            finish(SequenceOutcome::Failed);
        }
        return;
    }
}

} // namespace ptslgui
