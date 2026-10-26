#include "FileLoader.h"
#include "SpinePreview.h"
#include "imgui.h"
#include "AppProcess.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <mutex>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
std::mutex dialogMutex;
std::vector<std::string> dialogFiles;
std::string dialogError;
bool dialogPending = false, textureDialog = false, dialogReady = false;
void SDLCALL fileDialogResult(void *, const char *const *paths, int) {
    std::lock_guard<std::mutex> lock(dialogMutex);
    dialogPending = false;
    dialogReady = true;
    if (!paths) dialogError = SDL_GetError();
    else for (size_t i = 0; paths[i]; ++i) dialogFiles.emplace_back(paths[i]);
}
std::string shortPath(const fs::path &path) {
    return (path.parent_path().filename()/path.filename()).string() + "##" + path.string();
}
template<size_t N> void put(std::array<char,N> &buffer, const std::string &value) {
    if (value.size() >= N) throw std::runtime_error("Selected path is too long");
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
}
struct Stamp {
    fs::path path;
    uintmax_t size;
    fs::file_time_type time;
    explicit Stamp(const fs::path &p) : path(p), size(fs::file_size(p)), time(fs::last_write_time(p)) {}
    bool unchanged() const {
        std::error_code ec;
        const auto nowSize = fs::file_size(path, ec);
        if (ec || nowSize != size) return false;
        const auto nowTime = fs::last_write_time(path, ec);
        return !ec && nowTime == time;
    }
};
}

struct FileLoader::Impl {
    ResourceCandidates candidates;
    std::vector<TexturePage> overrides;
    std::vector<std::string> dropped;
    std::array<char,4096> skeletonPath{}, atlasPath{};
    AlphaMode alpha = AlphaMode::Auto;
    std::optional<ResourceFiles> current, pending, ready;
    std::vector<Stamp> stamps;
    SDL_Process *process = nullptr;
    Uint64 processStarted = 0;
    std::string processOutput, message;
    bool showSelection = false, closeSelection = false, error = false, attention = false;

