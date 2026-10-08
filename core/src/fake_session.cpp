#include <ptslgui/fake_session.hpp>

#include <utility>

namespace ptslgui {
namespace {

struct Delivery {
    RequestId id;
    ResponseSink sink;
    Response response;
};

void deliver(const std::vector<Delivery>& deliveries) {
    for (const auto& delivery : deliveries) {
        if (delivery.sink) {
            delivery.sink(delivery.id, delivery.response);
        }
    }
}

} // namespace

void FakePtslSession::script(int commandId, std::vector<Response> responses) {
    const std::scoped_lock lock(mutex_);
    scripts_[commandId] = std::move(responses);
}

void FakePtslSession::setConnectError(std::optional<std::string> error) {
    const std::scoped_lock lock(mutex_);
    connectError_ = std::move(error);
}

void FakePtslSession::setAutoDeliver(bool autoDeliver) {
    const std::scoped_lock lock(mutex_);
    autoDeliver_ = autoDeliver;
}

std::size_t FakePtslSession::deliverPending() {
    std::vector<Delivery> deliveries;
    {
        const std::scoped_lock lock(mutex_);
        for (auto& pending : pending_) {
            for (auto& response : pending.responses) {
                deliveries.push_back(Delivery{.id = pending.id, .sink = pending.sink, .response = std::move(response)});
            }
        }
        pending_.clear();
    }
    deliver(deliveries);
    return deliveries.size();
}

std::vector<SentRequest> FakePtslSession::sent() const {
    const std::scoped_lock lock(mutex_);
    return sent_;
}

ConnectionSettings FakePtslSession::lastSettings() const {
    const std::scoped_lock lock(mutex_);
    return settings_;
}

std::expected<void, std::string> FakePtslSession::connect(const ConnectionSettings& settings) {
    const std::scoped_lock lock(mutex_);
    settings_ = settings;
    if (connectError_) {
        state_ = ConnectionState::Disconnected;
        return std::unexpected(*connectError_);
    }
    state_ = ConnectionState::Connected;
    return {};
}

void FakePtslSession::disconnect() {
    cancelAll();
    const std::scoped_lock lock(mutex_);
    state_ = ConnectionState::Disconnected;
}

ConnectionState FakePtslSession::state() const {
    const std::scoped_lock lock(mutex_);
    return state_;
}

RequestId FakePtslSession::send(int commandId, std::string requestJson, ResponseSink sink) {
    RequestId id = 0;
    bool deliverNow = false;
    {
        const std::scoped_lock lock(mutex_);
        id = nextId_++;
        sent_.push_back(SentRequest{.id = id, .commandId = commandId, .requestJson = std::move(requestJson)});

        std::deque<Response> responses;
        if (state_ != ConnectionState::Connected) {
            responses.push_back(Response{.status = ResponseStatus::Failed,
                                         .progress = 0,
                                         .taskId = {},
                                         .bodyJson = {},
                                         .errorJson = R"({"message":"not connected"})"});
        } else if (const auto it = scripts_.find(commandId); it != scripts_.end()) {
            responses.assign(it->second.begin(), it->second.end());
        } else {
            responses.push_back(Response{
                .status = ResponseStatus::Completed, .progress = 100, .taskId = {}, .bodyJson = "{}", .errorJson = {}});
        }
        pending_.push_back(Pending{.id = id, .sink = std::move(sink), .responses = std::move(responses)});
        deliverNow = autoDeliver_;
    }
    if (deliverNow) {
        deliverPending();
    }
    return id;
}

void FakePtslSession::cancelAll() {
    std::vector<Delivery> deliveries;
    {
        const std::scoped_lock lock(mutex_);
        for (auto& pending : pending_) {
            deliveries.push_back(Delivery{.id = pending.id,
                                          .sink = std::move(pending.sink),
                                          .response = Response{.status = ResponseStatus::Cancelled,
                                                               .progress = 0,
                                                               .taskId = {},
                                                               .bodyJson = {},
                                                               .errorJson = {}}});
        }
        pending_.clear();
    }
    deliver(deliveries);
}

} // namespace ptslgui
