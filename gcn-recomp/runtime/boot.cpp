// Boot: emulate what the IPL + apploader leave behind, load the DOL, start __start.
#include "runtime.h"
#include "boot.h"
#include "gecko.h"
#include <vector>

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

void vi_set_tv_mode(uint32_t mode); void vi_init(); void si_init(); void exi_init(); void ai_init(); void dsp_init(); void cp_init();

// The console the disc was made for, from the region byte it carries (header + 3): 0 NTSC,
// 1 PAL. The PAL releases check it -- VIConfigure panics with "Tried to change mode from
// (0) to (1), which is forbidden" when the console says NTSC -- and the format also fixes
// the field rate, so the region decides both.
static uint32_t disc_tv_mode(const uint8_t* hdr) {
    switch (hdr[3]) {
    case 'E':  // USA
    case 'J':  // Japan
    case 'K':  // Korea
        return 0;
    default:   // Europe and its language variants
        return 1;
    }
}

uint32_t boot_load(const char* iso_path) {
    di_init(iso_path);
    // A mod's Gecko codes are read now, before the game's first instruction: the program
    // they became is run from the Video Interface's first frame on.
    gecko_init(iso_path);
    uint8_t hdr[0x440];
    if (!iso_read(0, hdr, sizeof(hdr))) fatal("cannot read disc header");
    uint32_t dol_off = be32(hdr + 0x420), fst_off = be32(hdr + 0x424), fst_size = be32(hdr + 0x428), fst_max = be32(hdr + 0x42C);
    LOG(LOG_OS, "disc %.6s \"%s\" dol@%X fst@%X size %X", (const char*)hdr, (const char*)hdr + 0x20, dol_off, fst_off, fst_size);
    // A patch is a whole disc of its own, and a hack may rename the ID it carries: SCON4 is
    // SG4JDA over a G4NJDA disc. The tree the patch decodes from is still the disc this
    // build was made for, so that is the header boot_check_disc() is given while one is on.
    uint8_t tree_hdr[0x20];
    const uint8_t* tree = nullptr;
    if (di_patch_active() && di_source_read(0, tree_hdr, sizeof(tree_hdr))) tree = tree_hdr;
    switch (boot_check_disc(hdr, tree, GCN_DISC_ID)) {
    case DiscCheck::Matched:
        break;
    case DiscCheck::WrongTree:
        fatal("this build was recompiled from disc %s, and the disc it patches is %.6s",
              GCN_DISC_ID, (const char*)tree);
    case DiscCheck::WrongDisc:
        fatal("this build was recompiled from disc %s, not %.6s", GCN_DISC_ID, (const char*)hdr);
    }

    // ---- DOL ----
    uint8_t dh[0x100];
    iso_read(dol_off, dh, sizeof(dh));
    uint32_t bss = be32(dh + 0xD8), bss_size = be32(dh + 0xDC), entry = be32(dh + 0xE0);
    // The header's BSS range spans .bss..sbss and overlaps .sdata, so zero it first.
    memset(mem_ptr(bss), 0, bss_size);
    for (int i = 0; i < 18; i++) {
        uint32_t off = be32(dh + 4 * i), addr = be32(dh + 0x48 + 4 * i), size = be32(dh + 0x90 + 4 * i);
        if (!size) continue;
        iso_read(dol_off + off, mem_ptr(addr), size);
    }

    // ---- low memory (IPL boot info) ----
    uint32_t tv_mode = disc_tv_mode(hdr);
    memcpy(mem_ptr(0x80000000), hdr, 0x20);
    mem_w32(0x8000001C, 0xC2339F3D);
    mem_w32(0x80000020, 0x0D15EA5E);        // booted from bootrom
    mem_w32(0x80000024, 1);
    mem_w32(0x80000028, RAM_SIZE);
    mem_w32(0x8000002C, 0x10000006);        // console type: latest devkit (as Dolphin)
    mem_w32(0x800000CC, tv_mode);           // 0 NTSC, 1 PAL: what the disc was made for
    vi_set_tv_mode(tv_mode);
    mem_w32(0x800000D0, ARAM_SIZE);
    mem_w32(0x800000F0, RAM_SIZE);
    mem_w32(0x800000F8, 162000000);         // bus clock
    mem_w32(0x800000FC, 486000000);         // cpu clock
    mem_w32(0x80000300, 0x4C000064);
    mem_w32(0x80000800, 0x4C000064);
    mem_w32(0x80000C00, 0x4C000064);

    // BI2 at the very top, FST just below it (what the apploader does).
    uint32_t bi2_addr = 0x817FE000;
    iso_read(0x440, mem_ptr(bi2_addr), 0x2000);
    mem_w32(0x800000F4, bi2_addr);
    uint32_t fst_addr = (bi2_addr - fst_max) & ~0x1Fu;
    iso_read(fst_off, mem_ptr(fst_addr), fst_size);
    mem_w32(0x80000038, fst_addr);
    mem_w32(0x8000003C, fst_max);
    mem_w32(0x80000034, fst_addr);          // arena hi
    LOG(LOG_OS, "entry %08X, FST at %08X, BI2 at %08X", entry, fst_addr, bi2_addr);

    vi_init();
    si_init();
    exi_init();
    ai_init();
    dsp_init();
    cp_init();
    return entry;
}
