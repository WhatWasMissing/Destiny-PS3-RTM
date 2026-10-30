#pragma once
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/error.h"

namespace brps3::ppc {

inline uint32_t s16(int64_t v) {
    if (v < -0x8000 || v > 0x7FFF) throw Error("immediate " + std::to_string(v) + " out of signed 16-bit range");
    return uint32_t(v) & 0xFFFF;
}

inline uint32_t u16(int64_t v) {
    if (v < 0 || v > 0xFFFF) throw Error("immediate " + std::to_string(v) + " out of unsigned 16-bit range");
    return uint32_t(v);
}

inline uint32_t ds(int64_t v) {
    if (v % 4) throw Error("DS-form displacement must be a multiple of 4");
    return s16(v);
}

 
inline std::pair<uint32_t, uint32_t> hiLo(uint32_t value) {
    uint32_t lo = value & 0xFFFF;
    return {((value >> 16) + ((lo & 0x8000) ? 1u : 0u)) & 0xFFFF, lo};
}

inline void putBE32(std::vector<uint8_t>& out, uint32_t w) {
    out.push_back(uint8_t(w >> 24));
    out.push_back(uint8_t(w >> 16));
    out.push_back(uint8_t(w >> 8));
    out.push_back(uint8_t(w));
}

 
inline uint32_t branchWord(uint32_t src, uint32_t dst, bool link = false) {
    int64_t off = int64_t(dst) - int64_t(src);
    if (off % 4 || off < -0x2000000 || off >= 0x2000000)
        throw Error("branch " + hex32(src) + " -> " + hex32(dst) + " out of range");
    return 0x48000000u | (uint32_t(off) & 0x3FFFFFCu) | (link ? 1u : 0u);
}

 
 
 
namespace enc {
inline uint32_t aForm(uint32_t op, int d, int a, int b, int c, uint32_t xo) {
    return op << 26 | uint32_t(d) << 21 | uint32_t(a) << 16 | uint32_t(b) << 11 | uint32_t(c) << 6 | xo << 1;
}
inline uint32_t fadds(int d, int a, int b) { return aForm(59, d, a, b, 0, 21); }
inline uint32_t fsubs(int d, int a, int b) { return aForm(59, d, a, b, 0, 20); }
inline uint32_t fmuls(int d, int a, int c) { return aForm(59, d, a, 0, c, 25); }
inline uint32_t fdivs(int d, int a, int b) { return aForm(59, d, a, b, 0, 18); }
inline uint32_t fmadds(int d, int a, int c, int b) { return aForm(59, d, a, b, c, 29); }    
inline uint32_t fsqrts(int d, int b) { return aForm(59, d, 0, b, 0, 22); }
inline uint32_t fneg(int d, int b) { return 63u << 26 | uint32_t(d) << 21 | uint32_t(b) << 11 | 40u << 1; }
inline uint32_t fabs(int d, int b) { return 63u << 26 | uint32_t(d) << 21 | uint32_t(b) << 11 | 264u << 1; }
inline uint32_t divwu(int d, int a, int b) { return 31u << 26 | uint32_t(d) << 21 | uint32_t(a) << 16 | uint32_t(b) << 11 | 459u << 1; }
inline uint32_t fmr(int d, int b) { return 63u << 26 | uint32_t(d) << 21 | uint32_t(b) << 11 | 72u << 1; }
inline uint32_t fcmpu(int crf, int a, int b) { return 63u << 26 | uint32_t(crf) << 23 | uint32_t(a) << 16 | uint32_t(b) << 11; }
inline uint32_t fcfid(int d, int b) { return 63u << 26 | uint32_t(d) << 21 | uint32_t(b) << 11 | 846u << 1; }
inline uint32_t frsp(int d, int b) { return 63u << 26 | uint32_t(d) << 21 | uint32_t(b) << 11 | 12u << 1; }
inline uint32_t lha(int d, int64_t disp, int a) { return 42u << 26 | uint32_t(d) << 21 | uint32_t(a) << 16 | s16(disp); }
inline uint32_t lbzu(int d, int64_t disp, int a) {
    if (a == 0 || a == d) throw Error("lbzu: invalid base register");
    return 35u << 26 | uint32_t(d) << 21 | uint32_t(a) << 16 | s16(disp);
}
inline uint32_t orDot(int a, int s, int b) { return 0x7C000379u | uint32_t(s) << 21 | uint32_t(a) << 16 | uint32_t(b) << 11; }    
inline uint32_t rlwinmDot(int a, int s, int sh, int mb, int me) {
    return 0x54000001u | uint32_t(s) << 21 | uint32_t(a) << 16 | uint32_t(sh) << 11 | uint32_t(mb) << 6 | uint32_t(me) << 1;
}
}   

class Asm {
public:
    explicit Asm(uint32_t base) : base_(base) {}

