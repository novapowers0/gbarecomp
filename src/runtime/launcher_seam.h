// launcher_seam.h — the recomp-ui pre-boot launcher seam for GBA games.
//
// Header-only and ONLY meaningful under RECOMP_LAUNCHER: a game target that
// calls recomp_target_launcher_ui() (recomp-ui/recomp_ui.cmake) gets that
// define plus the recomp-ui include dirs; its main() then runs
// gbarecomp_launcher_preboot() BEFORE gbarecomp::run_game():
//
//     gbarecomp::RunOptions opts;  ...fill identity...
//   #if defined(RECOMP_LAUNCHER)
//     std::vector<std::string> args(argv, argv + argc);
//     if (gbarecomp_launcher_preboot(args, opts)) return 0;   // user quit
//     std::vector<char*> av;
//     for (auto& s : args) av.push_back(s.data());
//     return gbarecomp::run_game((int)av.size(), av.data(), opts);
//   #else
//     return gbarecomp::run_game(argc, argv, opts);
//   #endif
//
// The seam translates launcher output into ordinary CLI args (--rom, --bios,
// --scale, --screen, --volume, --linear-filter, --fullscreen, --view-width),
// so run_game() needs no launcher knowledge: the same runtime path handles
// launcher-driven and hand-typed invocations identically. Settings persist in
// <exe>/config.ini [Launcher] (player-owned; the identity-gated game.toml is
// never written). Player keybinds + [KeyMap] hotkeys are persisted by the
// launcher itself (keybinds.ini / config.ini next to the exe — the same files
// host_window.cpp reads).
//
// The launcher is skipped (returns 0 with args untouched) for every headless
// or explicit-path invocation: --tcp, --steps, --frames, --no-window,
// --dump-bmp/--dump-png, --load-state, --help, an explicit --rom, or
// GBARECOMP_NO_LAUNCHER=1. `--launcher` forces it past a persisted
// skip_launcher=1; `--no-launcher` skips for one run. Both are seam-only
// flags, stripped before run_game() parses the args.

#pragma once

#if defined(RECOMP_LAUNCHER)

#include "runtime.h"
#include "../gba/gba_bios.h"
#include "../gba/sha1.h"
// The Spanish box art (cover_embedded.h) is a copyrighted JPEG embedded as a
// byte array. It is generated locally from a user-provided cover.jpg and is
// NOT shipped in the source tree. When absent, the launcher still works — it
// just uses the default box art for the Spanish dump (see below).
#if __has_include("cover_embedded.h")
#include "cover_embedded.h"
#define GBARECOMP_HAS_EMBEDDED_SPANISH_COVER 1
#endif
#if defined(GBARECOMP_ENABLE_MODS)
#include "mod_runtime.h"
#endif

