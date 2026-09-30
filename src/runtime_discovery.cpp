#include "runtime_discovery.h"

#include <Windows.h>
#include <hde32.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace HeadTracking {
namespace {

void Require(bool condition, const std::string& reason) {
    if (!condition) throw std::runtime_error(reason);
}

template<class T>
T Unique(std::vector<T> values, const std::string& what) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    Require(values.size() == 1, what + ": expected one candidate, found " + std::to_string(values.size()));
    return values.front();
}

struct Instruction {
    uint32_t address;
    hde32s decoded{};

    uint8_t Op() const { return decoded.opcode; }
    uint32_t End() const { return address + decoded.len; }
    bool Call() const { return Op() == 0xE8; }
    uint32_t Target() const {
        const int64_t displacement = decoded.flags & F_IMM8 ?
            static_cast<int8_t>(decoded.imm.imm8) : static_cast<int32_t>(decoded.imm.imm32);
        const int64_t target = static_cast<int64_t>(End()) + displacement;
        Require(target >= 0 && target <= UINT32_MAX, "relative branch overflows image address");
        return static_cast<uint32_t>(target);
    }
    bool Ret() const { return Op() == 0xC3 || Op() == 0xC2; }
    int Base() const {
        if (!(decoded.flags & F_MODRM) || decoded.modrm_mod == 3) return -1;
        if (decoded.flags & F_SIB) {
            return decoded.modrm_mod == 0 && decoded.sib_base == 5 ? -1 : decoded.sib_base;
        }
        return decoded.modrm_mod == 0 && decoded.modrm_rm == 5 ? -1 : decoded.modrm_rm;
    }
    int Index() const {
        return decoded.flags & F_SIB && decoded.sib_index != 4 ? decoded.sib_index : -1;
    }
    int32_t Disp() const {
        return decoded.flags & F_DISP8 ? static_cast<int8_t>(decoded.disp.disp8) :
            decoded.flags & F_DISP32 ? static_cast<int32_t>(decoded.disp.disp32) : 0;
    }
    uint32_t Absolute() const {
        if (decoded.p_seg) return 0;
        if (Op() >= 0xA0 && Op() <= 0xA3) return decoded.imm.imm32;
        if ((decoded.flags & F_MODRM) && decoded.modrm_mod != 3 && Base() == -1 && Index() == -1)
            return decoded.disp.disp32;
        return 0;
    }
    bool MovReg(int destination, int source) const {
        return decoded.modrm_mod == 3 &&
            ((Op() == 0x8B && decoded.modrm_reg == destination && decoded.modrm_rm == source) ||
             (Op() == 0x89 && decoded.modrm_rm == destination && decoded.modrm_reg == source));
    }
    bool Load(int destination, int base, int32_t offset) const {
        return Op() == 0x8B && decoded.modrm_mod != 3 && decoded.modrm_reg == destination &&
            Base() == base && Index() == -1 && Disp() == offset;
    }
};

using Code = std::vector<Instruction>;

class Image {
public:
    Image(const uint8_t* bytes, size_t size, uint32_t base) : bytes_(bytes), size_(size), base_(base) {
        Require(bytes && size >= sizeof(IMAGE_DOS_HEADER), "truncated DOS header");
        const auto dos = ReadRaw<IMAGE_DOS_HEADER>(0);
        Require(dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= sizeof(dos), "invalid DOS header");
        const auto nt = ReadRaw<IMAGE_NT_HEADERS32>(static_cast<uint32_t>(dos.e_lfanew));
        Require(nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
                nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC, "expected PE32 x86 image");
        Require(nt.OptionalHeader.SizeOfImage == size && static_cast<uint64_t>(base) + size <= UINT32_MAX,
                "invalid mapped image extent");
        Require(nt.FileHeader.NumberOfSections > 0 && nt.FileHeader.NumberOfSections <= 96 &&
                nt.FileHeader.SizeOfOptionalHeader >= sizeof(IMAGE_OPTIONAL_HEADER32), "invalid section table");
        const uint64_t sectionOffset = static_cast<uint64_t>(dos.e_lfanew) + 24 + nt.FileHeader.SizeOfOptionalHeader;
        for (uint32_t i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            const auto section = ReadRaw<IMAGE_SECTION_HEADER>(sectionOffset + i * sizeof(IMAGE_SECTION_HEADER));
            Require(section.VirtualAddress < size && section.Misc.VirtualSize <= size - section.VirtualAddress,
                    "section outside image");
            const Range range{base + section.VirtualAddress, section.Misc.VirtualSize, section.Characteristics};
            for (const auto& other : ranges_)
                Require(static_cast<uint64_t>(range.start) + range.size <= other.start ||
                        static_cast<uint64_t>(other.start) + other.size <= range.start, "overlapping sections");
            ranges_.push_back(range);
        }
        fingerprint = {nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage, nt.OptionalHeader.CheckSum};
        for (const auto& range : ranges_) {
            if (!(range.flags & IMAGE_SCN_MEM_READ) || range.flags & IMAGE_SCN_MEM_EXECUTE) continue;
            for (uint32_t i = 0; i + 4 <= range.size; i += 4)
                pointers_[Read<uint32_t>(range.start + i)].push_back(range.start + i);
        }
    }

    template<class T> T ReadRaw(uint64_t offset) const {
        Require(offset <= size_ && sizeof(T) <= size_ - offset, "truncated image read");
        T value;
        std::memcpy(&value, bytes_ + offset, sizeof(value));
        return value;
    }
    template<class T> T Read(uint32_t address) const {
        Require(Readable(address, sizeof(T)), "read outside readable image section");
        return ReadRaw<T>(address - base_);
    }
    bool Readable(uint32_t address, size_t length = 1) const { return Contains(address, length, IMAGE_SCN_MEM_READ); }
    bool Exec(uint32_t address, size_t length = 1) const { return Contains(address, length, IMAGE_SCN_MEM_EXECUTE); }
    bool Data(uint32_t address, size_t length = 4) const { return Readable(address, length) && !Exec(address); }
    std::vector<uint32_t> Strings(const std::string& text) const {
        std::vector<uint32_t> found;
        for (const auto& range : ranges_) {
            if (!(range.flags & IMAGE_SCN_MEM_READ) || range.flags & IMAGE_SCN_MEM_EXECUTE) continue;
            for (uint32_t i = 0; i + text.size() + 1 <= range.size; ++i) {
                const auto* p = bytes_ + range.start - base_ + i;
                if (std::memcmp(p, text.c_str(), text.size() + 1) == 0) found.push_back(range.start + i);
            }
        }
        return found;
    }
    const std::vector<uint32_t>& References(uint32_t value) const {
        const auto it = pointers_.find(value);
        static const std::vector<uint32_t> empty;
        return it == pointers_.end() ? empty : it->second;
    }
    std::vector<uint32_t> CodeReferences(uint32_t value) const {
        std::vector<uint32_t> result;
        for (const auto& range : ranges_) {
            if (!(range.flags & IMAGE_SCN_MEM_EXECUTE)) continue;
            for (uint32_t offset = 0; offset + 4 <= range.size; ++offset)
                if (ReadRaw<uint32_t>(range.start - base_ + offset) == value) result.push_back(range.start + offset);
        }
        return result;
    }
    std::vector<uint32_t> CallSites(uint32_t target) const {
        std::vector<uint32_t> result;
        for (const auto& range : ranges_) {
            if (!(range.flags & IMAGE_SCN_MEM_EXECUTE)) continue;
            for (uint32_t offset = 0; offset + 5 <= range.size; ++offset) {
                const uint32_t address = range.start + offset;
                if (ReadRaw<uint8_t>(address - base_) != 0xE8) continue;
                const int64_t resolved = static_cast<int64_t>(address) + 5 + ReadRaw<int32_t>(address - base_ + 1);
                if (resolved == target) result.push_back(address);
            }
        }
        return result;
    }
    std::vector<uint32_t> Enclosing(uint32_t address, uint32_t limit = 0x2000) {
        std::vector<uint32_t> functions;
        const uint32_t minimum = address > limit ? address - limit : 0;
        for (uint32_t entry = address & ~15u; entry >= minimum && entry >= 16; entry -= 16) {
            if (!Exec(entry) || !Readable(entry - 1) || Read<uint8_t>(entry - 1) != 0xCC) continue;
            try {
                const auto& function = Function(entry);
                if (std::any_of(function.begin(), function.end(), [address](const Instruction& i) { return i.address == address; }))
                    functions.push_back(entry);
            } catch (const std::runtime_error&) {
                // Padding is only an entry candidate; a valid control-flow decode establishes its boundary.
            }
        }
        return functions;
    }
    Instruction Decode(uint32_t address) const {
        Require(Exec(address) && Readable(address), "instruction outside executable image");
        uint8_t buffer[32]{};
        size_t available = 0;
        while (available < 16 && Exec(address + static_cast<uint32_t>(available)) &&
               Readable(address + static_cast<uint32_t>(available))) ++available;
        std::memcpy(buffer, bytes_ + address - base_, available);
        Instruction instruction{address};
        hde32_disasm(buffer, &instruction.decoded);
        Require(!(instruction.decoded.flags & F_ERROR) && instruction.decoded.len &&
                instruction.decoded.len <= available, "invalid or truncated x86 instruction");
        Require(!instruction.decoded.p_67, "unsupported x86 address-size override");
        return instruction;
    }
    const Code& Function(uint32_t entry) {
        const auto existing = functions_.find(entry);
        if (existing != functions_.end()) return existing->second;
        std::map<uint32_t, Instruction> instructions;
        std::deque<uint32_t> pending{entry};
        while (!pending.empty()) {
            uint32_t address = pending.front(); pending.pop_front();
            while (!instructions.count(address)) {
                Require(address >= entry && address - entry < 0x20000 && instructions.size() < 20000,
                        "function exceeds bounded decode window");
                const auto inst = Decode(address);
                Require(inst.Op() != 0xCC, "function runs into padding");
                const auto next = instructions.lower_bound(address);
                Require(next == instructions.end() || inst.End() <= next->first, "overlapping x86 instructions");
                Require(next == instructions.begin() || std::prev(next)->second.End() <= address,
                        "branch into an instruction");
                instructions.emplace(address, inst);
                const bool conditional = (inst.Op() >= 0x70 && inst.Op() <= 0x7F) ||
                    (inst.Op() == 0x0F && inst.decoded.opcode2 >= 0x80 && inst.decoded.opcode2 <= 0x8F);
                const bool jump = inst.Op() == 0xE9 || inst.Op() == 0xEB;
                if (conditional || jump) {
                    const uint32_t target = inst.Target();
                    Require(Exec(target), "branch target outside executable image");
                    if (conditional || (target >= entry && target - entry < 0x2000)) pending.push_back(target);
                }
                if (inst.Ret() || jump || (inst.Op() == 0xFF && inst.decoded.modrm_reg == 4)) break;
                address = inst.End();
            }
        }
        Code code;
        for (const auto& pair : instructions) code.push_back(pair.second);
        return functions_.emplace(entry, std::move(code)).first->second;
    }
    std::vector<uint32_t> Calls(uint32_t entry) {
        std::vector<uint32_t> result;
        for (const auto& i : Function(entry)) if (i.Call()) {
            Require(Exec(i.Target()), "direct call outside executable image");
            result.push_back(i.Target());
        }
        return result;
    }
    std::array<uint32_t, 3> fingerprint{};

private:
    struct Range { uint32_t start, size, flags; };
    bool Contains(uint32_t address, size_t length, uint32_t flag) const {
        for (const auto& range : ranges_)
            if ((range.flags & flag) && address >= range.start && address - range.start <= range.size &&
                length <= range.size - (address - range.start)) return true;
        return false;
    }
    const uint8_t* bytes_;
    size_t size_;
    uint32_t base_;
    std::vector<Range> ranges_;
    std::unordered_map<uint32_t, std::vector<uint32_t>> pointers_;
    std::map<uint32_t, Code> functions_;
};

