// cheat_engine.cpp — see cheat_engine.h.
#include "cheat_engine.h"

#include <cctype>
#include <cstdlib>

namespace gbarecomp {
namespace {

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Parse up to `max_digits` hex digits; returns false on a non-hex character.
bool parse_hex(const std::string& s, std::size_t pos, std::size_t max_digits,
               uint32_t* out) {
    uint32_t v = 0;
    std::size_t taken = 0;
    while (pos < s.size() && taken < max_digits) {
        const int d = hex_digit(s[pos]);
        if (d < 0) break;
        v = (v << 4) | static_cast<uint32_t>(d);
        ++pos;
        ++taken;
    }
    if (taken == 0) return false;
    *out = v;
    return true;
}

// Collapse a code line to its hex digits (drop spaces/tabs).
std::string hex_only(const std::string& line) {
    std::string out;
    out.reserve(line.size());
    for (char c : line) {
        if (hex_digit(c) >= 0) out.push_back(c);
    }
    return out;
}

}  // namespace

std::vector<CheatOp> cheat_parse_code(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    auto flush = [&]() {
        std::size_t a = 0;
        while (a < cur.size() && std::isspace(static_cast<unsigned char>(cur[a]))) ++a;
        std::size_t b = cur.size();
        while (b > a && std::isspace(static_cast<unsigned char>(cur[b - 1]))) --b;
        std::string trimmed = cur.substr(a, b - a);
        cur.clear();
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') return;
        lines.push_back(hex_only(trimmed));
    };
    for (char c : text) {
        if (c == '\n' || c == '\r') flush();
        else cur.push_back(c);
    }
    flush();

    std::vector<CheatOp> ops;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.size() < 12) continue;
        uint32_t word = 0, value = 0;
        if (!parse_hex(line, 0, 8, &word)) continue;
        if (!parse_hex(line, 8, 4, &value)) continue;
        CheatOp op;
        op.type = static_cast<uint8_t>((word >> 28) & 0xF);
        op.addr = word & 0x0FFFFFFFu;
        op.value = value & 0xFFFFu;
        if (op.type == 4 && i + 1 < lines.size()) {
            const std::string& cont = lines[++i];
            if (cont.size() >= 12) {
                uint32_t cw = 0, cv = 0;
                if (parse_hex(cont, 0, 8, &cw) && parse_hex(cont, 8, 4, &cv)) {
                    op.value_inc = (cw >> 16) & 0xFFFFu;
                    op.count = cw & 0xFFFFu;
                    op.addr_step = cv & 0xFFFFu;
                }
            }
        }
        ops.push_back(op);
    }
    return ops;
}

void cheat_apply_ops(const std::vector<CheatOp>& ops, const CheatMemory& mem) {
    bool skip = false;
    for (const CheatOp& op : ops) {
        if (skip) {
            skip = false;
            continue;
        }
        switch (op.type) {
            case 0x0:  // CodeBreaker master code 1
            case 0x1:  // CodeBreaker master code 2
                break;
            case 0x2:  // 16-bit OR
                if (mem.read16 && mem.write16) {
                    mem.write16(mem.ctx, op.addr,
                                static_cast<uint16_t>(mem.read16(mem.ctx, op.addr) |
                                                      op.value));
                }
                break;
            case 0x3:  // 8-bit write
                if (mem.write8) {
                    mem.write8(mem.ctx, op.addr, static_cast<uint8_t>(op.value));
                }
                break;
            case 0x4:  // serial repeater
                if (mem.write16) {
                    for (uint32_t k = 0; k < op.count; ++k) {
                        mem.write16(mem.ctx, op.addr + k * op.addr_step,
                                    static_cast<uint16_t>(op.value +
                                                          k * op.value_inc));
                    }
                }
                break;
            case 0x6:  // 16-bit AND
                if (mem.read16 && mem.write16) {
                    mem.write16(mem.ctx, op.addr,
                                static_cast<uint16_t>(mem.read16(mem.ctx, op.addr) &
                                                      op.value));
                }
                break;
            case 0x7:  // 16-bit "if equal": run next only when equal
                if (!(mem.read16 && mem.read16(mem.ctx, op.addr) == op.value)) {
                    skip = true;
                }
                break;
            case 0x8:  // 16-bit write
                if (mem.write16) {
                    mem.write16(mem.ctx, op.addr, static_cast<uint16_t>(op.value));
                }
                break;
            case 0xA:  // 16-bit "if not equal"
                if (!(mem.read16 && mem.read16(mem.ctx, op.addr) != op.value)) {
                    skip = true;
                }
                break;
            case 0xC:  // 16-bit "if less than"
                if (!(mem.read16 && mem.read16(mem.ctx, op.addr) < op.value)) {
                    skip = true;
                }
                break;
            case 0xD: {  // button activator (D00000X0 YYYY)
                const uint32_t sel = (op.addr >> 4) & 0xFu;
                const uint16_t keys =
                    mem.read16 ? mem.read16(mem.ctx, 0x04000130u) : 0xFFFFu;
                const bool pressed = ((~keys) & op.value) != 0;
                if (sel == 2) {
                    if (!pressed) skip = true;
                } else if (sel == 1) {
                    if (pressed) skip = true;
                } else {
                    skip = true;
                }
                break;
            }
            case 0xF:  // 16-bit "if (value & mask) != 0"
                if (!(mem.read16 &&
                      (mem.read16(mem.ctx, op.addr) & op.value) != 0)) {
                    skip = true;
                }
                break;
            default:
                break;
        }
    }
}

void CheatEngine::add_preset(const CheatPreset& preset) {
    Preset p;
    if (preset.name) p.name = preset.name;
    if (preset.description) p.description = preset.description;
    if (preset.code) p.ops = cheat_parse_code(preset.code);
    presets_.push_back(std::move(p));
}

const char* CheatEngine::preset_name(std::size_t i) const {
    return i < presets_.size() ? presets_[i].name.c_str() : "";
}

const char* CheatEngine::preset_description(std::size_t i) const {
    return i < presets_.size() ? presets_[i].description.c_str() : "";
}

bool CheatEngine::enabled(std::size_t i) const {
    return i < presets_.size() && presets_[i].enabled;
}

void CheatEngine::set_enabled(std::size_t i, bool on) {
    if (i < presets_.size()) presets_[i].enabled = on;
}

void CheatEngine::set_all(bool on) {
    for (Preset& p : presets_) p.enabled = on;
}

bool CheatEngine::any_enabled() const {
    for (const Preset& p : presets_) {
        if (p.enabled) return true;
    }
    return false;
}

void CheatEngine::apply(const CheatMemory& mem) const {
    for (const Preset& p : presets_) {
        if (p.enabled) cheat_apply_ops(p.ops, mem);
    }
}

}  // namespace gbarecomp