    uint32_t base() const { return base_; }
    uint32_t here() const { return base_ + 4 * uint32_t(words_.size()); }
    size_t size() const { return words_.size(); }                 
    uint32_t labelAddr(const std::string& name) const {
        auto it = labels_.find(name);
        if (it == labels_.end()) throw Error("undefined label " + name);
        return it->second;
    }

    Asm& word(uint32_t w) { words_.push_back(w); return *this; }
    Asm& label(const std::string& name) { labels_[name] = here(); return *this; }

     
    Asm& li(int rd, int64_t v) { return word(0x38000000u | rd << 21 | s16(v)); }
    Asm& lis(int rd, int64_t v) { return word(0x3C000000u | rd << 21 | u16(v)); }
    Asm& addi(int rd, int ra, int64_t v) {
        if (ra == 0) throw Error("addi with rA=r0 adds to literal 0 (that is li); use another register");
        return word(0x38000000u | rd << 21 | ra << 16 | s16(v));
    }
     
    Asm& addis(int rd, int ra, int64_t v) {
        if (ra == 0) throw Error("addis with rA=r0 is lis; use lis()");
        return word(0x3C000000u | rd << 21 | ra << 16 | s16(v));
    }
    Asm& ori(int ra, int rs, int64_t v) { return word(0x60000000u | rs << 21 | ra << 16 | u16(v)); }
    Asm& mr(int ra, int rs) { return word(0x7C000378u | rs << 21 | ra << 16 | rs << 11); }
    Asm& cmpwi(int ra, int64_t v) { return word(0x2C000000u | ra << 16 | s16(v)); }
    Asm& cmplwi(int ra, int64_t v) { return word(0x28000000u | ra << 16 | u16(v)); }
    Asm& cmplw(int ra, int rb) { return word(0x7C000040u | ra << 16 | rb << 11); }
    Asm& add(int rd, int ra, int rb) { return word(0x7C000214u | rd << 21 | ra << 16 | rb << 11); }
    Asm& andi_(int ra, int rs, int64_t v) { return word(0x70000000u | rs << 21 | ra << 16 | u16(v)); }
    Asm& rlwinm(int ra, int rs, int sh, int mb, int me) {
        return word(0x54000000u | rs << 21 | ra << 16 | sh << 11 | mb << 6 | me << 1);
    }
    Asm& slwi(int ra, int rs, int n) { return rlwinm(ra, rs, n, 0, 31 - n); }
    Asm& srwi(int ra, int rs, int n) { return rlwinm(ra, rs, 32 - n, n, 31); }
    Asm& lwzu(int rd, int64_t d, int ra) { return word(0x84000000u | rd << 21 | ra << 16 | s16(d)); }
    Asm& stwu(int rs, int64_t d, int ra) { return word(0x94000000u | rs << 21 | ra << 16 | s16(d)); }
    Asm& bdnz(const std::string& target) { return cond(0x42000000u, target); }
    Asm& bdnz(uint32_t target) { return cond(0x42000000u, target); }
    Asm& stvx(int vs, int ra, int rb) { return word(0x7C0001CEu | vs << 21 | ra << 16 | rb << 11); }
    Asm& extsw(int ra, int rs) { return word(0x7C0007B4u | rs << 21 | ra << 16); }
    Asm& cmpw(int ra, int rb) { return word(0x7C000000u | ra << 16 | rb << 11); }
    Asm& subf(int rd, int ra, int rb) { return word(0x7C000050u | rd << 21 | ra << 16 | rb << 11); }
    Asm& mulli(int rd, int ra, int64_t v) { return word(0x1C000000u | rd << 21 | ra << 16 | s16(v)); }
    Asm& nop() { return word(0x60000000u); }
    Asm& divwu(int rd, int ra, int rb) { return word(enc::divwu(rd, ra, rb)); }    
     
     
    Asm& and_(int ra, int rs, int rb) { return word(0x7C000038u | rs << 21 | ra << 16 | rb << 11); }
    Asm& andDot(int ra, int rs, int rb) { return word(0x7C000039u | rs << 21 | ra << 16 | rb << 11); }
    Asm& or_(int ra, int rs, int rb) { return word(0x7C000378u | rs << 21 | ra << 16 | rb << 11); }
    Asm& mullw(int rd, int ra, int rb) { return word(0x7C0001D6u | rd << 21 | ra << 16 | rb << 11); }
    Asm& clrldi(int ra, int rs, int n) {
        if (n < 0 || n > 63) throw Error("clrldi: bit count out of range");
        const uint32_t mb = uint32_t(n);
        return word(0x78000000u | rs << 21 | ra << 16 | (((mb & 0x1Fu) << 1) | (mb >> 5)) << 5);
    }

     
    Asm& load32(int rd, uint32_t value) {
        lis(rd, (value >> 16) & 0xFFFF);
        return ori(rd, rd, value & 0xFFFF);
    }
     
