#pragma once

#include <ptslgui/catalog.hpp>
#include <ptslgui/controller.hpp>
#include <ptslgui/history.hpp>

#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ptslgui {

/// One request in a sequence. The request is a JSON template that may contain {{placeholders}}.
struct SequenceStep {
    std::string label;
    std::string commandName;
    std::string requestTemplate;
    bool enabled = true;
    bool continueOnError = false;
};

struct Sequence {
    std::string name;
    /// User variables, available as {{vars.<name>}}, in definition order.
    std::vector<std::pair<std::string, std::string>> variables;
    std::vector<SequenceStep> steps;
};

/// Serialises a sequence (format version 1).
[[nodiscard]] std::string sequenceToJson(const Sequence& sequence);

/// Parses sequenceToJson() output; rejects other versions, invalid labels and duplicate labels.
[[nodiscard]] std::expected<Sequence, std::string> sequenceFromJson(std::string_view text);

/// Whether text is a valid step or variable name: a letter or '_' followed by letters, digits or '_'.
[[nodiscard]] bool isValidLabel(std::string_view label);

/// Returns base (made a valid label) or base_2, base_3, ... so that it is unique among the sequence's steps.
[[nodiscard]] std::string uniqueStepLabel(const Sequence& sequence, std::string_view base);

/// Whether a template contains any {{placeholder}}.
[[nodiscard]] bool hasPlaceholders(std::string_view requestTemplate);

/// Values available to placeholders: completed step responses (label -> response JSON) and variables.
struct SubstitutionContext {
    std::map<std::string, std::string, std::less<>> responses;
    std::vector<std::pair<std::string, std::string>> variables;
};

/// Replaces {{label.path}} and {{vars.name}} placeholders. A path is a chain of .field and [index] accessors.
/// A placeholder that is a whole JSON value (bare, or the whole content of a string literal) is replaced by the
/// JSON value itself; variables are parsed as JSON when possible, otherwise used as strings. Inside a longer
/// string literal the value is inserted as text (strings without quotes, other values as compact JSON).
[[nodiscard]] std::expected<std::string, std::string> substitute(std::string_view requestTemplate,
                                                                 const SubstitutionContext& context);

enum class StepStatus { Waiting, Skipped, Running, Completed, Failed, Cancelled };

struct StepResult {
    StepStatus status = StepStatus::Waiting;
    /// History sequence number once the request has been sent.
    std::optional<std::uint64_t> historySequence;
    /// Why the step failed before or without a response (substitution, validation, unknown command).
    std::string error;
};

enum class SequenceOutcome { Completed, Failed, Stopped };

[[nodiscard]] std::string_view toString(StepStatus status);

/// Runs a sequence's enabled steps in order through a RequestController, one at a time. A step that does not
/// complete stops the run unless it has continueOnError. Must be used on the controller's owning thread.
class SequenceRunner {
public:
    using StepCallback = std::function<void(std::size_t index, const StepResult& result, const HistoryEntry* entry)>;
    using FinishedCallback = std::function<void(SequenceOutcome outcome)>;

    SequenceRunner(const CommandCatalog& catalog, RequestController& controller, Dispatcher dispatcher = {});
    SequenceRunner(const SequenceRunner&) = delete;
    SequenceRunner& operator=(const SequenceRunner&) = delete;
    SequenceRunner(SequenceRunner&&) = delete;
    SequenceRunner& operator=(SequenceRunner&&) = delete;
    ~SequenceRunner();

    /// Starts a run; returns false if one is already running. Callbacks run on the owning thread.
    bool start(Sequence sequence, StepCallback onStep, FinishedCallback onFinished);

    /// Stops after the running step's final response (that response is still reported).
    void stop();

    [[nodiscard]] bool running() const { return running_; }

private:
    struct Alive;

    void runStep(std::size_t index);
    void onResponse(std::size_t index, const HistoryEntry& entry);
    void report(std::size_t index, const StepResult& result, const HistoryEntry* entry);
    void finish(SequenceOutcome outcome);
    void post(std::function<void()> task);

    const CommandCatalog& catalog_;
    RequestController& controller_;
    Dispatcher dispatcher_;
    Sequence sequence_;
    SubstitutionContext context_;
    StepCallback onStep_;
    FinishedCallback onFinished_;
    bool running_ = false;
    bool stopRequested_ = false;
    bool answered_ = false;
    std::uint64_t generation_ = 0;
    std::shared_ptr<Alive> alive_;
};

} // namespace ptslgui
