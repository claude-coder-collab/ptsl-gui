#include <ptslgui/protocol.hpp>
#include <ptslgui/sdk_session.hpp>

#include <PTSLC_CPP/CppPTSLClient.h>
#include <PTSLC_CPP/CppPTSLRequest.h>
#include <PTSLC_CPP/CppPTSLResponse.h>
#include <atomic>
#include <format>
#include <list>
#include <mutex>
#include <thread>

namespace ptslgui {
namespace {

namespace sdk = PTSLC_CPP;

class Client final : public sdk::CppPTSLClient {
public:
    using CppPTSLClient::CppPTSLClient;

    void refreshHostReady() { HostReadyCheck(); }
};

Response toResponse(const sdk::CppPTSLResponse& response) {
    return Response{
        .status = protocol::statusFromTaskStatus(static_cast<int>(response.GetStatus())),
        .progress = response.GetProgress(),
        .taskId = response.GetTaskId(),
        .bodyJson = response.GetResponseBodyJson(),
        .errorJson = response.GetResponseErrorJson(),
    };
}

std::string errorText(const sdk::CppPTSLResponse& response) {
    std::string text;
    for (const auto& error : protocol::errorsFromJson(response.GetResponseErrorJson())) {
        if (!text.empty()) {
            text += "; ";
        }
        text += error.message.empty() ? error.type : error.message;
    }
    return text.empty() ? std::string("no response") : text;
}

std::string errorJson(std::string_view message) {
    return std::format(R"({{"errors":[{{"command_error_type":"PTSLGUI_Error","command_error_message":"{}"}}]}})",
                       message);
}

struct Waiter {
    std::shared_ptr<std::atomic<bool>> done;
    std::jthread thread;
};

} // namespace

struct SdkPtslSession::Impl {
    SdkSessionOptions options;
    std::mutex connectMutex;
    mutable std::mutex mutex;
    std::shared_ptr<Client> client;
    std::list<Waiter> waiters;
    std::atomic<RequestId> nextId{1};
    std::atomic<std::uint64_t> cancelGeneration{0};

    void pruneWaiters() {
        waiters.remove_if([](const Waiter& waiter) { return waiter.done->load(); });
    }

    std::pair<std::shared_ptr<Client>, std::list<Waiter>> takeAll() {
        const std::scoped_lock lock(mutex);
        return {std::exchange(client, nullptr), std::exchange(waiters, {})};
    }
};

SdkPtslSession::SdkPtslSession(SdkSessionOptions options) : impl_(std::make_unique<Impl>()) {
    impl_->options = options;
}

SdkPtslSession::~SdkPtslSession() {
    disconnect();
}

std::expected<void, std::string> SdkPtslSession::connect(const ConnectionSettings& settings) {
    const std::scoped_lock connectLock(impl_->connectMutex);
    disconnect();

    const sdk::ClientConfig config{
        .address = settings.address,
        .serverMode = sdk::Mode::Mode_ProTools,
        .skipHostLaunch = settings.launchHost ? sdk::SkipHostLaunch::SHLaunch_No : sdk::SkipHostLaunch::SHLaunch_Yes,
    };
    std::shared_ptr<Client> client;
    try {
        client = std::make_shared<Client>(config);
    } catch (const std::exception& error) {
        return std::unexpected(std::format("cannot create PTSL client: {}", error.what()));
    }

    const auto timeout = settings.launchHost ? impl_->options.launchReadyTimeout : impl_->options.readyTimeout;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        const auto response = client->SendRequest(sdk::CppPTSLRequest(sdk::CommandId::CId_HostReadyCheck)).get();
        const bool completed = response.GetStatus() == sdk::CommandStatusType::TStatus_Completed;
        const auto ready = protocol::hostReadyFromResponse(response.GetResponseBodyJson());
        if (completed && (response.GetResponseBodyJson().empty() || ready.value_or(false))) {
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::unexpected(
                std::format("Pro Tools is not ready at {}: {}", settings.address,
                            completed ? std::string("host reports not ready") : errorText(response)));
        }
        std::this_thread::sleep_for(impl_->options.pollInterval);
    }
    client->refreshHostReady();

    const auto registration = client
                                  ->SendRequest(sdk::CppPTSLRequest(
                                      sdk::CommandId::CId_RegisterConnection,
                                      protocol::registerConnectionBody(settings.companyName, settings.applicationName)))
                                  .get();
    if (registration.GetStatus() != sdk::CommandStatusType::TStatus_Completed) {
        return std::unexpected(std::format("RegisterConnection failed: {}", errorText(registration)));
    }

    const std::scoped_lock lock(impl_->mutex);
    impl_->client = std::move(client);
    return {};
}

void SdkPtslSession::disconnect() {
    auto [client, waiters] = impl_->takeAll();
    if (client) {
        client->CancelRequests(true);
    }
    waiters.clear();
}

ConnectionState SdkPtslSession::state() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->client ? ConnectionState::Connected : ConnectionState::Disconnected;
}

RequestId SdkPtslSession::send(int commandId, std::string requestJson, ResponseSink sink) {
    const RequestId id = impl_->nextId++;
    std::unique_lock lock(impl_->mutex);
    if (!impl_->client) {
        lock.unlock();
        sink(id, Response{.status = ResponseStatus::Failed,
                          .progress = 0,
                          .taskId = {},
                          .bodyJson = {},
                          .errorJson = errorJson("not connected")});
        return id;
    }
    impl_->pruneWaiters();

    auto future = impl_->client->SendRequest(sdk::CppPTSLRequest(static_cast<sdk::CommandId>(commandId), requestJson),
                                             [id, sink](const sdk::CppPTSLResponse& response) {
                                                 const auto converted = toResponse(response);
                                                 if (!converted.isFinal()) {
                                                     sink(id, converted);
                                                 }
                                             });

    const auto done = std::make_shared<std::atomic<bool>>(false);
    const std::uint64_t generation = impl_->cancelGeneration.load();
    impl_->waiters.push_back(Waiter{
        .done = done,
        .thread =
            std::jthread([this, id, generation, done, sink = std::move(sink), future = std::move(future)] mutable {
                Response response;
                try {
                    response = toResponse(future.get());
                    if (!response.isFinal()) {
                        response.status = ResponseStatus::Failed;
                    }
                } catch (const std::exception& error) {
                    response = Response{.status = ResponseStatus::Failed,
                                        .progress = 0,
                                        .taskId = {},
                                        .bodyJson = {},
                                        .errorJson = errorJson(error.what())};
                }
                if (response.status == ResponseStatus::Failed && impl_->cancelGeneration.load() != generation) {
                    response.status = ResponseStatus::Cancelled;
                }
                sink(id, response);
                done->store(true);
            }),
    });
    return id;
}

void SdkPtslSession::cancelAll() {
    std::shared_ptr<Client> client;
    {
        const std::scoped_lock lock(impl_->mutex);
        client = impl_->client;
        ++impl_->cancelGeneration;
    }
    if (client) {
        client->CancelRequests(true);
    }
}

} // namespace ptslgui