    Asm& addr(int rd, int64_t value) {
        if (value < 0 || value >= 0x80000000LL) throw Error("addr() is for positive 32-bit addresses");
        auto [hi, lo] = hiLo(uint32_t(value));
        lis(rd, hi);
        return addi(rd, rd, (lo & 0x8000) ? int64_t(lo) - 0x10000 : int64_t(lo));
    }

     
    Asm& lbz(int rd, int64_t d, int ra) { return word(0x88000000u | rd << 21 | ra << 16 | s16(d)); }
    Asm& lhz(int rd, int64_t d, int ra) { return word(0xA0000000u | rd << 21 | ra << 16 | s16(d)); }
    Asm& lwz(int rd, int64_t d, int ra) { return word(0x80000000u | rd << 21 | ra << 16 | s16(d)); }
    Asm& stb(int rs, int64_t d, int ra) { return word(0x98000000u | rs << 21 | ra << 16 | s16(d)); }
    Asm& sth(int rs, int64_t d, int ra) { return word(0xB0000000u | rs << 21 | ra << 16 | s16(d)); }
    Asm& stw(int rs, int64_t d, int ra) { return word(0x90000000u | rs << 21 | ra << 16 | s16(d)); }
    Asm& ld(int rd, int64_t d, int ra) { return word(0xE8000000u | rd << 21 | ra << 16 | ds(d)); }
    Asm& std_(int rs, int64_t d, int ra) { return word(0xF8000000u | rs << 21 | ra << 16 | ds(d)); }
    Asm& stdu(int rs, int64_t d, int ra) { return word(0xF8000001u | rs << 21 | ra << 16 | ds(d)); }
    Asm& lwzx(int rd, int ra, int rb) { return word(0x7C00002Eu | rd << 21 | ra << 16 | rb << 11); }
    Asm& lbzx(int rd, int ra, int rb) { return word(0x7C0000AEu | rd << 21 | ra << 16 | rb << 11); }
    Asm& lhzx(int rd, int ra, int rb) { return word(0x7C00022Eu | rd << 21 | ra << 16 | rb << 11); }
    Asm& stwx(int rs, int ra, int rb) { return word(0x7C00012Eu | rs << 21 | ra << 16 | rb << 11); }
    Asm& lfs(int fd, int64_t d, int ra) { return word(0xC0000000u | fd << 21 | ra << 16 | s16(d)); }
    Asm& lfd(int fd, int64_t d, int ra) { return word(0xC8000000u | fd << 21 | ra << 16 | s16(d)); }
    Asm& stfs(int fs, int64_t d, int ra) { return word(0xD0000000u | fs << 21 | ra << 16 | s16(d)); }
    Asm& stfd(int fs, int64_t d, int ra) { return word(0xD8000000u | fs << 21 | ra << 16 | s16(d)); }
    Asm& lvx(int vd, int ra, int rb) { return word(0x7C0000CEu | vd << 21 | ra << 16 | rb << 11); }
    Asm& lwarx(int rd, int ra, int rb) { return word(0x7C000028u | rd << 21 | ra << 16 | rb << 11); }
    Asm& stwcx(int rs, int ra, int rb) { return word(0x7C00012Du | rs << 21 | ra << 16 | rb << 11); }
    Asm& lwsync() { return word(0x7C2004ACu); }
    Asm& sync() { return word(0x7C0004ACu); }
    Asm& lha(int rd, int64_t d, int ra) { return word(enc::lha(rd, d, ra)); }
    Asm& lbzu(int rd, int64_t d, int ra) { return word(enc::lbzu(rd, d, ra)); }
    Asm& orDot(int ra, int rs, int rb) { return word(enc::orDot(ra, rs, rb)); }
    Asm& rlwinmDot(int ra, int rs, int sh, int mb, int me) { return word(enc::rlwinmDot(ra, rs, sh, mb, me)); }

     
    Asm& fadds(int fd, int fa, int fb) { return word(enc::fadds(fd, fa, fb)); }
    Asm& fsubs(int fd, int fa, int fb) { return word(enc::fsubs(fd, fa, fb)); }
    Asm& fmuls(int fd, int fa, int fc) { return word(enc::fmuls(fd, fa, fc)); }
    Asm& fdivs(int fd, int fa, int fb) { return word(enc::fdivs(fd, fa, fb)); }
    Asm& fmadds(int fd, int fa, int fc, int fb) { return word(enc::fmadds(fd, fa, fc, fb)); }
    Asm& fsqrts(int fd, int fb) { return word(enc::fsqrts(fd, fb)); }
    Asm& fneg(int fd, int fb) { return word(enc::fneg(fd, fb)); }
    Asm& fabs(int fd, int fb) { return word(enc::fabs(fd, fb)); }
    Asm& fmr(int fd, int fb) { return word(enc::fmr(fd, fb)); }
    Asm& fcmpu(int crf, int fa, int fb) { return word(enc::fcmpu(crf, fa, fb)); }
    Asm& fcfid(int fd, int fb) { return word(enc::fcfid(fd, fb)); }
    Asm& frsp(int fd, int fb) { return word(enc::frsp(fd, fb)); }

     
    Asm& mflr(int rd) { return word(0x7C0802A6u | rd << 21); }
    Asm& mtlr(int rs) { return word(0x7C0803A6u | rs << 21); }
    Asm& mfcr(int rd) { return word(0x7C000026u | rd << 21); }
    Asm& mtcr(int rs) { return word(0x7C0FF120u | rs << 21); }
    Asm& mfctr(int rd) { return word(0x7C0902A6u | rd << 21); }
    Asm& mtctr(int rs) { return word(0x7C0903A6u | rs << 21); }
    Asm& bctrl() { return word(0x4E800421u); }
    Asm& blr() { return word(0x4E800020u); }
    Asm& bltlr() { return word(0x4D800020u); }    
    Asm& bgelr() { return word(0x4C800020u); }    

     
    Asm& b(uint32_t target) { return branch(0x48000000u, target); }
    Asm& b(const std::string& target) { return branch(0x48000000u, target); }
    Asm& bl(uint32_t target) { return branch(0x48000001u, target); }
    Asm& bl(const std::string& target) { return branch(0x48000001u, target); }
    Asm& beq(const std::string& t) { return cond(0x41820000u, t); }
    Asm& bne(const std::string& t) { return cond(0x40820000u, t); }
    Asm& blt(const std::string& t) { return cond(0x41800000u, t); }
    Asm& bge(const std::string& t) { return cond(0x40800000u, t); }
    Asm& bgt(const std::string& t) { return cond(0x41810000u, t); }
    Asm& ble(const std::string& t) { return cond(0x40810000u, t); }
    Asm& beq(uint32_t t) { return cond(0x41820000u, t); }
    Asm& bne(uint32_t t) { return cond(0x40820000u, t); }
    Asm& blt(uint32_t t) { return cond(0x41800000u, t); }
    Asm& bge(uint32_t t) { return cond(0x40800000u, t); }
    Asm& bgt(uint32_t t) { return cond(0x41810000u, t); }
    Asm& ble(uint32_t t) { return cond(0x40810000u, t); }
     
