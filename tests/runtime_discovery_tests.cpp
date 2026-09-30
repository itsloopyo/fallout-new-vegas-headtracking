#include "../src/runtime_discovery.cpp"
#include <cstdio>
#include <cstdlib>

using namespace HeadTracking;
namespace {
void Check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
template<class F> void Reject(F action, const char* message) {
    bool rejected = false;
    try { action(); } catch (const std::runtime_error&) { rejected = true; }
    Check(rejected, message);
}
struct Fixture {
    uint32_t base;
    std::vector<uint8_t> data = std::vector<uint8_t>(0x5000);
    explicit Fixture(uint32_t address = 0x400000) : base(address) {
        IMAGE_DOS_HEADER dos{};
        dos.e_magic = IMAGE_DOS_SIGNATURE;
        dos.e_lfanew = 0x80;
        Put(0, dos);
        IMAGE_NT_HEADERS32 nt{};
        nt.Signature = IMAGE_NT_SIGNATURE;
        nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
        nt.FileHeader.NumberOfSections = 2;
        nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
        nt.FileHeader.Characteristics = IMAGE_FILE_LARGE_ADDRESS_AWARE;
        nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
        nt.OptionalHeader.SizeOfImage = static_cast<DWORD>(data.size());
        nt.OptionalHeader.ImageBase = base;
        Put(0x80, nt);
        IMAGE_SECTION_HEADER section{};
        section.VirtualAddress = 0x1000;
        section.Misc.VirtualSize = 0x1000;
        section.Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;
        Put(0x80 + sizeof(nt), section);
        section.VirtualAddress = 0x2000;
        section.Misc.VirtualSize = 0x3000;
        section.Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
        Put(0x80 + sizeof(nt) + sizeof(section), section);
        std::fill(data.begin() + 0x1000, data.begin() + 0x2000, uint8_t{0xCC});
    }
    template<class T> void Put(size_t at, const T& value) { std::memcpy(data.data() + at, &value, sizeof(value)); }
    void Pointer(size_t at, uint32_t rva) { Put(at, base + rva); }
    void Text(size_t at, const char* text) { std::memcpy(data.data() + at, text, std::strlen(text) + 1); }
    void Code(size_t at, std::initializer_list<uint8_t> bytes) { std::copy(bytes.begin(), bytes.end(), data.begin() + at); }
    Image Open() { return Image(data.data(), data.size(), base); }
    void CommandAt(uint32_t descriptor, uint32_t function) {
        Pointer(descriptor, 0x2100);
        Put(descriptor + 8, uint32_t{0x1100});
        Pointer(descriptor + 24, function);
        Pointer(descriptor + 28, 0x1100);
        Pointer(descriptor + 32, function);
    }
    void VtableAt(uint32_t table) {
        Text(0x2208, ".?AVFixtureMenu@@");
        Pointer(0x230C, 0x2200);
        Pointer(0x2310, 0x2340);
        Put(0x2348, uint32_t{1});
        Pointer(0x234C, 0x2380);
        Pointer(0x2380, 0x23A0);
        Pointer(0x23A0, 0x2200);
        Put(0x23AC, uint32_t{UINT32_MAX});
        Pointer(table - 4, 0x2300);
        Pointer(table, 0x1000);
        Pointer(table + 4, 0x1100);
    }
};

void CommandsAndFields() {
    for (uint32_t base : {0x400000u, 0x51000000u, 0x90000000u}) {
        Fixture f(base);
        f.Text(0x2100, "FixtureQuery");
        f.Code(0x1000, {0x8B,0xC1,0x8B,0x80,0x38,0,0,0,0xC3});
        f.Code(0x1100, {0xC3});
        f.CommandAt(0x2400, 0x1000);
        auto image = f.Open();
        Check(Command(image,"FixtureQuery",true) == base + 0x1000, "relocated native registration");
        Check(FieldGetter(image, base + 0x1000,false) == 0x38, "owned dword getter");
        f.Put(0x1004,uint32_t{0xD8});
        auto moved = f.Open();
        Check(FieldGetter(moved,base+0x1000,false)==0xD8,"moved field");
        f.data[0x1001]=0xC2;
        auto wrongOwner=f.Open();
        Reject([&]{FieldGetter(wrongOwner,base+0x1000,false);},"reject wrong field owner");
        f.data[0x1001]=0xC1;
        f.data[0x1002]=0x8A;
        auto wrongWidth=f.Open();
        Reject([&]{FieldGetter(wrongWidth,base+0x1000,false);},"reject byte as dword");
        Check(FieldGetter(wrongWidth,base+0x1000,true)==0xD8,"typed byte getter");
        f.CommandAt(0x2480,0x1100);
        auto duplicate=f.Open();
        Reject([&]{Command(duplicate,"FixtureQuery");},"reject duplicate registration");
        f.data[0x2100]='X';
        auto missing=f.Open();
        Reject([&]{Command(missing,"FixtureQuery");},"reject missing named anchor");
    }
}

void VirtualOwners() {
    Fixture f;
    f.Code(0x1000,{0xB8,0xE9,3,0,0,0xC3});
    f.Code(0x1100,{0xC3});
    f.VtableAt(0x2500);
    auto initial=f.Open();
    Check(ClassVtable(initial,"FixtureMenu")==f.base+0x2500,"owned primary RTTI table");
    Check(MenuId(initial,"FixtureMenu",1000,1100)==1001,"menu ID from virtual method");
    Check(DerivedVtables(initial,"FixtureMenu")==std::vector<uint32_t>{f.base+0x2500},"derived RTTI ownership");
    f.Pointer(0x2500,0x1100);
    f.Pointer(0x2504,0x1000);
    auto moved=f.Open();
    Check(MenuId(moved,"FixtureMenu",1000,1100)==1001,"moved virtual slot");
    f.Pointer(0x23A0,0x2280);
    auto wrong=f.Open();
    Reject([&]{ClassVtable(wrong,"FixtureMenu");},"reject wrong RTTI base owner");
    f.VtableAt(0x2600);
    auto duplicate=f.Open();
    Reject([&]{ClassVtable(duplicate,"FixtureMenu");},"reject ambiguous primary table");
    f.Put(0x23A8,uint32_t{4});
    auto adjusted=f.Open();
    Reject([&]{DerivedVtables(adjusted,"FixtureMenu");},"reject nonzero base adjustment");
}

void MalformedCodeAndImages() {
    Fixture f;
    f.Code(0x1000,{0x75,0xFF,0xC3});
    auto overlapping=f.Open();
    Reject([&]{overlapping.Function(f.base+0x1000);},"reject branch into instruction");
    f.Code(0x1FFF,{0xE8});
    auto truncated=f.Open();
    Reject([&]{truncated.Decode(f.base+0x1FFF);},"reject truncated call");
    Reject([&]{truncated.Decode(f.base+0x2400);},"reject non-executable method");
    f.Put(0x80+offsetof(IMAGE_NT_HEADERS32,OptionalHeader)+offsetof(IMAGE_OPTIONAL_HEADER32,SizeOfImage),uint32_t{0x6000});
    Reject([&]{f.Open();},"reject mismatched image extent");
    Fixture sections;
    auto at=0x80+sizeof(IMAGE_NT_HEADERS32)+sizeof(IMAGE_SECTION_HEADER)+offsetof(IMAGE_SECTION_HEADER,VirtualAddress);
    sections.Put(at,uint32_t{0x1800});
    Reject([&]{sections.Open();},"reject overlapping sections");
    Fixture good;
    RuntimeBindings result;
    result.profile.playerBase=0x1234;
    result.playerVtables={0x5678};
    std::string diagnostic;
    Check(!DiscoverRuntime(good.data.data(),good.data.size(),good.base,result,diagnostic),"reject incomplete engine fixture");
    Check(result.profile.playerBase==0 && result.playerVtables.empty() && !diagnostic.empty(),"clear prior result on failed discovery");
    Check(!DiscoverRuntime(good.data.data(),2,good.base,result,diagnostic),"reject truncated PE header");
}

void MenuModes() {
    Fixture f;
    f.Code(0x1000,{0x55,0x8B,0xEC,0x51,0x89,0x4D,0xFC,0x8B,0x45,0xFC,
                  0x33,0xC9,0x83,0x78,0x2C,0x05,0x0F,0x95,0xC1,0x8A,0xC1,0x8B,0xE5,0x5D,0xC3});
    auto image=f.Open();
    Check(MenuPredicates(image.Function(f.base+0x1000))==std::vector<std::pair<uint32_t,uint32_t>>{{0x2C,5}},"owned menu field and changed gameplay enum");
    f.data[0x1009]=0xF8;
    auto wrong=f.Open();
    Check(MenuPredicates(wrong.Function(f.base+0x1000)).empty(),"reject menu comparison on wrong receiver");
    f.data[0x1009]=0xFC;
    f.data[0x1011]=0x94;
    auto equality=f.Open();
    Check(MenuPredicates(equality.Function(f.base+0x1000)).empty(),"reject reversed menu predicate");
}

void ConstructorTypes() {
    Fixture f;
    f.Code(0x1000,{0x55,0x8B,0xEC,0x51,0x89,0x4D,0xFC,0x8B,0x45,0xFC,
                   0xC7,0x80,0x70,0,0,0,0xFF,0xFF,0xFF,0xFF,0x8B,0xE5,0x5D,0xC3});
    auto good=f.Open();
    Check(StoredDefault(ReadConstructor(good,f.base+0x1000),4,UINT32_MAX)==0x70,"constructor key type and field");
    f.Put(0x100C,uint32_t{0xA0});
    auto moved=f.Open();
    Check(StoredDefault(ReadConstructor(moved,f.base+0x1000),4,UINT32_MAX)==0xA0,"moved constructor field");
    Reject([&]{StoredDefault(ReadConstructor(moved,f.base+0x1000),1,UINT32_MAX);},"reject constructor field width mismatch");
    f.Put(0x100C,uint32_t{0x1000});
    auto bounds=f.Open();
    Reject([&]{ReadConstructor(bounds,f.base+0x1000);},"reject unbounded member write");
}
}
int main() {
    CommandsAndFields();
    VirtualOwners();
    MalformedCodeAndImages();
    ConstructorTypes();
    MenuModes();
    std::puts("Runtime discovery contracts passed");
}