uint32_t Command(Image& image, const char* name, bool evaluate = false) {
    std::vector<uint32_t> candidates;
    for (uint32_t text : image.Strings(name)) for (uint32_t reference : image.References(text))
    for (uint32_t descriptor : {reference, reference - 4}) {
        if (!image.Data(descriptor, 40)) continue;
        const auto opcode = image.Read<uint32_t>(descriptor + 8);
        const auto params = image.Read<uint16_t>(descriptor + 18);
        if (opcode > 0xFFFF || params > 16) continue;
        const auto execute = image.Read<uint32_t>(descriptor + 24);
        const auto parse = image.Read<uint32_t>(descriptor + 28);
        const auto eval = image.Read<uint32_t>(descriptor + 32);
        if (!image.Exec(execute) || !image.Exec(parse) || (evaluate && !image.Exec(eval))) continue;
        candidates.push_back(evaluate ? eval : execute);
    }
    return Unique(candidates, std::string("native command ") + name);
}

uint32_t GlobalLoad(Image& image, uint32_t function, int reg) {
    std::vector<uint32_t> values;
    for (const auto& i : image.Function(function)) {
        if (i.Op() == 0xA1 && reg == 0 && image.Data(i.Absolute())) values.push_back(i.Absolute());
        if (i.Op() == 0x8B && i.decoded.modrm_reg == reg && image.Data(i.Absolute())) values.push_back(i.Absolute());
    }
    return Unique(values, "global load");
}

uint32_t FieldGetter(Image& image, uint32_t function, bool byte) {
    std::vector<uint32_t> offsets;
    const auto& code = image.Function(function);
    Require(code.size() <= 40, "field getter exceeds bound");
    std::array<bool,8> owner{};
    owner[1] = true;
    std::map<int32_t,bool> saved;
    for (const auto& i : code) {
        Require(!i.Call(), "field getter delegates without a validated receiver");
        if (i.Op() == (byte ? 0x8A : 0x8B) && i.decoded.modrm_reg == 0 && i.Base() >= 0 && owner[i.Base()] &&
            i.Index() == -1 && i.Disp() >= 0 && !i.decoded.p_66) offsets.push_back(static_cast<uint32_t>(i.Disp()));
        if (i.Op() == 0x89 && i.Base() == 5 && i.Index() == -1 && i.Disp() < 0)
            saved[i.Disp()] = owner[i.decoded.modrm_reg];
        if (i.Op() == 0x8B) {
            owner[i.decoded.modrm_reg] = i.decoded.modrm_mod == 3 ? owner[i.decoded.modrm_rm] :
                i.Base() == 5 && i.Disp() < 0 && saved[i.Disp()];
        } else if (i.Op() == 0x89 && i.decoded.modrm_mod == 3) {
            owner[i.decoded.modrm_rm] = owner[i.decoded.modrm_reg];
        } else if (i.Op() == 0x8A || i.Op() == 0x8D || (i.Op() == 0x0F && (i.decoded.opcode2 == 0xB6 || i.decoded.opcode2 == 0xB7))) {
            owner[i.decoded.modrm_reg] = false;
        } else if (i.Op() >= 0xB8 && i.Op() <= 0xBF) {
            owner[i.Op()-0xB8] = false;
        }
    }
    return Unique(offsets, "typed field getter");
}

uint32_t ClassVtable(Image& image, const std::string& name) {
    std::vector<uint32_t> tables;
    for (uint32_t text : image.Strings(".?AV" + name + "@@")) {
        const uint32_t descriptor = text - 8;
        for (uint32_t reference : image.References(descriptor)) {
            if (reference < 12 || !image.Data(reference - 12, 20)) continue;
            const uint32_t locator = reference - 12;
            if (image.Read<uint32_t>(locator) || image.Read<uint32_t>(locator + 4) || image.Read<uint32_t>(locator + 8)) continue;
            const uint32_t hierarchy = image.Read<uint32_t>(locator + 16);
            if (!image.Data(hierarchy, 16)) continue;
            const uint32_t count = image.Read<uint32_t>(hierarchy + 8);
            const uint32_t bases = image.Read<uint32_t>(hierarchy + 12);
            if (!count || count > 128 || !image.Data(bases, count * 4)) continue;
            const uint32_t first = image.Read<uint32_t>(bases);
            if (!image.Data(first, 24) || image.Read<uint32_t>(first) != descriptor) continue;
            for (uint32_t pointer : image.References(locator)) {
                const uint32_t table = pointer + 4;
                if (image.Data(table, 8) && image.Exec(image.Read<uint32_t>(table)) &&
                    image.Exec(image.Read<uint32_t>(table + 4))) tables.push_back(table);
            }
        }
    }
    return Unique(tables, name + " primary RTTI vtable");
}

std::vector<uint32_t> Methods(Image& image, uint32_t table) {
    std::vector<uint32_t> result;
    for (uint32_t slot = 0; slot < 1024 && image.Data(table + 4 * slot); ++slot) {
        const uint32_t entry = image.Read<uint32_t>(table + 4 * slot);
        if (!image.Exec(entry)) break;
        result.push_back(entry);
    }
    Require(!result.empty() && result.size() < 1024, "unterminated vtable");
    return result;
}

std::vector<uint32_t> DerivedVtables(Image& image, const char* baseName) {
    const auto name = Unique(image.Strings(std::string(".?AV") + baseName + "@@"), "RTTI base type");
    const uint32_t descriptor = name - 8;
    std::set<uint32_t> tables;
    for (uint32_t base : image.References(descriptor)) {
        if (!image.Data(base,24) || image.Read<int32_t>(base+8) != 0 || image.Read<int32_t>(base+12) != -1) continue;
        for (uint32_t element : image.References(base)) for (uint32_t index = 0; index < 128; ++index) {
            if (element < 4*index) break;
            const uint32_t array = element - 4*index;
            for (uint32_t arrayReference : image.References(array)) {
                if (arrayReference < 12 || !image.Data(arrayReference - 12,16)) continue;
                const uint32_t hierarchy = arrayReference - 12;
                const uint32_t count = image.Read<uint32_t>(hierarchy + 8);
                if (count <= index || count > 128 || !image.Data(array,count*4)) continue;
                const uint32_t primary = image.Read<uint32_t>(array);
                if (!image.Data(primary,24)) continue;
                for (uint32_t hierarchyReference : image.References(hierarchy)) {
                    if (hierarchyReference < 16 || !image.Data(hierarchyReference - 16,20)) continue;
                    const uint32_t locator = hierarchyReference - 16;
                    if (image.Read<uint32_t>(locator) || image.Read<uint32_t>(locator+4) || image.Read<uint32_t>(locator+8) ||
                        image.Read<uint32_t>(locator+12) != image.Read<uint32_t>(primary)) continue;
                    for (uint32_t reference : image.References(locator))
                        if (image.Data(reference+4,8) && image.Exec(image.Read<uint32_t>(reference+4)) &&
                            image.Exec(image.Read<uint32_t>(reference+8))) tables.insert(reference+4);
                }
            }
        }
    }
    Require(!tables.empty(), std::string(baseName) + " derived primary vtables missing");
    return {tables.begin(),tables.end()};
}

