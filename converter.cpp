#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <zlib.h>

using U = uint32_t;
using Byte = uint8_t;
using View = std::span<const Byte>;
using Slice = std::span<Byte>;
using Bytes = std::vector<Byte>;
constexpr U limit = 64 * 1024 * 1024;
constexpr char magic[16] = "DeSmuME SState";

void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

template<class T>
std::span<T> part(std::span<T> data, size_t at, size_t count) {
    if (at > data.size() || count > data.size() - at) {
        throw std::runtime_error("Truncated state or ROM at offset "
                                 + std::to_string(at) + " (need " + std::to_string(count)
                                 + " bytes, section size " + std::to_string(data.size()) + ")");
    }
    return data.subspan(at, count);
}

U get(View data, size_t at, unsigned width = 4) {
    U value = 0;
    auto bytes = part(data, at, width);
    for (unsigned i = 0; i < width; ++i) value |= U(bytes[i]) << (8 * i);
    return value;
}

void put(Slice data, size_t at, U value, unsigned width = 4) {
    auto bytes = part(data, at, width);
    for (unsigned i = 0; i < width; ++i) bytes[i] = Byte(value >> (8 * i));
}

struct Reader {
    View data;
    size_t pos = 0;
    View take(size_t size) {
        auto out = part(data, pos, size);
        pos += size;
        return out;
    }
    U read(unsigned width = 4) { return get(take(width), 0, width); }
    std::string name() {
        auto bytes = take(4);
        return {reinterpret_cast<const char*>(bytes.data()), 4};
    }
};

struct Source {
    std::map<std::string, View> sections;
    explicit Source(View data) {
        Reader reader{data};
        check(reader.name() == "MELN", "Expected a melonDS .ml state");
        check(reader.read() == 14, "Supported melonDS state version is 14.0");
        check(reader.read() == data.size(), "Invalid melonDS length");
        reader.take(4);
        while (reader.pos < data.size()) {
            auto name = reader.name();
            auto length = reader.read();
            check(length >= 16, "Invalid melonDS section length");
            reader.take(8);
            check(sections.emplace(name, reader.take(length - 16)).second, "Duplicate melonDS section");
        }
    }
    View at(const std::string& name) const {
        auto found = sections.find(name);
        check(found != sections.end(), "Missing melonDS section " + name);
        return found->second;
    }
};

struct Target {
    Bytes body;
    U version;
    bool is_compressed;
    std::map<U, std::pair<size_t, size_t>> chunks;
    explicit Target(View data) : version(get(data, 20)), is_compressed(get(data, 28) != 0xffffffff) {
        check(std::memcmp(part(data, 0, 16).data(), magic, 16) == 0 && get(data, 16) == 12,
              "Expected DeSmuME DST format version 12");
        const auto size = get(data, 24), packed = get(data, 28);
        check(size >= 4 && size <= limit, "Invalid DST length");
        if (packed == 0xffffffff) {
            check(size == data.size() && size >= 36, "Invalid uncompressed DST length");
            body.assign(data.begin() + 32, data.end());
        } else {
            check(packed == data.size() - 32, "Invalid compressed DST length");
            body.resize(size);
            uLongf decoded = size;
            uLong consumed = packed;
            check(uncompress2(body.data(), &decoded, data.data() + 32, &consumed) == Z_OK
                  && decoded == size && consumed == packed, "Invalid DST compressed data");
        }
        Reader reader{body};
        for (;;) {
            auto id = reader.read();
            if (id == 0xffffffff) break;
            auto length = reader.read();
            auto offset = reader.pos;
            reader.take(length);
            check(chunks.emplace(id, std::pair(offset, length)).second, "Duplicate DST chunk");
        }
        check(reader.pos == body.size(), "Unexpected data after DST terminator");
    }
    Slice chunk(U id) {
        auto found = chunks.find(id);
        check(found != chunks.end(), "Missing DST chunk " + std::to_string(id));
        const auto [offset, length] = found->second;
        return part(Slice(body), offset, length);
    }
    Slice field(U id, const std::string& name) {
        auto data = chunk(id);
        Reader reader{data};
        while (reader.pos < data.size()) {
            auto key = reader.name();
            auto width = reader.read(), count = reader.read();
            check(width && count <= (data.size() - reader.pos) / width, "Invalid DST field size");
            auto offset = reader.pos;
            auto bytes = reader.take(size_t(width) * count);
            if (key == name) return data.subspan(offset, bytes.size());
        }
        throw std::runtime_error("Missing DST field " + name);
    }
    void copy(U id, const std::string& name, View bytes) {
        auto out = field(id, name);
        check(out.size() == bytes.size(), "Unexpected DST field width for " + name);
        std::ranges::copy(bytes, out.begin());
    }
    void value(U id, const std::string& name, U value, unsigned width = 4) {
        auto out = field(id, name);
        check(out.size() == width, "Unexpected DST scalar width for " + name);
        put(out, 0, value, width);
    }
    Bytes encode() {
        uLongf size = is_compressed ? compressBound(body.size()) : body.size();
        Bytes output(32 + size);
        if (is_compressed) {
            check(compress2(output.data() + 32, &size, body.data(), body.size(), 1) == Z_OK,
                  "Could not compress DST");
        } else {
            std::ranges::copy(body, output.begin() + 32);
        }
        std::memcpy(output.data(), magic, 16);
        put(output, 16, 12);
        put(output, 20, version);
        put(output, 24, body.size() + (is_compressed ? 0 : 32));
        put(output, 28, is_compressed ? size : 0xffffffff);
        output.resize(32 + size);
        return output;
    }
};