    void cancel() {
        if (process) {
            SDL_KillProcess(process, true);
            SDL_WaitProcess(process, true, nullptr);
            SDL_DestroyProcess(process); process = nullptr;
        }
        pending.reset();
    }
    void fail(const std::string &text) { message = text; error = true; showSelection = true; attention = true; }
    void chooseAtlas(const fs::path &path) {
        put(atlasPath, path.string());
        overrides.clear();
        if (!path.empty()) for (const auto &name : atlasPageNames(path)) overrides.push_back({name, {}, false});
    }
    void recommendAtlas() {
        const auto matches = matchAtlases(fs::path(skeletonPath.data()), candidates.atlases);
        chooseAtlas(matches.size() == 1 ? matches.front() : fs::path());
    }
    void start() {
        try {
            cancel();
            if (!*skeletonPath.data() || !*atlasPath.data()) throw std::runtime_error("Choose a skeleton and an Atlas first.");
            pending = prepareResources(skeletonPath.data(), atlasPath.data(), alpha, overrides);
            stamps.clear();
            stamps.emplace_back(pending->skeleton); stamps.emplace_back(pending->atlas);
            for (const auto &page : pending->pages) stamps.emplace_back(page.path);
            std::vector<std::string> arguments = {applicationExecutablePath(), "--validate-resource",
                pending->skeleton.string(), pending->atlas.string(), std::to_string(static_cast<int>(alpha))};
            for (const auto &page : pending->pages) { arguments.push_back(page.name); arguments.push_back(page.path.string()); }
            std::vector<const char *> pointers;
            for (const auto &arg : arguments) pointers.push_back(arg.c_str());
            pointers.push_back(nullptr);
            auto props = SDL_CreateProperties();
            SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, pointers.data());
            SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
            SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
            process = SDL_CreateProcessWithProperties(props);
            SDL_DestroyProperties(props);
            if (!process) throw std::runtime_error(std::string("Cannot start validator: ") + SDL_GetError());
            processOutput.clear(); processStarted = SDL_GetTicks();
            message = "Validating resource..."; error = false;
        } catch (const std::exception &e) { pending.reset(); fail(e.what()); }
    }
    void discover(const std::vector<std::string> &paths) {
        try {
            cancel(); ready.reset();
            candidates = discoverResources(paths);
            skeletonPath.fill(0); atlasPath.fill(0); overrides.clear(); alpha = AlphaMode::Auto;
            if (candidates.skeletons.size() == 1) {
                put(skeletonPath, candidates.skeletons.front().string()); recommendAtlas();
            } else if (candidates.atlases.size() == 1) chooseAtlas(candidates.atlases.front());
            if (*skeletonPath.data() && *atlasPath.data()) start();
            else { showSelection = true; attention = true; message = "Choose the resource files to load."; error = false; }
        } catch (const std::exception &e) { fail(e.what()); }
    }
    void readOutput() {
        char buffer[1024];
        auto *stream = SDL_GetProcessOutput(process);
        size_t length;
        while (stream && (length = SDL_ReadIO(stream, buffer, sizeof(buffer))) != 0)
            if (processOutput.size() < 8192) processOutput.append(buffer, std::min(length, 8192-processOutput.size()));
    }
    void poll() {
        if (!process) return;
        readOutput();
        if (SDL_GetTicks()-processStarted > 15000) {
            cancel(); fail("Validation timed out. The previous animation was kept."); return;
        }
        int code = 0;
        if (!SDL_WaitProcess(process, false, &code)) return;
        readOutput();
        SDL_DestroyProcess(process); process = nullptr;
        if (code != 0) {
            const std::string notice = pending ? pending->versionNotice : "";
            pending.reset();
            fail((notice.empty() ? "" : notice+"\n") + (processOutput.empty()
                ? "The Spine parser rejected this file (corrupt or truncated data). The previous animation was kept." : processOutput));
        } else if (!std::all_of(stamps.begin(), stamps.end(), [](const auto &stamp) { return stamp.unchanged(); })) {
            pending.reset(); fail("Files changed during validation. Reload to try again.");
        } else { ready = std::move(pending); pending.reset(); }
    }
    void openDialog(SDL_Window *window, bool folder, bool textures) {
        {
            std::lock_guard<std::mutex> lock(dialogMutex);
            if (dialogPending) return;
            dialogPending = true; textureDialog = textures; dialogReady = false;
            dialogFiles.clear(); dialogError.clear();
        }
        static const SDL_DialogFileFilter filters[] = {
            {"Spine resources", "json;skel;atlas;txt;bytes;png;jpg;jpeg;webp;tga"}, {"All files", "*"}
        };
        if (folder) SDL_ShowOpenFolderDialog(fileDialogResult, nullptr, window, nullptr, false);
        else SDL_ShowOpenFileDialog(fileDialogResult, nullptr, window, filters, 2, nullptr, true);
    }
};

