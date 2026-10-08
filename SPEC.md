# PTSL GUI – Specification

## Purpose

A C++ desktop application that wraps every command in the Avid PTSL (Pro Tools Scripting Library) C++ SDK. A user can pick any PTSL command, fill in its request through a generated form (or raw JSON), send it to a running Pro Tools, and inspect the response. No code required.

## Non-goals

- Not a Pro Tools replacement or a mixing/editing UI. One command at a time, plus history/replay.
- No command sequences, scripting or macro recorder in v1 (planned for v2, see Future).
- Only the latest PTSL protocol (the `PTSL.proto` shipped with the newest SDK) is targeted. Older protocol snapshots are ignored.
- No hand-written UI per command. Forms are generated from the protocol definition, so new SDK versions need no UI code changes.

## Constraints

- C++26, limited to what GCC, Clang and MSVC all support. CMake + Ninja Multi-Config. Strict warnings, clang-format (repo `.clang-format`; there is no `~/.clang-format` on this machine), clang-tidy, ASan/UBSan/TSan builds.
- GUI: Qt 6 Widgets.
- Tests: Catch2. No test requires Pro Tools; the PTSL client is behind an interface and faked.
- Platform: macOS 13.3 or later, Apple Silicon (arm64) only. The distributable app, the SDK framework and every bundled library are built with a 13.3 deployment target (13.3 rather than 13.0 because libc++'s floating-point `std::format`/`to_chars` needs the macOS 13.3 runtime). No Intel (x86_64) or universal binaries; the SDK, the app and all dependencies are built for arm64. No Windows or Linux app, installer or SDK build support. The only non-Mac build is the SDK-independent core + tests on Linux/GCC in CI, kept as a check that the code stays within the GCC/Clang/MSVC-portable C++26 subset; it is not a supported platform and Mac-specific code is allowed outside `core/`.
- The PTSL SDK (`PTSL_SDK_CPP.*/`) is Avid-confidential. It lives in the project folder, is git-ignored, and must never be pushed. Nothing derived verbatim from it (proto files, generated code, the generated catalog, copied doc comments or examples) may be committed either; all of that is produced at build time from the local SDK copy.
- Personal use only: no distribution, no notarisation, ad-hoc code signing only.

## Licensing

- Project code: MIT (`LICENSE`).
- Qt 6 (Widgets, Core, Gui): used under LGPLv3, dynamically linked (frameworks bundled by `macdeployqt`, replaceable by the user).
- gRPC / protobuf / other SDK dependencies: their own permissive licences (Apache-2.0 / BSD).
- PTSL SDK: Avid SDK licence, confidential, never redistributed. The public repo contains only code that uses the SDK; building requires the user's own SDK copy.

## SDK facts this design relies on

- `PTSLC_CPP::CppPTSLClient(ClientConfig{address, Mode, SkipHostLaunch})`.
- The current API is JSON-based: `SendRequest(CppPTSLRequest{CommandId, requestBodyJson}, callback) -> std::future<CppPTSLResponse>`. The callback receives intermediate (progress) and final responses on a service thread. `CancelRequests()` aborts in-flight requests. Per-command typed methods are deprecated and not used.
- `CppPTSLResponse` exposes status, progress, task id, response body JSON, error JSON and parsed `ResponseError`, and version info.
- `RegisterConnection` must be sent first; the client then stores the session id.
- `Source/PTSL.proto` (package `ptsl`) defines the `CommandId` enum (deprecated aliases with `allow_alias`), all `*RequestBody`/`*ResponseBody` messages, and enums. Doxygen comments on each `CId_*` value carry: description, `@request_body_type`, `@response_body_type`, `@request_body_json_example`, `@since`, `@deprecated`, and `@category_*` (editing, events, export, general, io_setup, session_file, session_read, session_write, time_utility, transport, utility).

## Architecture

```
PTSL.proto ──► tools/gen_catalog.py (build time) ──► catalog.json  ─┐
PTSL.proto ─────────────────────────────────────────────────────────┤ loaded at runtime
                                                                    ▼
┌────────────── app (Qt Widgets) ──────┐
│ MainWindow                           │
│  ├ CommandBrowser   (catalog view)   │
│  ├ RequestEditor    (form ⇄ JSON)    │
│  ├ ResponseView     (tree + raw)     │
│  └ HistoryPanel                      │
└──────────────┬───────────────────────┘
               │ GUI thread only
┌──────────────▼───────────────────────┐
│ ptslgui_core (no Qt)                 │
│  CommandCatalog   ProtoSchema        │
│  RequestController  History  Version │
│  IPtslSession ◄── FakePtslSession    │
└──────────────▲───────────────────────┘
               │
       SdkPtslSession (ptslgui_sdk, over CppPTSLClient)
```

No protobuf code is generated for the GUI. `PTSL.proto` is parsed at runtime by protobuf's own parser into a private `DescriptorPool`, so:

- the same core code works with the real proto locally and the fixture proto in CI;
- the GUI's protobuf never registers `ptsl` types in the generated pool, so it cannot clash with the protobuf copy compiled into the SDK framework, and its version does not need to match the SDK's.

### Libraries/targets

| Target | Contents | Depends on |
|---|---|---|
| `ptslgui_core` (`core/`) | Catalog, schema, request controller, history, version, session interface, fake session | protobuf (libprotobuf + libprotoc), nlohmann_json; both private |
| `ptslgui_sdk` (`sdk/`) | `SdkPtslSession` adapter over `CppPTSLClient` | core, `PTSLC_CPP::PTSLC_CPP` (private) |
| `ptslgui_ui` (`ui/`) | Qt Widgets: `MainWindow`, `CommandBrowser`, `RequestEditor`, form editors, `ResponseView`, `HistoryPanel`, `PreferencesDialog`, `AppSettings`; no SDK dependency | core, Qt6::Widgets, Qt6::Concurrent |
| `ptslgui_app` (`app/`) | `PTSL GUI.app` bundle: `main.cpp`, embedded proto + catalog, SDK framework | ui, sdk |
| `ptslgui_core_tests`, `ptslgui_ui_tests`, `ptslgui_sdk_tests` (`tests/`) | Catch2 tests | core / ui / sdk, Catch2, Qt6::Test |

### Core API (`core/include/ptslgui/`)

Namespace `ptslgui`. Errors are returned as `std::expected<T, std::string>`; no exceptions cross the API.

- `version.hpp` — `Version{year, minor, revision}`, `parse("2025.10" | "2024.6.1")` (strict), `toString()` (omits zero revision), ordered with `<=>`.
- `catalog.hpp` — `CommandCatalog::fromJson(text)` loads schema version 1 (rejects others). `commands()`, `categories()`, `findById`, `findByName` (accepts `CId_X`, `X`, or a deprecated alias), `search(CommandFilter{text, category, includeDeprecated})` (case-insensitive; every whitespace-separated term must appear in name, display name or description). `CommandInfo` mirrors the catalog entry plus `hasCategory`, `isMutating()` (session_write, session_file, editing, export) and `isUnsupportedBy(hostVersion)` (host older than `since`).
- `schema.hpp` — `ProtoSchema::fromProtoText(text, fileName)` (move-only, pimpl). `message(name)` returns a `MessageSpec{fullName, name, fields, oneofs, comment}`; names may be fully qualified or package-relative. `FieldSpec{name, jsonName, number, kind, repeated, isMap, hasPresence, oneof, typeName, enumValues, mapEntry[key, value], comment}` where `kind` is Bool/Int32/Int64/UInt32/UInt64/Float/Double/String/Bytes/Enum/Message; synthetic proto3 `optional` oneofs are not reported as oneofs; comments come from the proto's leading/trailing comments. Nested message fields are described by `typeName` and looked up on demand (handles recursive types). `enumValues(name)`, `messageNames()`. `normalizeJson(message, json, JsonFormat{pretty})` parses with protobuf's JSON parser (accepts proto or JSON field names, enum names or numbers, int64 as strings) and prints with proto field names, omitting default values; blank input is `{}`; unknown fields and bad values are errors.
- `session.hpp` — `IPtslSession` (non-copyable): `connect(ConnectionSettings{address = "localhost:31416", companyName, applicationName, launchHost})`, `disconnect()`, `state()` (Disconnected/Connected), `send(commandId, requestJson, ResponseSink) -> RequestId`, `cancelAll()`. `Response{status: InProgress|Completed|Failed|Cancelled, progress, taskId, bodyJson, errorJson}`; every request gets exactly one final (non-InProgress) response; sinks may be called from any thread.
- `fake_session.hpp` — `FakePtslSession`, thread-safe. `script(commandId, responses)`; unscripted commands complete with `{}`; requests while disconnected fail with `{"message":"not connected"}`. Responses are queued until `deliverPending()` unless `setAutoDeliver(true)`. `setConnectError`, `sent()`, `lastSettings()`. `cancelAll()`/`disconnect()` deliver Cancelled to queued requests. Used by tests and the future demo mode.
- `history.hpp` — `History(maxEntries = 1000)`, not thread-safe. `add` returns a sequence number; `apply(sequence, response, receivedAt)` updates outcome/progress/task id, keeps the last non-empty body and error, and sets `duration` on the final response; oldest entries are evicted. `toJson()` / `loadJson()` (format version 1: `{"version":1,"entries":[{sequence, command_id, command_name, request, sent_at_ms, duration_ms|null, outcome, progress, task_id, response, error}]}`; request/response kept as raw JSON strings). A failed load leaves the history unchanged.
- `protocol.hpp` — SDK-independent protocol knowledge: command names `CId_HostReadyCheck`, `CId_RegisterConnection`, `CId_GetPTSLVersion`; `statusFromTaskStatus(int)` (TStatus Queued/Pending/InProgress/WaitingForUserInput → InProgress; Completed/CompletedWithBadResponse → Completed; anything else, including the SDK's NoResponseReceived −1 → Failed); `versionFromResponse` (GetPTSLVersion body `version`/`version_minor`/`version_revision`); `hostReadyFromResponse` (`is_host_ready`, missing = false); `registerConnectionBody(company, application)`; `errorsFromJson` (accepts `{"errors":[{command_error_type, command_error_message, is_warning}]}`, a single error object, `{"message": …}`, or arbitrary text as one error).
- `sample.hpp` — `sampleJson(schema, message, SampleOptions{seed, maxDepth = 3, maxElements = 3})`: deterministic random JSON that `normalizeJson` accepts, covering every field kind (int64/uint64 as strings, floats as multiples of 0.25, bytes as base64, enums by name, Unicode/escaped strings), oneofs (one member at most), optional presence, repeated fields and maps; nested messages beyond `maxDepth` are left out; unknown types give `{}`. Used for round-trip tests.
- `controller.hpp` — `RequestController(catalog, schema, session, history, Dispatcher, Clock)`. `prepare(commandId, json)`: commands with a request type are normalised against it; commands without one accept only blank or `{}` and send an empty body. `send(...)` refuses when disconnected or invalid (nothing recorded), otherwise records history and sends. Session callbacks are re-posted through the `Dispatcher` (Qt: queued invoke onto the GUI thread; default: inline), so history and callbacks are only touched on the owning thread. Responses arriving after the controller is destroyed are ignored.

### Command catalog (build time)

`tools/gen_catalog.py --proto <PTSL.proto> --output <catalog.json> [--strict] [--quiet]` parses the doc comments of the `CommandId` enum and writes a JSON catalog. The output file is only rewritten when its content changes (keeps CMake incremental builds quiet). Validation problems are printed as warnings; `--strict` makes them fatal.

Parsing rules:

- The enum body is tokenised; each value takes the `/** ... */` comment immediately preceding it (a comment never leaks to a later value). `option` and `reserved` statements and plain `//` comments are skipped.
- Only `CId_*` values become commands; negative values (`CId_None`) are dropped. Other names with the same number are recorded as `deprecated_aliases` of that command.
- Free text before the first structured tag is the description. `@brief`/`@details` text joins the description. `@note` paragraphs and `@sa` ("See also: …") go to `notes`. Lines starting with `TODO` are dropped. Doxygen list items (`-`, `-#` nested) are kept as Markdown-style list lines.
- `@request_body_type` / `@response_body_type` set the body types. If a type is absent, or names a message that does not exist (the SDK has a typo), the generator falls back to `<Name>RequestBody` / `<Name>ResponseBody` when that message exists, and records the attribute in `inferred_types`.
- `@request_body_json_example` / `@response_body_json_example` … `@end_example` produce examples. Free text after a structured tag and before an example is that example's `caption`. Lines starting with `#` inside an example are `comments`. Tabs become two spaces. Invalid JSON is repaired if possible (trailing commas, missing commas between lines) and flagged `repaired`; otherwise kept with `valid: false`.
- `@category_*` tags become `categories` (`all` omitted). `@since` keeps only the version number (`2022.12`). `@deprecated` text goes to `deprecated`.
- Display names split CamelCase while keeping acronyms (`Get PTSL Version`, `Get Session IDs`).

Output (schema version 1):

```json
{
  "schema_version": 1,
  "source": { "proto": "PTSL.proto", "sha256": "…" },
  "categories": ["editing", "export", "…"],
  "commands": [
    {
      "id": 3, "name": "CId_GetTrackList", "display_name": "Get Track List",
      "description": "…", "notes": ["…"], "categories": ["session_read"],
      "request_type": "GetTrackListRequestBody", "response_type": "GetTrackListResponseBody",
      "request_examples": [{ "text": "{…}", "caption": "", "comments": [], "valid": true, "repaired": false }],
      "response_examples": [],
      "since": "2022.12", "deprecated": null,
      "deprecated_aliases": ["GetTrackList"], "inferred_types": []
    }
  ]
}
```

Against SDK 2026.04: 153 commands, 10 categories, none deprecated, 5 examples repaired, 2 response examples unrepairable (placeholder `...` / malformed). The catalog is generated into the build tree and never committed.

Tests use `tests/fixtures/proto/fixture.proto`, a hand-written protocol in the same style (no SDK content). It also serves as the stand-in proto for SDK-independent C++ tests and CI, so it includes messages covering scalars, enums, nested, repeated, oneof, map, optional and bytes fields.

### SDK session (`sdk/`)

`SdkPtslSession(SdkSessionOptions{readyTimeout = 3 s, launchReadyTimeout = 120 s, pollInterval = 500 ms})` implements `IPtslSession` over `PTSLC_CPP::CppPTSLClient` (headers included as `<PTSLC_CPP/…>`). Thread-safe.

SDK behaviour it relies on: the client constructor launches Pro Tools if asked (`SkipHostLaunch::SHLaunch_No`, via `open -g -a 'Pro Tools'`) and runs one `HostReadyCheck`; until that succeeds the client answers every other command with a "host not ready" error. `HostReadyCheck()` is protected, so a private subclass exposes it to re-run the check. `SendRequest(request, callback)` streams intermediate and final responses to the callback on an SDK thread and returns a `std::future` with the final response (or an SDK-generated error response on gRPC failure, without a callback). The client stores the session id from the `RegisterConnection` response itself.

- `connect` (blocking; serialised by a mutex; disconnects first): create the client (`Mode_ProTools`); poll `CId_HostReadyCheck` until Completed with `is_host_ready` true (or an empty body) or the timeout (`launchReadyTimeout` when launching) expires; re-run the protected `HostReadyCheck()`; send `CId_RegisterConnection` with `{company_name, application_name}`; require Completed. Errors are returned as text, e.g. `Pro Tools is not ready at <address>: <error>`.
- `send`: if disconnected, the sink immediately gets Failed with `{"errors":[{"command_error_type":"PTSLGUI_Error","command_error_message":"not connected"}]}` (outside the lock). Otherwise `SendRequest(CppPTSLRequest(commandId, json), callback)`: the callback forwards only non-final (InProgress) responses; a per-request waiter `std::jthread` waits on the future and delivers the single final response (an InProgress final is turned into Failed; exceptions become Failed). Finished waiters are pruned on each send.
- `cancelAll`: bumps a cancel generation and calls `CancelRequests(true)`; requests sent before the cancel whose final response is Failed are reported as Cancelled.
- `disconnect` / destructor: take the client and waiters, `CancelRequests(true)`, join waiters, release the client.
- The framework exports no protobuf symbols, and a test confirms the runtime schema (Homebrew protobuf) and the SDK client work in one process.

## UI

### Main window

`MainWindow(catalog, schema, session, QSettings&)` owns the `History` and a `RequestController` whose dispatcher queues tasks onto the GUI thread (`QMetaObject::invokeMethod(this, task, Qt::QueuedConnection)`). Child widgets have stable object names (in parentheses) used by tests.

- **Toolbar** (`connectionToolbar`): address field (`addressEdit`, default `localhost:31416`), "Launch Pro Tools" checkbox (`launchCheck`), Connect/Disconnect (`connectAction`), Cancel all (`cancelAction`, calls `session.cancelAll()`), status label (`connectionStatus`: "Disconnected" / "Connecting…" / "Connected" / "Connected · PTSL <version>").
  - Connect runs `session.connect` on a worker thread (`QtConcurrent::run` + `QFutureWatcher`); address and launch inputs are disabled while connecting or connected. On failure the error goes to the status bar. On success `CId_GetPTSLVersion` is sent through the controller; the parsed version is shown and passed to the browser. Disconnect is synchronous and clears the host version.
- **Command browser** (left, `CommandBrowser`): search field (`commandSearch`), category combo (`categoryFilter`: "All categories" + catalog categories), tree (`commandTree`). With no search text and no category, commands are grouped under category headings (a command appears under each of its categories; uncategorised ones under "Other"); otherwise a flat filtered list. Sorted alphabetically. Category labels: underscores to spaces, first letter capitalised, `io` → `IO`. Tooltip shows the enum name; deprecated commands are struck through; commands newer than the connected host are greyed with "Requires Pro Tools <since>". The command id is stored in item data role `Qt::UserRole + 1`.
- **Request editor** (centre, `RequestEditor(schema, validator)`): title (`commandTitle`), documentation (`commandInfo`, Markdown: description, notes as quotes, then enum name · request/response types · since · categories · "Modifies the session" for mutating commands, and any deprecation), example combo (`exampleSelector`, caption or "Example N", "(invalid JSON)" marked) + "Load example" (`loadExample`), tabs (`requestTabs`): **Form** (index 0, default; scroll area `requestFormArea` holding the generated `MessageEditor` named `requestForm`, or a "takes no request body" label) and **JSON** (index 1, monospace editor `requestEditor`), validation label (`validationLabel`, validated 250 ms after typing via `RequestController::prepare`: "✓ Valid request" or the error), Format (`formatButton`, pretty-prints via `ProtoSchema::normalizeJson`), Send (`sendButton`, also Ctrl/Cmd+Return; enabled only while connected and a command is selected). Selecting a command rebuilds the form and resets the JSON to `{}`, or disables both for commands with no request body.
  - Sync: the JSON text is what gets validated and sent. Every form edit rewrites the JSON text (`ordered_json::dump(2)`, field order, defaults omitted). Switching to the Form tab, loading an example, or `setRequestText` while the Form tab is shown normalises the JSON against the request type and loads it into the form; if it does not validate, the editor stays on (or switches to) the JSON tab and shows "Fix the JSON before switching to the form: <error>".
- **Menus**: File (Import History…, Export History…, Quit), Request (Send, Format JSON, checkable "Ask Before Modifying the Session" `confirmAction`, Preferences… with the macOS Preferences role and Cmd+,), View (show/hide History), Help (About).
- **Response view** (right, `ResponseView`): "Copy JSON" button (`copyResponse`, copies the pretty body; disabled without a body), outcome (`responseStatus`: "Sent, waiting for a response…" / In progress / Completed / Failed / Cancelled), progress bar (`responseProgress`, visible while pending/in progress), meta line (`responseMeta`: command name · task id · duration ms), tabs (`responseTabs`): Tree (`responseTree`, Field/Value columns, objects and arrays summarised as `{n fields}` / `[n items]`, expanded two levels), JSON (`responseRaw`, pretty-printed), Errors (`responseErrors`, one line per error `Error|Warning [type]: message` followed by the raw error JSON; the tab title shows the count and it is selected when there is no body).
- Sending a command newer than the connected host shows a status-bar warning but still sends. The response view shows the most recently sent request, or the entry selected in History.
- **History dock** (`historyDock`, bottom, 160 px initially; `HistoryPanel`): list `historyList` (columns #, Time HH:mm:ss with ISO tooltip, Command without `CId_`, Outcome with "In progress N%", Duration ms), newest first, including the automatic GetPTSLVersion. Selecting an entry shows its response. Load (`historyLoad`, or double-click) selects the command and puts the pretty-printed request in the editor. Resend (`historyResend`) sends the stored request again (confirmation rules apply). Import… / Export… (`historyImport` / `historyExport`) use the `History` JSON format via file dialogs (`MainWindow::importHistory` / `exportHistory(path)`; export writes atomically with `QSaveFile`; a failed import leaves the history unchanged). Clear (`historyClear`) empties history and the response view.

### Confirmations

- Commands for which `CommandInfo::isMutating()` is true (session_file, session_write, editing, export categories — this includes Close Session) ask "<Command> modifies the Pro Tools session. Send it?" with Send / Cancel and a "Don't ask again" checkbox, before sending from the editor or resending from history. Cancel shows "<Command> was not sent" in the status bar.
- **User setting** "Ask before sending commands that modify the session" (`safety/confirmMutating`, default on). When off, no command is ever confirmed. It can be changed in three places that stay in sync: Preferences, the Request menu's checkable "Ask Before Modifying the Session", and "Don't ask again" in the dialog (which turns it off).
- The dialog is replaceable via `MainWindow::setConfirmHandler(bool(const CommandInfo&, bool& dontAskAgain))` for tests.

### Preferences and persistence

`AppSettings` wraps `QSettings` (the app uses the default `QSettings`, organisation/application "PTSL GUI"; tests use an INI file in a temporary directory). Keys:

| Key | Meaning | Default |
|---|---|---|
| `safety/confirmMutating` | ask before mutating commands | true |
| `requests/remember` | restore the last request per command | true |
| `lastRequests/<CId_Name>` | last request JSON sent from the editor for that command | — |
| `connection/address`, `connection/launchHost` | toolbar fields (saved on connect and on close) | `localhost:31416`, false |
| `ui/lastCommand` | last selected command, reselected at startup | — |
| `window/geometry`, `window/state`, `window/splitter` | layout (saved on close) | — |

- Selecting a command restores its saved request when remembering is on and the saved JSON still validates; otherwise the editor starts at `{}`.
- `PreferencesDialog` (`preferencesDialog`): checkboxes `confirmMutatingCheck` and `rememberRequestsCheck` (written on OK, discarded on Cancel) and "Forget saved requests" (`clearRequestsButton`, immediate).



### Generated forms (`ui/src/form_editor.*`)

Every editor is a `FieldEditor` (QWidget) with `setJson(value)`, `reset()`, `json()` (protobuf JSON mapping, `nlohmann::ordered_json`), `isDefault()` and a `changed()` signal. `createFieldEditor(schema, field)` handles the field's label (map / repeated / singular message); `createValueEditor` builds an editor for one value.

| Field | Editor | JSON / default |
|---|---|---|
| bool | checkbox | `true`/`false`; default false |
| int32, uint32 | line edit, digits (and leading `-` for signed) only, placeholder 0 | number; default empty or 0 |
| int64, uint64 | same | string (protobuf JSON); default empty or 0 |
| float, double | line edit, numeric validator | number; default empty or 0 |
| string | line edit; fields whose name contains path/location/folder/directory get a "…" browse button (directory picker for folder/directory/location, file picker otherwise) | string; default empty |
| bytes | line edit, placeholder "base64" | string |
| enum | combo of value names, the 0 value labelled "(default)"; combos size to 12 characters minimum rather than their longest item, so long enum names do not force horizontal scrolling | name; default the 0 value |
| singular message | checkable group (`messageGroup`, titled with the type name, drawn flat while unchecked); the nested `MessageEditor` is created when first checked, so recursive types are safe | object (possibly `{}`) when checked |
| repeated | list of value editors, each with a "−" remove button; "Add <type>" button; message elements in numbered group boxes | array; default empty |
| map | rows key → value with remove buttons; "Add entry" | object keyed by the key's string form (`mapKeyString`) |
| proto3 `optional` scalar | checkbox `set_<field>` enabling the value editor | included only when ticked, even if default |
| oneof | row labelled with the oneof name (italic): combo `oneof_<name>` with "(none)" + member names, and a stacked editor created on first selection | the selected member, always included (even at its default) |

`MessageEditor` lays fields out in a form layout (label = proto field name), names each field editor `field_<name>`, and sets tooltips to `name (type)` plus the proto comment. Its `json()` omits fields at their defaults (matching `normalizeJson`), so `normalizeJson(form.json()) == normalizeJson(input)` for any valid input. `setJson` accepts proto or JSON field names; absent fields are reset.

## Build

- CMake ≥ 3.28; on macOS `CMAKE_OSX_ARCHITECTURES` is fixed to `arm64`. C++26 (`CMAKE_CXX_STANDARD 26`), Ninja Multi-Config.
- Two dependency sources:
  - Development presets (`dev`, `asan`, `tsan`, `tidy`): Homebrew protobuf, nlohmann_json, Catch2 and Qt, built for the running macOS. Fast to set up; binaries only run on this machine's macOS.
  - `dist` preset: `conanfile.py` (protobuf 6.33.5, nlohmann_json 3.12.0, Catch2 3.16.0; CMakeDeps + CMakeToolchain without user presets) installed with profile `tools/conan/macos13` (`os.version=13.3`, armv8, apple-clang 21, libc++, cppstd 20, Release, static libraries) into `build/conan-dist`, using the Conan cache `~/.ptslconan`; and the official Qt 6.11.3 binaries (minimum macOS 13.0, universal) from aqtinstall in `~/Qt`. Homebrew's Qt cannot be used because its frameworks require macOS 14, and its protobuf/abseil require the running macOS.
- Dependencies found with `find_package`: protobuf (config package, falling back to CMake's FindProtobuf), nlohmann_json ≥ 3.11, Catch2 3, Python 3 (catalog generation). Locally from Homebrew; in CI from Ubuntu packages.
- Options: `PTSLGUI_BUILD_TESTS` (ON), `PTSLGUI_WARNINGS_AS_ERRORS` (OFF), `PTSLGUI_CLANG_TIDY` (OFF), `PTSLGUI_SANITIZE` (comma-separated: address, undefined, thread).
- `ptslgui_configure_target()` (`cmake/PtslGuiOptions.cmake`) applies strict warnings (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast …`) and sanitizer flags to project targets only.
- `cmake/PtslGuiSdk.cmake`: `PTSL_SDK_DIR` cache variable, defaulting to the newest `PTSL_SDK_CPP.*` folder in the project root (empty when absent; the `ci` preset forces it empty). Sets `PTSLGUI_SDK_PROTO` when the SDK proto exists. `ptslgui_generate_catalog(proto output)` adds a build step running `gen_catalog.py`.
- Presets (`CMakePresets.json`, build dirs `build/<preset>`): `dev`, `asan` (address+undefined), `tsan`, `tidy` (clang-tidy during compile), `ci` (no SDK, warnings as errors), `dist` (Release only; Conan toolchain `build/conan-dist/conan_toolchain.cmake`, Qt from `$HOME/Qt/6.11.3/macos`, deployment target 13.3). Build presets use Debug (plus `dev-release`). Test presets set `ASAN_OPTIONS=detect_container_overflow=0:detect_leaks=0` (container-overflow false positives from uninstrumented Homebrew Catch2; LeakSanitizer is unsupported on macOS arm64) and `halt_on_error` for UBSan/TSan.
- clang-tidy config in `.clang-tidy` (broad check set; noisy checks disabled); `tests/.clang-tidy` additionally disables `bugprone-unchecked-optional-access` (Catch2 `REQUIRE` guards are invisible to it) and static-initialisation checks.
- `PTSLGUI_BUILD_UI` (default ON on Apple): finds Qt 6 (Widgets, Concurrent, Test), builds `ptslgui_ui` and its tests. When the SDK client framework is also found (`find_package(PTSLC_CPP CONFIG)` in `PTSL_SDK_CPP.*/install/arm64/Release/PTSLC_CPP`, i.e. after `tools/build_sdk.py`), it builds `ptslgui_sdk`, its tests and the app; otherwise the app is skipped with a message.
- App bundle `PTSL GUI.app` (`build/<preset>/app/<config>/`): bundle id `io.github.claude-coder-collab.ptsl-gui`; the SDK's `PTSL.proto` and the generated catalog are embedded as Qt resources `:/ptsl/PTSL.proto` and `:/ptsl/catalog.json` (generated into the build tree, never committed); post-build `ditto` copies `PTSLC_CPP.framework` into `Contents/Frameworks` (rpath `@executable_path/../Frameworks`) and the bundle is ad-hoc signed (`codesign --force --deep --sign -`). Qt and protobuf are linked from Homebrew by absolute path; a self-contained bundle via `macdeployqt` is milestone 6. No Developer ID signing or notarisation.
- `tools/package_app.py [--skip-build] [--skip-tests] [--output dist]`: builds the `dist` preset (installing Qt 6.11.3 with `aqt install-qt mac desktop 6.11.3 clang_64 --outputdir ~/Qt` and the Conan dependencies if needed), runs its tests, copies the app to `dist/PTSL GUI.app` (git-ignored), runs the official Qt's `macdeployqt`, thins universal binaries to arm64 (`lipo -thin`), removes plugins whose `@rpath` dependencies were not deployed, and fails if any other binary has undeployed dependencies, any Mach-O file references `/opt/homebrew` or `/usr/local` (ignoring a library's own install name), any binary's minimum macOS (`LC_BUILD_VERSION minos` / `LC_VERSION_MIN_MACOSX`, highest across architectures) is above 13.3, or `Info.plist` `LSMinimumSystemVersion` is not 13.3. It then ad-hoc signs and verifies the signature. **The bundle embeds the SDK proto and framework: personal use only, never publish it.**
- `--demo` runs the app with a `FakePtslSession` (auto-delivering; GetPTSLVersion answers 2026.4; every other command completes with `{}`).
- clang-tidy runs with `--warnings-as-errors=*`; `cppcoreguidelines-owning-memory` is disabled because Qt parents own their children.

### Running

```sh
python3 tools/build_sdk.py            # once per SDK version (framework for macOS 13.3+)
cmake --preset dev && cmake --build --preset dev
open "build/dev/app/Debug/PTSL GUI.app"                       # real Pro Tools
"build/dev/app/Debug/PTSL GUI.app/Contents/MacOS/PTSL GUI" --demo  # no Pro Tools needed
python3 tools/package_app.py          # self-contained dist/PTSL GUI.app for macOS 13.3+ (personal use only)
```

### Building the PTSL SDK (`tools/build_sdk.py`)

The SDK ships its own Conan + CMake build (`setup/build_cpp_ptsl_sdk.py`), which does not work as-is on this machine. `tools/build_sdk.py` drives the SDK's inner `Config/ptsl_build_script.py` with these workarounds, without modifying SDK files:

| Problem | Workaround |
|---|---|
| Conan cache is placed inside the SDK folder; the project path contains a space, which breaks OpenSSL's makefile build | `CONAN_HOME=~/.ptslconan` (must not contain spaces) |
| Conan rejects the detected macOS version (e.g. 27.2), and the framework must run on the project's minimum macOS | `PTSL_OS_VERSION` set to `--os-version` (default 13.3), which becomes Conan's `os.version` and the deployment target |
| c-ares' configure check finds `pipe2()` in the macOS 27 SDK headers, which is unavailable on 13.3 (`-Werror,-Wunguarded-availability-new`, and it would fail to load on older macOS) | `~/.ptslconan/global.conf` gets `c-ares/*:tools.cmake.cmaketoolchain:extra_variables={"HAVE_PIPE2": "0"}` (added by the script if missing) |
| Generated presets hard-code the Xcode generator; only Command Line Tools are installed | Rewrite `xcode` → `ninja` (Ninja Multi-Config) in the generated `CMakeUserPresets.json`, then run `cmake --workflow` |
| `find_program(protoc)` picks Homebrew's newer protoc (or another protobuf in the shared Conan cache), producing code incompatible with the SDK's protobuf headers | Prepend the `bin` dirs of the exact protobuf and gRPC packages the SDK resolved (read from `set(<pkg>_PACKAGE_FOLDER_RELEASE "…")` in `MacBuild/arm64/Dependencies/*-release-*-data.cmake`) to `PATH`; clear stale `CMakeCache.txt` |

- A persistent venv at `~/.ptsl-venv` holds the SDK's Python requirements (Conan, CMake, Jinja2) plus pytest.
- Always arm64 (no `--arch` option). Default `--config Release`, `--os-version 13.3`. The generated `CMakeUserPresets.json` is deleted before each run, so a failed Conan step is detected instead of reusing stale presets; `--with-ptslcmd` also builds the example CLI.
- Output: `PTSL_SDK_CPP.*/install/arm64/<config>/PTSLC_CPP/PTSLC_CPP.framework` (with `Resources/CMake/PTSLC_CPPConfig.cmake` for `find_package`) and `.../ptslcmd/ptslcmd`.
- Dependency versions in use (SDK `conanfile.py`): gRPC 1.72.0, protobuf 5.27.0 (`protoc` 27.0), OpenSSL 3.6.0, nlohmann_json 3.12.0, date 3.0.4.

### protobuf for the GUI

Because the schema is parsed at runtime (see Architecture), the GUI uses whatever protobuf `find_package` finds: Homebrew's (36.x) for development, Conan's 6.33.5 (static) for the `dist` build, Ubuntu's (3.21) in CI. `schema.cpp` supports both APIs (error collectors switch on `GOOGLE_PROTOBUF_VERSION` ≥ 4.22). Confirmed in milestone 4: the SDK framework exports no protobuf symbols and both work in one process (covered by an SDK test).

## Testing

- **pytest** (`tests/tools`): `gen_catalog.py` against the fixture proto and the real SDK proto (skipped if absent); `build_sdk.py` and `check.py` helpers.
- **Catch2** (`tests/core`, one test case per behaviour, registered with `catch_discover_tests`): version parsing/ordering; catalog loading, lookup, search and filtering, malformed documents; schema field model for every field kind, comments, enums, JSON normalisation and its errors, parse/build errors, move; history updates, eviction, JSON round-trip and invalid input; fake session scripting, auto-delivery, disconnected and cancelled requests, multi-threaded use; controller validation, sending, dispatcher re-posting, destruction safety.
- Test data paths are compile definitions: `PTSLGUI_FIXTURE_PROTO`, `PTSLGUI_FIXTURE_CATALOG` (generated at build time), and `PTSLGUI_SDK_PROTO`/`PTSLGUI_SDK_CATALOG` when the SDK is present. The `[sdk]` test checks that every catalog request/response type exists in the real schema and that ≥ 90% of documented request examples validate (currently 100 of 101; the failure is a malformed SDK example).
- Protocol helpers: task-status mapping, version / host-ready parsing, RegisterConnection body, error parsing.
- **UI** (`tests/ui`, own `main` creating a `QApplication` with `QT_QPA_PLATFORM=offscreen`, fixture proto + catalog, `FakePtslSession` with auto-delivery): grouping and filtering, command selection and examples, validation, formatting, connect/version/disconnect, failed connect, sending and response tree/JSON, error display, unsupported-command warning, helper functions. Widgets are found by object name; asynchronous results are awaited with `QTest::qWaitFor`.
- **History, settings, confirmations** (`tests/ui/test_history_settings.cpp`): `AppSettings` defaults and persistence; confirmation asked for mutating commands only, Cancel blocks sending, "Don't ask again" and the menu toggle turn it off (and back on); resend is confirmed; Preferences OK/Cancel/forget; history ordering, selection, copy, load, resend, clear; export/import including bad files and paths; remembered requests (and ignoring invalid ones); address, launch flag and last command restored in a new window; panel helpers. The fixture's `CId_DeleteWidget` carries `@category_editing` so a mutating command exists.
- **Packaging** (`tests/tools/test_package_app.py`): otool `-L`/`-D`/`-l` parsing (including the highest minimum macOS across architectures and old-style `LC_VERSION_MIN_MACOSX`), version ordering, external-reference and missing-`@rpath` detection, Mach-O detection, `LSMinimumSystemVersion` reading. `tests/tools/test_build_sdk.py` covers reading Conan package folders and idempotent `global.conf` updates.
- The full suite also runs in the `dist` build (`ctest --preset dist`, run by `package_app.py`), against Conan protobuf 6.33 and the official Qt.
- **SDK** (`tests/sdk`, only when the framework is built): unreachable host gives a "not ready" error quickly; sending while disconnected fails; runtime schema and SDK client coexist in one process. No test needs Pro Tools.
- Hidden test `[.screenshot]` renders the main window with the real catalog and a fake session to the PNG path in `PTSLGUI_SCREENSHOT` (for visual checks; `screencapture` is unavailable on this machine).
- **Forms** (`tests/ui/test_form_editor.cpp`): round-trip of generated samples through `MessageEditor` for every fixture message (60 seeds) and every SDK request type (4 seeds, when the SDK is present); defaults omitted; integer validators; optional presence; oneof selection and loading; lazy nested messages; repeated add/remove; maps; tooltips; Form ⇄ JSON tab sync including invalid JSON blocking the switch.
- Samples (`tests/core/test_sample.cpp`): valid for every fixture message (50 seeds) and every SDK message; deterministic; cover all fields; depth limit.
- The `[.screenshot]` test accepts `PTSLGUI_SCREENSHOT_COMMAND` (catalog name, default GetTrackList) to choose the command shown.

## Repository and CI

- Public GitHub repo `claude-coder-collab/ptsl-gui`. The SDK and anything derived from it are git-ignored (see Constraints).
- GitHub Actions (`.github/workflows/ci.yml`) runs on pull requests only, on ubuntu-24.04: `python-tools` (pytest), `format` (clang-format 23.1.2 from pip, `--dry-run --Werror`, matching the local Homebrew version), `core` (GCC 14, Ubuntu protobuf/nlohmann/Catch2 packages, `ci` preset with warnings as errors, ctest). No scheduled runs. The SDK is never available in CI.
- `tools/check.py` runs everything locally: pytest, clang-format check (`--fix-format` to apply), and configure/build/test for the `dev`, `asan`, `tsan` presets plus a `tidy` build. `--preset` limits presets; `--keep-going` continues after failures.

## Milestones

1. ~~Build SDK + ptslcmd example locally; confirm Conan/protobuf toolchain.~~ Done.
2. ~~Catalog generator + tests.~~ Done.
3. ~~Core: schema, request controller, history, fake session + tests.~~ Done.
4. ~~Minimal UI: connect, command list, raw JSON editor, response view; SDK session.~~ Done.
5. ~~Generated forms.~~ Done.
6. ~~History, persistence, safety prompts (with a setting to turn them off), polish, packaging.~~ Done.

## Decisions

- GUI toolkit: Qt 6 Widgets.
- Licence: MIT.
- Protocol: latest only.
- Command sequences: v2.
- Distribution: personal use, ad-hoc signing only.
- Platform: macOS 13.3+, Apple Silicon (arm64) only.

- protobuf: parsed at runtime; any recent protobuf (Homebrew for development, Conan for distribution, Ubuntu in CI).
- Distributable dependencies: Conan (macOS 13.3 profile) and the official Qt binaries via aqtinstall.
- JSON: nlohmann_json in core.

## Open questions

None currently.

## Future

- v2: request sequences / macros with variable substitution between steps.
- Subscribing to PTSL events (`category_events`) with a live event log.
- Export a request as C++/Python (`py-ptsl`) snippet.

## Status

- Project folder created, SDK `PTSL_SDK_CPP.2026.04.0.1301892` copied in, git initialised.
- Spec drafted (this file). MIT licence added.
- Milestone 1 done: SDK client framework and ptslcmd build locally via `tools/build_sdk.py` (pytest: `tests/tools`). Not yet tested against a running Pro Tools.
- Milestone 2 done: `tools/gen_catalog.py` with pytest coverage; GitHub Actions runs `pytest tests/tools` on pull requests.
- Milestone 3 done: `ptslgui_core` with 40 Catch2 tests passing in dev, ASan+UBSan and TSan builds; clang-tidy clean; GCC 14 CI job.
- Milestone 4 done: SDK session, Qt UI and app bundle with demo mode; 59 tests (core, UI offscreen, SDK without Pro Tools) pass in dev, ASan+UBSan and TSan; clang-tidy clean with warnings as errors. Not yet tried against a running Pro Tools.
- Milestone 5 done: generated request forms with Form ⇄ JSON sync; sample generator; 75 tests pass in dev, ASan+UBSan and TSan; clang-tidy clean.
- Milestone 6 done: history dock, preferences and persisted settings, confirmations for session-modifying commands with a user setting to turn them off, menus, copy button, form layout fixes, self-contained ad-hoc-signed bundle via `tools/package_app.py`; 87 tests pass in dev, ASan+UBSan and TSan; clang-tidy clean.
- macOS 13.3 support: `dist` preset (Conan dependencies, official Qt 6.11.3), SDK framework rebuilt for 13.3, `package_app.py` verifies every binary targets ≤ 13.3; the packaged app is 47 MB, loads no Homebrew libraries and starts in demo mode. Not run on an actual macOS 13 machine (only this macOS 27.2 host is available).