#include "recomp_launcher.h"    // recomp-ui C ABI (include dir via recomp_ui.cmake)
#include "launcher_profile.h"   // launcher_profile_apply("gba", ...)

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace gbarecomp_seam {

inline int verify_retail_gba_bios(const char* path,
                                  RecompLauncherCBiosVerify* out) {
    if (!out) return 0;
    std::memset(out, 0, sizeof(*out));
    if (!path || !*path) {
        std::snprintf(out->detail, sizeof(out->detail),
                      "Retail Game Boy Advance BIOS required (16 KB).");
        return 1;
    }

    gba::GbaBios bios;
    std::string error;
    if (!bios.load_from_file(path, gba::GbaBios::kExpectedSha1, &error)) {
        std::snprintf(out->detail, sizeof(out->detail), "%s",
                      error.empty() ? "Invalid Game Boy Advance BIOS."
                                    : error.c_str());
        return 1;
    }

    out->ok = 1;
    std::snprintf(out->detail, sizeof(out->detail),
                  "Retail Game Boy Advance BIOS verified (16 KB).");
    return 1;
}

template <typename T, typename = void>
struct has_gyro_sensitivity : std::false_type {};

template <typename T>
struct has_gyro_sensitivity<
    T,
    std::void_t<decltype(std::declval<T&>().gyro_sensitivity)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_gyro_controls : std::false_type {};

template <typename T>
struct has_gyro_controls<
    T,
    std::void_t<decltype(std::declval<T&>().has_gyro_controls)>>
    : std::true_type {};

template <typename T>
inline void set_gyro_sensitivity(T& settings, float value) {
    if constexpr (has_gyro_sensitivity<T>::value) {
        settings.gyro_sensitivity = value;
    }
}

template <typename T>
inline float get_gyro_sensitivity(const T& settings, float fallback) {
    if constexpr (has_gyro_sensitivity<T>::value) {
        return settings.gyro_sensitivity;
    }
    return fallback;
}

template <typename T>
inline void set_has_gyro_controls(T& game_info, bool enabled) {
    if constexpr (has_gyro_controls<T>::value) {
        game_info.has_gyro_controls = enabled ? 1 : 0;
    }
}

template <typename T, typename = void>
struct has_presentation_filters : std::false_type {};

template <typename T>
struct has_presentation_filters<
    T,
    std::void_t<decltype(std::declval<T&>().sharp_filter),
                decltype(std::declval<T&>().affine_filter)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_presentation_filter_caps : std::false_type {};

template <typename T>
struct has_presentation_filter_caps<
    T,
    std::void_t<decltype(std::declval<T&>().has_sharp_filter),
                decltype(std::declval<T&>().has_affine_filter)>>
    : std::true_type {};

template <typename T>
inline void set_presentation_filters(T& settings, bool sharp, bool affine) {
    if constexpr (has_presentation_filters<T>::value) {
        settings.sharp_filter = sharp ? 1 : 0;
        settings.affine_filter = affine ? 1 : 0;
    }
}

template <typename T>
inline bool get_sharp_filter(const T& settings, bool fallback) {
    if constexpr (has_presentation_filters<T>::value)
        return settings.sharp_filter != 0;
    return fallback;
}

template <typename T>
inline bool get_affine_filter(const T& settings, bool fallback) {
    if constexpr (has_presentation_filters<T>::value)
        return settings.affine_filter != 0;
    return fallback;
}

template <typename T>
inline void set_presentation_filter_caps(T& game_info,
                                         bool sharp, bool affine) {
    if constexpr (has_presentation_filter_caps<T>::value) {
        game_info.has_sharp_filter = sharp ? 1 : 0;
        game_info.has_affine_filter = affine ? 1 : 0;
    }
}

// Same treatment for the solar-sensor fields, so this header still compiles
// against a recomp-ui pin that predates them. Without the guard a game only has
// to update gbarecomp to get a hard error in a third-party header it did not
// change.
template <typename T, typename = void>
struct has_solar_sensor_field : std::false_type {};

template <typename T>
struct has_solar_sensor_field<
    T, std::void_t<decltype(std::declval<T&>().has_solar_sensor)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_solar_settings : std::false_type {};

template <typename T>
struct has_solar_settings<
    T, std::void_t<decltype(std::declval<T&>().solar_zip)>> : std::true_type {};

template <typename T>
inline void set_has_solar_sensor(T& game_info, bool enabled) {
    if constexpr (has_solar_sensor_field<T>::value) {
        game_info.has_solar_sensor = enabled ? 1 : 0;
    }
}

// Returns false when this recomp-ui pin has no solar fields, so the caller can
// leave config.ini [Solar] exactly as the game wrote it rather than round-trip
// it through settings that do not exist.
template <typename T, typename S>
inline bool push_solar_settings(T& settings, const S& src) {
    if constexpr (has_solar_settings<T>::value) {
        std::snprintf(settings.solar_zip, sizeof(settings.solar_zip), "%s",
                      src.zip.c_str());
        std::snprintf(settings.solar_country, sizeof(settings.solar_country),
                      "%s", src.country.c_str());
        settings.solar_source      = src.source;
        settings.solar_manual_step = src.manual_step;
        settings.solar_full_sun    = src.full_sun;
        return true;
    }
    return false;
}

template <typename T, typename S>
inline bool pull_solar_settings(const T& settings, S* dst) {
    if constexpr (has_solar_settings<T>::value) {
        dst->zip         = settings.solar_zip;
        dst->country     = settings.solar_country[0] ? settings.solar_country
                                                     : "us";
        dst->source      = settings.solar_source ? 1 : 0;
        dst->manual_step = settings.solar_manual_step;
        dst->full_sun    = settings.solar_full_sun > 0 ? settings.solar_full_sun
                                                       : 900;
        return true;
    }
    return false;
}

// gbarecomp's screen-model tokens, in the launcher's kGbaScreenKindNames
// index order (Raw/Unlit/Frontlit/Backlit/Classic).
inline const char* const kScreenTokens[5] = {
    "raw", "unlit", "frontlit", "backlit", "classic"
};

inline int screen_token_to_index(const std::string& tok) {
    for (int i = 0; i < 5; ++i)
        if (tok == kScreenTokens[i]) return i;
    return 0;
}

inline std::string exe_dir(const std::vector<std::string>& args) {
    if (!args.empty() && !args[0].empty()) {
        std::filesystem::path p(args[0]);
        if (p.has_parent_path()) return p.parent_path().string();
    }
    return ".";
}

inline std::string read_single_line(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    if (f && std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
                                 line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        return line;
    }
    return {};
}

inline void write_single_line(const std::string& path, const std::string& value) {
    if (value.empty()) return;
    std::ofstream f(path, std::ios::trunc);
    if (f) f << value << "\n";
}

// Minimal reader for a single `key = "value"` under [section] in game.toml —
// enough to prefill the launcher from [rom].path / [bios].path without
// pulling in the full TOML parser. Returns the value resolved relative to the
// toml's own directory (matching the runtime's resolve_config_path), or "".
inline std::string toml_path_value(const std::string& toml_path,
                                   const char* section, const char* key) {
    std::ifstream f(toml_path);
    if (!f) return {};
    std::string line, cur;
    std::string found;
    while (std::getline(f, line)) {
        size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        if (line[b] == '#' || line[b] == ';') continue;
        if (line[b] == '[') {
            size_t e = line.find(']', b);
            if (e != std::string::npos) cur = line.substr(b + 1, e - b - 1);
            continue;
        }
        if (cur != section) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(b, eq - b);
        size_t ke = k.find_last_not_of(" \t");
        if (ke != std::string::npos) k = k.substr(0, ke + 1);
        if (k != key) continue;
        std::string v = line.substr(eq + 1);
        size_t vb = v.find('"');
        size_t ve = v.rfind('"');
        if (vb == std::string::npos || ve == vb) continue;
        found = v.substr(vb + 1, ve - vb - 1);
        break;
    }
    if (found.empty()) return {};
    // Resolve relative to the toml's directory.
    std::filesystem::path p(found);
    if (p.is_absolute()) return p.string();
    std::filesystem::path base = std::filesystem::path(toml_path).parent_path();
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::weakly_canonical(base / p, ec);
    return (ec ? (base / p) : abs).string();
}

// Launcher-owned persisted settings (config.ini [Launcher]).
struct SeamConfig {
    int  scale = 3;
    int  fullscreen = 0;
    int  linear_filter = 0;
    int  sharp_filter = -1;
    int  affine_filter = -1;
    int  screen_kind = 0;      // kScreenTokens index
    int  volume = 100;
    int  audio_freq = 0;       // host audio device rate; 0 = engine default
    int  audio_shadow = 0;     // MP2K enhancement mixer ([audio].shadow)
    int  language_index = 0;   // EN/ES boot-dump selection (GameInfo.languages)
    int  skip_launcher = 0;
    int  widescreen = 0;       // legacy fixed-width toggle
    int  aspect_index = 0;     // games with a launcher_aspect vocabulary only
    int  adaptive_view = 0;    // live drawable aspect; fixed aspect is retained
    float gyro_sensitivity = 1.00f;
};

inline void seam_config_load(const std::string& path, SeamConfig* c) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    bool in_section = false;
    while (std::getline(f, line)) {
        size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        size_t e = line.find_last_not_of(" \t\r\n");
        std::string s = line.substr(b, e - b + 1);
        if (s.empty() || s[0] == '#' || s[0] == ';') continue;
        if (s[0] == '[') {
            in_section = s.rfind("[Launcher]", 0) == 0;
            continue;
        }
        if (!in_section) continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string v) {
            size_t vb = v.find_first_not_of(" \t");
            if (vb == std::string::npos) return std::string();
            size_t ve = v.find_last_not_of(" \t");
            return v.substr(vb, ve - vb + 1);
        };
        std::string key = trim(s.substr(0, eq));
        std::string val = trim(s.substr(eq + 1));
        if (key == "scale")         c->scale = std::atoi(val.c_str());
        else if (key == "fullscreen")    c->fullscreen = std::atoi(val.c_str());
        else if (key == "linear_filter") c->linear_filter = std::atoi(val.c_str());
        else if (key == "sharp_filter") c->sharp_filter = std::atoi(val.c_str());
        else if (key == "affine_filter") c->affine_filter = std::atoi(val.c_str());
        else if (key == "screen")        c->screen_kind = screen_token_to_index(val);
        else if (key == "volume")        c->volume = std::atoi(val.c_str());
        else if (key == "audio_freq")    c->audio_freq = std::atoi(val.c_str());
        else if (key == "audio_shadow")  c->audio_shadow = std::atoi(val.c_str());
        else if (key == "language_index") c->language_index = std::atoi(val.c_str());
        else if (key == "skip_launcher") c->skip_launcher = std::atoi(val.c_str());
        else if (key == "widescreen")    c->widescreen = std::atoi(val.c_str());
        else if (key == "aspect_index")  c->aspect_index = std::atoi(val.c_str());
        else if (key == "adaptive_view") c->adaptive_view = std::atoi(val.c_str());
        else if (key == "gyro_sensitivity")
            c->gyro_sensitivity = std::strtof(val.c_str(), nullptr);
    }
    if (c->scale < 1) c->scale = 1;
    if (c->scale > 8) c->scale = 8;
    if (c->volume < 0) c->volume = 0;
    if (c->volume > 100) c->volume = 100;
    if (c->screen_kind < 0 || c->screen_kind > 4) c->screen_kind = 0;
    // Tri-state: 0 off, 1 borderless, 2 exclusive. Guard against a hand-
    // edited/garbage config.ini value; do NOT collapse to a bool here.
    if (c->fullscreen < 0 || c->fullscreen > 2) c->fullscreen = 0;
    if (c->gyro_sensitivity < 0.25f) c->gyro_sensitivity = 0.25f;
    if (c->gyro_sensitivity > 4.00f) c->gyro_sensitivity = 4.00f;
}