uint32_t NetVtable(Image& image, const char* name) {
    std::vector<uint32_t> tables;
    for (uint32_t text : image.Strings(name)) for (uint32_t rtti : image.References(text)) {
        if (!image.Data(rtti, 8)) continue;
        const uint32_t parent = image.Read<uint32_t>(rtti + 4);
        if (parent && !image.Data(parent, 8)) continue;
        for (uint32_t reference : image.CodeReferences(rtti)) {
            if (!image.Exec(reference - 1, 6)) continue;
            const auto mov = image.Decode(reference - 1);
            if (mov.Op() != 0xB8 || mov.decoded.imm.imm32 != rtti || image.Decode(mov.End()).Op() != 0xC3) continue;
            for (uint32_t slot : image.References(reference - 1)) {
                uint32_t table = slot;
                for (uint32_t before = 0; before < 32 && image.Data(table - 4) && image.Exec(image.Read<uint32_t>(table - 4)); ++before)
                    table -= 4;
                tables.push_back(table);
            }
        }
    }
    return Unique(tables, std::string(name) + " NetImmerse RTTI vtable");
}

uint32_t MenuId(Image& image, const char* name, uint32_t lower, uint32_t upper) {
    std::vector<uint32_t> ids;
    for (uint32_t method : Methods(image, ClassVtable(image, name))) {
        const auto& code = image.Function(method);
        if (code.size() > 12) continue;
        for (const auto& i : code)
            if (i.Op() == 0xB8 && i.decoded.imm.imm32 >= lower && i.decoded.imm.imm32 <= upper)
                ids.push_back(i.decoded.imm.imm32);
    }
    return Unique(ids, std::string(name) + " menu type");
}

std::vector<std::pair<uint32_t,uint32_t>> MenuPredicates(const Code& code) {
    std::vector<std::pair<uint32_t,uint32_t>> found;
    if (code.size() >= 20) return found;
    int32_t receiver = 0;
    for (size_t n = 0; n + 2 < code.size(); ++n) {
        const auto& i = code[n];
        if (i.Call()) return {};
        if (i.Op() == 0x89 && i.Base() == 5 && i.Disp() < 0 && i.decoded.modrm_reg == 1)
            receiver = i.Disp();
        if (n < 2 || !receiver || !code[n-2].Load(0,5,receiver) ||
            code[n-1].Op() != 0x33 || code[n-1].decoded.modrm != 0xC9) continue;
        if (i.Op() == 0x83 && !i.decoded.p_66 && i.decoded.modrm_reg == 7 && i.Base() == 0 &&
            i.Index() == -1 && i.Disp() > 0 && i.Disp() % 4 == 0 &&
            code[n+1].Op() == 0x0F && code[n+1].decoded.opcode2 == 0x95 &&
            code[n+1].decoded.modrm == 0xC1 && code[n+2].Op() == 0x8A && code[n+2].decoded.modrm == 0xC1)
            found.emplace_back(static_cast<uint32_t>(i.Disp()), static_cast<uint32_t>(static_cast<int8_t>(i.decoded.imm.imm8)));
    }
    return found;
}

void DiscoverMenus(Image& image, RuntimeBindings& bindings) {
    auto& p = bindings.profile;
    const uint32_t menu = Command(image, "MenuMode", true);
    std::vector<uint32_t> roots;
    for (uint32_t function : image.Calls(menu)) {
        const auto& code = image.Function(function);
        for (uint32_t called : image.Calls(function)) {
            const auto& getter = image.Function(called);
            if (getter.size() == 5 && getter[0].Op() == 0x55 && getter[2].Op() == 0xA1 &&
                getter[3].Op() == 0x5D && getter[4].Op() == 0xC3 && image.Data(getter[2].Absolute()))
                roots.push_back(called);
        }
        (void)code;
    }
    const uint32_t uiGetter = Unique(roots, "MenuMode interface singleton getter");
    p.interfaceManager = GlobalLoad(image, uiGetter, 0);
    std::vector<std::pair<uint32_t,uint32_t>> modeFields;
    std::vector<uint32_t> visibilityHelpers;
    for (uint32_t function : image.Calls(menu)) for (uint32_t called : image.Calls(function)) {
        const auto& code = image.Function(called);
        const auto predicates = MenuPredicates(code);
        modeFields.insert(modeFields.end(),predicates.begin(),predicates.end());
        if (code.size() < 20) for (const auto& i : code) {
            if (i.Op() == 0x8A && i.decoded.modrm_reg == 0 && i.Base() == 0 && image.Data(static_cast<uint32_t>(i.Disp())))
                visibilityHelpers.push_back(called);
        }
    }
    const auto mode = Unique(modeFields, "interface gameplay mode dword and predicate");
    bindings.layout.uiMode = mode.first;
    bindings.layout.uiGameplayMode = mode.second;
    const auto& visible = image.Function(Unique(visibilityHelpers, "menu visibility indexed query"));
    std::vector<uint32_t> bounds;
    for (const auto& i : visible) {
        if (i.Op() == 0x8A && i.Base() == 0 && image.Data(static_cast<uint32_t>(i.Disp()))) p.menuVisibility = static_cast<uint32_t>(i.Disp());
        if (i.Op() == 0x81 && i.decoded.modrm_reg == 7 && i.Base() == 5 && i.Disp() == 8) bounds.push_back(i.decoded.imm.imm32);
    }
    Require(bounds.size() == 2 && bounds[0] < bounds[1] && bounds[1] - bounds[0] <= 4096 &&
            image.Data(static_cast<uint32_t>(p.menuVisibility) + bounds[0], bounds[1] - bounds[0] + 1), "menu visibility bounds");
    auto& l = bindings.layout;
    l.menuLoading = MenuId(image, "LoadingMenu", bounds[0], bounds[1]);
    l.menuDialog = MenuId(image, "DialogMenu", bounds[0], bounds[1]);
    l.menuPipboy = MenuId(image, "InventoryMenu", bounds[0], bounds[1]);
    l.menuPipboyStats = MenuId(image, "StatsMenu", bounds[0], bounds[1]);
    l.menuPipboyData = MenuId(image, "MapMenu", bounds[0], bounds[1]);
    l.menuPause = MenuId(image, "StartMenu", bounds[0], bounds[1]);
    l.menuCharGen = MenuId(image, "CharGenMenu", bounds[0], bounds[1]);
    l.menuVats = MenuId(image, "VATSMenu", bounds[0], bounds[1]);
    bindings.hudVtable = ClassVtable(image, "HUDMainMenu");
    const uint32_t destructor = image.Read<uint32_t>(bindings.hudVtable);
    std::vector<uint32_t> globals;
    for (uint32_t function : image.Calls(destructor)) {
        const auto& code = image.Function(function);
        bool owner = false;
        std::set<uint32_t> reads;
        for (const auto& i : code) {
            if (i.Op() == 0xC7 && i.Base() >= 0 && i.Disp() == 0 && i.decoded.imm.imm32 == bindings.hudVtable) owner = true;
            if (i.Op() == 0x8B && image.Data(i.Absolute())) reads.insert(i.Absolute());
        }
        if (!owner) continue;
        for (const auto& i : code)
            if (i.Op() == 0xC7 && i.decoded.imm.imm32 == 0 && reads.count(i.Absolute())) globals.push_back(i.Absolute());
    }
    p.hudMainMenu = Unique(globals, "HUDMainMenu singleton cleared by its destructor");
}

