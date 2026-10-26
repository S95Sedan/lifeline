#include "install.hpp"
#include "converters.hpp"
#include <shlobj.h>
#include <chrono>
#include <set>
#include <thread>
#include <mutex>

namespace lifeline::setup {
namespace {
std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return wchar_t(towlower(c)); });
    return text;
}
bool inside(const fs::path &child, const fs::path &parent) {
    auto c = child.lexically_normal(), p = parent.lexically_normal();
    auto ci = c.begin();
    for (auto pi = p.begin(); pi != p.end(); ++pi, ++ci)
        if (ci == c.end() || lower(ci->native()) != lower(pi->native()))
            return false;
    return true;
}
struct Lock {
    fs::path path;
    HANDLE handle = INVALID_HANDLE_VALUE;
    explicit Lock(const fs::path &root) : path(root / L".lifeline-setup.lock") {
        handle =
            CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(handle != INVALID_HANDLE_VALUE,
                "Another setup is using this folder, or an interrupted setup left .lifeline-setup.lock. "
                "Close the other setup before removing a stale lock.");
    }
    ~Lock() {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            std::error_code ignored;
            fs::remove(path, ignored);
        }
    }
};
struct RecoveryRequired : std::runtime_error {
    using std::runtime_error::runtime_error;
};
// Assets are written to their final paths. Only files that actually change
// need a backup, beside the original file; no alternate installation tree.
class FileTransaction {
    struct Entry {
        fs::path path, backup;
        DWORD attributes = FILE_ATTRIBUTE_NORMAL;
        bool touched = false, backed_up = false;
    };
    std::vector<Entry> entries;
    std::set<fs::path> tracked;
    std::vector<fs::path> directories;
    std::wstring id;
    bool finished = false;