// Rewrite ONLY the [Launcher] section of config.ini, preserving every other
// line (notably [KeyMap], which the launcher's hotkey editor edits in place).
inline void seam_config_save(const std::string& path, const SeamConfig& c) {
    std::vector<std::string> lines;
    {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) {
            while (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }
    // Strip the existing [Launcher] section body.
    std::vector<std::string> kept;
    bool in_section = false;
    for (const auto& l : lines) {
        size_t b = l.find_first_not_of(" \t");
        bool is_header = b != std::string::npos && l[b] == '[';
        if (is_header) in_section = l.compare(b, 10, "[Launcher]") == 0;
        if (is_header && in_section) continue;
        if (in_section && !is_header) continue;
        kept.push_back(l);
    }
    while (!kept.empty() && kept.back().empty()) kept.pop_back();

    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    for (const auto& l : kept) f << l << "\n";
    if (!kept.empty()) f << "\n";
    f << "[Launcher]\n";
    f << "scale = " << c.scale << "\n";
    f << "fullscreen = " << c.fullscreen << "\n";
    f << "linear_filter = " << c.linear_filter << "\n";
    f << "sharp_filter = " << c.sharp_filter << "\n";
    f << "affine_filter = " << c.affine_filter << "\n";
    f << "screen = " << kScreenTokens[c.screen_kind] << "\n";
    f << "volume = " << c.volume << "\n";
    f << "audio_freq = " << c.audio_freq << "\n";
    f << "audio_shadow = " << c.audio_shadow << "\n";
    f << "language_index = " << c.language_index << "\n";
    f << "skip_launcher = " << c.skip_launcher << "\n";
    f << "widescreen = " << c.widescreen << "\n";
    f << "aspect_index = " << c.aspect_index << "\n";
    f << "adaptive_view = " << c.adaptive_view << "\n";
    f << "gyro_sensitivity = " << c.gyro_sensitivity << "\n";
}

// ---- multi-dump discovery ---------------------------------------------------
// The launcher can offer one boot ROM per language (EN/ES). Each language maps
// to a cart dump the game already accepts, so the identity gate still passes.
// Scan the exe directory for the known dumps by real SHA-1 (extension-filtered
// to keep the pass cheap; dumps are ~8-16 MB, everything else is skipped).

inline bool iequals_ascii(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return false;
    }
    return true;
}