void DiscoverCamera(Image& image, RuntimeBindings& bindings) {
    auto& p = bindings.profile;
    auto& l = bindings.layout;
    bindings.playerVtables = {ClassVtable(image, "PlayerCharacter")};
    bindings.sceneVtable = ClassVtable(image, "SceneGraph");
    bindings.cameraVtable = ClassVtable(image, "NiCamera");
    const auto& fov = image.Function(static_cast<uint32_t>(p.setCameraFov));
    int32_t thisSpill = 0;
    for (size_t i = 0; i < std::min<size_t>(fov.size(), 8); ++i)
        if (fov[i].Op() == 0x89 && fov[i].Base() == 5 && fov[i].decoded.modrm_reg == 1) thisSpill = fov[i].Disp();
    Require(thisSpill < 0, "FOV receiver spill");
    std::vector<uint32_t> cameraFields;
    for (size_t i = 1; i < std::min<size_t>(fov.size(), 18); ++i)
        if (fov[i-1].Load(1, 5, thisSpill) && fov[i].Op() == 0x8B && fov[i].Base() == 1 && fov[i].Disp() > 0)
            cameraFields.push_back(static_cast<uint32_t>(fov[i].Disp()));
    l.sceneCamera = Unique(cameraFields, "SceneGraph camera field");

    std::vector<uint32_t> frustums;
    for (uint32_t call : image.Calls(static_cast<uint32_t>(p.setCameraFov))) {
        const auto& setter = image.Function(call);
        if (setter.size() > 100 || setter.empty() || !setter.front().Load(2, 4, 4)) continue;
        std::vector<uint32_t> fields;
        for (size_t i = 1; i + 5 < setter.size(); ++i) {
            bool copies = true;
            uint32_t first = 0;
            for (size_t n = 0; n < 3; ++n) {
                const auto& load = setter[i + 2*n];
                const auto& store = setter[i + 2*n + 1];
                copies &= load.Op() == 0xD9 && load.decoded.modrm_reg == 0 && load.Base() == 2 && load.Disp() == 4 * static_cast<int32_t>(n) &&
                    store.Op() == 0xD9 && store.decoded.modrm_reg == 3 && store.Base() == 1;
                if (n == 0) first = static_cast<uint32_t>(store.Disp());
                copies &= static_cast<uint32_t>(store.Disp()) == first + 4*n;
            }
            if (copies) fields.push_back(first);
        }
        frustums.insert(frustums.end(), fields.begin(), fields.end());
    }
    l.cameraFrustum = Unique(frustums, "camera float frustum setter");

    std::vector<uint32_t> projections;
    std::vector<uint32_t> worldUpdates;
    for (uint32_t method : Methods(image, bindings.cameraVtable)) {
        const auto calls = image.Calls(method);
        if (calls.size() != 2 || image.Function(method).size() > 20) continue;
        const auto& update = image.Function(calls.back());
        std::set<int32_t> inputs, outputs;
        for (const auto& i : update) {
            if (i.Base() != 1 || i.Index() != -1) continue;
            if (i.Op() == 0xD9 && i.decoded.modrm_reg == 0) inputs.insert(i.Disp());
            if (i.Op() == 0xD9 && i.decoded.modrm_reg == 3) outputs.insert(i.Disp());
        }
        if (inputs.size() < 12 || outputs.size() < 12 || !inputs.count(static_cast<int32_t>(l.cameraFrustum + 4))) continue;
        const uint32_t transform = static_cast<uint32_t>(*inputs.begin());
        bool matrix = true;
        for (uint32_t n = 0; n < 9; ++n) matrix &= inputs.count(static_cast<int32_t>(transform + 4*n)) != 0;
        if (!matrix) continue;
        std::set<int32_t> positions;
        for (const auto& i : update)
            if (i.Op() == 0x8B && i.Base() == 1 && i.Index() == -1) positions.insert(i.Disp());
        if (positions.size() != 3) continue;
        const uint32_t position = static_cast<uint32_t>(*positions.begin());
        if (!positions.count(static_cast<int32_t>(position + 4)) || !positions.count(static_cast<int32_t>(position + 8))) continue;
        Require(position == transform + 36 && position + 12 <= l.cameraFrustum, "camera transform overlap or width mismatch");
        projections.push_back(calls.back());
        worldUpdates.push_back(calls.front());
        l.cameraTransform = transform;
        l.cameraPosition = position;
    }
    p.updateCameraProjection = Unique(projections, "NiCamera virtual update to projection rebuild");
    const auto worldUpdate = Unique(image.Calls(Unique(worldUpdates, "NiCamera base world update")), "NiAVObject world-transform update");
    const auto& worldCode = image.Function(worldUpdate);
    std::vector<uint32_t> parentMembers;
    bool transformCopy = false;
    for (size_t n = 1; n < worldCode.size(); ++n) {
        if (worldCode[n-1].MovReg(7,1) && worldCode[n].Op() == 0x8B && worldCode[n].Base() == 7 && worldCode[n].decoded.modrm_reg == 0)
            parentMembers.push_back(static_cast<uint32_t>(worldCode[n].Disp()));
        transformCopy |= worldCode[n].Op() == 0xB9 && worldCode[n].decoded.imm.imm32 == 13;
    }
    Require(transformCopy,"NiTransform containing width");
    l.nodeParent = Unique(parentMembers,"world-transform parent owner");
    const auto compose = Unique(image.Calls(worldUpdate),"parent/local transform composition");
    std::vector<uint32_t> scales;
    const auto& composeCode = image.Function(compose);
    for (size_t n = 0; n + 2 < composeCode.size(); ++n)
        if (composeCode[n].Op() == 0xD9 && composeCode[n].decoded.modrm_reg == 0 && composeCode[n].Base() == 0 &&
            composeCode[n+1].Load(1,5,12) && composeCode[n+2].Op() == 0xD8 && composeCode[n+2].decoded.modrm_reg == 1 &&
            composeCode[n+2].Base() == 1 && composeCode[n+2].Disp() == composeCode[n].Disp())
            scales.push_back(static_cast<uint32_t>(composeCode[n].Disp()));
    const uint32_t scale = Unique(scales,"float transform scale composition");
    Require(scale == 48,"NiTransform float rotation/translation/scale widths");
    l.nodeWorldScale = l.cameraTransform + scale;

    std::vector<uint32_t> planeFunctions;
    const auto cullingMethods = Methods(image, NetVtable(image, "NiCullingProcess"));
    for (uint32_t method : cullingMethods) {
        const auto& body = image.Function(method);
        for (size_t i = 3; i < body.size(); ++i) {
            if (!body[i].Call() || body[i-3].Op() != 0x8D || body[i-3].Disp() != static_cast<int32_t>(l.cameraFrustum)) continue;
            const auto& copy = image.Function(body[i].Target());
            bool sevenWords = false, transform = false;
            std::vector<uint32_t> targets;
            for (const auto& op : copy) {
                sevenWords |= op.Op() == 0xB9 && op.decoded.imm.imm32 == 7;
                transform |= op.Op() == 0x83 && op.decoded.modrm_mod == 3 && op.decoded.modrm_reg == 0 &&
                    op.decoded.imm.imm8 == l.cameraTransform;
                if (op.Call()) targets.push_back(op.Target());
            }
            if (sevenWords && transform && targets.size() == 1) planeFunctions.push_back(targets.front());
        }
    }
    p.calcCullingPlanes = Unique(planeFunctions, "NiCullingProcess camera/frustum plane builder");
}

int32_t ReceiverSpill(const Code& code) {
    for (size_t n = 0; n < std::min<size_t>(code.size(), 20); ++n) {
        const auto& i = code[n];
        if (i.Call()) break;
        if (i.Op() == 0x89 && i.Base() == 5 && i.Index() == -1 && i.Disp() < 0 && i.decoded.modrm_reg == 1) {
            const int32_t slot = i.Disp();
            for (size_t k = n + 1; k < code.size(); ++k)
                if (code[k].Base() == 5 && code[k].Disp() == slot &&
                    (code[k].Op() == 0x89 || code[k].Op() == 0xC7 || code[k].Op() == 0x88)) return 0;
            return slot;
        }
    }
    return 0;
}

bool GlobalReceiver(Image& image, uint32_t method, uint32_t global, unsigned depth) {
    if (!depth) return false;
    for (uint32_t site : image.CallSites(method)) for (uint32_t entry : image.Enclosing(site, 0x5000)) {
        const auto& code = image.Function(entry);
        for (size_t n = 1; n < code.size(); ++n) if (code[n].address == site) {
            const auto& receiver = code[n-1];
            if (receiver.Op() == 0x8B && receiver.decoded.modrm_reg == 1 && receiver.Absolute() == global) return true;
            const int32_t spill = ReceiverSpill(code);
            if (spill && receiver.Load(1, 5, spill) && GlobalReceiver(image, entry, global, depth - 1)) return true;
        }
    }
    return false;
}

