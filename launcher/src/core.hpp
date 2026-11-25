// Peanuts Native for Linux — the launcher's model: where things live, the settings, the game library, and the jobs it runs (scans, builds, setup, games).
#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/types.h>

namespace pn {

// ---------- Paths ----------
struct Paths {
    std::string appdir;     // $APPDIR (inside the AppImage) or the folder above bin/ in a plain install
    std::string share;      // <appdir>/usr/share/peanuts-native: engine/, fonts, catalog.tsv
    std::string bin;        // <appdir>/usr/bin: peanuts-run, nximport_cli, spirv-val
    std::string python;     // <appdir>/usr/python/bin/python3
    std::string engine_src; // <share>/engine (read-only in the AppImage)
    std::string data;       // ~/.local/share/peanuts-native
    std::string config;     // ~/.config/peanuts-native
    std::string cache;      // ~/.cache/peanuts-native
    std::string engine;     // <data>/engine: the build engine, a writable copy of engine_src
    std::string dotnet;     // <appdir>/usr/lib/dotnet: the .NET 10 runtime the shader translator runs on
    std::string logs;       // <data>/logs
    std::string version;    // this release
    std::string engine_version;
    void init();
};
extern Paths P;

// ---------- Settings: ~/.config/peanuts-native/settings.ini, key=value; per-game keys are "<title id>.<key>" ----------
struct Config {
    std::map<std::string, std::string> kv;
    void load(); void save() const;
    std::string get(const std::string &k, const std::string &def = "") const;
    bool on(const std::string &k, bool def) const;
    int num(const std::string &k, int def) const;
    void set(const std::string &k, const std::string &v);
    std::string game(const std::string &tid, const std::string &k, const std::string &def) const;   // the game's own, else the global default
};
extern Config C;

// ---------- Small helpers ----------
std::string home();
bool is_file(const std::string &p);
bool is_dir(const std::string &p);
bool mkdirs(const std::string &p);
std::string read_file(const std::string &p, size_t max = 1 << 20);
bool write_file(const std::string &p, const std::string &s);
std::string basename_of(const std::string &p);
std::string dirname_of(const std::string &p);
std::string lower(std::string s);
std::string trim(const std::string &s);
std::vector<std::string> split(const std::string &s, char c);
std::string human_size(uint64_t bytes);
std::string now_stamp();
std::string version_display(uint32_t v);   // 65536 → "v65536"

// ---------- Processes ----------
// argv[0] is looked up on PATH. env: extra KEY=value lines over this process's environment. out_fd: where stdout and stderr go (-1: /dev/null)
pid_t spawn(const std::vector<std::string> &argv, const std::vector<std::string> &env, int out_fd, bool own_group);
// Runs to the end; the output (stdout and stderr) comes back
int run_capture(const std::vector<std::string> &argv, const std::vector<std::string> &env, std::string &out);

// A long job (a build, the tools setup): its output line by line, its state, cancel
struct Job {
    std::string title;
    std::atomic<bool> running{false}, done{false}, cancelled{false};
    std::atomic<int> rc{0};
    std::atomic<float> progress{-1.f};     // 0..1, or -1 when not known
    std::atomic<pid_t> pid{-1};
    double started = 0, ended = 0;
    std::string log_path;
    std::mutex mu;
    std::deque<std::string> lines;        // the last few hundred lines
    std::string phase;                    // what it is doing now, for the screen
    std::vector<std::string> errors;      // its [RED] lines
    void add_line(const std::string &l);
    void set_phase(const std::string &p) { std::lock_guard<std::mutex> g(mu); phase = p; }
    std::string get_phase() { std::lock_guard<std::mutex> g(mu); return phase; }
    void cancel();
};
// Runs the steps one after the other on a thread of their own; a step returns its exit code (non-zero stops the job)
using Step = std::function<int(Job &)>;
std::shared_ptr<Job> start_job(const std::string &title, const std::string &log_path, std::vector<Step> steps);
// One process as a job step: its output goes to the job (and its log), a line filter may set the progress
int job_run(Job &j, const std::vector<std::string> &argv, const std::vector<std::string> &env, std::function<void(Job &, const std::string &)> on_line = nullptr);

// ---------- The supported games (catalog.tsv, made from peanuts/games.json) ----------
struct CatalogEntry { std::string tid, name; std::vector<std::pair<std::string, uint32_t>> builds; bool companion = false; };
struct Catalog {
    std::map<std::string, CatalogEntry> by_tid;
    void load(const std::string &path);
    const CatalogEntry *find(const std::string &tid) const;
};
extern Catalog CAT;

// ---------- The library ----------
struct GameFile { std::string path, title, app, kind, name; uint32_t version = 0; uint64_t size = 0; };
enum class GameState { Unsupported, NeedsBuild, Building, Ready, NeedsRebuild };
struct Game {
    std::string tid;                 // the application's title ID
    std::string name;
    std::string base;                // the base game file (.nsp / .xci / .nsz / .xcz), or a .nup that holds everything
    bool is_nup = false;
    bool in_games_dir = false;       // the scan found one of its files in the games folder (not only in the updates folder)
    std::vector<GameFile> updates;   // newest first
    std::vector<std::string> dlcs;
    uint32_t version = 0;            // the version a build uses: the newest update the catalog supports, else the newest
    bool version_supported = false;
    const CatalogEntry *cat = nullptr;
    // its build
    std::string build_dir, pack_dir, nup;   // pack_dir / nup empty: not built
    std::string built_version;
    std::string icon_path;
    GameState state = GameState::Unsupported;
    std::string supported_versions() const;
};
struct Library {
    std::mutex mu;
    std::vector<Game> games;
    std::atomic<bool> scanning{false};
    std::atomic<int> scan_done{0}, scan_total{0}, unreadable{0};   // unreadable: files the importer could not read (other keys?)
    std::string scan_error;
    std::atomic<int> generation{0};
    void rescan();                    // in the background
    void refresh_builds();            // the build state of each game, again (after a build)
    std::vector<Game> snapshot();
};
extern Library LIB;
std::string builds_root();
void refresh_build(Game &g);

// ---------- Setup ----------
bool keys_ok();
bool tools_ok(std::string *why = nullptr);       // the bundled tools are all there (Python, the importer, .NET, spirv-val)
bool engine_ok();                                 // the build engine copy is this release's
std::shared_ptr<Job> prepare_engine_job();        // copies the engine (first start / new release)
std::shared_ptr<Job> extract_fonts_job(const std::string &firmware);
std::vector<std::string> detect_keys();           // prod.keys of emulators already on this system
bool clocks_helper_installed();
std::string clocks_installer_path();              // writes the one-time installer script and returns its path

// ---------- Build and play ----------
std::shared_ptr<Job> build_job(const Game &g, bool force);
struct PlayResult { int rc = 0; bool crashed = false; std::string log; double seconds = 0; };
// Starts the game; the launcher waits on it with play_wait
pid_t play_start(const Game &g, std::string &log_path, std::string &error);
bool play_poll(pid_t pid, int &rc);   // true once it has ended
void clocks(bool on);

// ---------- The bottom screen (Eden Duo companion packages) ----------
// The package for a game: one you imported (<data>/companions/<title id>) wins over the one in this AppImage (<share>/companions/<title id>). Empty: none
std::string companion_source(const std::string &tid);
std::string companion_label(const std::string &tid);   // "Persona 5 Royal companion 1.2.0 (imported)" and so on
// Puts the package where the game's host looks (builds/<id>/<id>-work/peanuts/DualScreen-<id>), when it is missing or different. false: no package
bool companion_install(const Game &g, std::string *why = nullptr);
// A .dsmod.zip from the Eden Duo companions releases: checked and unpacked into <data>/companions/<its title id>. Returns the title id, or "" with why
std::string companion_import(const std::string &zip, std::string &why);

// ---------- Mods (<data>/mods/<title id>/<mod name>/{romfs,exefs,exefs_patches,cheats}) ----------
struct ModInfo { std::string name, dir; bool romfs = false, code = false, cheats = false, enabled = true; };
std::vector<ModInfo> mods_of(const std::string &tid);
std::string mod_import(const std::string &tid, const std::string &path, std::string &why);   // the mod's name, or "" with why
bool mod_remove(const std::string &tid, const std::string &name);
void mod_enable(const std::string &tid, const std::string &name, bool on);
std::string code_mods_key(const std::string &tid);   // what the enabled code mods are: part of a build's stamp, so changing them asks for a build
// ---------- Game versions ----------
std::vector<uint32_t> versions_available(const Game &g);   // 0 for the base game, then the updates found

// ---------- Updates (the port's GitHub releases) ----------
struct UpdateInfo { std::string version, url, sha_url, notes; uint64_t size = 0; };
bool version_newer(const std::string &a, const std::string &b);   // a > b, numerically by dotted parts
// Asks GitHub for the newest release (blocking, a few seconds at most). true: out filled; false: why
bool update_check(UpdateInfo &out, std::string &why);
// Whether this copy can replace itself: run from an AppImage file in a folder it can write to
bool update_possible(std::string &why);
// Downloads, checks the SHA-256, and puts the new AppImage in place of this one (the old one kept as <name>.old)
std::shared_ptr<Job> update_job(const UpdateInfo &u);
std::string update_repo();

// ---------- Steam shortcuts ----------
bool steam_running();
bool steam_game_mode();   // inside Steam's Game Mode session (gamescope)
std::vector<std::string> steam_users();   // .../Steam/userdata/<id> folders
// Adds (or updates) the game's shortcut, with artwork, for every Steam user here. Steam must be closed; it reads shortcuts when it starts
bool steam_add(const Game &g, std::string &msg);
bool steam_has(const Game &g);
std::string launch_command_exe();   // what the shortcut runs: this AppImage

}  // namespace pn