  public:
    FileTransaction() {
        GUID guid{};
        require(SUCCEEDED(CoCreateGuid(&guid)), "Cannot create installation transaction ID");
        wchar_t name[64];
        StringFromGUID2(guid, name, 64);
        id = name;
    }
    void directory(const fs::path &path) {
        if (fs::exists(path)) {
            require(fs::is_directory(path) && !reparse(path), "Invalid installation directory");
            return;
        }
        directory(path.parent_path());
        directories.push_back(path);
        require(fs::create_directory(path), "Cannot create installation directory: " + path_text(path));
    }
    void protect(const fs::path &path) {
        if (tracked.contains(path))
            return;
        directory(path.parent_path());
        entries.push_back({path, {}});
        tracked.insert(path);
        auto &entry = entries.back();
        if (fs::exists(path)) {
            require(fs::is_regular_file(path) && !reparse(path), "Cannot replace installation file");
            entry.backup = path;
            entry.backup += L".lifeline-backup-" + id;
            require(!fs::exists(entry.backup), "An installation backup already exists");
            entry.attributes = GetFileAttributesW(path.c_str());
            fs::rename(path, entry.backup);
            entry.backed_up = true;
            SetFileAttributesW(entry.backup.c_str(), entry.attributes | FILE_ATTRIBUTE_HIDDEN);
        }
        entry.touched = true;
    }
    void write(const fs::path &path, View output) {
        protect(path);
        write_file(path, output);
    }
    void text(const fs::path &path, std::string_view output) { write(path, bytes(output)); }
    void remove(const fs::path &path) { protect(path); }
    std::string rollback() noexcept {
        if (finished)
            return {};
        finished = true;
        std::string failures;
        for (auto i = entries.rbegin(); i != entries.rend(); ++i) {
            if (!i->touched && !i->backed_up)
                continue;
            std::error_code error;
            fs::remove(i->path, error);
            if (!error && i->backed_up) {
                fs::rename(i->backup, i->path, error);
                if (!error)
                    SetFileAttributesW(i->path.c_str(), i->attributes);
            }
            if (error && failures.empty())
                failures = path_text(i->backed_up ? i->backup : i->path) + ": " + error.message();
        }
        for (auto i = directories.rbegin(); i != directories.rend(); ++i) {
            std::error_code ignored;
            fs::remove(*i, ignored); // Remove only new, empty directories.
        }
        return failures;
    }
    std::string commit() {
        finished = true;
        std::string failures;
        for (const auto &entry : entries)
            if (entry.backed_up) {
                std::error_code error;
                fs::remove(entry.backup, error);
                if (error && failures.empty())
                    failures = path_text(entry.backup) + ": " + error.message();
            }
        return failures;
    }
    ~FileTransaction() { rollback(); }
};
std::vector<std::string> source_names(std::string value) {
    value = value.substr(0, value.find(": "));
    std::vector<std::string> result;
    std::size_t start = 0;
    for (;;) {
        auto end = value.find(" + ", start);
        result.push_back(value.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos)
            break;
        start = end + 3;
    }
    return result;
}
std::string suffix(const std::string &text) { return path_text(fs::path(wide(text)).extension()); }
std::string filename(const std::string &text) { return path_text(fs::path(wide(text)).filename()); }
std::string stem(const std::string &text) { return path_text(fs::path(wide(text)).stem()); }
std::string local_sample_path(const std::string &path) {
    auto position = path.rfind("/samples/");
    if (position != std::string::npos)
        return path.substr(position + 1);
    return filename(path);
}
std::wstring command_argument(std::wstring_view text) {
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (auto c : text) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result += c;
        } else {
            result.append(slashes, L'\\');
            result += c;
        }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result += L'\"';
    return result;
}
void movie_process(const fs::path &iso, const fs::path &movies,
                   const std::string &name, const std::string &archive, const std::string &member,
                   const Report &report, int progress) {
    auto converter = executable_path();
    std::wstring command;
    for (const auto &argument : {converter.wstring(), std::wstring(L"--convert-movie"), iso.wstring(),
                                 movies.wstring(), wide(name), wide(archive), wide(member)}) {
        if (!command.empty())
            command += L' ';
        command += command_argument(argument);
    }
    struct Handle {
        HANDLE value = INVALID_HANDLE_VALUE;
        ~Handle() {
            if (value && value != INVALID_HANDLE_VALUE)
                CloseHandle(value);
        }
    } input, output, null_input, process_handle;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    require(CreatePipe(&input.value, &output.value, &security, 0), "Cannot create movie output pipe");
    require(SetHandleInformation(input.value, HANDLE_FLAG_INHERIT, 0), "Cannot secure movie output pipe");
    null_input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(null_input.value != INVALID_HANDLE_VALUE, "Cannot open movie worker input");
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    Bytes storage(size);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    require(InitializeProcThreadAttributeList(attributes, 1, 0, &size), "Cannot initialize movie worker");
    struct Attributes {
        LPPROC_THREAD_ATTRIBUTE_LIST value;
        ~Attributes() { DeleteProcThreadAttributeList(value); }
    } cleanup{attributes};
    HANDLE inherited[] = {output.value, null_input.value};
    require(UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                      sizeof(inherited), nullptr, nullptr), "Cannot secure movie worker handles");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = output.value;
    startup.StartupInfo.hStdError = output.value;
    startup.StartupInfo.hStdInput = null_input.value;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process{};
    auto launched = CreateProcessW(converter.c_str(), command.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, movies.c_str(),
                                   &startup.StartupInfo, &process);
    require(launched, "Cannot start the native movie worker");
    process_handle.value = process.hProcess;
    CloseHandle(output.value);
    output.value = INVALID_HANDLE_VALUE;
    CloseHandle(process.hThread);
    std::string detail;
    auto drain = [&] {
        DWORD available = 0;
        while (PeekNamedPipe(input.value, nullptr, 0, nullptr, &available, nullptr) && available) {
            char buffer[4096];
            DWORD received = 0;
            if (!ReadFile(input.value, buffer, std::min<DWORD>(available, sizeof(buffer)), &received, nullptr))
                break;
            if (detail.size() < 65536)
                detail.append(buffer, std::min<std::size_t>(received, 65536-detail.size()));
        }
    };
    bool cancelled = false;
    while (WaitForSingleObject(process.hProcess, 100) == WAIT_TIMEOUT) {
        drain();
        if (report.cancel && report.cancel->load()) {
            TerminateProcess(process.hProcess, 2);
            cancelled = true;
            break;
        }
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    drain();
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    if (cancelled)
        throw Cancelled();
    if (code) {
        throw std::runtime_error("Native movie conversion failed for " + name + ": " +
                                 (detail.empty() ? "worker exit " + std::to_string(code) : detail));
    }
    report.update(L"Prepared movie: " + wide(name), progress);
}
bool reusable_movie(const fs::path &cache, const std::string &name,
                    const std::string &source_hash, const std::string &identity) {
    if (cache.empty())
        return false;
    auto folder = cache / L"media/movies", metadata = safe_path(folder, name + ".json");
    if (!fs::is_regular_file(metadata))
        return false;
    try {
        auto info = read_json(metadata);
        if (info.at("version") != 4 || info.at("sha256").get<std::string>() != source_hash ||
            info.at("source") != identity)
            return false;
        auto movie = safe_path(folder, name + ".mp4");
        if (!fs::is_regular_file(movie) ||
            fs::file_size(movie) != info.at("media_bytes").get<std::uint64_t>() ||
            file_sha256(movie) != info.at("media_sha256").get<std::string>())
            return false;
        unsigned w = info.value("width", 640u), h = info.value("height", 448u),
                 channels = info.at("audio_samples").get<std::uint64_t>() ? 2u : 0u;
        auto descriptor = "lifeline-movie-5\n" + std::to_string(w) + " " + std::to_string(h) + " 30 " +
                          std::to_string(info.at("timeline_frames").get<unsigned>()) + " " +
                          std::to_string(info.at("video_frames").get<unsigned>()) + " " +
                          std::to_string(channels ? 48000 : 0) + " " + std::to_string(channels) + " " +
                          std::to_string(info.at("audio_samples").get<std::uint64_t>()) + " " +
                          std::to_string(channels ? 1024 : 0) + "\n";
        auto text = read_file(safe_path(folder, name + ".txt"));
        if (text != bytes(descriptor) || !fs::is_regular_file(safe_path(folder, name + ".aperture")))
            return false;
        return true;
    } catch (const std::exception &) {
        return false;
    }
}
std::vector<std::string> shortcuts(const fs::path &root, bool desktop, bool menu) {
    std::vector<std::string> result;
    auto make = [&](REFKNOWNFOLDERID folder_id, bool subfolder) {
        PWSTR text = nullptr;
        require(SUCCEEDED(SHGetKnownFolderPath(folder_id, 0, nullptr, &text)),
                "Cannot locate shortcut folder");
        fs::path folder(text);
        CoTaskMemFree(text);
        if (subfolder)
            folder /= L"Lifeline";
        fs::create_directories(folder);
        auto path = folder / L"Lifeline.lnk";
        IShellLinkW *link = nullptr;
        require(
            SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))),
            "Cannot create Windows shortcut");
        struct Release {
            IShellLinkW *link;
            ~Release() { link->Release(); }
        } release{link};
        IPersistFile *file = nullptr;
        require(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file))), "Cannot persist Windows shortcut");
        struct FileRelease {
            IPersistFile *file;
            ~FileRelease() { file->Release(); }
        } file_release{file};
        if (fs::exists(path)) {
            require(!reparse(path) && SUCCEEDED(file->Load(path.c_str(), STGM_READ)),
                    "Existing shortcut cannot be inspected");
            wchar_t target[32768];
            require(SUCCEEDED(link->GetPath(target, 32768, nullptr, SLGP_RAWPATH)) &&
                        lower(target) == lower((root / L"lifeline.exe").wstring()),
                    "An existing Lifeline shortcut points to another installation");
        }
        require(SUCCEEDED(link->SetPath((root / L"lifeline.exe").c_str())) &&
                    SUCCEEDED(link->SetWorkingDirectory(root.c_str())) &&
                    SUCCEEDED(link->SetDescription(L"Lifeline")) &&
                    SUCCEEDED(link->SetIconLocation((root / L"lifeline.exe").c_str(), 0)) &&
                    SUCCEEDED(file->Save(path.c_str(), TRUE)),
                "Cannot save shortcut");
        result.push_back(path_text(path));
    };
    if (desktop)
        make(FOLDERID_Desktop, false);
    if (menu)
        make(FOLDERID_Programs, true);
    return result;
}
} // namespace
fs::path validate_destination(const fs::path &destination, const fs::path &iso) {
    require(!destination.empty(), "Choose an installation folder");
    auto root = fs::absolute(destination).lexically_normal();
    require(root != root.root_path(), "Choose a game folder rather than a drive root");
    for (auto part = root; !part.empty(); part = part == part.root_path() ? fs::path{} : part.parent_path()) {
        require(!reparse(part), "Choose a folder without symbolic links or junctions");
        require(lower(part.filename().wstring()) != L"sources", "The sources folder is read-only");
    }
    require(!inside(fs::absolute(iso), root),
            "Choose an installation folder that does not contain the source ISO");
    require(!(fs::exists(root / L"sources") && fs::exists(root / L"project")),
            "Choose a separate game folder rather than the project root");
    require(!fs::exists(root) || fs::is_directory(root), "The installation folder is already a file");
    return root;
}
Json verify_installation(const fs::path &data, const Report &report) {
    auto manifest = read_json(data / L"manifest.json");
    require(manifest.at("layout_version") == 2 && manifest.at("game") == "SLUS_208.48",
            "Unsupported installed manifest");
    std::uint64_t total = 0;
    std::set<std::wstring> paths;
    unsigned count = 0;
    for (const auto &record : manifest.at("assets")) {
        report.checkpoint();
        auto name = record.at("path").get<std::string>();
        auto path = safe_path(data, name);
        require(paths.insert(lower(wide(name))).second, "Duplicate installed path");
        require(fs::is_regular_file(path) && fs::file_size(path) == record.at("bytes").get<std::uint64_t>() &&
                    file_sha256(path) == record.at("sha256").get<std::string>(),
                "Installed resource failed verification: " + name);
        total += fs::file_size(path);
        ++count;
    }
    return Json{{"assets", count}, {"bytes", total}};
}
Json perform_install(const InstallOptions &options, const Report &report) {
    auto started = std::chrono::steady_clock::now();
    auto phase_started = started;
    Json timings;
    auto finish_phase = [&](const char *name) {
        auto now = std::chrono::steady_clock::now();
        timings[name] = std::chrono::duration<double>(now-phase_started).count();
        phase_started = now;
    };
    report.update(L"Validate the original Lifeline disc", 1);
    Disc disc(options.iso);
    auto root = validate_destination(options.destination, options.iso);
    auto package = Package::embedded();
    auto recipe = Json::parse(package.get("recipe.json"));
    require(recipe.at("recipe_version") == 1 && recipe.at("layout_version") == 2,
            "Unsupported native installation plan");
    fs::create_directories(root);
    Lock lock(root);
    std::map<std::string, Json> previous;
    Json program_state = {{"files", Json::array()}};
    if (fs::exists(root / L"installation.json")) {
        require(!reparse(root / L"installation.json"),
                "Installation metadata is a symbolic link or junction");
        program_state = read_json(root / L"installation.json");
        require(program_state.at("product") == "LifelineNative" && program_state.at("format") == 1,
                "Unrecognized installation metadata");
    }
    std::map<std::string, std::string> owned_programs;
    for (const auto &record : program_state.at("files"))
        owned_programs.emplace(record.at("path"), record.at("sha256").get<std::string>());
    auto check_programs = [&] {
        for (const auto &file : package.files())
            if (file.name.starts_with("program/")) {
                auto name = file.name.substr(8);
                auto path = safe_path(root, name);
                if (fs::exists(path)) {
                    require(fs::is_regular_file(path), "A program filename is already a directory");
                    auto actual = file_sha256(path);
                    require(actual == file.sha256 ||
                                (owned_programs.contains(name) && actual == owned_programs.at(name)),
                            "A modified or unowned program file would be overwritten: " + name);
                }
            }
    };
    check_programs();
    if (fs::exists(root / L"data")) {
        check_tree(root / L"data");
        require(fs::is_regular_file(root / L"data/manifest.json"),
                "Existing data has no Lifeline ownership manifest. Choose a separate folder");
        auto old = read_json(root / L"data/manifest.json");
        require(old.at("layout_version") == 2 && old.at("game") == "SLUS_208.48",
                "Unsupported previous installed layout");
        for (const auto &record : old.at("assets"))
            previous.emplace(record.at("path"), record);
    }
    if (!options.cache_data.empty())
        check_tree(options.cache_data / L"media/movies");
    auto parent = root;
    while (!fs::exists(parent))
        parent = parent.parent_path();
    ULARGE_INTEGER free{};
    require(GetDiskFreeSpaceExW(parent.c_str(), &free, nullptr, nullptr) &&
                free.QuadPart >= 6ull * 1024 * 1024 * 1024,
            "Setup requires at least 6 GiB of free space");
    FileTransaction transaction;
    auto data = root / L"data";
    finish_phase("validation_seconds");
    try {
        transaction.directory(data);
        report.update(L"Prepare the installation folder", 3);
        std::map<std::string, std::vector<Json>> archives;
        std::map<std::string, Json> installed;
        std::size_t done = 0, total = recipe.at("assets").size();
        std::size_t reused_assets = 0, converted_movies = 0;
        struct MovieJob {
            std::string name, archive, member;
            std::vector<Json> records;
        };
        std::vector<MovieJob> movie_jobs;
        auto save = [&](Json record, const Bytes &output, bool exact = true) {
            report.checkpoint();
            auto name = record.at("path").get<std::string>();
            auto hash = sha256(output);
            if (exact)
                require(hash == record.at("sha256").get<std::string>() &&
                            output.size() == record.at("bytes").get<std::uint64_t>(),
                        "Native converter output differs from reference: " + name);
            auto path = safe_path(data, name);
            bool unchanged = false;
            if (fs::exists(path)) {
                require(fs::is_regular_file(path), "A data filename is already a directory: " + name);
                auto actual = file_sha256(path);
                require(actual == hash || (previous.contains(name) &&
                                           actual == previous.at(name).at("sha256").get<std::string>()),
                        "A modified or unowned data file would be overwritten: " + name);
                unchanged = actual == hash;
            }
            if (!unchanged)
                transaction.write(path, output);
            record["bytes"] = output.size();
            record["sha256"] = hash;
            record.erase("width");
            record.erase("height");
            installed[name] = std::move(record);
            ++done;
        };
        for (const auto &record : recipe.at("assets"))
            archives[record.at("archive")].push_back(record);
        for (const auto &[archive, all_records] : archives) {
            report.update(L"Install game resources: " + wide(archive),
                          5 + int(70 * done / std::max<std::size_t>(1, total)));
            std::vector<Json> records;
            for (auto record : all_records) {
                report.checkpoint();
                auto name = record.at("path").get<std::string>();
                if (!record.value("conversion", "raw").starts_with("movie-")) {
                    auto path = safe_path(data, name);
                    if (fs::is_regular_file(path) && fs::file_size(path) == record.at("bytes").get<std::uint64_t>() &&
                        file_sha256(path) == record.at("sha256").get<std::string>()) {
                        record.erase("width");
                        record.erase("height");
                        installed[name] = std::move(record);
                        ++done;
                        ++reused_assets;
                        continue;
                    }
                }
                records.push_back(std::move(record));
            }
            if (records.empty())
                continue;
            if (archive == "OS.FAT") {
                for (const auto &record : records) {
                    auto conversion = record.value("conversion", "raw");
                    save(record, bytes(conversion == "resource-catalog-v1"
                                           ? recipe.at("resource_catalog").get<std::string>()
                                           : recipe.at("catalog").dump(2) + "\n"));
                }
                continue;
            }
            if (archive == "SLUS_208.48") {
                for (const auto &record : records)
                    save(record, normalization(disc.executable()));
                continue;
            }
            auto raw = disc.file(archive);
            std::map<std::string, Bytes> members;
            if (suffix(archive) == ".a")
                members = archive_members(raw);
            else
                members.emplace(filename(archive), std::move(raw));
            std::map<std::string, Converted> converted;
            std::set<std::string> prepared_movies;
            auto member = [&](const std::string &name) -> const Bytes & {
                auto found = members.find(name);
                require(found != members.end(), "Missing archive member: " + archive + ":" + name);
                return found->second;
            };
            for (const auto &record : records) {
                report.checkpoint();
                auto conversion = record.value("conversion", "raw"), name = record.at("path").get<std::string>(),
                     original = record.at("member").get<std::string>();
                auto names = source_names(original);
                if (conversion.starts_with("movie-")) {
                    auto movie_name = stem(name), movie_original = names.front();
                    if (!prepared_movies.insert(movie_name).second)
                        continue;
                    auto movie_source = member(movie_original);
                    auto source_hash = sha256(movie_source);
                    require(source_hash == record.at("source_sha256").get<std::string>(),
                            "Movie source changed: " + movie_name);
                    auto movie_folder = data / L"media/movies";
                    auto identity = archive + (suffix(archive) == ".a" ? ":" + movie_original : "");
                    // Never accept locally modified movie files as a reusable cache.
                    for (const auto &r : records)
                        if (stem(r.at("path").get<std::string>()) == movie_name) {
                            auto n = r.at("path").get<std::string>();
                            auto path = safe_path(data, n);
                            if (fs::exists(path))
                                require(previous.contains(n) &&
                                            file_sha256(path) == previous.at(n).at("sha256").get<std::string>(),
                                        "A modified movie would be overwritten: " + n);
                        }
                    bool reused = reusable_movie(data, movie_name, source_hash, identity);
                    bool cached = !reused && reusable_movie(options.cache_data, movie_name, source_hash, identity);
                    if (!reused)
                        for (const auto *extension : {".mp4", ".json", ".txt", ".aperture"}) {
                            auto target = safe_path(movie_folder, movie_name + extension);
                            transaction.protect(target);
                            if (cached) {
                                auto from = safe_path(options.cache_data / L"media/movies", movie_name + extension);
                                std::error_code error;
                                fs::create_hard_link(from, target, error);
                                if (error)
                                    fs::copy_file(from, target);
                            }
                        }
                    reused = reused || cached;
                    if (!reused) {
                        MovieJob job{movie_name, archive, suffix(archive) == ".a" ? movie_original : "", {}};
                        for (const auto &r : records)
                            if (stem(r.at("path").get<std::string>()) == movie_name &&
                                r.value("conversion", "").starts_with("movie-"))
                                job.records.push_back(r);
                        movie_jobs.push_back(std::move(job));
                        continue;
                    } else {
                        reused_assets += 4;
                    }
                    for (auto r : records)
                        if (stem(r.at("path").get<std::string>()) == movie_name &&
                            r.value("conversion", "").starts_with("movie-")) {
                            auto n = r.at("path").get<std::string>();
                            auto path = safe_path(data, n);
                            require(fs::is_regular_file(path), "Incomplete converted movie set");
                            r["bytes"] = fs::file_size(path);
                            r["sha256"] = file_sha256(path);
                            installed[n] = std::move(r);
                            ++done;
                        }
                    continue;
                }
                if (conversion == "voice-wav-v1") {
                    auto key = "voice:" + names.front();
                    if (!converted.contains(key)) {
                        Bytes tail;
                        if (record.contains("stream_source"))
                            tail = disc.file(record.at("stream_source"));
                        converted[key] = voice_bank(member(names.front()), tail);
                    }
                    save(record, converted.at(key).at(filename(name)));
                    continue;
                }
                if (conversion == "font-bitmap-v1") {
                    auto key = "font:" + names.front();
                    if (!converted.contains(key))
                        converted[key] = font(member(names.front()));
                    save(record, converted.at(key).at(filename(name)));
                    continue;
                }
                if (conversion == "music-sequence-v2" || conversion == "music-pcm-v1" ||
                    conversion == "sound-bank-pcm-v1") {
                    std::string hd, bd, sq;
                    for (const auto &input : names) {
                        if (suffix(input) == ".hd")
                            hd = input;
                        else if (suffix(input) == ".bd")
                            bd = input;
                        else if (suffix(input) == ".sq")
                            sq = input;
                    }
                    if (hd.empty())
                        for (const auto &[input, _] : members)
                            if (suffix(input) == ".hd") {
                                hd = input;
                                break;
                            }
                    if (bd.empty())
                        bd = stem(hd) + ".bd";
                    if (conversion != "sound-bank-pcm-v1" && sq.empty())
                        for (const auto &[input, _] : members)
                            if (suffix(input) == ".sq") {
                                sq = input;
                                break;
                            }
                    auto key = conversion + ":" + hd + ":" + bd + ":" + sq;
                    if (!converted.contains(key))
                        converted[key] = conversion == "sound-bank-pcm-v1"
                                             ? sound_bank(member(hd), member(bd))
                                             : music_bank(member(sq), member(hd), member(bd),
                                                          conversion == "music-pcm-v1" ? 1 : 2);
                    auto local = local_sample_path(name);
                    if (suffix(local) == ".tsv" && conversion != "sound-bank-pcm-v1")
                        local = "sequence.tsv";
                    save(record, converted.at(key).at(local));
                    continue;
                }
                if (conversion == "map-bitmap-v1" || conversion == "pause-bitmap-v1") {
                    require(names.size() == 2, "Indexed map has no palette");
                    const auto &indices = member(names[0]);
                    const auto &palette = member(names[1]);
                    require(palette.size() == 1024, "Invalid map palette");
                    Bytes rgba;
                    rgba.reserve(indices.size() * 4);
                    for (auto index : indices) {
                        unsigned entry = (index & ~24u) | ((index & 8u) << 1) | ((index & 16u) >> 1);
                        append(rgba, slice(palette, entry * 4, 4));
                    }
                    save(record, convert_member("rgba-bitmap-v1", rgba, record));
                    continue;
                }
                save(record, convert_member(conversion, member(names.front()), record));
            }
        }
        finish_phase("resources_seconds");
        if (!movie_jobs.empty()) {
            std::atomic_size_t next_job{0}, complete{0};
            std::atomic_bool stop{false};
            unsigned parallel = std::min<unsigned>(3, std::max(1u, std::thread::hardware_concurrency()/4));
            parallel = std::min<unsigned>(parallel, unsigned(movie_jobs.size()));
            std::atomic_uint active{parallel};
            std::mutex failure_mutex;
            std::exception_ptr failure;
            std::vector<std::jthread> workers;
            auto run_movies = [&] {
                try {
                    Report local;
                    local.cancel = &stop;
                    local.status = [&](const std::wstring &value, int) {
                        if (report.status)
                            report.status(value, 75+int(9*complete.load()/movie_jobs.size()));
                    };
                    while (!stop.load()) {
                        auto index = next_job.fetch_add(1);
                        if (index >= movie_jobs.size())
                            break;
                        const auto &job = movie_jobs[index];
                        local.update(L"Preparing movie: " + wide(job.name), 75);
                        movie_process(options.iso, data / L"media/movies", job.name,
                                      job.archive, job.member, local, 75);
                        ++complete;
                    }
                } catch (...) {
                    std::lock_guard guard(failure_mutex);
                    if (!failure)
                        failure = std::current_exception();
                    stop = true;
                }
                --active;
            };
            try {
                for (unsigned i = 0; i < parallel; ++i)
                    workers.emplace_back(run_movies);
            } catch (...) {
                stop = true;
                throw;
            }
            while (active.load()) {
                if (report.cancel && report.cancel->load())
                    stop = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            workers.clear();
            if (failure)
                std::rethrow_exception(failure);
            report.checkpoint();
            for (const auto &job : movie_jobs)
                for (auto record : job.records) {
                    auto name = record.at("path").get<std::string>();
                    auto path = safe_path(data, name);
                    require(fs::is_regular_file(path), "Incomplete converted movie set");
                    record["bytes"] = fs::file_size(path);
                    record["sha256"] = file_sha256(path);
                    installed[name] = std::move(record);
                    ++done;
                }
            converted_movies = movie_jobs.size();
        }
        finish_phase("movies_seconds");
        require(done == total, "The installation plan was not completed");
        Json manifest = {{"format", 1},
                         {"layout_version", 2},
                         {"game", "SLUS_208.48"},
                         {"source", disc.identity()},
                         {"assets", Json::array()},
                         {"deferred", recipe.value("deferred", Json::array())}};
        for (auto &[_, record] : installed)
            manifest["assets"].push_back(record);
        for (const auto &[name, record] : previous)
            if (!installed.contains(name)) {
                auto path = safe_path(data, name);
                if (fs::is_regular_file(path) && file_sha256(path) == record.at("sha256").get<std::string>())
                    transaction.remove(path);
            }
        transaction.text(data / L"manifest.json", manifest.dump(2) + "\n");
        report.update(L"Verify the installed game data", 85);
        auto result = verify_installation(data, report);
        finish_phase("verification_seconds");
        Json state = {{"format", 1},
                      {"product", "LifelineNative"},
                      {"version", "0.1.0"},
                      {"source", disc.identity()},
                      {"files", Json::array()}};
        if (fs::exists(root / L"licenses"))
            check_tree(root / L"licenses");
        check_programs();
        report.update(L"Install the native PC executable", 94);
        for (const auto &file : package.files())
            if (file.name.starts_with("program/")) {
                auto name = file.name.substr(8);
                report.checkpoint();
                auto path = safe_path(root, name);
                if (!fs::is_regular_file(path) || file_sha256(path) != file.sha256)
                    transaction.write(path, package.get(file.name));
                state["files"].push_back(Json{{"path", name}, {"bytes", file.size}, {"sha256", file.sha256}});
            }
        // Remove only unchanged files owned by a previous package, including the
        // app-local runtime DLLs superseded by the statically linked executable.
        for (const auto &[name, hash] : owned_programs) {
            if (std::any_of(state["files"].begin(), state["files"].end(), [&](const auto &record) {
                    return record.at("path").template get<std::string>() == name;
                }))
                continue;
            auto path = safe_path(root, name);
            if (fs::is_regular_file(path) && file_sha256(path) == hash)
                transaction.remove(path);
        }
        report.checkpoint();
        if (report.status)
            report.status(L"Finish the verified installation", 97);
        result["warnings"] = Json::array();
        try {
            auto links = shortcuts(root, options.desktop, options.start_menu);
            state["shortcuts"] = links;
        } catch (const std::exception &error) {
            result["warnings"].push_back(
                std::string("Installed successfully, but a shortcut could not be created: ") + error.what());
        }
        transaction.text(root / L"installation.json", state.dump(2) + "\n");
        result["destination"] = path_text(root);
        result["reused_assets"] = reused_assets;
        result["converted_movies"] = converted_movies;
        finish_phase("publication_seconds");
        if (report.status)
            report.status(L"Finishing installation...", 99);
        auto cleanup_error = transaction.commit();
        finish_phase("cleanup_seconds");
        if (!cleanup_error.empty())
            result["warnings"].push_back("Installed successfully, but a previous-file backup could not be removed: " +
                                          cleanup_error);
        result["timings"] = timings;
        result["seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        if (report.status)
            report.status(L"Lifeline is installed", 100);
        return result;
    } catch (...) {
        auto original = std::current_exception();
        auto error = transaction.rollback();
        if (!error.empty())
            throw RecoveryRequired("Setup could not finish restoring the previous installation. "
                                   "Recovery files have been preserved beside their original files. " + error);
        std::rethrow_exception(original);
    }
}
} // namespace lifeline::setup