void DiscoverRenderPasses(Image& image, RuntimeBindings& bindings) {
    auto& p = bindings.profile;
    auto& l = bindings.layout;
    std::vector<uint32_t> sky;
    bindings.skyVtable = NetVtable(image, "SkyShader");
    bindings.geometryVtables = DerivedVtables(image, "NiGeometry");
    for (uint32_t method : Methods(image, bindings.skyVtable)) {
        const auto& code = image.Function(method);
        for (size_t i = 1; i < std::min<size_t>(code.size(), 25); ++i) {
            if (code[i-1].Op() != 0x8B || code[i-1].decoded.modrm_reg != 1 || !image.Data(code[i-1].Absolute()) ||
                !code[i].Load(3, 1, 0)) continue;
            const bool geometry = std::any_of(code.begin(), code.begin() + std::min<size_t>(code.size(), 30), [&l](const Instruction& op) {
                return op.Op() == 0x8D && op.Base() == 3 && op.Disp() == static_cast<int32_t>(l.cameraTransform);
            });
            if (geometry) {
                sky.push_back(method);
                p.currentRenderPass = code[i-1].Absolute();
            }
        }
    }
    p.setupSkyGeometry = Unique(sky, "SkyShader geometry transform upload");
    const auto& skyCode = image.Function(static_cast<uint32_t>(p.setupSkyGeometry));
    std::vector<std::pair<uint32_t, uint32_t>> accumulators;
    for (size_t i = 0; i + 1 < skyCode.size(); ++i) if (skyCode[i].Call()) {
        const auto& getter = image.Function(skyCode[i].Target());
        if (getter.size() != 2 || getter[0].Op() != 0xA1 || getter[1].Op() != 0xC3 || !image.Data(getter[0].Absolute())) continue;
        const auto& next = skyCode[i+1];
        if (next.Op() == 0x8B && next.decoded.modrm_reg == 0 && next.Base() == 0 && next.Disp() > 0)
            accumulators.emplace_back(getter[0].Absolute(), static_cast<uint32_t>(next.Disp()));
    }
    const auto accumulator = Unique(accumulators, "sky accumulator camera getter");
    p.currentAccumulator = accumulator.first;
    l.accumulatorCamera = accumulator.second;

    std::vector<uint32_t> cameraSetters;
    for (uint32_t method : Methods(image, NetVtable(image, "NiCullingProcess"))) {
        const auto& code = image.Function(method);
        for (size_t i = 2; i < code.size(); ++i)
            if (code[i].Call() && code[i-1].Op() >= 0x50 && code[i-1].Op() <= 0x57 &&
                code[i-2].Op() == 0x8B && code[i-2].decoded.modrm_reg == 1 && image.Data(code[i-2].Absolute()))
                cameraSetters.push_back(code[i].Target());
    }
    const auto setRendererCamera = Unique(cameraSetters, "culling renderer camera setter");
    std::vector<uint32_t> renderers;
    const uint32_t accumulatorTable = NetVtable(image, "BSShaderAccumulator");
    bindings.accumulatorVtable = accumulatorTable;
    const auto accumulatorMethods = Methods(image, accumulatorTable);
    for (uint32_t site : image.CallSites(setRendererCamera)) for (uint32_t entry : image.Enclosing(site, 0x100)) {
        const auto& code = image.Function(entry);
        if (code.size() > 55) continue;
        std::vector<uint32_t> slots;
        bool cameraArg = false, accumulatorArg = false, tail = false;
        for (const auto& i : code) {
            cameraArg |= i.Load(7, 4, 12);
            accumulatorArg |= i.Load(6, 4, 12);
            if (i.Op() == 0x8B && i.Base() == 0 && i.Index() == -1 && i.Disp() > 0 && i.Disp() % 4 == 0)
                slots.push_back(static_cast<uint32_t>(i.Disp()) / 4);
            tail |= i.Op() == 0xFF && i.decoded.modrm_mod == 3 && i.decoded.modrm_reg == 4;
        }
        if (!cameraArg || !accumulatorArg || !tail || slots.size() != 3 ||
            *std::max_element(slots.begin(), slots.end()) >= accumulatorMethods.size()) continue;
        const auto& begin = image.Function(accumulatorMethods[slots.front()]);
        bool cameraStore = false;
        for (const auto& i : begin)
            cameraStore |= i.Op() == 0x89 && i.Base() == 1 && i.Disp() == static_cast<int32_t>(l.accumulatorCamera);
        if (cameraStore) renderers.push_back(entry);
    }
    p.renderAccumulator = Unique(renderers, "render accumulator camera dispatch");
    std::vector<uint32_t> cameraMembers;
    for (uint32_t site : image.CallSites(static_cast<uint32_t>(p.renderAccumulator))) {
        for (uint32_t entry : image.Enclosing(site, 0x1000)) {
            const auto& code = image.Function(entry);
            const int32_t spill = ReceiverSpill(code);
            if (!spill) continue;
            for (size_t i = 4; i < code.size(); ++i) {
                if (code[i].address != site || code[i-1].Op() != 0x50 || !code[i-2].Call() ||
                    code[i-3].Op() != 0x81 || code[i-3].decoded.modrm != 0xC1 ||
                    !code[i-4].Load(1, 5, spill)) continue;
                if (FieldGetter(image, code[i-2].Target(), false) != 0 || !GlobalReceiver(image, entry, static_cast<uint32_t>(p.gameMain), 4)) continue;
                cameraMembers.push_back(code[i-3].decoded.imm.imm32);
            }
        }
    }
    l.mainWeaponCamera = Unique(cameraMembers, "main first-person render camera argument");
}

void DiscoverConsole(Image& image, RuntimeBindings& bindings) {
    const auto commandCalls = image.Calls(Command(image, "GetPlayerName"));
    std::vector<std::vector<uint32_t>> openers;
    for (uint32_t target : commandCalls) {
        const auto calls = image.Calls(target);
        if (calls.size() >= 7 && calls[0] == calls[1] && calls[3] == calls[4] && calls[3] == calls[6] &&
            GlobalLoad(image, calls[0], 0) == bindings.profile.interfaceManager) openers.push_back(calls);
    }
    const auto opener = Unique(openers, "console command UI/singleton relationship");
    std::vector<uint32_t> queries;
    for (uint32_t site : image.CallSites(opener[5])) for (uint32_t entry : image.Enclosing(site, 0x100)) {
        const auto calls = image.Calls(entry);
        if (calls.size() != 6 || !std::equal(calls.begin(), calls.end(), opener.begin())) continue;
        const auto& code = image.Function(entry);
        if (code.size() > 32) continue;
        queries.push_back(entry);
    }
    const uint32_t query = Unique(queries, "console visibility query");
    std::vector<uint32_t> caches;
    for (uint32_t site : image.CallSites(query)) for (uint32_t entry : image.Enclosing(site, 0x5000)) {
        const auto& code = image.Function(entry);
        for (size_t n = 0; n + 1 < code.size(); ++n)
            if (code[n].address == site && code[n+1].Op() == 0xA2 && image.Data(code[n+1].Absolute(), 1))
                caches.push_back(code[n+1].Absolute());
    }
    bindings.profile.consoleOpen = Unique(caches, "console visibility byte cache");
}

bool FloatTileSetter(Image& image, uint32_t target) {
    const auto& code = image.Function(target);
    if (!ReceiverSpill(code) || code.size() > 55) return false;
    bool trait = false, value = false, propagate = false, cleanup = false;
    for (const auto& i : code) {
        trait |= i.Op() == 0x8B && i.Base() == 5 && i.Disp() == 8;
        value |= i.Op() == 0xD9 && i.decoded.modrm_reg == 0 && i.Base() == 5 && i.Disp() == 12;
        propagate |= i.Op() == 0x0F && i.decoded.opcode2 == 0xB6 && i.Base() == 5 && i.Disp() == 16;
        cleanup |= i.Op() == 0xC2 && i.decoded.imm.imm16 == 12;
    }
    return trait && value && propagate && cleanup;
}

void DiscoverReticle(Image& image, RuntimeBindings& bindings) {
    auto& p = bindings.profile;
    auto& l = bindings.layout;
    bindings.tileVtables = DerivedVtables(image, "Tile");
    std::vector<std::pair<uint32_t, uint32_t>> reticles;
    for (uint32_t text : image.Strings("ReticleCenter")) for (uint32_t reference : image.CodeReferences(text)) {
        if (image.Read<uint8_t>(reference - 1) != 0x68) continue;
        for (uint32_t entry : image.Enclosing(reference - 1, 0x8000)) {
            const auto& code = image.Function(entry);
            for (size_t n = 0; n + 9 < code.size(); ++n) if (code[n].address == reference - 1) {
                if (code[n+1].Op() != 0x68 || !code[n+2].Call() || code[n+4].Op() != 0x50 || !code[n+5].Call() ||
                    code[n+7].Op() != 0x8B || code[n+7].Absolute() != p.hudMainMenu ||
                    code[n+8].Op() != 0x89 || code[n+8].Base() != code[n+7].decoded.modrm_reg ||
                    code[n+8].decoded.modrm_reg != 0 || code[n+8].Disp() <= 0) continue;
                reticles.emplace_back(entry, static_cast<uint32_t>(code[n+8].Disp()));
            }
        }
    }
    const auto reticle = Unique(reticles, "named HUD reticle ownership");
    l.hudCrosshair = reticle.second;
    const auto& code = image.Function(reticle.first);
    std::vector<uint32_t> setters;
    for (size_t n = 2; n < code.size(); ++n) if (code[n].Call() && code[n-1].Op() == 0x8B &&
        code[n-1].decoded.modrm_reg == 1 && code[n-1].Disp() == static_cast<int32_t>(l.hudCrosshair) &&
        (code[n-2].Op() == 0xA1 || code[n-2].Op() == 0x8B) && code[n-2].Absolute() == p.hudMainMenu &&
        code[n-1].Base() == (code[n-2].Op() == 0xA1 ? 0 : code[n-2].decoded.modrm_reg)) {
        if (FloatTileSetter(image, code[n].Target())) setters.push_back(code[n].Target());
    }
    p.tileSetFloatValue = Unique(setters, "reticle float trait setter ABI");
    std::vector<uint32_t> visible;
    for (uint32_t text : image.Strings("visible")) for (uint32_t reference : image.CodeReferences(text)) {
        if (image.Read<uint8_t>(reference - 1) != 0x68) continue;
        for (uint32_t entry : image.Enclosing(reference - 1, 0x4000)) {
            const auto& registration = image.Function(entry);
            for (size_t n = 1; n + 1 < registration.size(); ++n)
                if (registration[n].address == reference - 1 && registration[n-1].Op() == 0x68 && registration[n+1].Call())
                    visible.push_back(registration[n-1].decoded.imm.imm32);
        }
    }
    l.tileVisible = Unique(visible, "named visible trait registration");
}

uint32_t VectorCopyMember(Image& image, uint32_t target) {
    const auto& code = image.Function(target);
    if (code.size() > 24 || !ReceiverSpill(code)) return 0;
    std::set<int32_t> source, destination;
    std::vector<uint32_t> members;
    bool argument = false, cleanup = false;
    for (const auto& i : code) {
        argument |= i.Load(0, 5, 8);
        cleanup |= i.Op() == 0xC2 && i.decoded.imm.imm16 == 4;
        if (i.Op() == 0x83 && i.decoded.modrm == 0xC1) members.push_back(i.decoded.imm.imm8);
        if (i.Op() == 0x81 && i.decoded.modrm == 0xC1) members.push_back(i.decoded.imm.imm32);
        if (i.Op() == 0x8B && i.Base() == 0 && i.Index() == -1) source.insert(i.Disp());
        if (i.Op() == 0x89 && i.Base() == 1 && i.Index() == -1) destination.insert(i.Disp());
    }
    if (!argument || !cleanup || members.size() != 1 || source != std::set<int32_t>{0,4,8} || destination != source) return 0;
    return members.front();
}