FileLoader::FileLoader() : impl(new Impl) {}
FileLoader::~FileLoader() { cancel(); }
void FileLoader::cancel() { impl->cancel(); }
void FileLoader::open(SDL_Window *window) { impl->openDialog(window, false, false); }
bool FileLoader::wantsPanel() { const bool attention = impl->attention; impl->attention = false; return attention; }
bool FileLoader::isCurrent(const ResourceFiles &files) const {
    return impl->current && impl->current->skeleton == files.skeleton && impl->current->atlas == files.atlas;
}
void FileLoader::reload() {
    auto &s = *impl;
    if (!s.current) return;
    put(s.skeletonPath, s.current->skeleton.string());
    put(s.atlasPath, s.current->atlas.string());
    s.overrides = s.current->pages; s.alpha = s.current->alpha; s.start();
}
void FileLoader::drop(const std::string &path) { impl->dropped.push_back(path); }
void FileLoader::failed(const std::string &message) { impl->fail(message); }
void FileLoader::loaded(const ResourceFiles &files) {
    ResourceFiles summary;
    summary.skeleton = files.skeleton; summary.atlas = files.atlas;
    summary.version = files.version; summary.versionNotice = files.versionNotice; summary.pages = files.pages;
    summary.alpha = files.alpha; summary.binary = files.binary;
    impl->current = std::move(summary);
    impl->message = "Loaded " + files.skeleton.filename().string() + " | " + std::to_string(files.pages.size()) + " texture page(s)";
    impl->error = false;
    impl->closeSelection = true;
    for (const auto &page : files.pages)
        if (std::find(impl->candidates.images.begin(), impl->candidates.images.end(), page.path) == impl->candidates.images.end())
            impl->candidates.images.push_back(page.path);
}
std::optional<ResourceFiles> FileLoader::takeValidated() {
    auto result = std::move(impl->ready); impl->ready.reset(); return result;
}
void FileLoader::update() {
    std::vector<std::string> selected;
    std::string error;
    bool textures = false;
    {
        std::lock_guard<std::mutex> lock(dialogMutex);
        if (dialogReady) {
            selected.swap(dialogFiles); error.swap(dialogError); textures = textureDialog; dialogReady = false;
        }
    }
    if (!error.empty()) impl->fail(error);
    if (!selected.empty()) {
        if (textures) {
            for (const auto &name : selected) impl->candidates.images.emplace_back(name);
        } else impl->discover(selected);
    }
    if (!impl->dropped.empty()) {
        auto paths = std::move(impl->dropped); impl->dropped.clear(); impl->discover(paths);
    }
    impl->poll();
}
void FileLoader::draw(SDL_Window *window) {
    auto &s = *impl;
    if (s.current) {
        const bool allPma = std::all_of(s.current->pages.begin(), s.current->pages.end(),
                                      [](const auto &page) { return page.premultiplied; });
        const bool anyPma = std::any_of(s.current->pages.begin(), s.current->pages.end(),
                                      [](const auto &page) { return page.premultiplied; });
        bool pma = allPma;
        ImGui::BeginDisabled(s.process != nullptr);
        if (ImGui::Checkbox("Premultiplied alpha (PMA)", &pma)) {
            put(s.skeletonPath, s.current->skeleton.string());
            put(s.atlasPath, s.current->atlas.string());
            s.overrides = s.current->pages;
            s.alpha = pma ? AlphaMode::Premultiplied : AlphaMode::Straight;
            s.start();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enable for PMA textures; disable for straight-alpha textures.\nReloads the current textures. Auto mode is available in Resource details.");
        ImGui::EndDisabled();
        if (anyPma && !allPma) ImGui::TextDisabled("Auto: mixed alpha across pages");
    }
    if (ImGui::Button("Open files...")) s.openDialog(window, false, false);
    ImGui::SameLine();
    if (ImGui::Button("Folder...")) s.openDialog(window, true, false);
    if (s.current) {
        if (ImGui::Button("Reload")) reload();
        ImGui::SameLine();
        if (ImGui::Button("Resource details...")) {
            put(s.skeletonPath, s.current->skeleton.string());
            put(s.atlasPath, s.current->atlas.string());
            s.overrides = s.current->pages; s.alpha = s.current->alpha; s.showSelection = true;
        }
    }
    ImGui::TextWrapped("Drop .json / .skel, Atlas, images, or a folder");
    const auto *resource = s.pending ? &*s.pending : (s.current ? &*s.current : nullptr);
    if (resource && !resource->versionNotice.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1));
        ImGui::TextWrapped("%s", resource->versionNotice.c_str());
        ImGui::PopStyleColor();
    }
    if (!s.message.empty()) {
        if (s.error) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.35f, 1));
        ImGui::TextWrapped("%s", s.message.c_str());
        if (s.error) ImGui::PopStyleColor();
    }
    if (s.showSelection) { ImGui::OpenPopup("Load Spine resource"); s.showSelection = false; }
    ImGui::SetNextWindowSize(ImVec2(std::min(700.0f, ImGui::GetIO().DisplaySize.x-32),
                                  std::min(620.0f, ImGui::GetIO().DisplaySize.y-48)), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Load Spine resource", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextWrapped("Choose the skeleton and Atlas. Image filenames come from Atlas page entries.");
        const auto skeletonLabel = fs::path(s.skeletonPath.data()).filename().string();
        if (ImGui::BeginCombo("Skeleton", skeletonLabel.empty() ? "Choose skeleton..." : skeletonLabel.c_str())) {
            for (const auto &p : s.candidates.skeletons)
                if (ImGui::Selectable(shortPath(p).c_str(), p.string() == s.skeletonPath.data())) {
                    put(s.skeletonPath, p.string());
                    try { s.recommendAtlas(); } catch (const std::exception &e) { s.fail(e.what()); }
                }
            ImGui::EndCombo();
        }
        ImGui::InputText("Skeleton path", s.skeletonPath.data(), s.skeletonPath.size());
        const auto atlasLabel = fs::path(s.atlasPath.data()).filename().string();
        if (ImGui::BeginCombo("Atlas", atlasLabel.empty() ? "Choose Atlas..." : atlasLabel.c_str())) {
            for (const auto &p : s.candidates.atlases)
                if (ImGui::Selectable(shortPath(p).c_str(), p.string() == s.atlasPath.data())) {
                    try { s.chooseAtlas(p); } catch (const std::exception &e) { s.fail(e.what()); }
                }
            ImGui::EndCombo();
        }
        if (ImGui::InputText("Atlas path", s.atlasPath.data(), s.atlasPath.size())) s.overrides.clear();
        const char *alphaNames[] = {"Auto (Atlas pma / -pma name)", "Straight alpha", "Premultiplied alpha"};
        int alpha = static_cast<int>(s.alpha);
        if (ImGui::Combo("Texture alpha", &alpha, alphaNames, 3)) s.alpha = static_cast<AlphaMode>(alpha);
        if (ImGui::Button("Read texture pages")) {
            try { s.chooseAtlas(s.atlasPath.data()); } catch (const std::exception &e) { s.fail(e.what()); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Browse textures...")) s.openDialog(window, false, true);
        ImGui::BeginChild("Texture pages", ImVec2(0, std::min(180.0f, static_cast<float>(s.overrides.size())*55+15)), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < s.overrides.size(); ++i) {
            auto &page = s.overrides[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TextUnformatted(page.name.c_str());
            const auto label = page.path.empty() ? std::string("Atlas-relative path") : page.path.filename().string();
            if (ImGui::BeginCombo("##texture", label.c_str())) {
                if (ImGui::Selectable("Atlas-relative path", page.path.empty())) page.path.clear();
                for (const auto &p : s.candidates.images)
                    if (ImGui::Selectable(shortPath(p).c_str(), p == page.path)) page.path = p;
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (s.error) ImGui::TextWrapped("%s", s.message.c_str());
        ImGui::BeginDisabled(s.process != nullptr);
        if (ImGui::Button("Load")) s.start();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { s.cancel(); ImGui::CloseCurrentPopup(); }
        if (s.process) ImGui::TextUnformatted("Validating...");
        if (s.closeSelection) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    s.closeSelection = false;
}

int validateResourceArguments(int argc, char *argv[]) {
    // The legacy parser does not bounds-check all binary reads. A worker may
    // terminate on malformed input; never attempt to recover in that process.
    auto invalidData = [](int) {
        constexpr char message[] = "Corrupt or truncated skeleton data rejected by the Spine parser.\n";
        (void)write(STDERR_FILENO, message, sizeof(message)-1);
        _exit(2);
    };
    for (int signal : {SIGSEGV, SIGBUS, SIGABRT, SIGFPE}) std::signal(signal, invalidData);
    try {
        if (argc < 5 || (argc-5)%2) throw std::runtime_error("Invalid validation arguments");
        const int alpha = std::stoi(argv[4]);
        if (alpha < 0 || alpha > 2) throw std::runtime_error("Invalid alpha mode");
        std::vector<TexturePage> overrides;
        for (int i = 5; i < argc; i += 2) overrides.push_back({argv[i], argv[i+1], false});
        const auto files = prepareResources(argv[2], argv[3], static_cast<AlphaMode>(alpha), overrides);
        SpinePreview::validate(files);
        std::puts("Resource validation passed");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