// Returns one absolute path per known SHA-1 (empty = dump not found in `dir`).
inline std::vector<std::string> seam_scan_cart_dumps(
    const std::string& dir, const std::vector<std::string>& known_sha1s) {
    std::vector<std::string> found(known_sha1s.size());
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const std::filesystem::directory_entry& entry = *it;
        if (!entry.is_regular_file(ec)) { ec.clear(); continue; }
        std::string ext = entry.path().extension().string();
        if (!ext.empty()) {
            for (char& c : ext) {
                if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            }
        }
        if (ext != ".gba" && ext != ".bin" && ext != ".rom" && ext != ".agb")
            continue;
        const std::uintmax_t size = entry.file_size(ec);
        if (ec || size == 0 || size > 64u * 1024u * 1024u) { ec.clear(); continue; }
        std::ifstream f(entry.path(), std::ios::binary);
        if (!f) continue;
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
        if (!f.read(reinterpret_cast<char*>(buf.data()),
                    static_cast<std::streamsize>(buf.size())))
            continue;
        const std::string hex = gba::sha1(buf.data(), buf.size()).hex();
        for (std::size_t i = 0; i < known_sha1s.size(); ++i) {
            if (found[i].empty() && iequals_ascii(hex, known_sha1s[i]))
                found[i] = entry.path().string();
        }
    }
    return found;
}

// ---- config.ini [Solar] ------------------------------------------------------
// Cartridge light-sensor location, for games with RunOptions::has_solar_sensor.
// The GAME also reads and writes this section (it owns the sensor's light
// source), so the key names and the live/manual spelling here must match its
// writer exactly, and unknown keys inside the section are preserved rather than
// dropped -- the game keeps settings of its own here that the launcher has no
// opinion about.
struct SeamSolar {
    std::string zip;
    std::string country = "us";
    int source = 0;        // 0 live, 1 manual
    int manual_step = 0;   // 0..8
    int full_sun = 900;    // W/m^2
};

inline void seam_solar_load(const std::string& path, SeamSolar* s) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    bool in_section = false;
    while (std::getline(f, line)) {
        size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        size_t e = line.find_last_not_of(" \t\r\n");
        std::string t = line.substr(b, e - b + 1);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t[0] == '[') { in_section = t.rfind("[Solar]", 0) == 0; continue; }
        if (!in_section) continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string v) {
            size_t vb = v.find_first_not_of(" \t");
            if (vb == std::string::npos) return std::string();
            size_t ve = v.find_last_not_of(" \t");
            return v.substr(vb, ve - vb + 1);
        };
        const std::string k = trim(t.substr(0, eq));
        const std::string v = trim(t.substr(eq + 1));
        if (k == "zip")              s->zip = v;
        else if (k == "country")     s->country = v;
        else if (k == "source")      s->source = (v == "manual") ? 1 : 0;
        else if (k == "manual_step") s->manual_step = std::atoi(v.c_str());
        else if (k == "full_sun")    s->full_sun = std::atoi(v.c_str());
    }
}

inline void seam_solar_save(const std::string& path, const SeamSolar& s) {
    std::vector<std::string> lines;
    {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) {
            while (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }
    // Keep every other section, and inside [Solar] keep the keys we do not own.
    static const char* kOwned[] = {"zip", "country", "source", "manual_step",
                                   "full_sun"};
    std::vector<std::string> kept, foreign;
    bool in_section = false;
    for (const auto& l : lines) {
        size_t b = l.find_first_not_of(" \t");
        const bool is_header = b != std::string::npos && l[b] == '[';
        if (is_header) in_section = l.compare(b, 7, "[Solar]") == 0;
        if (is_header && in_section) continue;
        if (in_section) {
            if (b == std::string::npos) continue;
            size_t eq = l.find('=');
            if (eq != std::string::npos) {
                std::string k = l.substr(b, eq - b);
                while (!k.empty() && (k.back() == ' ' || k.back() == '\t'))
                    k.pop_back();
                bool owned = false;
                for (const char* o : kOwned)
                    if (k == o) { owned = true; break; }
                if (!owned) foreign.push_back(l);
            }
            continue;   // drop comments and owned keys; they are rewritten
        }
        kept.push_back(l);
    }
    while (!kept.empty() && kept.back().empty()) kept.pop_back();

    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    for (const auto& l : kept) f << l << "\n";
    if (!kept.empty()) f << "\n";
    f << "[Solar]\n";
    f << "zip = " << s.zip << "\n";
    f << "country = " << s.country << "\n";
    f << "source = " << (s.source ? "manual" : "live") << "\n";
    f << "manual_step = " << s.manual_step << "\n";
    f << "full_sun = " << s.full_sun << "\n";
    for (const auto& l : foreign) f << l << "\n";
}

// Append the persisted/committed settings as ordinary run_game() CLI args.
inline void seam_append_setting_args(std::vector<std::string>& args,
                                     const SeamConfig& c,
                                     const gbarecomp::RunOptions& opts) {
    args.push_back("--scale");
    args.push_back(std::to_string(c.scale));
    args.push_back("--volume");
    args.push_back(std::to_string(c.volume));
    if (c.audio_freq > 0) {
        args.push_back("--audio-freq");
        args.push_back(std::to_string(c.audio_freq));
    }
    args.push_back("--audio-shadow");
    args.push_back(c.audio_shadow ? "1" : "0");
    args.push_back("--linear-filter");
    args.push_back(c.linear_filter ? "1" : "0");
    if (opts.launcher_expose_sharp_filter) {
        args.push_back("--sharp-filter");
        args.push_back(c.sharp_filter ? "1" : "0");
    }
    if (opts.launcher_expose_affine_filter) {
        args.push_back("--affine-filter");
        args.push_back(c.affine_filter ? "1" : "0");
    }
    args.push_back("--screen");
    args.push_back(kScreenTokens[c.screen_kind]);
    // Tri-state: 0 off, 1 borderless, 2 exclusive. Pass the value explicitly
    // so run_game() gets the exact mode; bare `--fullscreen` (no `=N`) still
    // parses as mode 1 for back-compat with older seam/CLI invocations.
    if (c.fullscreen) {
        args.push_back("--fullscreen=" + std::to_string(c.fullscreen));
    }
    if (opts.launcher_expose_gyro) {
        args.push_back("--gyro-sensitivity");
        args.push_back(std::to_string(c.gyro_sensitivity));
    }
    const bool adaptive_view =
        c.adaptive_view && opts.launcher_expose_adaptive_view &&
        opts.resize_driven_view && opts.max_resize_view_width > 240;
    if (adaptive_view) {
        args.push_back("--resize-view");
    }

    // Windowed adaptive sessions use the selected fixed aspect as their
    // initial window size, then follow live resizes. In fullscreen, the host
    // display supplies the aspect, so adaptive mode deliberately ignores the
    // stored fixed selection.
    const bool fixed_view_honored = !adaptive_view || !c.fullscreen;
    if (fixed_view_honored && opts.launcher_num_aspects > 0 &&
        opts.launcher_aspect_view_widths) {
        // Aspect vocabulary (multi-width games): committed index -> width.
        int idx = c.aspect_index;
        if (idx < 0 || idx >= opts.launcher_num_aspects) idx = 0;
        const std::uint16_t w = opts.launcher_aspect_view_widths[idx];
        if (w > 240) {
            args.push_back("--view-width");
            args.push_back(std::to_string(w));
        }
    } else if (fixed_view_honored && c.widescreen && !adaptive_view &&
               opts.resize_driven_view &&
               opts.max_resize_view_width > 240) {
        // Resize-driven games start faithful and reveal more world as the
        // drawable becomes wider. This is deliberately distinct from the
        // fixed --view-width contract used by Mega Man Zero and others.
        args.push_back("--resize-view");
    } else if (fixed_view_honored && c.widescreen &&
               opts.widescreen_view_width > 240) {
        args.push_back("--view-width");
        args.push_back(std::to_string(opts.widescreen_view_width));
    }
}

}  // namespace gbarecomp_seam