void DiscoverRig(Image& image, RuntimeBindings& bindings) {
    const auto playerTable = ClassVtable(image, "PlayerCharacter");
    bindings.playerVtables = {playerTable};
    bindings.nodeVtables = DerivedVtables(image, "NiNode");
    const auto actorMethods = Methods(image, ClassVtable(image, "Actor"));
    std::vector<std::array<uint32_t, 3>> updates;
    for (uint32_t reference : image.CodeReferences(static_cast<uint32_t>(bindings.profile.playerBase))) {
        if (image.Read<uint16_t>(reference - 2) != 0x0D8B || image.Read<uint8_t>(reference + 4) != 0xE8 ||
            image.Read<uint8_t>(reference - 7) != 0xE8) continue;
        const int64_t candidate = static_cast<int64_t>(reference) - 2 + image.Read<int32_t>(reference - 6);
        if (candidate < 0 || candidate > UINT32_MAX || !image.Exec(static_cast<uint32_t>(candidate))) continue;
        const auto previous = image.Decode(reference - 7);
        if (!image.Exec(previous.Target())) continue;
        const uint32_t member = VectorCopyMember(image, previous.Target());
        if (!member) continue;
        for (uint32_t entry : image.Enclosing(reference + 4, 0x4000)) {
            if (std::find(actorMethods.begin(), actorMethods.end(), entry) == actorMethods.end()) continue;
            const auto& code = image.Function(entry);
            for (size_t n = 6; n < code.size(); ++n) if (code[n].address == reference + 4 &&
                code[n-2].address == previous.address && code[n-3].Op() == 0x8B && code[n-3].Base() == 5 &&
                code[n-4].Op() == 0x50 && code[n-5].Call())
                updates.push_back({code[n].Target(), member,code[n].End()});
        }
    }
    const auto update = Unique(updates, "actor virtual update first-person translation boundary");
    bindings.profile.updateFirstPerson = update[0];
    bindings.layout.nodeLocalPosition = update[1];
    bindings.rigTranslationCaller = update[2];
    const auto& code = image.Function(update[0]);
    const int32_t spill = ReceiverSpill(code);
    Require(spill != 0, "first-person update receiver ABI");
    std::vector<uint32_t> roots;
    for (size_t n = 2; n + 2 < code.size(); ++n) if (code[n].Call() &&
        code[n-1].Op() == 0x81 && code[n-1].decoded.modrm == 0xC1 && code[n-2].Load(1, 5, spill) &&
        code[n+1].MovReg(1, 0) && code[n+2].Call() && FieldGetter(image, code[n].Target(), false) == 0)
        roots.push_back(code[n-1].decoded.imm.imm32);
    bindings.layout.playerFirstPersonRoot = Unique(roots, "first-person scene node update receiver");
}

uint32_t Setting(Image& image, const char* name) {
    std::vector<uint32_t> settings;
    for (uint32_t text : image.Strings(name)) for (uint32_t reference : image.References(text))
        if (reference >= 8 && image.Data(reference - 8, 12) && image.Data(image.Read<uint32_t>(reference - 8)))
            settings.push_back(reference - 8);
    return Unique(settings, std::string("named setting ") + name);
}

std::pair<uint32_t, uint32_t> ProcessQuery(Image& image, uint32_t target) {
    const auto& code = image.Function(target);
    if (code.size() > 24 || !ReceiverSpill(code) || !image.Calls(target).empty()) return {};
    std::vector<uint32_t> members, slots;
    for (size_t n = 0; n + 1 < code.size(); ++n) {
        const auto& i = code[n];
        if (i.Op() == 0x8B && i.Base() == 0 && i.decoded.modrm_reg == 1 && i.Disp() > 0)
            members.push_back(static_cast<uint32_t>(i.Disp()));
        if (i.Op() == 0x8B && i.Base() == 2 && i.decoded.modrm_reg == 0 && i.Disp() > 0 && i.Disp() % 4 == 0 &&
            code[n+1].Op() == 0xFF && code[n+1].decoded.modrm == 0xD0) slots.push_back(static_cast<uint32_t>(i.Disp()) / 4);
    }
    if (members.size() != 1 || slots.size() != 1) return {};
    return {members.front(), slots.front()};
}

void DiscoverGameplay(Image& image, RuntimeBindings& bindings) {
    auto& l = bindings.layout;
    const auto cellCalls = image.Calls(Command(image, "GetInSameCell", true));
    std::vector<uint32_t> cellGetters;
    for (uint32_t target : cellCalls)
        if (std::count(cellCalls.begin(), cellCalls.end(), target) == 2 && image.Function(target).size() < 15)
            cellGetters.push_back(target);
    l.playerCell = FieldGetter(image, Unique(cellGetters, "same-cell reference getter"), false);
    const uint32_t crosshairSetting = Setting(image, "bDisableDynamicCrosshair:GamePlay");
    const uint32_t ironSetting = Setting(image, "bTrueIronSights:GamePlay");
    bindings.processVtable = ClassVtable(image, "HighProcess");
    const auto processMethods = Methods(image, bindings.processVtable);
    std::vector<std::pair<uint32_t, uint32_t>> ads;
    for (uint32_t reference : image.CodeReferences(crosshairSetting)) {
        if (image.Read<uint8_t>(reference - 1) != 0xB9) continue;
        for (uint32_t entry : image.Enclosing(reference - 1, 0x1000)) {
            const auto& code = image.Function(entry);
            if (!std::any_of(code.begin(), code.end(), [ironSetting](const Instruction& i) {
                return i.Op() == 0xB9 && i.decoded.imm.imm32 == ironSetting;
            })) continue;
            for (size_t n = 1; n < code.size(); ++n) if (code[n].Call() && code[n-1].Op() == 0x8B &&
                code[n-1].decoded.modrm_reg == 1 && code[n-1].Absolute() == bindings.profile.playerBase) {
                const auto query = ProcessQuery(image, code[n].Target());
                if (!query.first || query.second >= processMethods.size()) continue;
                ads.emplace_back(query.first, FieldGetter(image, processMethods[query.second], true));
            }
        }
    }
    const auto aiming = Unique(ads, "named iron-sights HUD process query");
    l.playerProcess = aiming.first;
    l.processAds = aiming.second;

    std::vector<uint32_t> layerTables;
    for (uint32_t text : image.Strings("UNIDENTIFIED")) for (uint32_t table : image.References(text)) {
        bool indexed = false;
        for (uint32_t reference : image.CodeReferences(table)) {
            if (reference < 3 || image.Read<uint8_t>(reference - 3) != 0x8B) continue;
            const auto i = image.Decode(reference - 3);
            indexed |= i.Base() == -1 && i.Index() == 0 && i.decoded.sib_scale == 2;
        }
        if (indexed) layerTables.push_back(table);
    }
    const uint32_t layerTable = Unique(layerTables, "collision filter named layer table");
    std::vector<uint32_t> cameraLayers, aimLayers;
    const auto cameraNames = image.Strings("CAMERAPICK"), aimNames = image.Strings("PROJECTILE");
    for (uint32_t n = 0; n < 128 && image.Data(layerTable + n*4); ++n) {
        const uint32_t text = image.Read<uint32_t>(layerTable + n*4);
        if (!text) break;
        Require(image.Data(text), "invalid collision layer name pointer");
        if (std::find(cameraNames.begin(), cameraNames.end(), text) != cameraNames.end()) cameraLayers.push_back(n);
        if (std::find(aimNames.begin(), aimNames.end(), text) != aimNames.end()) aimLayers.push_back(n);
    }
    l.layerCamera = Unique(cameraLayers, "camera collision layer");
    l.layerAim = Unique(aimLayers, "aim collision layer");
}

uint32_t AddedReceiverMember(Image& image, uint32_t target, int reg) {
    const auto& code = image.Function(target);
    const int32_t spill = ReceiverSpill(code);
    Require(spill != 0, "member-address receiver ABI");
    std::vector<uint32_t> offsets;
    for (size_t n = 1; n < code.size(); ++n) {
        const auto& i = code[n];
        if ((i.Op() == 0x81 || i.Op() == 0x83) && i.decoded.modrm_mod == 3 &&
            i.decoded.modrm_reg == 0 && i.decoded.modrm_rm == reg && code[n-1].Load(reg, 5, spill))
            offsets.push_back(i.Op() == 0x81 ? i.decoded.imm.imm32 : i.decoded.imm.imm8);
    }
    return Unique(offsets, "owned member address");
}