// KEY1 uses Blowfish with little-endian words, swapped block order, and a
// cartridge-specific key schedule. See melonDS src/NDSCart.cpp.
using Key1Table = std::array<U, 0x412>;

void encrypt_key1(const Key1Table& table, U* block) {
    U left = block[1], right = block[0];
    for (unsigned i = 0; i < 16; ++i) {
        const U value = left ^ table[i];
        const U mixed = ((table[0x12 + (value >> 24)]
                        + table[0x112 + ((value >> 16) & 255)])
                        ^ table[0x212 + ((value >> 8) & 255)])
                        + table[0x312 + (value & 255)];
        left = right ^ mixed;
        right = value;
    }
    block[0] = left ^ table[16];
    block[1] = right ^ table[17];
}

Key1Table key1_table(View bios, U game_code, unsigned level) {
    Key1Table table{};
    if (!bios.empty()) {
        for (size_t i = 0; i < table.size(); ++i) table[i] = get(bios, 0x30 + i * 4);
    }
    U keycode[] = {game_code, game_code >> 1, game_code << 1};
    for (unsigned step = 1; step <= level; ++step) {
        if (step == 3) {
            keycode[1] <<= 1;
            keycode[2] >>= 1;
        }
        encrypt_key1(table, keycode + 1);
        encrypt_key1(table, keycode);
        for (unsigned i = 0; i < 18; ++i) {
            const U word = keycode[i % 2];
            table[i] ^= (word << 24) | ((word & 0x0000ff00) << 8)
                      | ((word & 0x00ff0000) >> 8) | (word >> 24);
        }
        U block[2]{};
        for (size_t i = 0; i < table.size(); i += 2) {
            encrypt_key1(table, block);
            table[i] = block[1];
            table[i + 1] = block[0];
        }
    }
    return table;
}

U normalized_rom_crc(View code, View bios) {
    const auto header = code.first(64);
    constexpr size_t secure_area_size = 0x800;
    auto area = part(code, header.size(), secure_area_size);
    std::array<Byte, secure_area_size> encrypted;
    std::ranges::copy(area, encrypted.begin());
    std::memcpy(encrypted.data(), "encryObj", 8);
    auto table = key1_table(bios, get(header, 0x0c), 3);
    auto encrypt_at = [&](size_t at) {
        U block[] = {get(encrypted, at), get(encrypted, at + 4)};
        encrypt_key1(table, block);
        put(encrypted, at, block[0]);
        put(encrypted, at + 4, block[1]);
    };
    for (size_t at = 0; at < encrypted.size(); at += 8) encrypt_at(at);
    table = key1_table(bios, get(header, 0x0c), 2);
    encrypt_at(0);
    auto crc = crc32(0, header.data(), header.size());
    crc = crc32(crc, encrypted.data(), encrypted.size());
    const auto rest = code.subspan(header.size() + encrypted.size());
    return crc32(crc, rest.data(), rest.size());
}

