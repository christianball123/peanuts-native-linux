// Peanuts Native for Linux — the launcher's model (core.hpp).
#include "core.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <regex>
#include <set>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace pn {
Paths P; Config C; Catalog CAT; Library LIB;

static double now_s() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// ---------- helpers ----------
std::string home() { const char *h = getenv("HOME"); return h && *h ? h : "/tmp"; }
bool is_file(const std::string &p) { struct stat st; return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }
bool is_dir(const std::string &p) { struct stat st; return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
bool mkdirs(const std::string &p) {
    if (p.empty() || is_dir(p)) return true;
    std::string parent = dirname_of(p);
    if (parent != p && !mkdirs(parent)) return false;
    return mkdir(p.c_str(), 0755) == 0 || errno == EEXIST;
}
std::string read_file(const std::string &p, size_t max) {
    FILE *f = fopen(p.c_str(), "rb"); if (!f) return "";
    std::string s; char b[65536]; size_t n;
    while (s.size() < max && (n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
    fclose(f); return s;
}
bool write_file(const std::string &p, const std::string &s) {
    mkdirs(dirname_of(p));
    std::string tmp = p + ".tmp"; FILE *f = fopen(tmp.c_str(), "wb"); if (!f) return false;
    bool ok = fwrite(s.data(), 1, s.size(), f) == s.size(); ok = fclose(f) == 0 && ok;
    return ok && rename(tmp.c_str(), p.c_str()) == 0;
}
std::string basename_of(const std::string &p) { size_t k = p.find_last_of('/'); return k == std::string::npos ? p : p.substr(k + 1); }
std::string dirname_of(const std::string &p) { size_t k = p.find_last_of('/'); return k == std::string::npos ? "." : k == 0 ? "/" : p.substr(0, k); }
std::string lower(std::string s) { for (auto &c : s) c = (char)tolower((unsigned char)c); return s; }
std::string trim(const std::string &s) { size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n"); return a == std::string::npos ? "" : s.substr(a, b - a + 1); }
std::vector<std::string> split(const std::string &s, char c) { std::vector<std::string> v; size_t a = 0, b; while ((b = s.find(c, a)) != std::string::npos) { v.push_back(s.substr(a, b - a)); a = b + 1; } v.push_back(s.substr(a)); return v; }
std::string human_size(uint64_t b) { char t[32]; if (b >= (1ull << 30)) snprintf(t, sizeof t, "%.1f GB", b / 1073741824.0); else snprintf(t, sizeof t, "%.0f MB", b / 1048576.0); return t; }
std::string now_stamp() { time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm); char b[32]; strftime(b, sizeof b, "%Y%m%d-%H%M%S", &tm); return b; }
std::string version_display(uint32_t v) { return v ? "v" + std::to_string(v) : "base game"; }

// ---------- paths ----------
void Paths::init() {
    char exe[4096]; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1); exe[n > 0 ? n : 0] = 0;
    const char *ad = getenv("APPDIR");
    std::string usr = ad && *ad ? std::string(ad) + "/usr" : dirname_of(dirname_of(exe));   // <usr>/bin/peanuts-native
    appdir = ad && *ad ? ad : dirname_of(usr);
    bin = usr + "/bin"; share = usr + "/share/peanuts-native"; python = usr + "/python/bin/python3"; dotnet = usr + "/lib/dotnet";
    engine_src = share + "/engine";
    const char *x = getenv("XDG_DATA_HOME"); data = (x && *x ? std::string(x) : home() + "/.local/share") + "/peanuts-native";
    x = getenv("XDG_CONFIG_HOME"); config = (x && *x ? std::string(x) : home() + "/.config") + "/peanuts-native";
    x = getenv("XDG_CACHE_HOME"); cache = (x && *x ? std::string(x) : home() + "/.cache") + "/peanuts-native";
    engine = data + "/engine"; logs = data + "/logs";
    version = trim(read_file(share + "/VERSION")); if (version.empty()) version = "dev";
    engine_version = trim(read_file(engine_src + "/PNL_ENGINE_VERSION"));
    mkdirs(data); mkdirs(config); mkdirs(cache); mkdirs(logs);
}

// ---------- settings ----------
void Config::load() {
    kv.clear();
    for (auto &l : split(read_file(P.config + "/settings.ini"), '\n')) {
        if (l.empty() || l[0] == '#') continue;
        size_t e = l.find('='); if (e == std::string::npos) continue;
        std::string v = l.substr(e + 1);
        for (size_t i = 0; (i = v.find("\\n", i)) != std::string::npos;) { v.replace(i, 2, "\n"); i++; }
        kv[trim(l.substr(0, e))] = v;
    }
}
void Config::save() const {
    std::string s = "# Peanuts Native settings\n";
    for (auto &p : kv) { std::string v = p.second; for (size_t i = 0; (i = v.find('\n', i)) != std::string::npos;) { v.replace(i, 1, "\\n"); i += 2; } s += p.first + "=" + v + "\n"; }
    write_file(P.config + "/settings.ini", s);
}
std::string Config::get(const std::string &k, const std::string &def) const { auto i = kv.find(k); return i == kv.end() ? def : i->second; }
bool Config::on(const std::string &k, bool def) const { auto i = kv.find(k); return i == kv.end() ? def : i->second == "1" || i->second == "true"; }
int Config::num(const std::string &k, int def) const { auto i = kv.find(k); return i == kv.end() || i->second.empty() ? def : atoi(i->second.c_str()); }
void Config::set(const std::string &k, const std::string &v) { kv[k] = v; save(); }
std::string Config::game(const std::string &tid, const std::string &k, const std::string &def) const { auto i = kv.find(tid + "." + k); return i != kv.end() ? i->second : get(k, def); }

// ---------- processes ----------
static std::vector<std::string> merged_env(const std::vector<std::string> &extra) {
    std::map<std::string, std::string> m; std::vector<std::string> order;
    for (char **e = environ; *e; e++) { std::string s = *e; size_t k = s.find('='); if (k == std::string::npos) continue; std::string key = s.substr(0, k); if (!m.count(key)) order.push_back(key); m[key] = s; }
    for (auto &s : extra) { size_t k = s.find('='); if (k == std::string::npos) continue; std::string key = s.substr(0, k); if (!m.count(key)) order.push_back(key); m[key] = s; }
    std::vector<std::string> out; for (auto &k : order) out.push_back(m[k]); return out;
}
pid_t spawn(const std::vector<std::string> &argv, const std::vector<std::string> &env, int out_fd, bool own_group) {
    std::vector<std::string> envs = merged_env(env);
    std::vector<char *> a, e; for (auto &s : argv) a.push_back((char *)s.c_str()); a.push_back(nullptr);
    for (auto &s : envs) e.push_back((char *)s.c_str()); e.push_back(nullptr);
    std::string path; for (auto &s : envs) if (s.rfind("PATH=", 0) == 0) path = s.substr(5);
    pid_t pid = fork();
    if (pid == 0) {
        if (own_group) setpgid(0, 0);
        int dn = open("/dev/null", O_RDWR); dup2(dn, 0);
        int o = out_fd >= 0 ? out_fd : dn; dup2(o, 1); dup2(o, 2);
        for (int fd = 3; fd < 1024; fd++) close(fd);
        signal(SIGPIPE, SIG_DFL); signal(SIGCHLD, SIG_DFL);
        if (argv[0].find('/') != std::string::npos) execve(a[0], a.data(), e.data());
        else for (auto &d : split(path, ':')) { std::string p = (d.empty() ? "." : d) + "/" + argv[0]; execve(p.c_str(), a.data(), e.data()); }
        _exit(127);
    }
    return pid;
}
int run_capture(const std::vector<std::string> &argv, const std::vector<std::string> &env, std::string &out) {
    int p[2]; if (pipe2(p, O_CLOEXEC)) return -1;
    pid_t pid = spawn(argv, env, p[1], false); close(p[1]);
    out.clear(); char b[8192]; ssize_t n; while ((n = read(p[0], b, sizeof b)) > 0) out.append(b, (size_t)n); close(p[0]);
    if (pid < 0) return -1;
    int st = 0; while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
}

void Job::add_line(const std::string &l0) {
    std::string l = l0; while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
    std::lock_guard<std::mutex> g(mu);
    lines.push_back(l); while (lines.size() > 400) lines.pop_front();
    if (l.find("[RED]") != std::string::npos || l.rfind("error:", 0) == 0) { errors.push_back(l); if (errors.size() > 40) errors.erase(errors.begin()); }
}
void Job::cancel() {
    cancelled = true; pid_t p = pid;
    if (p > 0) { kill(-p, SIGTERM); kill(p, SIGTERM); std::thread([p] { sleep(4); kill(-p, SIGKILL); kill(p, SIGKILL); }).detach(); }
}
std::shared_ptr<Job> start_job(const std::string &title, const std::string &log_path, std::vector<Step> steps) {
    auto j = std::make_shared<Job>(); j->title = title; j->log_path = log_path; j->running = true; j->started = now_s();
    std::thread([j, steps] {
        int rc = 0;
        for (auto &s : steps) { if (j->cancelled) { rc = 130; break; } rc = s(*j); if (rc) break; }
        if (j->cancelled && !rc) rc = 130;
        j->rc = rc; j->ended = now_s(); j->running = false; j->done = true;
    }).detach();
    return j;
}
int job_run(Job &j, const std::vector<std::string> &argv, const std::vector<std::string> &env, std::function<void(Job &, const std::string &)> on_line) {
    int p[2]; if (pipe2(p, O_CLOEXEC)) return -1;
    FILE *log = j.log_path.empty() ? nullptr : fopen(j.log_path.c_str(), "ab");
    if (log) { fprintf(log, "+"); for (auto &a : argv) fprintf(log, " %s", a.c_str()); fprintf(log, "\n"); fflush(log); }
    pid_t pid = spawn(argv, env, p[1], true); close(p[1]);
    j.pid = pid;
    std::string buf; char b[4096]; ssize_t n;
    while ((n = read(p[0], b, sizeof b)) > 0) {
        if (log) { fwrite(b, 1, (size_t)n, log); fflush(log); }
        buf.append(b, (size_t)n);
        size_t k;
        while ((k = buf.find_first_of("\n\r")) != std::string::npos) {
            std::string line = buf.substr(0, k); buf.erase(0, k + 1);
            if (line.empty()) continue;
            j.add_line(line); if (on_line) on_line(j, line);
        }
    }
    if (!buf.empty()) { j.add_line(buf); if (on_line) on_line(j, buf); }
    close(p[0]);
    int st = 0; if (pid > 0) while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    j.pid = -1;
    int rc = pid < 0 ? 127 : WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
    if (log) { fprintf(log, "(exit code %d)\n", rc); fclose(log); }
    return rc;
}

// ---------- catalog ----------
void Catalog::load(const std::string &path) {
    by_tid.clear();
    for (auto &l : split(read_file(path), '\n')) {
        if (l.empty() || l[0] == '#') continue;
        auto c = split(l, '\t'); if (c.size() < 4) continue;
        CatalogEntry e; e.tid = c[0]; e.name = c[1]; e.companion = c[3] == "1";
        for (auto &b : split(c[2], ',')) { size_t k = b.find('='); if (k == std::string::npos) continue; e.builds.push_back({b.substr(0, k), (uint32_t)strtoul(b.c_str() + k + 1, nullptr, 10)}); }
        by_tid[e.tid] = e;
    }
}
const CatalogEntry *Catalog::find(const std::string &tid) const { auto i = by_tid.find(tid); return i == by_tid.end() ? nullptr : &i->second; }
std::string Game::supported_versions() const {
    if (!cat) return "";
    std::string s; for (auto &b : cat->builds) { if (!s.empty()) s += ", "; s += b.first; }
    return s;
}

// ---------- tools ----------
static std::vector<std::string> tool_env() {
    std::string path = P.bin + ":" + P.dotnet + ":" + (getenv("PATH") ? getenv("PATH") : "/usr/bin:/bin");
    return {"PATH=" + path, "NX_IMPORTER=" + P.bin + "/nximport_cli", "NX_KEYS=" + C.get("keys"), "DOTNET_ROOT=" + P.dotnet,
            "DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1", "DOTNET_CLI_TELEMETRY_OPTOUT=1", "DOTNET_NOLOGO=1", "DOTNET_gcServer=0",
            "PYTHONUNBUFFERED=1", "PYTHONUTF8=1", "PYTHONDONTWRITEBYTECODE=1", "PYTHONNOUSERSITE=1",
            "DM_PACKER_COMMIT=" + trim(read_file(P.engine + "/PNL_UPSTREAM_COMMIT"))};   // pack.tsv's packer row (no git in the engine copy)
}
bool keys_ok() { std::string k = C.get("keys"); return is_file(k) && read_file(k, 1 << 16).find("header_key") != std::string::npos; }
bool tools_ok(std::string *why) {
    struct { const char *what; std::string path; } need[] = {{"the game player", P.bin + "/peanuts-run"}, {"the importer", P.bin + "/nximport_cli"},
        {"Python", P.python}, {".NET", P.dotnet + "/dotnet"}, {"spirv-val", P.bin + "/spirv-val"}};
    for (auto &n : need) if (!is_file(n.path)) { if (why) *why = std::string(n.what) + " is missing (" + n.path + ")"; return false; }
    return true;
}
bool engine_ok() {
    bool ok = !P.engine_version.empty() && trim(read_file(P.engine + "/PNL_ENGINE_VERSION")) == P.engine_version && is_file(P.engine + "/peanuts/scripts/peanuts_build.py");
    // The copy's content (PNL_ENGINE_STAMP: the upstream commit and the port's edits) can change while the build identity stays: then it is copied again
    // (the Prepare step, a minute), and games built before stay ready
    std::string stamp = trim(read_file(P.engine_src + "/PNL_ENGINE_STAMP"));
    if (ok && !stamp.empty() && trim(read_file(P.engine + "/PNL_ENGINE_STAMP")) != stamp) ok = false;
    // The launcher's helper is not part of the build engine's identity: a newer release brings a newer one without a rebuild of the games
    // and so are the files PNL_ENGINE_SYNC lists (fixes that need no rebuild of games already built, e.g. a game's patches for an update it gained)
    if (ok) {
        std::vector<std::string> files = {"pnl_tool.py"};
        for (auto &l : split(read_file(P.engine_src + "/PNL_ENGINE_SYNC"), '\n')) { std::string f = trim(l); if (!f.empty() && f[0] != '#' && f.find("..") == std::string::npos) files.push_back(f); }
        for (auto &f : files) { std::string mine = read_file(P.engine_src + "/" + f, 16 << 20); if (!mine.empty() && read_file(P.engine + "/" + f, 16 << 20) != mine) write_file(P.engine + "/" + f, mine); }
    }
    return ok;
}
std::shared_ptr<Job> prepare_engine_job() {
    return start_job("Preparing the build engine", P.logs + "/engine.log", {
        [](Job &j) { j.set_phase("Copying the build tools (one time, about a minute)"); std::string tmp = P.engine + ".new";
                     return job_run(j, {"sh", "-c", "rm -rf \"$1\" && cp -a \"$2\" \"$1\"", "sh", tmp, P.engine_src}, {}); },
        [](Job &j) { j.set_phase("Finishing");   // the prebuilt importer, Oodle worker and Python modules are newer than their sources (peanuts_build.py rebuilds older ones)
                     std::string tmp = P.engine + ".new";
                     return job_run(j, {"sh", "-c", "find \"$1/peanuts/.build\" -type f -exec touch {} + 2>/dev/null; rm -rf \"$2.old\"; [ ! -e \"$2\" ] || mv \"$2\" \"$2.old\"; mv \"$1\" \"$2\" && rm -rf \"$2.old\"",
                                        "sh", tmp, P.engine}, {}); },
        [](Job &j) { j.set_phase("Fonts"); mkdirs(P.data + "/fonts");   // the shared-font stand-ins (GameFonts.java): the host serves them unless firmware fonts are set
                     clocks_installer_path();   // the max-clocks installer beside the data, for a terminal (README)
                     return job_run(j, {"sh", "-c", "cp -f \"$1\"/standard.ttf \"$1\"/nintendo_ext.ttf \"$2\"/ 2>/dev/null; true", "sh", P.share + "/fonts", P.data + "/fonts"}, {}); },
    });
}
std::shared_ptr<Job> extract_fonts_job(const std::string &firmware) {
    std::string out = P.data + "/firmware/fonts";
    return start_job("Reading the firmware", P.logs + "/firmware.log", {
        [firmware, out](Job &j) { j.set_phase("Taking the console's fonts from your firmware"); mkdirs(out);
            return job_run(j, {P.bin + "/nximport_cli", "--keys", C.get("keys"), "fonts", firmware, out}, tool_env()); },
    });
}
std::vector<std::string> detect_keys() {
    std::string h = home();
    std::vector<std::string> c = {h + "/.switch/prod.keys", h + "/.local/share/eden/keys/prod.keys", h + "/.local/share/citron/keys/prod.keys",
        h + "/.local/share/yuzu/keys/prod.keys", h + "/.local/share/suyu/keys/prod.keys", h + "/.local/share/sudachi/keys/prod.keys",
        h + "/.config/Ryujinx/system/prod.keys", h + "/.var/app/io.github.ryubing.Ryujinx/config/Ryujinx/system/prod.keys",
        h + "/Emulation/bios/switch/prod.keys", h + "/Emulation/bios/yuzu/prod.keys", h + "/Emulation/bios/eden/keys/prod.keys",
        h + "/.var/app/dev.eden_emu.eden/data/eden/keys/prod.keys", h + "/Documents/prod.keys", h + "/Downloads/prod.keys"};
    std::vector<std::string> out; for (auto &p : c) if (is_file(p)) out.push_back(p); return out;
}

// ---------- the library ----------
std::string builds_root() { std::string b = C.get("builds_dir"); return b.empty() ? P.data + "/builds" : b; }
static bool game_ext(const std::string &n, bool *nup) {
    std::string l = lower(n); auto ends = [&](const char *e) { size_t k = strlen(e); return l.size() > k && l.compare(l.size() - k, k, e) == 0; };
    *nup = ends(".nup");
    return *nup || ends(".nsp") || ends(".xci") || ends(".nsz") || ends(".xcz");
}
static void walk(const std::string &dir, int depth, std::vector<std::pair<std::string, uint64_t>> &out) {
    DIR *d = opendir(dir.c_str()); if (!d) return;
    while (struct dirent *e = readdir(d)) {
        std::string n = e->d_name; if (n.empty() || n[0] == '.') continue;
        std::string p = dir + "/" + n; struct stat st; if (stat(p.c_str(), &st)) continue;
        bool nup;
        if (S_ISDIR(st.st_mode)) { if (depth < 4) walk(p, depth + 1, out); }
        else if (S_ISREG(st.st_mode) && game_ext(n, &nup)) out.push_back({p, (uint64_t)st.st_size});
    }
    closedir(d);
}
struct ProbeLine { std::string title, app, kind, name; uint32_t version; };
static std::map<std::string, std::vector<ProbeLine>> load_probe_cache() {
    std::map<std::string, std::vector<ProbeLine>> m;
    for (auto &l : split(read_file(P.cache + "/probe.tsv", 16 << 20), '\n')) {
        auto c = split(l, '\t'); if (c.size() != 6) continue;
        m[c[0]].push_back({c[1], c[2], c[3], c[5], (uint32_t)strtoul(c[4].c_str(), nullptr, 10)});
    }
    return m;
}
static std::string cache_key(const std::string &p) { struct stat st; if (stat(p.c_str(), &st)) return p; return p + "|" + std::to_string(st.st_size) + "|" + std::to_string(st.st_mtime); }

void refresh_build(Game &g) {
    g.build_dir = builds_root() + "/" + g.tid;
    g.pack_dir.clear(); g.nup.clear(); g.built_version.clear();
    std::string pack = g.build_dir + "/" + g.tid + "-work/android-pack";
    std::string tsv = read_file(pack + "/pack.tsv");
    if (!tsv.empty()) {
        std::string romfs;
        for (auto &l : split(tsv, '\n')) { auto c = split(l, '\t'); if (c.size() >= 2 && c[0] == "version") g.built_version = c[1]; if (c.size() >= 2 && c[0] == "romfs") romfs = c[1]; }
        size_t k = romfs.find(".nup:");
        std::string nup = k == std::string::npos ? "" : g.build_dir + "/" + romfs.substr(0, k + 4);
        if (is_file(nup) && is_file(g.build_dir + "/.pnl-built")) { g.pack_dir = pack; g.nup = nup; }
    }
    if (!g.cat) g.state = GameState::Unsupported;
    else if (g.pack_dir.empty()) g.state = GameState::NeedsBuild;
    else {
        std::string stamp = trim(read_file(g.build_dir + "/.pnl-built"));   // "<engine version> <game version>"
        std::string mk = code_mods_key(g.tid);
        bool same = stamp == P.engine_version + " " + std::to_string(g.version) + (mk.empty() ? "" : " " + mk);
        g.state = same ? GameState::Ready : GameState::NeedsRebuild;
    }
    std::string icon = P.cache + "/icons/" + g.tid + ".png";
    g.icon_path = is_file(icon) ? icon : "";
}

void Library::rescan() {
    if (scanning.exchange(true)) return;
    std::thread([this] {
        std::string dir = C.get("games_dir"); std::string keys = C.get("keys");
        std::vector<std::pair<std::string, uint64_t>> files; if (is_dir(dir)) walk(dir, 0, files);
        // the updates & DLC folder (Settings): its updates and add-ons join the games of the games folder; a game only there is not listed
        std::set<std::string> extra;
        std::string ud = C.get("updates_dir");
        if (!ud.empty() && is_dir(ud)) {
            std::vector<std::pair<std::string, uint64_t>> more; walk(ud, 0, more);
            std::set<std::string> have; for (auto &f : files) have.insert(f.first);
            for (auto &f : more) if (!have.count(f.first)) { files.push_back(f); extra.insert(f.first); }
        }
        std::sort(files.begin(), files.end());
        scan_total = (int)files.size(); scan_done = 0;
        auto cache = load_probe_cache(); std::map<std::string, std::vector<ProbeLine>> found;
        std::vector<std::string> to_probe;
        for (auto &f : files) { auto key = cache_key(f.first); auto it = cache.find(key); if (it != cache.end()) { found[f.first] = it->second; scan_done++; } else to_probe.push_back(f.first); }
        // probe the new files: .nup through pnl_tool.py, the rest with the importer, a few at a time
        static const std::regex FILE_RE(R"(^(.+)  \((\d+) titles?, [0-9.]+s\)$)");
        static const std::regex TITLE_RE(R"re(^\s+title ([0-9A-Fa-f]{16})\s+app ([0-9A-Fa-f]{16})\s+kind (\S+)\s+version (\d+)\s+name "(.*)"\s+publisher ".*"\s*$)re");
        std::vector<std::string> batch;
        auto flush = [&] {
            if (batch.empty()) return;
            std::vector<std::string> argv = {P.bin + "/nximport_cli", "--keys", keys, "probe"}; argv.insert(argv.end(), batch.begin(), batch.end());
            std::string out; run_capture(argv, tool_env(), out);
            std::string cur; std::smatch m;
            for (auto &l : split(out, '\n')) {
                if (std::regex_match(l, m, FILE_RE)) { cur = m[1]; found[cur]; continue; }
                if (!cur.empty() && std::regex_match(l, m, TITLE_RE)) found[cur].push_back({std::string(m[1]), std::string(m[2]), std::string(m[3]), std::string(m[5]), (uint32_t)strtoul(std::string(m[4]).c_str(), nullptr, 10)});
            }
            for (auto &b : batch) if (!found.count(b)) found[b] = {};   // unreadable (wrong keys?): remembered as nothing
            scan_done += (int)batch.size(); batch.clear();
        };
        for (auto &f : to_probe) {
            bool nup; game_ext(f, &nup);
            if (nup) {
                std::string out; run_capture({P.python, P.engine + "/pnl_tool.py", "probe-nup", f}, tool_env(), out);
                ProbeLine pl{}; pl.kind = "nup";
                for (auto &l : split(out, '\n')) { size_t k = l.find(' '); if (k == std::string::npos) continue; std::string key = l.substr(0, k), v = l.substr(k + 1);
                    if (key == "title") pl.title = v; else if (key == "app") pl.app = v; else if (key == "version") pl.version = (uint32_t)atoi(v.c_str()); else if (key == "name") pl.name = v; }
                found[f] = pl.app.empty() ? std::vector<ProbeLine>{} : std::vector<ProbeLine>{pl}; scan_done++;
            } else { batch.push_back(f); if (batch.size() >= 8) flush(); }
        }
        flush();
        { int bad = 0; for (auto &f : files) if (found[f.first].empty()) bad++; unreadable = bad; }
        // the cache, again
        std::string cs; for (auto &f : files) for (auto &pl : found[f.first]) cs += cache_key(f.first) + "\t" + pl.title + "\t" + pl.app + "\t" + pl.kind + "\t" + std::to_string(pl.version) + "\t" + pl.name + "\n";
        write_file(P.cache + "/probe.tsv", cs);
        // group by application
        std::map<std::string, Game> by;
        for (auto &f : files) for (auto &pl : found[f.first]) {
            std::string app = pl.app; for (auto &c : app) c = (char)toupper((unsigned char)c);
            if (app.size() != 16) continue;
            if (extra.count(f.first) && pl.kind != "update" && pl.kind != "dlc") continue;   // a base game or .nup in the updates folder: not taken from there
            Game &g = by[app]; g.tid = app;
            if (!extra.count(f.first)) g.in_games_dir = true;
            if (pl.kind == "base") { g.base = f.first; if (!pl.name.empty()) g.name = pl.name; }
            else if (pl.kind == "update") { GameFile u; u.path = f.first; u.version = pl.version; u.kind = "update"; u.size = f.second; g.updates.push_back(u); if (g.name.empty() && !pl.name.empty()) g.name = pl.name; }
            else if (pl.kind == "dlc") g.dlcs.push_back(f.first);
            else if (pl.kind == "nup") { if (g.base.empty() || !g.is_nup) { g.base = f.first; g.is_nup = true; g.version = pl.version; } if (g.name.empty()) g.name = pl.name; }
        }
        std::vector<Game> out;
        for (auto &kv : by) {
            Game g = kv.second;
            if (g.base.empty() && g.updates.empty()) continue;   // DLC alone
            if (!g.in_games_dir) continue;   // only updates in the updates folder: a game you do not have in the games folder
            g.cat = CAT.find(g.tid);
            if (g.cat && (g.name.empty() || g.name == "?")) g.name = g.cat->name;
            if (g.name.empty()) g.name = basename_of(g.base.empty() ? g.updates[0].path : g.base);
            std::sort(g.updates.begin(), g.updates.end(), [](const GameFile &a, const GameFile &b) { return a.version > b.version; });
            if (!g.is_nup) {
                g.version = g.updates.empty() ? 0 : g.updates[0].version; g.version_supported = false;
                std::vector<uint32_t> have = {}; if (!g.base.empty()) have.push_back(0); for (auto &u : g.updates) have.push_back(u.version);
                if (g.cat) for (uint32_t v : have) { for (auto &b : g.cat->builds) if (b.second == v && (!g.version_supported || v > g.version)) { g.version = v; g.version_supported = true; } }
                // the version the player chose in Game settings, when that file is here
                std::string pick = C.game(g.tid, "version_pick", "");
                if (!pick.empty() && pick.find_first_not_of("0123456789") == std::string::npos) {
                    uint32_t pv = (uint32_t)strtoul(pick.c_str(), nullptr, 10);
                    if (std::find(have.begin(), have.end(), pv) != have.end()) {
                        g.version = pv; g.version_supported = false;
                        if (g.cat) for (auto &b : g.cat->builds) if (b.second == pv) g.version_supported = true;
                    }
                }
            } else if (g.cat) for (auto &b : g.cat->builds) if (b.second == g.version) g.version_supported = true;
            if (g.base.empty() && !g.updates.empty() && !g.is_nup) g.base = "";   // an update alone: the builder needs the base too
            refresh_build(g);
            // Automatic keeps the update a game was last built from (when its file is still here), so Build again does not drop back to another version
            if (!g.is_nup && C.game(g.tid, "version_pick", "").empty() && !g.built_version.empty()) {
                uint32_t bv = (uint32_t)strtoul(g.built_version.c_str(), nullptr, 10);
                bool here = bv == 0 ? !g.base.empty() : std::any_of(g.updates.begin(), g.updates.end(), [&](const GameFile &u) { return u.version == bv; });
                if (here && bv != g.version) {
                    g.version = bv; g.version_supported = false;
                    if (g.cat) for (auto &b : g.cat->builds) if (b.second == bv) g.version_supported = true;
                    refresh_build(g);
                }
            }
            out.push_back(std::move(g));
        }
        std::sort(out.begin(), out.end(), [](const Game &a, const Game &b) {
            if ((a.cat != nullptr) != (b.cat != nullptr)) return a.cat != nullptr;
            return lower(a.name) < lower(b.name); });
        { std::lock_guard<std::mutex> g(mu); games = std::move(out); scan_error = is_dir(dir) ? "" : "The games folder is not there: " + dir; }
        generation++;
        // the icons, after the list is up
        mkdirs(P.cache + "/icons");
        for (auto &g : snapshot()) {
            std::string icon = P.cache + "/icons/" + g.tid + ".png"; if (is_file(icon)) continue;
            std::string src = g.base.empty() && !g.updates.empty() ? g.updates[0].path : g.base;
            std::string o;
            if (g.is_nup) { std::string beside = g.base.substr(0, g.base.size() - 4) + ".png"; if (is_file(beside)) run_capture({"cp", beside, icon}, {}, o); }
            else if (!src.empty()) run_capture({P.bin + "/nximport_cli", "--keys", keys, "icon", src, g.tid, icon}, tool_env(), o);
            if (is_file(icon)) { std::lock_guard<std::mutex> l(mu); for (auto &x : games) if (x.tid == g.tid) x.icon_path = icon; generation++; }
        }
        scanning = false;
    }).detach();
}
void Library::refresh_builds() { std::lock_guard<std::mutex> g(mu); for (auto &x : games) refresh_build(x); generation++; }
std::vector<Game> Library::snapshot() { std::lock_guard<std::mutex> g(mu); return games; }

// ---------- build ----------
std::shared_ptr<Job> build_job(const Game &g0, bool force) {
    Game g = g0;
    std::string log = P.logs + "/build-" + g.tid + "-" + now_stamp() + ".log";
    return start_job("Building " + g.name, log, {
        [g, force](Job &j) {
            if (!engine_ok()) { j.add_line("[RED] the build engine is not ready: restart Peanuts Native"); return 1; }
            mkdirs(g.build_dir); unlink((g.build_dir + "/.pnl-built").c_str());
            std::vector<std::string> argv = {P.python, P.engine + "/peanuts/scripts/peanuts_build.py"};
            if (g.is_nup) argv.push_back(g.base);
            else {
                if (g.base.empty()) { j.add_line("[RED] the base game's file is not in your games folder: the build needs it with the update"); return 1; }
                argv.push_back(g.base);
                for (auto &u : g.updates) if (u.version == g.version) argv.push_back(u.path);
                for (auto &d : g.dlcs) argv.push_back(d);
            }
            argv.insert(argv.end(), {"--keys", C.get("keys"), "--nup", g.build_dir});
            if (!g.is_nup && g.version) { argv.push_back("--update"); argv.push_back(std::to_string(g.version)); }
            if (force) argv.push_back("--force-nup");
            if (!g.version_supported) argv.push_back("--any-version");
            // code mods (exefs / exefs_patches of the mods that are on): pack_android.py patches the modules before it rewrites them
            {
                std::vector<std::string> modules;
                for (auto &l : split(read_file(P.engine + "/dm/games/" + g.tid + "/game.tsv"), '\n')) if (l.rfind("modules\t", 0) == 0) modules = split(trim(l.substr(8)), ' ');
                std::vector<std::string> extra;
                for (auto &m : mods_of(g.tid)) {
                    if (!m.enabled || !m.code) continue;
                    std::vector<std::string> patch_dirs;
                    std::string ex = m.dir + "/exefs";
                    if (is_dir(ex)) {
                        bool ips = false;
                        if (DIR *d = opendir(ex.c_str())) { while (dirent *e = readdir(d)) {
                            std::string n = e->d_name, ln = lower(n);
                            if (ln.size() > 4 && (ln.compare(ln.size() - 4, 4, ".ips") == 0 || (ln.size() > 6 && ln.compare(ln.size() - 6, 6, ".ips32") == 0))) ips = true;
                            else if (std::find(modules.begin(), modules.end(), n) != modules.end() && is_file(ex + "/" + n)) { extra.push_back("--mod-exefs"); extra.push_back(n + "=" + ex + "/" + n); }
                          } closedir(d); }
                        if (ips) patch_dirs.push_back(ex);
                        // IPSwitch text patches (yuzu / Eden's exefs/*.pchtxt): made into IPS32 files, one folder each, for the builder
                        int nth = 0;
                        if (DIR *d = opendir(ex.c_str())) { while (dirent *e = readdir(d)) {
                            std::string n = e->d_name, ln = lower(n);
                            if (ln.size() <= 7 || ln.compare(ln.size() - 7, 7, ".pchtxt") != 0) continue;
                            std::string od = g.build_dir + "/.mod-pchtxt/" + m.name + "/" + std::to_string(nth++), o;
                            run_capture({"rm", "-rf", od}, {}, o);
                            int rc = run_capture({P.python, P.engine + "/pnl_tool.py", "pchtxt", ex + "/" + n, od}, tool_env(), o);
                            j.add_line((rc ? "[RED] " : "") + std::string("code mod ") + m.name + ": " + n + ": " + trim(o));
                            if (!rc) patch_dirs.push_back(od);
                          } closedir(d); }
                    }
                    std::string ep = m.dir + "/exefs_patches";
                    if (DIR *d = opendir(ep.c_str())) { while (dirent *e = readdir(d)) { std::string n = e->d_name; if (n[0] != '.' && is_dir(ep + "/" + n)) patch_dirs.push_back(ep + "/" + n); } closedir(d); }
                    for (auto &pd : patch_dirs) for (auto &mod : modules) { extra.push_back("--mod-patches"); extra.push_back(mod + "=" + pd); }
                    j.add_line("code mod: " + m.name);
                }
                if (!extra.empty()) { argv.push_back("--"); argv.insert(argv.end(), extra.begin(), extra.end()); }
            }
            j.set_phase("Starting");
            auto on_line = [](Job &jj, const std::string &l) {
                // the steps peanuts_build.py and pack_android.py print, as phases for the screen
                static const std::pair<const char *, const char *> marks[] = {
                    {"building the importer", "Preparing the importer"}, {"import", "Importing your game files"}, {"nximport_cli", "Importing your game files"},
                    {"pack_android.py", "Rewriting the game's code"}, {"rewrit", "Rewriting the game's code"}, {"patch_nso", "Rewriting the game's code"},
                    {"shader", "Translating shaders (the longest step)"}, {"split", "Reading the game's shaders"}, {"translat", "Translating shaders (the longest step)"},
                    {"done:", "Finishing"}};
                std::string ll = lower(l);
                for (auto &m : marks) if (ll.find(m.first) != std::string::npos) { jj.set_phase(m.second); break; }
                unsigned d = 0, t = 0; char ph[128];
                if (sscanf(l.c_str(), "progress %u %u %127[^\n]", &d, &t, ph) >= 2 && t) jj.progress = (float)d / (float)t;
                else { size_t pct = l.find('%'); if (pct != std::string::npos && pct >= 1 && pct <= 5) { float v = (float)atof(l.c_str()); if (v > 0 && v <= 100) jj.progress = v / 100.f; } }
            };
            int rc = job_run(j, argv, tool_env(), on_line);
            if (rc) return rc;
            Game t = g; refresh_build(t);
            std::string pack = g.build_dir + "/" + g.tid + "-work/android-pack";
            if (!is_file(pack + "/pack.tsv")) { j.add_line("[RED] the builder finished without making " + pack + "/pack.tsv"); return 1; }
            { std::string mk = code_mods_key(g.tid); write_file(g.build_dir + "/.pnl-built", P.engine_version + " " + std::to_string(g.version) + (mk.empty() ? "" : " " + mk) + "\n"); }
            // the title's icon the builder writes beside the .nup
            std::string icon = P.cache + "/icons/" + g.tid + ".png";
            if (!is_file(icon)) { std::string o; run_capture({"sh", "-c", "for f in \"$1\"/*.png; do [ -f \"$f\" ] && cp \"$f\" \"$2\" && break; done", "sh", g.build_dir, icon}, {}, o); }
            j.set_phase("Ready to play"); j.progress = 1.f;
            return 0;
        }});
}

// ---------- clocks ----------
static const char *CLOCKS_HELPER = "/etc/peanuts-native/pn-clocks";
bool clocks_helper_installed() { return is_file(CLOCKS_HELPER) && is_file("/etc/sudoers.d/peanuts-native-clocks"); }
std::string clocks_installer_path() {
    std::string src = read_file(P.share + "/install-max-clocks.sh");
    std::string dst = P.data + "/install-max-clocks.sh";
    write_file(dst, src); chmod(dst.c_str(), 0755); return dst;
}
void clocks(bool on) {
    if (!clocks_helper_installed()) return;
    std::string out; run_capture({"sudo", "-n", CLOCKS_HELPER, on ? "on" : "off"}, {}, out);
}

// ---------- play ----------
pid_t play_start(const Game &g, std::string &log_path, std::string &error) {
    if (g.pack_dir.empty() || g.nup.empty()) { error = "This game is not built yet."; return -1; }
    std::string out;
    int rc = run_capture({P.python, P.engine + "/pnl_tool.py", "launch", g.pack_dir, g.nup, P.data}, tool_env(), out);
    std::vector<std::string> env; std::string save_id = g.tid;
    for (auto &l : split(out, '\n')) {
        if (l.rfind("error: ", 0) == 0) { error = l.substr(7); return -1; }
        if (l.rfind("PNL_SAVE_ID=", 0) == 0) save_id = l.substr(12);
        if (l.find('=') != std::string::npos && (l.rfind("DM_", 0) == 0 || l.rfind("PNL_", 0) == 0)) env.push_back(l);
    }
    if (rc) { error = "The game could not be prepared (" + std::to_string(rc) + "): " + trim(out); return -1; }
    std::string save = P.data + "/saves/" + save_id, temp = P.data + "/temp/" + g.tid, root = P.data + "/root", tmp = P.cache + "/tmp";
    mkdirs(save); mkdirs(temp); mkdirs(root); mkdirs(tmp);
    env.insert(env.end(), {"DM_SAVE=" + save, "DM_TEMP=" + temp, "DM_ROOT=" + root, "TMPDIR=" + tmp, "DM_FONT_DIR=" + P.data + "/fonts",
                           "DM_BUILD_COMMIT=peanuts-native-linux " + P.version, "PNL_TITLE=" + g.name});
    // mods: the romfs folders of the mods that are on, first one wins (the host's DM_ROMFS_OVERLAY; the companion reads the same files)
    { std::string ov; for (auto &m : mods_of(g.tid)) if (m.enabled && m.romfs && m.dir.find(':') == std::string::npos) ov += (ov.empty() ? "" : ":") + m.dir + "/romfs";
      if (!ov.empty()) env.push_back("DM_ROMFS_OVERLAY=" + ov); }
    // the bottom screen: the game's companion package in place, or off
    if (C.game(g.tid, "bottom_screen", "1") == "1" && companion_install(g)) env.push_back("PNL_BOTTOM_LOG=" + P.logs + "/" + g.tid + "-bottom.log");
    else env.push_back("PN_COMPANION=off");
    std::string fw = P.data + "/firmware/fonts";
    if (is_file(fw + "/standard.ttf")) { env.push_back("DM_SHARED_FONT=" + fw + "/standard.ttf"); if (is_file(fw + "/nintendo_ext.ttf")) env.push_back("DM_SHARED_FONT_EXT=" + fw + "/nintendo_ext.ttf"); }
    if (is_file(P.data + "/firmware/miimodel/ShapeHigh.dat")) env.push_back("DM_MII_MODEL=" + P.data + "/firmware/miimodel");
    // the game's settings (Game settings in the launcher), each with its global default
    // Mesa 26.3's Turnip draws some games' scenery wrong from pipelines linked out of graphics pipeline libraries (Mystery Dungeon DX: the world broken, the
    // Pokémon fine; DM_VK_GPL=0 fixed it on the Thor). Whole pipelines instead, on Linux by default; "DM_VK_GPL=1" in a game's extra environment turns them back on
    env.push_back("DM_VK_GPL=0");
    // Per game, found on the Thor (ArmadaOS, Mesa 26.3 Turnip); a game's extra environment comes later and wins
    static const std::pair<const char *, const char *> game_env[] = {
        {"01003D200BAA2000", "DM_VK_POOL_FB=0"},   // Mystery Dungeon DX: menu art, the "!" emote and the sky scrambled with the texture pool reads on
        {"01003D200BAA2000", "DM_VK_CS_ASYNC=0"},  // Mystery Dungeon DX: models burst into spikes now and then with compute work (skinning) run beside the draws
    };
    for (auto &ge : game_env) if (g.tid == ge.first) env.push_back(ge.second);
    env.push_back("DM_VK_PSO_ASYNC=" + std::string(C.game(g.tid, "pso_async", "1") == "1" ? "2" : "0"));
    std::string mode = C.game(g.tid, "console_mode", "auto"); if (mode == "handheld") env.push_back("DM_OPMODE=0"); else if (mode == "docked") env.push_back("DM_OPMODE=1");
    if (C.game(g.tid, "ram_saver", "0") == "1") { env.push_back("DM_MEM_FILEBACK=1"); env.push_back("DM_MEM_FILEBACK_KINDS=shader,texture"); }
    env.push_back("PNL_LAYOUT=" + C.game(g.tid, "layout", "switch"));
    env.push_back("PNL_DISPLAY=" + C.game(g.tid, "display", "0"));
    env.push_back("PNL_WINDOWED=" + C.game(g.tid, "windowed", "0"));
    env.push_back("PNL_GYRO=" + C.game(g.tid, "gyro", "1"));
    env.push_back("PNL_RUMBLE=" + C.game(g.tid, "rumble", "1"));
    env.push_back("PNL_QUIT_COMBO=" + C.get("quit_combo", "1"));
    for (auto &l : split(C.get(g.tid + ".env"), '\n')) { std::string t = trim(l); if (!t.empty() && t[0] != '#' && t.find('=') != std::string::npos) env.push_back(t); }   // extra lines win
    log_path = P.logs + "/" + g.tid + "-" + now_stamp() + ".log";
    int fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) { error = "Cannot write the log " + log_path; return -1; }
    std::string head = "Peanuts Native " + P.version + " (engine " + P.engine_version + ") — " + g.name + " [" + g.tid + "] " + version_display(g.version) + "\n";
    for (auto &e : env) head += "  " + e + "\n";
    if (write(fd, head.data(), head.size()) < 0) {}
    if (C.game(g.tid, "max_clocks", "1") == "1") clocks(true);
    std::vector<std::string> e2 = env; e2.push_back("LD_LIBRARY_PATH=" + dirname_of(P.bin) + "/lib" + (getenv("LD_LIBRARY_PATH") ? std::string(":") + getenv("LD_LIBRARY_PATH") : ""));
    pid_t pid = spawn({P.bin + "/peanuts-run", g.pack_dir}, e2, fd, true);
    close(fd);
    if (pid < 0) { error = "The game player did not start."; clocks(false); return -1; }
    return pid;
}
bool play_poll(pid_t pid, int &rc) {
    int st = 0; pid_t r = waitpid(pid, &st, WNOHANG);
    if (r == 0) return false;
    rc = r < 0 ? -1 : WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
    clocks(false);
    return true;
}

// ---------- the bottom screen ----------
static std::string json_str(const std::string &j, const std::string &key) {   // a top-level "key": "value" (package.json is small and flat where we read it)
    size_t k = j.find("\"" + key + "\""); if (k == std::string::npos) return "";
    k = j.find(':', k); if (k == std::string::npos) return "";
    k = j.find('"', k); if (k == std::string::npos) return "";
    std::string out; for (size_t i = k + 1; i < j.size() && j[i] != '"'; i++) { if (j[i] == '\\' && i + 1 < j.size()) i++; out += j[i]; }
    return out;
}
std::string companion_source(const std::string &tid) {
    for (auto &d : {P.data + "/companions/" + tid, P.share + "/companions/" + tid})
        if (is_file(d + "/package.json") && is_file(d + "/dualscreen/manifest.json")) return d;
    return "";
}
std::string companion_label(const std::string &tid) {
    std::string src = companion_source(tid); if (src.empty()) return "";
    std::string pj = read_file(src + "/package.json");
    std::string name = json_str(pj, "display_name"); if (name.empty()) name = json_str(pj, "name");
    std::string ver = json_str(pj, "version");
    std::string l = name.empty() ? "Companion" : name; if (!ver.empty()) l += " " + ver;
    if (src.rfind(P.data, 0) == 0) l += " (imported)";
    return l;
}
bool companion_install(const Game &g, std::string *why) {
    std::string src = companion_source(g.tid);
    if (src.empty()) { if (why) *why = "no companion package for this game"; return false; }
    std::string root = g.build_dir + "/" + g.tid + "-work/peanuts", dst = root + "/DualScreen-" + g.tid;
    std::string stamp = src + "\n" + read_file(src + "/package.json") + read_file(src + "/dualscreen/manifest.json");
    if (trim(read_file(dst + "/.pnl-source")) == trim(stamp) && is_file(dst + "/package.json")) return true;   // already in place
    mkdirs(root);
    std::string tmp = dst + ".new", old = dst + ".old", out;   // .new/.old: the host skips these while they exist
    run_capture({"rm", "-rf", tmp, old}, {}, out);
    if (run_capture({"cp", "-a", src, tmp}, {}, out) != 0) { if (why) *why = "could not copy the companion: " + trim(out); return false; }
    write_file(tmp + "/.pnl-source", stamp + "\n");
    if (is_dir(dst)) rename(dst.c_str(), old.c_str());
    if (rename(tmp.c_str(), dst.c_str()) != 0) { if (why) *why = "could not put the companion in place"; return false; }
    run_capture({"rm", "-rf", old}, {}, out);
    return true;
}
std::string companion_import(const std::string &zip, std::string &why) {
    std::string out;
    int rc = run_capture({P.python, P.engine + "/pnl_tool.py", "import-dsmod", zip, P.data + "/companions"}, tool_env(), out);
    std::string tid;
    for (auto &l : split(out, '\n')) { if (l.rfind("title_id=", 0) == 0) tid = trim(l.substr(9)); if (l.rfind("error: ", 0) == 0) why = l.substr(7); }
    if (rc != 0 || tid.empty()) { if (why.empty()) why = trim(out); return ""; }
    return tid;
}

// ---------- mods ----------
static std::string mods_root(const std::string &tid) { return P.data + "/mods/" + tid; }
std::vector<ModInfo> mods_of(const std::string &tid) {
    std::vector<ModInfo> out; std::string root = mods_root(tid);
    if (DIR *d = opendir(root.c_str())) {
        while (dirent *e = readdir(d)) {
            std::string n = e->d_name; if (n[0] == '.' || (n.size() > 8 && n.compare(n.size() - 8, 8, ".partial") == 0)) continue;
            ModInfo m; m.name = n; m.dir = root + "/" + n; if (!is_dir(m.dir)) continue;
            m.romfs = is_dir(m.dir + "/romfs"); m.code = is_dir(m.dir + "/exefs") || is_dir(m.dir + "/exefs_patches"); m.cheats = is_dir(m.dir + "/cheats");
            m.enabled = C.get(tid + ".mod:" + n, "1") == "1";
            out.push_back(m);
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end(), [](const ModInfo &a, const ModInfo &b) { return lower(a.name) < lower(b.name); });
    return out;
}
std::string mod_import(const std::string &tid, const std::string &path, std::string &why) {
    std::string out;
    int rc = run_capture({P.python, P.engine + "/pnl_tool.py", "import-mod", tid, path, mods_root(tid)}, tool_env(), out);
    std::string name;
    for (auto &l : split(out, '\n')) { if (l.rfind("name=", 0) == 0) name = trim(l.substr(5)); if (l.rfind("error: ", 0) == 0) why = l.substr(7); }
    if (rc != 0 || name.empty()) { if (why.empty()) why = trim(out); return ""; }
    return name;
}
bool mod_remove(const std::string &tid, const std::string &name) {
    if (name.empty() || name.find('/') != std::string::npos || name == "." || name == "..") return false;
    std::string out; return run_capture({"rm", "-rf", "--", mods_root(tid) + "/" + name}, {}, out) == 0;
}
void mod_enable(const std::string &tid, const std::string &name, bool on) { C.set(tid + ".mod:" + name, on ? "1" : "0"); }
static void tree_sig(const std::string &dir, uint64_t &h) {   // names, sizes and times of a folder's files
    if (DIR *d = opendir(dir.c_str())) {
        std::vector<std::string> names; while (dirent *e = readdir(d)) if (e->d_name[0] != '.') names.push_back(e->d_name);
        closedir(d); std::sort(names.begin(), names.end());
        for (auto &n : names) {
            std::string p = dir + "/" + n; struct stat st; if (stat(p.c_str(), &st)) continue;
            for (char c : n) h = (h ^ (unsigned char)c) * 1099511628211ull;
            if (S_ISDIR(st.st_mode)) tree_sig(p, h); else { h = (h ^ (uint64_t)st.st_size) * 1099511628211ull; h = (h ^ (uint64_t)st.st_mtime) * 1099511628211ull; }
        }
    }
}
std::string code_mods_key(const std::string &tid) {
    uint64_t h = 1469598103934665603ull; bool any = false;
    for (auto &m : mods_of(tid)) {
        if (!m.enabled || !m.code) continue;
        any = true; for (char c : m.name) h = (h ^ (unsigned char)c) * 1099511628211ull;
        tree_sig(m.dir + "/exefs", h); tree_sig(m.dir + "/exefs_patches", h);
    }
    if (!any) return "";
    char b[32]; snprintf(b, sizeof b, "mods-%016llx", (unsigned long long)h); return b;
}
// ---------- versions ----------
std::vector<uint32_t> versions_available(const Game &g) {
    std::vector<uint32_t> v; if (g.is_nup) { v.push_back(g.version); return v; }
    if (!g.base.empty()) v.push_back(0);
    for (auto &u : g.updates) if (std::find(v.begin(), v.end(), u.version) == v.end()) v.push_back(u.version);
    return v;
}

// ---------- updates ----------
#ifndef PNL_UPDATE_REPO
#define PNL_UPDATE_REPO "christianball123/peanuts-native-linux"
#endif
std::string update_repo() { const char *r = getenv("PNL_UPDATE_REPO"); return r && *r ? r : PNL_UPDATE_REPO; }
bool version_newer(const std::string &a, const std::string &b) {
    auto parts = [](const std::string &v) { std::vector<long> o; std::string t = v; if (!t.empty() && (t[0] == 'v' || t[0] == 'V')) t = t.substr(1);
                                            for (auto &x : split(t, '.')) o.push_back(strtol(x.c_str(), nullptr, 10)); return o; };
    auto x = parts(a), y = parts(b); size_t n = std::max(x.size(), y.size()); x.resize(n, 0); y.resize(n, 0);
    for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return x[i] > y[i];
    return false;
}
// The string value of "key" in a JSON text from pos on (GitHub's release JSON; escapes \" \\ \n \/ \uXXXX handled enough for names, URLs and notes)
static std::string json_value(const std::string &j, const std::string &key, size_t &pos) {
    size_t k = j.find("\"" + key + "\"", pos); if (k == std::string::npos) return "";
    size_t c = j.find(':', k); if (c == std::string::npos) return "";
    size_t i = c + 1; while (i < j.size() && isspace((unsigned char)j[i])) i++;
    if (i >= j.size()) return "";
    std::string out;
    if (j[i] != '"') { while (i < j.size() && j[i] != ',' && j[i] != '}' && !isspace((unsigned char)j[i])) out += j[i++]; pos = i; return out; }
    for (i++; i < j.size() && j[i] != '"'; i++) {
        if (j[i] == '\\' && i + 1 < j.size()) { char e = j[++i]; if (e == 'n') out += '\n'; else if (e == 't') out += ' '; else if (e == 'r') continue; else if (e == 'u' && i + 4 < j.size()) { out += '?'; i += 4; } else out += e; }
        else out += j[i];
    }
    pos = i; return out;
}
bool update_check(UpdateInfo &out, std::string &why) {
    std::string body;
    int rc = run_capture({"curl", "-fsSL", "--max-time", "12", "-H", "Accept: application/vnd.github+json", "-H", "User-Agent: peanuts-native-linux/" + P.version,
                          "https://api.github.com/repos/" + update_repo() + "/releases/latest"}, {}, body);
    if (rc != 0) { why = rc == 127 || rc == -1 ? "curl is not installed" : "GitHub could not be reached (offline, or no release yet)"; return false; }
    size_t p = 0; out.version = json_value(body, "tag_name", p);
    if (out.version.empty()) { why = "GitHub's answer had no release in it"; return false; }
    if (!out.version.empty() && (out.version[0] == 'v' || out.version[0] == 'V')) out.version = out.version.substr(1);
    p = 0; out.notes = json_value(body, "body", p);
    // the assets: the AppImage and its .sha256, by name
    size_t a = body.find("\"assets\""); if (a == std::string::npos) { why = "the release has no files"; return false; }
    for (size_t q = a;;) {
        size_t before = q; std::string name = json_value(body, "name", q); if (name.empty() || q <= before) break;
        size_t q2 = q; std::string url = json_value(body, "browser_download_url", q2);
        size_t q3 = q; std::string size = json_value(body, "size", q3);
        if (name == "PeanutsNative-aarch64.AppImage") { out.url = url; if (q3 < q2 + 2000) out.size = strtoull(size.c_str(), nullptr, 10); }
        else if (name == "PeanutsNative-aarch64.AppImage.sha256") out.sha_url = url;
        q = std::max(q, q2);
    }
    if (out.url.empty()) { why = "the newest release (" + out.version + ") has no PeanutsNative-aarch64.AppImage"; return false; }
    return true;
}
bool update_possible(std::string &why) {
    const char *ai = getenv("APPIMAGE");
    if (!ai || !*ai || !is_file(ai)) { why = "this copy is not running from an AppImage file"; return false; }
    if (access(dirname_of(ai).c_str(), W_OK) != 0) { why = "the folder of " + std::string(ai) + " is read-only for you"; return false; }
    return true;
}
std::shared_ptr<Job> update_job(const UpdateInfo &u) {
    return start_job("Updating Peanuts Native to " + u.version, P.logs + "/update-" + now_stamp() + ".log", {[u](Job &j) {
        std::string why; if (!update_possible(why)) { j.add_line("[RED] " + why); return 1; }
        std::string ai = getenv("APPIMAGE"), tmp = ai + ".download";
        unlink(tmp.c_str());
        j.set_phase("Downloading " + u.version);
        int devnull = open("/dev/null", O_WRONLY | O_CLOEXEC);
        pid_t pid = spawn({"curl", "-fsSL", "--retry", "3", "--connect-timeout", "20", "-o", tmp, u.url}, {}, devnull, true);
        if (devnull >= 0) close(devnull);
        if (pid < 0) { j.add_line("[RED] curl could not start"); return 1; }
        int st = 0;
        for (;;) {
            pid_t r = waitpid(pid, &st, WNOHANG);
            if (r == pid) break;
            if (j.cancelled) { kill(-pid, SIGTERM); waitpid(pid, &st, 0); unlink(tmp.c_str()); j.add_line("cancelled"); return 1; }
            struct stat sb; if (u.size && stat(tmp.c_str(), &sb) == 0) j.progress = std::min(0.95f, (float)sb.st_size / (float)u.size * 0.95f);
            usleep(200000);
        }
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) { unlink(tmp.c_str()); j.add_line("[RED] the download failed (curl " + std::to_string(WIFEXITED(st) ? WEXITSTATUS(st) : -1) + ")"); return 1; }
        j.set_phase("Checking the download");
        if (!u.sha_url.empty()) {
            std::string want, got;
            if (run_capture({"curl", "-fsSL", "--max-time", "30", u.sha_url}, {}, want) != 0) { unlink(tmp.c_str()); j.add_line("[RED] the checksum file could not be downloaded"); return 1; }
            run_capture({"sha256sum", tmp}, {}, got);
            want = trim(want).substr(0, 64); got = trim(got).substr(0, 64);
            if (want.size() != 64 || want != got) { unlink(tmp.c_str()); j.add_line("[RED] the download is damaged (SHA-256 " + got + ", expected " + want + "): nothing was changed"); return 1; }
            j.add_line("SHA-256 " + got + ": correct");
        }
        struct stat sb; if (stat(tmp.c_str(), &sb) != 0 || sb.st_size < (1 << 20)) { unlink(tmp.c_str()); j.add_line("[RED] the download is too small to be the AppImage"); return 1; }
        chmod(tmp.c_str(), 0755);
        std::string old = ai + ".old"; unlink(old.c_str());
        if (link(ai.c_str(), old.c_str()) != 0) j.add_line("(could not keep the old version as " + old + ")");
        if (rename(tmp.c_str(), ai.c_str()) != 0) { j.add_line("[RED] could not replace " + ai + ": " + strerror(errno)); return 1; }
        j.add_line("Peanuts Native " + u.version + " is in place: " + ai + " (the previous version: " + old + ")");
        j.set_phase("Updated to " + u.version); j.progress = 1.f;
        return 0;
    }});
}

}  // namespace pn
