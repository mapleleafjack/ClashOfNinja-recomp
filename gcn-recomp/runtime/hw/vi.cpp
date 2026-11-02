// Video Interface: retrace interrupts and framebuffer registers.
#include "../runtime.h"
#include "../gecko.h"
#include <algorithm>
#include <atomic>

static uint16_t g_vi[0x80];
static uint64_t g_frame_start;
// Two fields make a frame: 59.94 fields a second on NTSC, 50 exactly on PAL. The console's
// format is the disc's, so a PAL release is timed as it was on a PAL console (vi_set_tv_mode).
static double g_fields_per_second = 60000.0 / 1001.0;
static uint32_t g_field_half_lines = 525;
std::atomic<uint32_t> g_vi_retrace_count{0};
std::atomic<uint32_t> g_vi_xfb_addr{0};

static uint64_t field_ticks() { return (uint64_t)(TB_FREQ / g_fields_per_second); }

static void vi_update_irq() {
    bool any = false;
    for (int i = 0; i < 4; i++) {
        uint16_t hi = g_vi[(0x30 + 4 * i) >> 1];
        if ((hi & 0x8000) && (hi & 0x1000)) any = true;
    }
    pi_set_interrupt(INT_VI, any);
}

// Which field of an interlaced frame is being scanned out: 0 the first, 1 the second.
static int g_field;

static bool vi_progressive() { return (g_vi[0x02 >> 1] >> 2) & 1; }

static void vi_retrace() {
    g_frame_start = now_ticks();
    g_field = vi_progressive() ? 0 : g_field ^ 1;
    g_vi_retrace_count++;
    // A mod's Gecko codes run once a frame, as they do on a console: on the first field,
    // and on every field of a progressive display, where a field is the whole frame.
    if (g_field == 0) gecko_frame();
    uint32_t tfbl = ((uint32_t)g_vi[0x1C >> 1] << 16) | g_vi[0x1E >> 1];
    g_vi_xfb_addr = (tfbl & 0x10000000) ? ((tfbl & 0xFFFFFF) << 5) : (tfbl & 0xFFFFFF);
    // Raise display interrupts 0 and 1 if enabled.
    for (int i = 0; i < 2; i++) {
        uint16_t& hi = g_vi[(0x30 + 4 * i) >> 1];
        if (hi & 0x1000) hi |= 0x8000;
    }
    vi_update_irq();
    event_schedule(g_frame_start + field_ticks(), vi_retrace);
}

void vi_set_tv_mode(uint32_t mode) {
    // MPAL is a 60 Hz field like NTSC; only PAL scans 625 half-lines.
    const bool pal = mode == 1;
    g_fields_per_second = pal ? 50.0 : 60000.0 / 1001.0;
    g_field_half_lines = pal ? 625 : 525;
}

void vi_init() {
    event_schedule_in(field_ticks(), vi_retrace);
}

// The lines the VI scans out: ACV per field, two fields unless progressive (DCR.NIN).
uint32_t vi_display_height() {
    uint32_t acv = (g_vi[0x00 >> 1] >> 4) & 0x3FF;
    return vi_progressive() ? acv : acv * 2;
}

// Where the beam is, as the VI counts it: (vertical << 16) | horizontal, both from 1. A
// line is two half-lines of HLW (HTR0) horizontal counts each, and a field is 525 half-lines
// (524 progressive), 625 on PAL. An interlaced frame counts its lines through both fields,
// 1..525, so the second field starts half way along line 263 -- where the SDK puts the
// display interrupt that marks it, and how VIGetNextField tells the fields apart. Counting
// each field from line 1 with the horizontal count fixed at 1 made every field the first,
// and a movie player that waits for the second to start never did.
static uint32_t beam_position() {
    uint32_t hlw = g_vi[0x06 >> 1] & 0x1FF;
    if (!hlw) hlw = 429;
    uint64_t ticks = field_ticks();
    uint64_t per_field = (uint64_t)(vi_progressive() ? g_field_half_lines - 1
                                                     : g_field_half_lines) * hlw;
    uint64_t t = std::min<uint64_t>(now_ticks() - g_frame_start, ticks - 1);
    uint64_t pos = g_field * per_field + t * per_field / ticks;
    return (uint32_t)((pos / (2 * hlw) + 1) << 16 | (pos % (2 * hlw) + 1));
}

uint16_t vi_read16(uint32_t off) {
    off &= 0xFF;
    switch (off) {
    case 0x2C: return (uint16_t)(beam_position() >> 16);  // DPV: vertical count, 1-based
    case 0x2E: return (uint16_t)beam_position();          // DPH: horizontal count, 1-based
    case 0x6E: return 0;  // DTV status: no component cable
    default: return g_vi[off >> 1];
    }
}

void vi_write16(uint32_t off, uint16_t v) {
    off &= 0xFF;
    if (getenv("GCN_VILOG") && off < 0x40 && g_vi[off >> 1] != v) fprintf(stderr, "[vi] reg %02X = %04X\n", off, v);
    g_vi[off >> 1] = v;
    if (off >= 0x30 && off < 0x40) vi_update_irq();
}