void verify_rom(const Source& source, Target& target, View code, U rom_size, View bios) {
    const auto header = part(code, 0, 64);
    for (unsigned at : {0x20, 0x30}) {
        auto offset = get(header, at), size = get(header, at + 12);
        check(size && offset <= rom_size && size <= rom_size - offset, "Invalid NDS ROM header");
    }
    check(code.size() == 64ULL + get(header, 0x2c) + get(header, 0x3c), "Invalid NDS ROM code size");
    check(std::ranges::equal(header, part(target.field(130, "GINF"), 0, 64))
          && get(target.field(130, "GRSZ"), 0) == rom_size, "The DST template uses a different ROM");
    auto cart = source.at("NDSC");
    // Slot header followed by two cartridge bus interfaces.
    constexpr size_t type_offset = 20 + 2 * 61;
    check(get(cart, type_offset) != 0, "The melonDS state has no cartridge or uses an unsupported cartridge layout");
    const U expected = get(cart, type_offset + 4);
    if (expected == crc32(0, code.data(), code.size())) return;

    const U arm9_offset = get(header, 0x20), arm9_size = get(header, 0x2c);
    constexpr U decrypted_marker = 0xe7ffdeff;
    if (arm9_offset >= 0x4000 && arm9_offset < 0x8000 && arm9_size >= 0x800
        && get(code, 64) == decrypted_marker && get(code, 64 + 0x10) != decrypted_marker) {
        // melonDS encrypts in memory when inserting a ROM with a decrypted area.
        // Its built-in BIOS uses a zero-initialized table for this operation.
        if (expected == normalized_rom_crc(code, {})) return;
        check(!bios.empty(), "内蔵BIOSでのROM照合が一致しません。外部BIOSで保存した場合は、そのARM7 BIOS（bios7.bin）を選択してください。");
        check(bios.size() == 0x4000 && crc32(0, bios.data(), bios.size()) == 0x1280f0d5,
              "melonDSが純正DS BIOSとして認識するARM7 BIOS（bios7.bin）を選択してください。");
        if (expected == normalized_rom_crc(code, bios)) return;
    }
    throw std::runtime_error("The melonDS state uses a different ROM or cartridge layout");
}

void cpu(View in, Target& out, U id, char core) {
    check(in.size() == 172, "Unsupported ARM section layout");
    auto write = [&](const char* name, U value, unsigned width = 4) {
        out.value(id, core + std::string(name), value, width);
    };
    const auto cpsr = get(in, 72), mode = cpsr & 31, pc = get(in, 68), halted = get(in, 4);
    const auto width = cpsr & 32 ? 2 : 4;
    check(halted <= 2 && pc % width == 0, "Unsupported ARM halt or PC state");
    write("INS", get(in, 160, width));
    write("INA", pc - width);
    write("INN", pc);
    const auto regs = core + std::string("REG");
    out.copy(id, regs, part(in, 8, 64));
    put(out.field(id, regs), 60, pc + width);
    write("CPS", cpsr);

    struct Bank { unsigned mode, offset; const char* sp; const char* lr; const char* ps; };
    constexpr Bank banks[] = {
        {17, 96, "DFI", "EFI", "FIQ"}, {18, 132, "DIR", "EIR", "IRQ"},
        {19, 108, "DSV", "ESV", "SVC"}, {23, 120, "DAB", "EAB", "ABT"},
        {27, 144, "DUN", "EUN", "UND"},
    };
    auto sp_user = get(in, 60), lr_user = get(in, 64), spsr = cpsr;
    bool known = mode == 16 || mode == 31;
    for (const auto& bank : banks) {
        const bool active = mode == bank.mode;
        if (active) {
            known = true;
            sp_user = get(in, bank.offset);
            lr_user = get(in, bank.offset + 4);
            spsr = get(in, bank.offset + 8);
        }
        write(bank.sp, active ? get(in, 60) : get(in, bank.offset));
        write(bank.lr, active ? get(in, 64) : get(in, bank.offset + 4));
        write(bank.ps, get(in, bank.offset + 8));
    }
    check(known, "Unsupported ARM mode");
    constexpr const char* fiq[] = {"8FI", "9FI", "AFI", "BFI", "CFI"};
    for (unsigned i = 0; i < 5; ++i) write(fiq[i], get(in, (mode == 17 ? 40 : 76) + i * 4));
    write("DUS", sp_user);
    write("EUS", lr_user);
    write("SPS", spsr);
    write("int", get(in, 168));
    write("FRZ", halted == 1 ? 3 : 0);
    write("iws", 0, 1);
}

