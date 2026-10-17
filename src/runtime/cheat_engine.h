// cheat_engine.h — optional CodeBreaker Advance cheat presets.
//
// A preset is a named block of CodeBreaker Advance codes. When the player
// enables it from the in-game "Cheats" menu the runtime re-applies its codes
// once per guest frame (the standard emulator model: the game keeps writing
// its own values, so a cheat that pins HP/Zenie must be re-asserted).
//
// This is presentation/player convenience only: it writes to guest RAM through
// the same bus the game uses, never to generated code, and is off by default so
// native rendering, savestates and frame hashes are unaffected until enabled.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gbarecomp {

// Memory access used to apply codes. The runtime supplies a GbaBus-backed
// implementation; tests supply a plain array. All pointers may be null, in
// which case the corresponding access is a no-op.
struct CheatMemory {
    void* ctx = nullptr;
    uint8_t (*read8)(void*, uint32_t) = nullptr;
    uint16_t (*read16)(void*, uint32_t) = nullptr;
    void (*write8)(void*, uint32_t, uint8_t) = nullptr;
    void (*write16)(void*, uint32_t, uint16_t) = nullptr;
    void (*write32)(void*, uint32_t, uint32_t) = nullptr;
};

// One named cheat offered to the player. `code` is newline-separated
// CodeBreaker Advance code lines (8-hex address/type word + 4-hex value).
struct CheatPreset {
    const char* name = nullptr;
    const char* description = nullptr;
    const char* code = nullptr;
    // Optional in-game menu section. Null groups the preset under "Cheats";
    // games may group related presets (e.g. "Cheats - Multipliers") so the
    // menu reads as tidy categories instead of one flat list.
    const char* section = nullptr;
};

// A "multiplier" preset watches a live value and, whenever the guest changes
// it, writes back the change scaled by the configured factors. Intended for
// optional conveniences like "EXP x2" or a difficulty knob: the player keeps
// playing, but gains land twice (or losses hurt twice).
//
// Gains: new = last + delta x factor / gain_div, for positive deltas only.
// Losses: new = last + delta x loss_factor / loss_div, for negative deltas
// only (defaults 1/1 leave losses untouched; 2/1 doubles damage taken; 1/2
// halves it). Absolute resets and implausibly large jumps pass through
// untouched; enabling the preset seeds each address with the value present at
// that moment, so it never multiplies the current total.
struct CheatMultiplierTarget {
    uint32_t address = 0;
    uint32_t factor = 2;       // gain multiplier (paired with gain_div)
    uint32_t cap = 0;          // upper bound for written values; 0 = none
    uint32_t gain_div = 1;     // gain divisor (factor 1 + div 2 = x0.5)
    uint32_t loss_factor = 1;  // loss multiplier (damage x2 = 2)
    uint32_t loss_div = 1;     // loss divisor (damage x0.5 = 2)
    uint8_t  width = 4;        // watched width in bytes: 2 or 4
};

struct CheatMultiplierSpec {
    const char* name = nullptr;
    const char* description = nullptr;
    const CheatMultiplierTarget* targets = nullptr;
    std::size_t target_count = 0;
    // Optional in-game menu section; null follows the CheatPreset grouping.
    const char* section = nullptr;
};

// A parsed CodeBreaker op. Serial repeaters (type 4) fold their continuation
// line into the extra fields so the interpreter sees one op per source block.
struct CheatOp {
    uint8_t  type = 0;
    uint32_t addr = 0;
    uint32_t value = 0;
    uint32_t value_inc = 0;  // type 4: per-iteration value increment
    uint32_t count = 0;      // type 4: number of writes
    uint32_t addr_step = 0;  // type 4: per-iteration address increment
};

// Parse CodeBreaker Advance code text. Blank lines and lines starting with
// '#' or ';' are ignored. Unknown/garbled lines are skipped rather than
// aborting the block. Exposed so tests can drive the interpreter directly.
std::vector<CheatOp> cheat_parse_code(const std::string& text);

// Apply a parsed op list once. Unknown master codes are no-ops.
void cheat_apply_ops(const std::vector<CheatOp>& ops, const CheatMemory& mem);

class CheatEngine {
public:
    void add_preset(const CheatPreset& preset);
    void add_multiplier(const CheatMultiplierSpec& spec);

    std::size_t preset_count() const { return presets_.size(); }
    const char* preset_name(std::size_t i) const;
    const char* preset_description(std::size_t i) const;
    // Menu section for preset i; never null (defaults to "Cheats").
    const char* preset_section(std::size_t i) const;

    bool enabled(std::size_t i) const;
    void set_enabled(std::size_t i, bool on);
    void set_all(bool on);
    bool any_enabled() const;

    // Apply every enabled preset once. Cheap when nothing is enabled.
    void apply(const CheatMemory& mem) const;

private:
    struct Preset {
        struct Multiplier {
            uint32_t address = 0;
            uint32_t factor = 2;
            uint32_t cap = 0;
            uint32_t gain_div = 1;
            uint32_t loss_factor = 1;
            uint32_t loss_div = 1;
            uint8_t  width = 4;
            // Watch state; mutable because apply() runs on a const engine,
            // like the ops it sits beside.
            mutable uint32_t last = 0;
            mutable bool seeded = false;
        };
        std::string name;
        std::string description;
        std::string section;
        std::vector<CheatOp> ops;
        std::vector<Multiplier> multipliers;
        bool enabled = false;
    };
    std::vector<Preset> presets_;
};

}  // namespace gbarecomp