    Asm& b(const char* t) { return b(std::string(t)); }
    Asm& bl(const char* t) { return bl(std::string(t)); }
    Asm& beq(const char* t) { return beq(std::string(t)); }
    Asm& bne(const char* t) { return bne(std::string(t)); }
    Asm& blt(const char* t) { return blt(std::string(t)); }
    Asm& bge(const char* t) { return bge(std::string(t)); }
    Asm& bgt(const char* t) { return bgt(std::string(t)); }
    Asm& ble(const char* t) { return ble(std::string(t)); }
    Asm& bdnz(const char* t) { return bdnz(std::string(t)); }

     
    std::vector<uint8_t> link() {
        for (const auto& f : fix_) {
            uint32_t dst = std::holds_alternative<std::string>(f.target) ? labelAddr(std::get<std::string>(f.target))
                                                                           : std::get<uint32_t>(f.target);
            uint32_t at = base_ + 4 * uint32_t(f.index);
            int64_t off = int64_t(dst) - int64_t(at);
            int64_t limit = int64_t(1) << (f.bits - 1);
            if (off % 4 || off < -limit || off >= limit)
                throw Error("branch at " + hex32(at) + " cannot reach " + hex32(dst));
            words_[f.index] = f.op | (uint32_t(off) & uint32_t((int64_t(1) << f.bits) - 4));
        }
        std::vector<uint8_t> out;
        out.reserve(words_.size() * 4);
        for (uint32_t w : words_) putBE32(out, w);
        return out;
    }

private:
    struct Fix {
        size_t index;
        uint32_t op;
        std::variant<uint32_t, std::string> target;
        int bits;
    };
    Asm& branch(uint32_t op, std::variant<uint32_t, std::string> target) {
        fix_.push_back({words_.size(), op, std::move(target), 26});
        return word(0);
    }
    Asm& cond(uint32_t op, std::variant<uint32_t, std::string> target) {
        fix_.push_back({words_.size(), op, std::move(target), 16});
        return word(0);
    }

    uint32_t base_;
    std::vector<uint32_t> words_;
    std::map<std::string, uint32_t> labels_;
    std::vector<Fix> fix_;
};

 
 
inline void prologue(Asm& a, int64_t frame, const std::vector<int>& save) {
    a.stdu(1, -frame, 1).mflr(0);
    for (size_t i = 0; i < save.size(); ++i) a.std_(save[i], 0x70 + 8 * int64_t(i), 1);
    a.std_(0, 0x70 + 8 * int64_t(save.size()), 1);
}

inline void epilogue(Asm& a, int64_t frame, const std::vector<int>& save) {
    a.ld(0, 0x70 + 8 * int64_t(save.size()), 1).mtlr(0);
    for (size_t i = 0; i < save.size(); ++i) a.ld(save[i], 0x70 + 8 * int64_t(i), 1);
    a.addi(1, 1, frame);
}

}   