void memory(const Source& source, Target& out) {
    Reader r{source.at("NDSG")};
    check(r.read() == 0, "Only Nintendo DS mode is supported");
    out.copy(4, "WRAM", r.take(0x1000000).first(0x400000));
    out.copy(60, "MSWI", r.take(0x8000));
    out.copy(60, "M7ER", r.take(0x10000));
    auto io9 = out.field(4, "9REG"), io7 = out.field(60, "M7RG");
    put(io9, 0x204, r.read(2), 2);
    put(io7, 0x204, r.read(2), 2);
    r.take(2);
    out.copy(60, "MIME", r.take(8));
    out.copy(60, "MIE_", r.take(8));
    out.copy(60, "MIF_", r.take(8));
    std::ranges::fill(out.field(60, "MIFP"), 0);
    r.take(8);
    put(io9, 0x300, r.read(1), 1);
    put(io7, 0x300, r.read(1), 1);
    put(io9, 0x304, r.read(2), 2);
    put(io7, 0x304, r.read(2), 2);
    put(io7, 0x308, r.read(2), 2);
    put(io9, 0x180, r.read(2), 2);
    put(io7, 0x180, r.read(2), 2);
    put(io9, 0x184, r.read(2), 2);
    put(io7, 0x184, r.read(2), 2);
    for (std::string prefix : {"F0", "F1"}) {
        auto count = r.read(), read = r.read(), write = r.read();
        check(count <= 16 && read < 16 && write < 16 && (read + count) % 16 == write,
              "Invalid melonDS IPC FIFO");
        out.value(60, prefix + "TH", read, 1);
        out.value(60, prefix + "TL", write, 1);
        out.value(60, prefix + "SZ", count, 1);
        out.copy(60, prefix + "BF", r.take(64));
    }
    // Keep the template's emulator-specific timers, DMA and scheduler state.
    r.take(4 + 4 + 8 * 12 + 2 + 16 + 16 + 24 * 16 + 4 + 7 * 8 + 12);
    put(io9, 0x132, r.read(2), 2);
    put(io7, 0x132, r.read(2), 2);
    put(io7, 0x134, r.read(2), 2);
    auto wram = r.read(1);
    check(wram < 4, "Invalid melonDS WRAM mapping");
    put(io9, 0x247, wram, 1);
    put(io7, 0x241, wram, 1);
    put(out.field(60, "MWRA"), 0, wram, 1);

    Reader cp{source.at("CP15")};
    auto control = cp.read(), dtcm = cp.read(), itcm = cp.read();
    out.copy(4, "ITCM", cp.take(0x8000));
    out.copy(4, "DTCM", cp.take(0x4000));
    auto cp15 = out.chunk(3);
    check(cp15.size() == 496 && get(cp15, 0) == 1, "Unsupported DeSmuME CP15 layout");
    auto code_cache = cp.read(), data_cache = cp.read(), write_buffer = cp.read();
    auto code_access = cp.read(), data_access = cp.read();
    auto regions = cp.take(32);
    if (control & 1) {
        check(std::ranges::equal(regions, part(cp15, 44, 32))
              && code_access == get(cp15, 40) && data_access == get(cp15, 36),
              "MPU configuration differs. Use a DeSmuME template closer to the original scene.");
    }
    put(cp15, 16, control);
    put(cp15, 20, data_cache);
    put(cp15, 24, code_cache);
    put(cp15, 28, write_buffer);
    put(cp15, 88, itcm);
    put(cp15, 92, dtcm & 0x0ffff000);
    out.value(60, "MDTC", control & (1 << 16) ? dtcm & 0x0ffff000 : 0x08000000);
    out.value(60, "M9RW", (control >> 7) & 1, 1);
    out.value(1, "9LDT", (control & (1 << 15)) == 0, 1);
}