void DiscoverCollisionFilter(Image& image, RuntimeBindings& bindings) {
    auto& l = bindings.layout;
    const auto methods = Methods(image, bindings.processVtable);
    std::vector<std::array<uint32_t, 3>> candidates;
    for (uint32_t target : image.Calls(Command(image, "GetLOS", true))) {
        const auto calls = image.Calls(target);
        if (calls.size() != 3 || image.Function(target).size() > 40) continue;
        const auto query = ProcessQuery(image, calls[0]);
        if (query.first == l.playerProcess && query.second < methods.size())
            candidates.push_back({target, methods[query.second], calls[1]});
    }
    const auto filter = Unique(candidates, "line-of-sight actor collision filter");
    l.processController = AddedReceiverMember(image, filter[1], 1);
    Require(FieldGetter(image, Unique(image.Calls(filter[1]), "process controller smart pointer"), false) == 0,
            "process controller pointer representation");
    l.controllerPhantom = AddedReceiverMember(image, filter[2], 1);
    const auto controllerCalls = image.Calls(filter[2]);
    Require(controllerCalls.size() == 4 && controllerCalls[0] == controllerCalls[1] &&
            FieldGetter(image, controllerCalls[0], false) == 0, "controller phantom smart-pointer contract");
    const auto phantomCalls = image.Calls(controllerCalls[2]);
    Require(phantomCalls.size() == 3, "phantom collision-filter query contract");
    const auto objectCalls = image.Calls(phantomCalls[0]);
    Require(objectCalls.size() == 4, "phantom collision-object query contract");
    l.phantomObject = FieldGetter(image, Unique(image.Calls(objectCalls[0]), "phantom native object getter"), false);
    const uint32_t collidable = AddedReceiverMember(image, objectCalls[2], 0);
    const uint32_t broadPhase = AddedReceiverMember(image, phantomCalls[1], 1);
    const uint32_t filterInfo = FieldGetter(image, Unique(image.Calls(phantomCalls[1]), "broad-phase filter-info getter"), false);
    Require(collidable < 0x1000 && broadPhase < 0x1000 && filterInfo < 0x1000, "collision-filter containing bounds");
    l.objectFilter = collidable + broadPhase + filterInfo;
    std::vector<std::pair<uint32_t,uint32_t>> groupBits;
    for (uint32_t target : image.Calls(Command(image, "GetLOS", true))) {
        const auto& code = image.Function(target);
        if (code.size() > 16) continue;
        for (size_t n = 0; n + 1 < code.size(); ++n)
            if (code[n].Op() == 0xC1 && code[n].decoded.modrm == 0xE8 && code[n+1].Op() == 0x25)
                groupBits.emplace_back(code[n].decoded.imm.imm8,code[n+1].decoded.imm.imm32);
    }
    const auto bits = Unique(groupBits, "collision filter group extraction");
    Require(bits.first < 32 && (static_cast<uint64_t>(bits.second) << bits.first) <= UINT32_MAX,
            "collision filter group bit width");
    l.collisionGroupMask = bits.second << bits.first;
}

struct MemberCall { uint32_t target, offset; };
struct MemberStore { uint32_t offset, width, value; };
struct Constructor {
    std::vector<MemberCall> calls;
    std::vector<MemberStore> stores;
};

Constructor ReadConstructor(Image& image, uint32_t target) {
    const auto& code = image.Function(target);
    Require(code.size() <= 60, "constructor exceeds contract bound");
    const int32_t spill = ReceiverSpill(code);
    Require(spill != 0, "constructor receiver not preserved");
    std::array<int64_t, 8> receivers;
    receivers.fill(-1);
    receivers[1] = 0;
    Constructor result;
    bool floatOne = false;
    for (const auto& i : code) {
        Require(i.Op() == 0x8B || i.Op() == 0x89 || i.Op() == 0x83 || i.Op() == 0x81 || i.Op() == 0xC7 ||
                i.Op() == 0xC6 || i.Op() == 0xD9 || i.Op() == 0xE8 || i.Op() == 0xC2 || i.Op() == 0xC3 ||
                (i.Op() >= 0x50 && i.Op() <= 0x5F) || i.Op() == 0x6A || i.Op() == 0x68 ||
                (i.Op() >= 0xB8 && i.Op() <= 0xBF), "unsupported constructor instruction");
        Require(!(i.Op() >= 0x70 && i.Op() <= 0x7F) && i.Op() != 0xE9 && i.Op() != 0xEB &&
                !(i.Op() == 0x0F && i.decoded.opcode2 >= 0x80 && i.decoded.opcode2 <= 0x8F), "conditional constructor contract");
        if (i.Call()) {
            result.calls.push_back({i.Target(), receivers[1] < 0 ? UINT32_MAX : static_cast<uint32_t>(receivers[1])});
            receivers[0] = receivers[1] = receivers[2] = -1;
        } else if (i.Op() == 0x8B) {
            const int destination = i.decoded.modrm_reg;
            if (i.decoded.modrm_mod == 3) receivers[destination] = receivers[i.decoded.modrm_rm];
            else receivers[destination] = i.Base() == 5 && i.Disp() == spill ? 0 : -1;
        } else if (i.Op() == 0x89 && i.decoded.modrm_mod == 3) {
            receivers[i.decoded.modrm_rm] = receivers[i.decoded.modrm_reg];
        } else if (i.Op() >= 0xB8 && i.Op() <= 0xBF) {
            receivers[i.Op()-0xB8] = -1;
        } else if ((i.Op() == 0x83 || i.Op() == 0x81) && i.decoded.modrm_mod == 3) {
            const int reg = i.decoded.modrm_rm;
            if (i.decoded.modrm_reg == 0 && receivers[reg] >= 0)
                receivers[reg] += i.Op() == 0x83 ? static_cast<int8_t>(i.decoded.imm.imm8) : static_cast<int32_t>(i.decoded.imm.imm32);
            else receivers[reg] = -1;
        } else if (i.Op() == 0xD9 && i.decoded.modrm == 0xE8) {
            floatOne = true;
        } else if (i.Base() >= 0 && receivers[i.Base()] >= 0 && i.Index() == -1) {
            const int64_t offset = receivers[i.Base()] + i.Disp();
            Require(offset >= 0 && offset < 4096, "constructor field outside containing bound");
            if (i.Op() == 0xC7 || i.Op() == 0xC6) {
                result.stores.push_back({static_cast<uint32_t>(offset), i.Op() == 0xC7 ? 4u : 1u,
                    i.Op() == 0xC7 ? i.decoded.imm.imm32 : i.decoded.imm.imm8});
            } else if (i.Op() == 0xD9 && i.decoded.modrm_reg == 3) {
                Require(floatOne, "unknown constructor floating default");
                result.stores.push_back({static_cast<uint32_t>(offset), 4, 0x3F800000});
                floatOne = false;
            } else if (i.Op() == 0x89 || i.Op() == 0x88) {
                Require(false, "unknown constructor member value");
            }
        }
    }
    return result;
}

uint32_t StoredDefault(const Constructor& constructor, uint32_t width, uint32_t value) {
    std::vector<uint32_t> fields;
    for (const auto& s : constructor.stores) if (s.width == width && s.value == value) fields.push_back(s.offset);
    return Unique(fields, "typed query default");
}