// Run the pre-boot launcher. Returns 1 if the user quit (caller returns 0
// from main without booting), 0 to continue into run_game() — with `args`
// extended by the launcher-committed settings when the launcher ran.
inline int gbarecomp_launcher_preboot(std::vector<std::string>& args,
                                      const gbarecomp::RunOptions& opts) {
    using namespace gbarecomp_seam;

    // ---- skip decisions -----------------------------------------------------
    bool force_launcher = false;
    bool skip_once = false;
    bool headless = false;
    {
        std::vector<std::string> filtered;
        filtered.reserve(args.size());
        for (size_t i = 0; i < args.size(); ++i) {
            const std::string& s = args[i];
            if (i > 0 && s == "--launcher")    { force_launcher = true; continue; }
            if (i > 0 && s == "--no-launcher") { skip_once = true; continue; }
            if (i > 0 && (s == "--tcp" || s == "--steps" || s == "--frames" ||
                          s == "--no-window" || s == "--dump-bmp" ||
                          s == "--dump-png" || s == "--load-state" ||
                          s == "--rom" || s == "--help" || s == "-h"))
                headless = true;
            filtered.push_back(s);
        }
        args.swap(filtered);
    }
    if (const char* env = std::getenv("GBARECOMP_NO_LAUNCHER"))
        if (env[0] && env[0] != '0') skip_once = true;
    if (headless || (skip_once && !force_launcher)) return 0;

    const std::string dir = exe_dir(args);
    const auto state_path = [&](const char* filename,
                                const char* fallback) -> std::string {
        const char* chosen = (filename && filename[0]) ? filename : fallback;
        std::filesystem::path path(chosen);
        if (path.is_absolute()) return path.string();
        return (std::filesystem::path(dir) / path).string();
    };
    const std::string config_path =
        state_path(opts.launcher_config_filename, "config.ini");
    const std::string keybinds_path =
        state_path(opts.launcher_keybinds_filename, "keybinds.ini");

#if defined(GBARECOMP_ENABLE_MODS)
    const RecompLauncherCModProvider* mod_provider = nullptr;
    if (opts.mod_game_id && *opts.mod_game_id &&
        opts.builtin_rom_sha1 && *opts.builtin_rom_sha1) {
        std::vector<std::string> mod_rom_sha1s;
        mod_rom_sha1s.push_back(opts.builtin_rom_sha1);
        for (std::size_t i = 0; i < opts.num_builtin_rom_sha1_alts; ++i) {
            if (opts.builtin_rom_sha1_alts && opts.builtin_rom_sha1_alts[i] &&
                opts.builtin_rom_sha1_alts[i][0]) {
                mod_rom_sha1s.push_back(opts.builtin_rom_sha1_alts[i]);
            }
        }
        std::string mod_error;
        if (gbarecomp::mod_runtime_initialize(
                std::filesystem::path(dir) / "mods",
                opts.mod_game_id, mod_rom_sha1s, &mod_error)) {
            mod_provider = gbarecomp::mod_runtime_launcher_provider();
        } else {
            std::fprintf(stderr, "[gbarecomp:launcher] mods unavailable: %s\n",
                         mod_error.c_str());
        }
    }
#endif

    SeamConfig cfg;
    seam_config_load(config_path, &cfg);
    if (cfg.sharp_filter < 0)
        cfg.sharp_filter = opts.launcher_default_sharp_filter ? 1 : 0;
    if (cfg.affine_filter < 0)
        cfg.affine_filter = opts.launcher_default_affine_filter ? 1 : 0;
    SeamSolar solar;
    if (opts.has_solar_sensor) seam_solar_load(config_path, &solar);
    if (cfg.skip_launcher && !force_launcher) {
        // Boot straight in, but still honor the persisted settings.
        seam_append_setting_args(args, cfg, opts);
        return 0;
    }

    // ---- seed the launcher: last pick (rom.cfg/bios.cfg) -> game.toml -------
    // so a first run (no sidecar yet) still prefills from the paths the game
    // already declares, instead of opening blank and forcing a re-browse.
    const std::string rom_cfg =
        state_path(opts.launcher_rom_cache_filename, "rom.cfg");
    const std::string bios_cfg =
        state_path(opts.launcher_bios_cache_filename, "bios.cfg");
    std::string seed_rom  = read_single_line(rom_cfg);
    std::string seed_bios = read_single_line(bios_cfg);
    auto normalize_cached_path =
        [](const std::string& value, const std::string& cache_path) {
            if (value.empty()) return std::string{};
            std::filesystem::path path(value);
            if (path.is_relative())
                path = std::filesystem::path(cache_path).parent_path() / path;
            std::error_code ec;
            if (!std::filesystem::exists(path, ec) || ec) return std::string{};
            const std::filesystem::path absolute =
                std::filesystem::weakly_canonical(path, ec);
            return (ec ? path.lexically_normal() : absolute).string();
        };
    seed_rom = normalize_cached_path(seed_rom, rom_cfg);
    seed_bios = normalize_cached_path(seed_bios, bios_cfg);
    if ((seed_rom.empty() || seed_bios.empty()) &&
        opts.launcher_game_config && opts.launcher_game_config[0]) {
        // Packaged builds put game.toml beside the executable. Development
        // builds commonly keep it one directory above build/.
        std::string toml = opts.launcher_game_config;
        if (!std::filesystem::exists(toml)) {
            std::string alt = dir + "/" + toml;
            if (std::filesystem::exists(alt)) {
                toml = alt;
            } else {
                alt = (std::filesystem::path(dir).parent_path() / toml).string();
                if (std::filesystem::exists(alt)) toml = alt;
            }
        }
        if (std::filesystem::exists(toml)) {
            auto config_relative_path =
                [&toml](const std::string& value) -> std::string {
                    if (value.empty()) return {};
                    std::filesystem::path path(value);
                    if (path.is_relative())
                        path = std::filesystem::path(toml).parent_path() / path;
                    return path.lexically_normal().string();
                };
            if (seed_rom.empty())
                seed_rom = config_relative_path(
                    toml_path_value(toml, "rom", "path"));
            if (seed_bios.empty())
                seed_bios = config_relative_path(
                    toml_path_value(toml, "bios", "path"));
        }
    }

    RecompLauncherCSettings ls;
    std::memset(&ls, 0, sizeof(ls));
    ls.output_method = 2;                 // launcher UI renders via OpenGL
    ls.window_scale  = cfg.scale;
    ls.fullscreen    = cfg.fullscreen;
    ls.linear_filter = cfg.linear_filter;
    ls.widescreen    = cfg.widescreen;
    ls.adaptive_view = cfg.adaptive_view;
    ls.enable_audio  = 1;
    // Seed the sample-rate cycle from the persisted choice (0 => the UI's
    // default), then persist the committed value back to config.ini and hand
    // it to the runtime as --audio-freq so the user's selection is honored.
    ls.audio_freq    = cfg.audio_freq > 0 ? cfg.audio_freq : 32768;
    ls.audio_shadow  = cfg.audio_shadow != 0;
    ls.language_index = cfg.language_index;
    ls.volume        = cfg.volume;
    ls.player_src[0] = 1;                 // keyboard (gbarecomp host input)
    ls.skip_launcher = cfg.skip_launcher;
    bool solar_in_launcher = false;
    if (opts.has_solar_sensor) solar_in_launcher = push_solar_settings(ls, solar);
    ls.screen_kind   = cfg.screen_kind;
    ls.aspect_index  = cfg.aspect_index;
    gbarecomp_seam::set_gyro_sensitivity(ls, cfg.gyro_sensitivity);
    gbarecomp_seam::set_presentation_filters(
        ls, cfg.sharp_filter != 0, cfg.affine_filter != 0);
    std::snprintf(ls.bios_path, sizeof(ls.bios_path), "%s", seed_bios.c_str());

    RecompLauncherCGameInfo gi;
    std::memset(&gi, 0, sizeof(gi));
    launcher_profile_apply("gba", &gi);   // GBA identity + capability defaults
    gi.name = opts.builtin_game_name ? opts.builtin_game_name : "GBA cartridge";
    gi.region = opts.launcher_region;
    // SHA-1 is gbarecomp's real ROM identity gate — hand the launcher the
    // SAME fingerprints so its "verified" check agrees with the runtime (a
    // CRC32 is dump-specific and would reject other valid dumps). CRC32, when
    // present, stays informational only. The accepted set covers the primary
    // plus every declared alternative dump.
    std::vector<const char*> known_sha1;
    if (opts.builtin_rom_sha1 && opts.builtin_rom_sha1[0]) {
        known_sha1.push_back(opts.builtin_rom_sha1);
    }
    for (std::size_t i = 0; i < opts.num_builtin_rom_sha1_alts; ++i) {
        if (opts.builtin_rom_sha1_alts && opts.builtin_rom_sha1_alts[i] &&
            opts.builtin_rom_sha1_alts[i][0]) {
            known_sha1.push_back(opts.builtin_rom_sha1_alts[i]);
        }
    }
    if (!known_sha1.empty()) {
        gi.known_sha1_hex = known_sha1.data();
        gi.num_known_sha1 = known_sha1.size();
    }
    gi.widescreen_supported = opts.launcher_expose_widescreen &&
        opts.widescreen_view_width > 240 ? 1 : 0;
    set_has_solar_sensor(gi, opts.has_solar_sensor);
    gi.adaptive_view_supported = opts.launcher_expose_adaptive_view &&
        opts.resize_driven_view && opts.max_resize_view_width > 240 ? 1 : 0;
    gi.config_path = config_path.c_str();
    gi.keybinds_path = keybinds_path.c_str();
    gi.boxart_path = opts.launcher_boxart;   // NULL => default assets/img/boxart.tga
    gi.bios_verify = &gbarecomp_seam::verify_retail_gba_bios;
    gi.has_audio_shadow = 1;   // Audio panel: MP2K enhancement-mixer checkbox

    // ---- multi-dump presentation + EN/ES boot selection ---------------------
    // The Spanish translation dump is the same retail cart (BG3E) with a
    // different text region, so the identical generated image boots it. When
    // that dump is among the accepted SHA-1s, offer a per-dump region label
    // ("USA + Mod SPA"), an alternative cover (cover.jpg beside the exe), and
    // an EN/ES language selector whose segments mount the matching cart dump.
    // The vectors below only live for this preboot call, and the model only
    // reads them while the launcher window is open — safe borrowed storage.
    const char* const kSpanishSha1 =
        "ee667f656f6c3a128b9d59c657b3686e58b23eb5";
    int spanish_index = -1;
    for (std::size_t i = 0; i < known_sha1.size(); ++i) {
        if (known_sha1[i] && std::strcmp(known_sha1[i], kSpanishSha1) == 0) {
            spanish_index = static_cast<int>(i);
            break;
        }
    }
    std::vector<std::string> dump_paths;
    std::vector<const char*> known_regions;
    std::vector<const char*> known_boxarts;
    std::vector<const char*> language_labels;
    std::vector<const char*> language_paths;
    std::vector<int> language_sha1;
    std::string usa_path;
    std::string spa_path;
    std::string spanish_cover_path;   // persists for the launcher window — must
                                      // NOT be a temporary .string().c_str()
    if (spanish_index >= 0) {
        std::vector<std::string> known_sha1_str;
        known_sha1_str.reserve(known_sha1.size());
        for (const char* s : known_sha1)
            known_sha1_str.emplace_back(s ? s : "");
        dump_paths = seam_scan_cart_dumps(dir, known_sha1_str);
        if (!dump_paths.empty()) usa_path = dump_paths[0];
        for (std::size_t i = 0; i < dump_paths.size(); ++i) {
            if (static_cast<int>(i) == spanish_index) continue;
            if (!dump_paths[i].empty() && usa_path.empty())
                usa_path = dump_paths[i];
        }
        if (static_cast<std::size_t>(spanish_index) < dump_paths.size())
            spa_path = dump_paths[spanish_index];

        // The Spanish translation is a code-identical dump of the same cart,
        // so when it is DETECTED (regardless of which language is selected to
        // boot) this build is the "USA + Mod SPA" variant: show that label on
        // every known dump rather than only when the Spanish file is mounted.
        known_regions.assign(known_sha1.size(), "USA + Mod SPA");
        known_boxarts.assign(known_sha1.size(), nullptr);
        // Spanish box art is EMBEDDED in the executable (cover_embedded.h), not
        // read from a loose cover.jpg in the folder, so the shipped exe carries
        // both covers with no extra file to keep beside it. cover_embedded.h is
        // generated locally from a user-provided cover.jpg and is NOT shipped in
        // the source tree (it embeds copyrighted art). In a source checkout
        // without it, the Spanish dump simply falls through to the default box
        // art below.
#ifdef GBARECOMP_HAS_EMBEDDED_SPANISH_COVER
        const std::filesystem::path cover_out =
            std::filesystem::path(dir) / "assets" / "img" / "boxart_spanish.jpg";
        std::error_code ec;
        std::filesystem::create_directories(cover_out.parent_path(), ec);
        std::ofstream cf(cover_out, std::ios::binary | std::ios::trunc);
        if (cf) {
            cf.write(reinterpret_cast<const char*>(
                         gbarecomp_seam::kSpanishCoverJpg),
                     static_cast<std::streamsize>(
                         gbarecomp_seam::kSpanishCoverJpgSize));
            cf.close();
        }
        if (cf.good() || std::filesystem::file_size(cover_out, ec) ==
                            gbarecomp_seam::kSpanishCoverJpgSize) {
            spanish_cover_path = cover_out.string();   // stable storage
            known_boxarts[spanish_index] = spanish_cover_path.c_str();
        }
#endif
        // Non-Spanish dumps get the default boxart so the model resets
        // boxart_path (and the launcher reloads the texture) when the user
        // switches back to EN. Without this, the Spanish cover persists
        // because model_set_rom only overwrites boxart_path when the
        // per-dump entry is non-null.
        for (std::size_t i = 0; i < known_boxarts.size(); ++i) {
            if (static_cast<int>(i) != spanish_index && !known_boxarts[i])
                known_boxarts[i] = "assets/img/boxart.tga";
        }

        if (!spa_path.empty()) {
            language_labels.push_back("EN");
            language_paths.push_back(
                !usa_path.empty() ? usa_path.c_str() : spa_path.c_str());
            language_sha1.push_back(0);
            language_labels.push_back("ES");
            language_paths.push_back(spa_path.c_str());
            language_sha1.push_back(spanish_index);
        } else {
            language_labels.push_back("EN");
            language_paths.push_back(usa_path.c_str());
            language_sha1.push_back(0);
        }
    }
    if (!known_regions.empty()) {
        gi.known_rom_regions      = known_regions.data();
        gi.known_rom_boxarts      = known_boxarts.data();
        gi.language_labels        = language_labels.data();
        gi.num_languages          = static_cast<int>(language_labels.size());
        gi.language_rom_paths     = language_paths.data();
        gi.num_language_rom_paths = static_cast<int>(language_paths.size());
        gi.language_rom_sha1_index = language_sha1.data();
    }
    gbarecomp_seam::set_has_gyro_controls(gi, opts.launcher_expose_gyro);
    gbarecomp_seam::set_presentation_filter_caps(
        gi, opts.launcher_expose_sharp_filter,
        opts.launcher_expose_affine_filter);
#if defined(GBARECOMP_ENABLE_MODS)
    gi.mods = mod_provider;
#endif
    if (opts.launcher_expose_widescreen && opts.launcher_num_aspects > 0 &&
        opts.launcher_aspect_labels) {
        // Multi-width extended view: game-supplied aspect cycle, tagged
        // EXPERIMENTAL (the snesrecomp/psxrecomp widescreen convention).
        gi.aspect_labels       = opts.launcher_aspect_labels;
        gi.num_aspect_labels   = opts.launcher_num_aspects;
        gi.aspect_experimental = 1;
        gi.widescreen_supported = 0;   // the cycle supersedes the bool toggle
    }
    // Save row: the explicit per-game save path when the game declares one,
    // else the runtime's <rom>.sav convention derived from the seeded ROM.
    std::string save_display;
    if (opts.launcher_save_path && opts.launcher_save_path[0]) {
        save_display = opts.launcher_save_path;
    } else if (!seed_rom.empty()) {
        std::filesystem::path p(seed_rom);
        p.replace_extension(".sav");
        save_display = p.string();
    }
    if (!save_display.empty()) gi.sram_path = save_display.c_str();

    std::string title = std::string(gi.name) + " \xE2\x80\x94 Launcher";

    char picked_rom[1024] = {0};
    int rc = recomp_launcher_run_window(title.c_str(), &ls, &gi, dir.c_str(),
                                        seed_rom.c_str(),
                                        picked_rom, sizeof(picked_rom));
    if (rc == 1) return 1;    // user closed the launcher: quit without booting
    if (rc != 0) return 0;    // unavailable: fall back to the asset picker

    // ---- persist + translate the committed settings -------------------------
    cfg.scale         = ls.window_scale > 0 ? ls.window_scale : cfg.scale;
    // ls.fullscreen is already tri-state (0 off / 1 borderless / 2 exclusive)
    // per recomp_launcher.h. Persist it as-is — collapsing to a bool here
    // would silently downgrade an exclusive-fullscreen commit to borderless
    // on every subsequent launch.
    cfg.fullscreen    = (ls.fullscreen >= 0 && ls.fullscreen <= 2) ? ls.fullscreen : 0;
    cfg.linear_filter = ls.linear_filter ? 1 : 0;
    cfg.sharp_filter =
        gbarecomp_seam::get_sharp_filter(ls, cfg.sharp_filter != 0) ? 1 : 0;
    cfg.affine_filter =
        gbarecomp_seam::get_affine_filter(ls, cfg.affine_filter != 0) ? 1 : 0;
    cfg.screen_kind   = (ls.screen_kind >= 0 && ls.screen_kind <= 4)
                          ? ls.screen_kind : 0;
    cfg.volume        = ls.volume;
    cfg.audio_freq    = ls.audio_freq > 0 ? ls.audio_freq : 0;
    cfg.audio_shadow  = ls.audio_shadow != 0 ? 1 : 0;
    cfg.language_index = ls.language_index >= 0 ? ls.language_index : 0;
    cfg.skip_launcher = ls.skip_launcher ? 1 : 0;
    cfg.widescreen    = ls.widescreen ? 1 : 0;
    cfg.adaptive_view = ls.adaptive_view ? 1 : 0;
    cfg.aspect_index  = (opts.launcher_num_aspects > 0 &&
                         ls.aspect_index >= 0 &&
                         ls.aspect_index < opts.launcher_num_aspects)
                          ? ls.aspect_index : 0;
    cfg.gyro_sensitivity =
        std::clamp(gbarecomp_seam::get_gyro_sensitivity(
                       ls, cfg.gyro_sensitivity),
                   0.25f, 4.00f);
    seam_config_save(config_path, cfg);
    // Only rewrite [Solar] when this recomp-ui pin actually surfaced the panel;
    // otherwise the values never round-tripped through it and writing them back
    // would just churn the file the game owns.
    if (solar_in_launcher && pull_solar_settings(ls, &solar))
        seam_solar_save(config_path, solar);

    // Boot the cart dump the committed language maps to. The model already
    // mounted it when the user toggled EN/ES (so the region + cover matched
    // live), but the picked path is authoritative here — belt and suspenders
    // for hosts that seed the language without toggling.
    const char* boot_rom = picked_rom;
    if (ls.language_index >= 0 &&
        ls.language_index < static_cast<int>(language_paths.size()) &&
        language_paths[ls.language_index] &&
        language_paths[ls.language_index][0]) {
        boot_rom = language_paths[ls.language_index];
    }
    if (boot_rom && boot_rom[0]) {
        args.push_back("--rom");
        args.push_back(boot_rom);
        // Persist the pick NOW (not only after a successful boot) so the next
        // launch prefills instead of re-prompting — the whole point of the
        // launcher remembering. Mirrors the runtime asset picker's cache.
        write_single_line(rom_cfg, boot_rom);
    }
    if (ls.bios_path[0]) {
        args.push_back("--bios");
        args.push_back(ls.bios_path);
        write_single_line(bios_cfg, ls.bios_path);
    }
    seam_append_setting_args(args, cfg, opts);
    return 0;
}

#endif  // RECOMP_LAUNCHER