void video(const Source& source, Target& out) {
    Reader r{source.at("GPUG")};
    r.take(2);
    r.take(7);
    auto status9 = r.read(2), status7 = r.read(2);
    r.take(4 + 32 + 2 + 512);
    auto bright9 = r.read(2), bright7 = r.read(2), capture = r.read();
    r.take(1);
    out.copy(4, "VMEM", r.take(0x800));
    out.copy(4, "OAMS", r.take(0x800));
    out.copy(4, "LCDM", r.take(0xa4000));
    auto io9 = out.field(4, "9REG"), io7 = out.field(60, "M7RG");
    for (unsigned i = 0; i < 9; ++i) put(io9, 0x240 + i + (i >= 7), r.read(1), 1);
    put(io7, 0x240, r.read(1), 1);
    // Preserve the destination scheduler's blanking flags and scanline.
    put(io9, 4, (status9 & ~7U) | (get(io9, 4, 2) & 7), 2);
    put(io7, 4, (status7 & ~7U) | (get(io7, 4, 2) & 7), 2);
    put(io9, 0x64, capture);
    put(io9, 0x6c, bright9, 2);
    put(io9, 0x106c, bright7, 2);
    for (unsigned engine = 0; engine < 2; ++engine) {
        Reader g{source.at(engine ? "GP2B" : "GP2A")};
        auto io = part(io9, engine * 0x1000, 0x70);
        put(io, 0, g.read());
        g.take(15);
        auto copy = [&](unsigned at, unsigned size) {
            auto dest = part(io, at, size);
            std::ranges::copy(g.take(size), dest.begin());
        };
        copy(8, 8);
        auto x = g.take(8), y = g.take(8);
        for (unsigned i = 0; i < 4; ++i) {
            put(io, 0x10 + 4 * i, get(x, 2 * i, 2), 2);
            put(io, 0x12 + 4 * i, get(y, 2 * i, 2), 2);
        }
        x = g.take(8);
        y = g.take(8);
        g.take(32);
        for (unsigned i = 0; i < 2; ++i) {
            put(io, 0x28 + 0x10 * i, get(x, 4 * i));
            put(io, 0x2c + 0x10 * i, get(y, 4 * i));
        }
        // The state groups affine coefficients by letter. IO groups by background.
        for (unsigned i = 0; i < 4; ++i) {
            put(io, 0x20 + 2 * i, g.read(2), 2);
            put(io, 0x30 + 2 * i, g.read(2), 2);
        }
        auto win0 = g.take(4), win1 = g.take(4);
        for (unsigned i = 0; i < 2; ++i) {
            put(io, 0x40 + 4 * i, (win0[2 * i] << 8) | win0[2 * i + 1], 2);
            put(io, 0x42 + 4 * i, (win1[2 * i] << 8) | win1[2 * i + 1], 2);
        }
        copy(0x48, 4);
        U mosaic = 0;
        for (unsigned i = 0; i < 4; ++i) mosaic |= (g.read(1) & 15) << (4 * i);
        put(io, 0x4c, mosaic, 2);
        g.take(13);
        copy(0x50, 4);
        g.take(2);
        put(io, 0x54, g.read(1), 2);
    }
}

static Bytes result;
static std::string error;

extern "C" {
int convert_state(const Byte* ml, U ml_size, const Byte* dst, U dst_size,
                  const Byte* rom_code, U code_size, U rom_size,
                  const Byte* bios, U bios_size) {
    Bytes{}.swap(result);
    error.clear();
    try {
        check(ml_size <= limit && dst_size <= limit && code_size <= limit, "Input exceeds 64 MiB");
        Source source({ml, ml_size});
        Target target({dst, dst_size});
        verify_rom(source, target, {rom_code, code_size}, rom_size, {bios, bios_size});
        cpu(source.at("ARM9"), target, 1, '9');
        cpu(source.at("ARM7"), target, 2, '7');
        memory(source, target);
        video(source, target);
        result = target.encode();
        return 1;
    } catch (const std::exception& e) {
        error = e.what();
        return 0;
    }
}
const Byte* result_data() { return result.data(); }
U result_size() { return result.size(); }
const char* result_error() { return error.c_str(); }
void clear_result() { Bytes{}.swap(result); }
}