void DiscoverRayQuery(Image& image, RuntimeBindings& bindings) {
    auto& l = bindings.layout;
    const auto& los = image.Function(Command(image, "GetLOS", true));
    std::vector<int32_t> querySlots;
    for (size_t n = 3; n < los.size(); ++n) if (los[n].Call() && los[n-1].Absolute() == bindings.profile.tesWorld &&
        los[n-2].Op() >= 0x50 && los[n-2].Op() <= 0x57 && los[n-3].Op() == 0x8D && los[n-3].Base() == 5 &&
        los[n-3].decoded.modrm_reg == los[n-2].Op() - 0x50) querySlots.push_back(los[n-3].Disp());
    const int32_t querySlot = Unique(querySlots, "line-of-sight stack query argument");
    std::vector<uint32_t> queryMethods;
    for (size_t n = 1; n < los.size(); ++n) if (los[n].Call() && los[n-1].Op() == 0x8D &&
        los[n-1].Base() == 5 && los[n-1].decoded.modrm_reg == 1 && los[n-1].Disp() == querySlot)
        queryMethods.push_back(los[n].Target());
    Require(querySlot < 0 && querySlot % 16 == 0, "ray query stack alignment");
    std::vector<uint32_t> constructors;
    for (uint32_t target : queryMethods) {
        const auto& code = image.Function(target);
        if (code.size() > 40 || image.Calls(target).size() != 4) continue;
        if (std::any_of(code.begin(), code.end(), [](const Instruction& i) { return i.Op() == 0xC6 && i.decoded.imm.imm8 == 0; }))
            constructors.push_back(target);
    }
    const auto ctor = ReadConstructor(image, Unique(constructors, "stack ray-query constructor"));
    Require(ctor.calls.size() == 4 && ctor.calls[0].offset == 0 && ctor.stores.size() == 4, "ray-query construction shape");
    const uint32_t lastByte = StoredDefault(ctor, 1, 0);
    l.raySize = (lastByte + 16) & ~15u;
    Require(l.raySize <= 512 && l.raySize <= static_cast<uint32_t>(-querySlot), "ray-query output buffer extent");
    const auto& defaultGetter = image.Function(ctor.calls[2].target);
    std::vector<uint32_t> defaultVectors;
    for (const auto& i : defaultGetter) if (i.Op() == 0xB8 && image.Data(i.decoded.imm.imm32, 16))
        defaultVectors.push_back(i.decoded.imm.imm32);
    Require(defaultGetter.size() <= 5, "query default-vector getter ABI");
    const uint32_t defaultVector = Unique(defaultVectors, "query default vector");
    for (uint32_t n = 0; n < 4; ++n) Require(image.Read<uint32_t>(defaultVector + n*4) == 0, "nonzero query vector default");
    const auto input = ReadConstructor(image, ctor.calls[0].target);
    Require(input.calls.size() == 3 && input.calls[0].target == input.calls[1].target && input.stores.size() == 1,
            "ray input float-vector constructor contract");
    l.rayStart = input.calls[0].offset;
    l.rayEnd = input.calls[1].offset;
    l.rayFilter = StoredDefault(input, 4, 0);
    const auto output = ReadConstructor(image, ctor.calls[1].target);
    Require(output.calls.size() == 1 && output.calls[0].offset == 0, "world ray output base contract");
    const auto shape = ReadConstructor(image, output.calls[0].target);
    Require(shape.calls.size() == 2 && shape.calls[0].offset == 0 && shape.calls[1].offset == 0, "shape ray output construction");
    const auto hit = ReadConstructor(image, shape.calls[0].target);
    Require(hit.calls.size() == 2 && hit.calls[0].target == input.calls[0].target && hit.calls[1].offset == 0,
            "ray hit normal and fraction construction");
    const auto fraction = ReadConstructor(image, hit.calls[1].target);
    Require(fraction.calls.empty() && fraction.stores.size() == 2, "ray fraction/key defaults");
    l.rayFraction = ctor.calls[1].offset + StoredDefault(fraction, 4, 0x3F800000);
    l.rayShapeKey = ctor.calls[1].offset + StoredDefault(fraction, 4, UINT32_MAX);
    const auto root = ReadConstructor(image, shape.calls[1].target);
    Require(root.calls.empty() && root.stores.size() == 2, "ray root-shape defaults");
    l.rayRootShapeKey = ctor.calls[1].offset + StoredDefault(root, 4, UINT32_MAX);
    for (const auto& range : std::array<std::pair<uint32_t,uint32_t>,6>{{
        {l.rayStart,16},{l.rayEnd,16},{l.rayFilter,4},{l.rayFraction,4},{l.rayShapeKey,4},{l.rayRootShapeKey,4}}}) {
        Require(range.first <= l.raySize && range.second <= l.raySize - range.first && range.first % 4 == 0,
                "ray field alignment or containing bounds");
    }
    Require(l.rayStart % 16 == 0 && l.rayEnd % 16 == 0 && l.rayEnd >= l.rayStart + 16 &&
            l.rayFilter >= l.rayEnd + 16 && l.rayFraction >= l.rayFilter + 4 &&
            l.rayShapeKey >= l.rayFraction + 4 && l.rayRootShapeKey >= l.rayShapeKey + 4, "ray field overlap");
    std::vector<uint32_t> conversions;
    for (uint32_t target : queryMethods) {
        const auto calls = image.Calls(target);
        if (calls.size() != 4 || image.Function(target).size() > 40) continue;
        const auto conversionCalls = image.Calls(calls[1]);
        if (conversionCalls.size() != 7 || conversionCalls[0] != conversionCalls[2] || conversionCalls[0] != conversionCalls[4] ||
            conversionCalls[1] != conversionCalls[3] || conversionCalls[1] != conversionCalls[5] || conversionCalls[1] != conversionCalls[6]) continue;
        const auto& scale = image.Function(conversionCalls[0]);
        if (scale.size() > 12) continue;
        for (const auto& i : scale) if (i.Op() == 0xD9 && i.decoded.modrm_reg == 0 && image.Data(i.Absolute()))
            conversions.push_back(i.Absolute());
    }
    const uint32_t scale = Unique(conversions, "world-position to physics-vector conversion");
    l.worldToHavok = image.Read<float>(scale);
    Require(l.worldToHavok > 0 && l.worldToHavok < 1, "invalid physics unit scale");
}

void ValidateCallContracts(Image& image, RuntimeBindings& bindings) {
    const auto& p = bindings.profile;
    for (const auto& contract : std::array<std::pair<uint32_t,uint32_t>,7>{{
        {static_cast<uint32_t>(p.calcCullingPlanes),8}, {static_cast<uint32_t>(p.tileSetFloatValue),12},
        {static_cast<uint32_t>(p.castRay),8}, {static_cast<uint32_t>(p.setupSkyGeometry),4},
        {static_cast<uint32_t>(p.setCameraFov),16}, {static_cast<uint32_t>(p.updateCameraProjection),0},
        {static_cast<uint32_t>(p.updateFirstPerson),0}}}) {
        bool found = false;
        for (const auto& i : image.Function(contract.first)) if (i.Ret()) {
            found = true;
            Require((i.Op() == 0xC2 ? i.decoded.imm.imm16 : 0u) == contract.second,
                    "native function argument cleanup mismatch");
        }
        Require(found, "native function return ABI not established");
    }
    const auto& angle = image.Function(Command(image, "GetAngle", true));
    std::vector<uint32_t> rotations;
    for (size_t n = 0; n + 6 < angle.size(); ++n) if (angle[n].Call() && angle[n+1].Load(1,0,0) &&
        angle[n+3].Load(2,0,4) && angle[n+5].Load(0,0,8)) rotations.push_back(angle[n].Target());
    bindings.layout.playerRotation = AddedReceiverMember(image, Unique(rotations, "named reference angle vector"), 0);
}

void DiscoverCommands(Image& image, RuntimeBindings& bindings) {
    auto& profile = bindings.profile;
    auto& layout = bindings.layout;
    profile.playerBase = GlobalLoad(image, Command(image, "GetPlayerName"), 0);
    profile.gameMain = GlobalLoad(image, Command(image, "QuitGame"), 1);
    const uint32_t firstPerson = Command(image, "IsPC1stPerson", true);
    Require(GlobalLoad(image, firstPerson, 1) == profile.playerBase, "POV command player ownership mismatch");
    layout.playerThirdPerson = FieldGetter(image, Unique(image.Calls(firstPerson), "POV query"), true);
    const auto& combat = image.Function(Command(image, "IsInCombat", true));
    std::vector<uint32_t> combatGetters;
    for (size_t i = 1; i < combat.size(); ++i)
        if (combat[i].Call() && combat[i-1].Op() == 0x8B && combat[i-1].decoded.modrm_reg == 1 &&
            combat[i-1].Absolute() == profile.playerBase) combatGetters.push_back(combat[i].Target());
    layout.playerCombat = FieldGetter(image, Unique(combatGetters, "player combat query"), true);

    const auto& fov = image.Function(Command(image, "SetCameraFOV"));
    std::vector<std::pair<uint32_t, uint32_t>> setters;
    for (size_t i = 2; i < fov.size(); ++i)
        if (fov[i].Call() && fov[i-1].MovReg(1, 0) && fov[i-2].Call())
            setters.emplace_back(fov[i-2].Target(), fov[i].Target());
    const auto fovPair = Unique(setters, "FOV command scene receiver");
    profile.setCameraFov = fovPair.second;
    std::vector<uint32_t> sceneGlobals;
    const auto& getter = image.Function(fovPair.first);
    for (const auto& i : getter)
        if (i.Op() == 0xB9 && image.Data(i.decoded.imm.imm32)) sceneGlobals.push_back(i.decoded.imm.imm32);
    profile.sceneGraphBase = Unique(sceneGlobals, "scene graph smart pointer");
    Require(FieldGetter(image, Unique(image.Calls(fovPair.first), "scene smart pointer getter"), false) == 0,
            "scene graph smart pointer layout");

    std::vector<uint32_t> fovSettings;
    for (const auto& i : fov) if (i.Op() == 0xB9 && image.Data(i.decoded.imm.imm32, 12))
        for (uint32_t name : image.Strings("fDefaultWorldFOV:Display"))
            if (image.Read<uint32_t>(i.decoded.imm.imm32 + 8) == name) fovSettings.push_back(i.decoded.imm.imm32 + 4);
    profile.defaultWorldFov = Unique(fovSettings, "named world FOV setting");

    const auto& los = image.Function(Command(image, "GetLOS", true));
    std::vector<std::pair<uint32_t, uint32_t>> traces;
    for (size_t i = 2; i < los.size(); ++i) if (los[i].Call() && los[i-1].Op() == 0x8B &&
        los[i-1].decoded.modrm_reg == 1 && image.Data(los[i-1].Absolute()) && los[i-2].Op() >= 0x50 && los[i-2].Op() <= 0x57) {
        const auto& wrapper = image.Function(los[i].Target());
        if (wrapper.size() < 18 && image.Calls(los[i].Target()).size() == 1)
            traces.emplace_back(los[i-1].Absolute(), image.Calls(los[i].Target()).front());
    }
    const auto trace = Unique(traces, "GetLOS world trace receiver");
    profile.tesWorld = trace.first;
    profile.castRay = trace.second;
}

}

bool DiscoverRuntime(const uint8_t* mappedImage, size_t size, uint32_t imageBase,
                     RuntimeBindings& output, std::string& diagnostic) {
    output = {};
    diagnostic.clear();
    try {
        Image image(mappedImage, size, imageBase);
        RuntimeBindings result;
        result.profile.name = "runtime-discovery";
        result.profile.timeDateStamp = image.fingerprint[0];
        result.profile.sizeOfImage = image.fingerprint[1];
        result.profile.checkSum = image.fingerprint[2];
        DiscoverCommands(image, result);
        DiscoverMenus(image, result);
        DiscoverCamera(image, result);
        DiscoverRenderPasses(image, result);
        DiscoverConsole(image, result);
        DiscoverReticle(image, result);
        DiscoverRig(image, result);
        DiscoverGameplay(image, result);
        DiscoverCollisionFilter(image, result);
        DiscoverRayQuery(image, result);
        ValidateCallContracts(image, result);
        output = std::move(result);
        return true;
    } catch (const std::runtime_error& error) {
        diagnostic = error.what();
        return false;
    }
}

}
