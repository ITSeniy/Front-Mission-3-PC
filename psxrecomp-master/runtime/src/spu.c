/*
 * spu.c - PS1 Sound Processing Unit register and direct ADPCM voice model.
 *
 * Compact hardware model: SPU registers, DMA4, 24 ADPCM voices + ADSR, CD/XA
 * input. Reverb, noise, sweep volumes, and IRQ timing are not modeled yet.
 *
 * Timing (2026-07 guest-clock pass): the SPU is advanced from guest system
 * cycles via spu_advance() — one stereo sample every 768 cycles (33.8688 MHz
 * / 44100), matching Beetle's UpdateFromCDC clock divider. Host audio
 * (spu_render) only drains the produced sample ring; it never steps voices.
 */

#include "spu.h"
#include "spu_shadow.h"

#include <string.h>

#define SPU_RAM_SIZE       (512 * 1024)
#define SPU_REG_COUNT      256
#define SPU_VOICE_COUNT    24
#define SPU_BLOCK_SAMPLES  28

static uint8_t  spu_ram[SPU_RAM_SIZE];
static uint16_t spu_regs[SPU_REG_COUNT];
static uint32_t transfer_addr;
static uint32_t key_on_count;
static uint64_t render_frames;
static uint64_t nonzero_frames;
static int32_t last_peak;
static int32_t peak;

/* End-block-reached latch (SPU register 0x1F801D9C/D9E on real hw).
 * Set when a voice decodes a block whose flag byte has bit 0 (loop end).
 * Polled by music engines to know when a one-shot voice has finished. */
static uint32_t endx_latch;

/* KEYON/KEYOFF latches: most recent values written, sticky across reads
 * so debug snapshots always see what the BIOS last requested even if the
 * BIOS clears them quickly. */
static uint32_t kon_latch;
static uint32_t koff_latch;

/* External vblank counter (debug_server.c) used as event timestamp. */
extern uint64_t s_frame_count;

/* ---- Always-on event ring -------------------------------------------- */
/* 1M entries × ~32B = 32 MB. Power of 2 so wrap is a mask. Beetle peaks at
 * a few hundred audible-voice events per chime second; recomp at <1k total
 * per chime. 1M gives ~minutes of headroom for game-scene capture too. */
#define SPU_EVENT_CAP (1u << 20)
static SpuEvent  s_events[SPU_EVENT_CAP];
static uint32_t  s_event_idx = 0;
static uint64_t  s_event_seq = 0;

/* CD input FIFO, fed by the CD-ROM XA decoder at 44.1 kHz stereo. */
#define SPU_CD_RING_FRAMES (44100u * 8u)
static int16_t  cd_ring[SPU_CD_RING_FRAMES * 2u];
static uint32_t cd_read_pos;
static uint32_t cd_write_pos;
static uint32_t cd_frame_count;
static uint64_t cd_push_frames;
static uint64_t cd_overflow_frames;
static uint64_t cd_underflow_frames;

/* Guest-clock output ring: samples produced by spu_advance, consumed by
 * spu_render for SDL.
 *
 * Latency budget (not a multi-second FIFO): if guest runs slightly ahead of
 * wall-clock (idle-skip, turbo, early frames), the ring would grow forever and
 * audio lag accumulates. We hard-cap lag and drop oldest samples so A/V stays
 * near real-time. */
#define SPU_CYCLES_PER_SAMPLE 768u   /* 33868800 / 44100 */
#define SPU_OUT_RING_FRAMES   (44100u * 1u)              /* 1 s capacity (hard) */
#define SPU_OUT_LAG_TARGET    (44100u * 40u / 1000u)     /* keep ~40 ms */
#define SPU_OUT_LAG_MAX       (44100u * 80u / 1000u)     /* drop down to target above this */
static int16_t  s_out_ring[SPU_OUT_RING_FRAMES * 2u];
static uint32_t s_out_rpos;
static uint32_t s_out_wpos;
static uint32_t s_out_count;
static uint32_t s_spu_cycle_accum;
static int16_t  s_last_out_l;
static int16_t  s_last_out_r;
static uint64_t s_out_overflow_frames;
static uint64_t s_out_underflow_frames;
static int16_t  s_last_cd_l;
static int16_t  s_last_cd_r;

/* ADSR phases — match Beetle's order so cross-process diffs read straight. */
#define ADSR_ATTACK   0
#define ADSR_DECAY    1
#define ADSR_SUSTAIN  2
#define ADSR_RELEASE  3

typedef struct {
    int active;
    uint32_t cur_addr;
    uint32_t repeat_addr;
    int16_t samples[SPU_BLOCK_SAMPLES];
    int sample_idx;
    uint32_t phase;
    int16_t hist1;
    int16_t hist2;
    uint8_t flags;

    /* ADSR envelope state. Stepped once per output sample (44.1 kHz). */
    uint16_t env_level;     /* 0..0x7FFF — applied to raw decoded sample */
    uint32_t adsr_divider;  /* fixed-point counter; level updates on overflow */
    uint8_t  adsr_phase;    /* ADSR_ATTACK / DECAY / SUSTAIN / RELEASE */
} SpuVoice;

static SpuVoice voices[SPU_VOICE_COUNT];

static void spu_event_record(uint8_t kind, int voice, uint32_t addr) {
    SpuEvent *e = &s_events[s_event_idx & (SPU_EVENT_CAP - 1u)];
    e->seq      = s_event_seq++;
    e->frame    = (uint32_t)s_frame_count;
    e->kind     = kind;
    e->voice    = (uint8_t)voice;
    e->pitch    = spu_regs[(uint32_t)voice * 8u + 2u];
    e->addr     = addr;
    /* PSX voice block layout (16-bit register indices from voice base):
     *   0=VOL_L 1=VOL_R 2=PITCH 3=START 4=ADSR_LO 5=ADSR_HI 6=CURVOL 7=LOOP */
    e->adsr_lo  = spu_regs[(uint32_t)voice * 8u + 4u];
    e->adsr_hi  = spu_regs[(uint32_t)voice * 8u + 5u];
    e->vol_l    = spu_regs[(uint32_t)voice * 8u + 0u];
    e->vol_r    = spu_regs[(uint32_t)voice * 8u + 1u];
    s_event_idx++;
}

/* PS1 envelope rate decoder. Ported verbatim from Beetle's CalcVCDelta
 * (beetle-psx/mednafen/psx/spu.cpp). Each call emits the per-step
 * `increment` to add to env_level, and `divinco` added to a divider —
 * level is updated only when divider crosses 0x8000. The combination
 * encodes both linear and pseudo-exponential ramps at PSX-faithful
 * rates (rates 0..127 span ~0.1 ms .. ~30+ s). */
static void calc_vc_delta(uint8_t zs, uint8_t speed, int log_mode, int dec_mode,
                          int inv_increment, int16_t current,
                          int *out_increment, int *out_divinco)
{
    int increment = (7 - (speed & 0x3));
    if (inv_increment) increment = ~increment;
    int divinco = 32768;

    if (speed < 0x2C)
        increment = (unsigned)increment << ((0x2F - speed) >> 2);
    if (speed >= 0x30)
        divinco >>= (speed - 0x2C) >> 2;

    if (log_mode) {
        if (dec_mode) {
            increment = (current * increment) >> 15;
        } else if ((current & 0x7FFF) >= 0x6000) {
            if (speed < 0x28) {
                increment >>= 2;
            } else if (speed >= 0x2C) {
                divinco >>= 2;
            } else {
                increment >>= 1;
                divinco   >>= 1;
            }
        }
    }

    if (divinco == 0 && speed < zs) divinco = 1;

    *out_increment = increment;
    *out_divinco   = divinco;
}

/* Step ADSR envelope by one output sample for voice `idx`. Mirrors
 * Beetle's PS_SPU::RunEnvelope. */
static void adsr_run(int idx, SpuVoice *v) {
    uint32_t raw = (uint32_t)spu_regs[(uint32_t)idx * 8u + 4u]
                 | ((uint32_t)spu_regs[(uint32_t)idx * 8u + 5u] << 16);

    int     Sl           = (int)(raw >> 0)  & 0x0F;
    int     Dr           = (int)(raw >> 4)  & 0x0F;
    int     Ar           = (int)(raw >> 8)  & 0x7F;
    int     attack_exp   = (int)((raw >> 15) & 1);
    int     Rr           = (int)(raw >> 16) & 0x1F;
    int     release_exp  = (int)((raw >> 21) & 1);
    int     Sr           = (int)(raw >> 22) & 0x7F;
    int     sustain_dec  = (int)((raw >> 30) & 1);
    int     sustain_exp  = (int)((raw >> 31) & 1);
    int     sustain_lvl  = (Sl + 1) << 11;

    /* Attack tops out at 0x7FFF — switch to Decay (Beetle does this
     * before the switch on Phase). */
    if (v->adsr_phase == ADSR_ATTACK && v->env_level == 0x7FFF)
        v->adsr_phase = ADSR_DECAY;

    int increment = 0, divinco = 0;
    int16_t uoflow_reset = 0;

    switch (v->adsr_phase) {
    case ADSR_ATTACK:
        calc_vc_delta(0x7F, (uint8_t)Ar, attack_exp, 0, 0,
                      (int16_t)v->env_level, &increment, &divinco);
        uoflow_reset = 0x7FFF;
        break;
    case ADSR_DECAY:
        calc_vc_delta(0x1F << 2, (uint8_t)(Dr << 2), 1, 1, 1,
                      (int16_t)v->env_level, &increment, &divinco);
        uoflow_reset = 0;
        break;
    case ADSR_SUSTAIN:
        calc_vc_delta(0x7F, (uint8_t)Sr, sustain_exp, sustain_dec, sustain_dec,
                      (int16_t)v->env_level, &increment, &divinco);
        uoflow_reset = sustain_dec ? 0 : 0x7FFF;
        break;
    case ADSR_RELEASE:
        calc_vc_delta(0x1F << 2, (uint8_t)(Rr << 2), release_exp, 1, 1,
                      (int16_t)v->env_level, &increment, &divinco);
        uoflow_reset = 0;
        break;
    default:
        return;
    }

    v->adsr_divider += (uint32_t)divinco;
    if (v->adsr_divider & 0x8000u) {
        uint16_t prev = v->env_level;
        v->adsr_divider = 0;
        v->env_level = (uint16_t)((int)v->env_level + increment);

        if (v->adsr_phase == ADSR_ATTACK) {
            /* If high bit just rolled over (0→1), clamp to uoflow_reset. */
            if (((prev ^ v->env_level) & v->env_level) & 0x8000u)
                v->env_level = (uint16_t)uoflow_reset;
        } else {
            if (v->env_level & 0x8000u)
                v->env_level = (uint16_t)uoflow_reset;
        }

        if (v->adsr_phase == ADSR_DECAY && v->env_level < (uint16_t)sustain_lvl)
            v->adsr_phase = ADSR_SUSTAIN;
    }
}

static inline int16_t clamp16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static inline int32_t abs32(int32_t v) {
    return v < 0 ? -v : v;
}

/* DuckStation / Mednafen ApplyVolume: (sample * vol) >> 15 */
static inline int32_t apply_volume15(int32_t sample, int16_t volume) {
    return (sample * (int32_t)volume) >> 15;
}

/* ---- SPU reverb (DuckStation ProcessReverb, scalar; psx-spx FIR) --------
 * Work area lives in SPU RAM from mBASE. Processed at effective 22050 Hz
 * (every other 44.1 kHz sample) with 20-tap half-band FIR up/down. */
#define SPU_REVERB_REGS 32
static uint16_t s_rev[SPU_REVERB_REGS];
static int16_t  s_vLOUT, s_vROUT;
static uint16_t s_mBASE;
static uint32_t s_reverb_base;     /* word address (halfword index / 4?) */
static uint32_t s_reverb_current;  /* advances each 22050 Hz tick */
static int16_t  s_rev_down[2][128];
static int16_t  s_rev_up[2][64];
static int      s_rev_pos;         /* 0..63 */
static uint32_t s_eon_latch;       /* 24-bit voice reverb enable */

/* rev[] layout matches DuckStation ReverbRegisters::rev */
#define RV_FB_SRC_A      s_rev[0]
#define RV_FB_SRC_B      s_rev[1]
#define RV_IIR_ALPHA     ((int16_t)s_rev[2])
#define RV_ACC_COEF_A    ((int16_t)s_rev[3])
#define RV_ACC_COEF_B    ((int16_t)s_rev[4])
#define RV_ACC_COEF_C    ((int16_t)s_rev[5])
#define RV_ACC_COEF_D    ((int16_t)s_rev[6])
#define RV_IIR_COEF      ((int16_t)s_rev[7])
#define RV_FB_ALPHA      ((int16_t)s_rev[8])
#define RV_FB_X          ((int16_t)s_rev[9])
#define RV_IIR_DEST_A(c) s_rev[10 + (c)]
#define RV_ACC_SRC_A(c)  s_rev[12 + (c)]
#define RV_ACC_SRC_B(c)  s_rev[14 + (c)]
#define RV_IIR_SRC_A(c)  s_rev[16 + (c)]
#define RV_IIR_DEST_B(c) s_rev[18 + (c)]
#define RV_ACC_SRC_C(c)  s_rev[20 + (c)]
#define RV_ACC_SRC_D(c)  s_rev[22 + (c)]
#define RV_IIR_SRC_B(c)  s_rev[24 + (c)]
#define RV_MIX_DEST_A(c) s_rev[26 + (c)]
#define RV_MIX_DEST_B(c) s_rev[28 + (c)]
#define RV_IN_COEF(c)    ((int16_t)s_rev[30 + (c)])

static void reverb_reset(void) {
    memset(s_rev, 0, sizeof(s_rev));
    s_vLOUT = s_vROUT = 0;
    s_mBASE = 0;
    s_reverb_base = s_reverb_current = 0;
    memset(s_rev_down, 0, sizeof(s_rev_down));
    memset(s_rev_up, 0, sizeof(s_rev_up));
    s_rev_pos = 0;
    s_eon_latch = 0;
}

/* DuckStation: address is in "reverb words"; RAM is byte-addressed. */
static uint32_t reverb_mem_addr(uint32_t address) {
    const uint32_t MASK = (SPU_RAM_SIZE - 1u) / 2u;
    uint32_t offset = s_reverb_current + (address & MASK);
    /* if offset wraps past end of RAM/2, add base (sign-extend trick) */
    offset += s_reverb_base & (uint32_t)((int32_t)(offset << 13) >> 31);
    return (offset & MASK) * 2u;
}

static int16_t reverb_read(uint32_t address, int32_t offset) {
    uint32_t real = reverb_mem_addr((address << 2) + (uint32_t)offset);
    return (int16_t)(spu_ram[real] | ((uint16_t)spu_ram[real + 1] << 8));
}

static void reverb_write(uint32_t address, int16_t data) {
    uint32_t real = reverb_mem_addr(address << 2);
    spu_ram[real]     = (uint8_t)((uint16_t)data & 0xFF);
    spu_ram[real + 1] = (uint8_t)(((uint16_t)data >> 8) & 0xFF);
}

static int32_t reverb_iiasm(int16_t insamp) {
    if (RV_IIR_ALPHA == (int16_t)-32768)
        return (insamp == (int16_t)-32768) ? 0 : (insamp * -65536);
    return insamp * (32768 - RV_IIR_ALPHA);
}

static int32_t reverb_neg(int32_t samp) {
    return (samp == -32768) ? 0x7FFF : -samp;
}

/* FIR coeffs (zeros removed from 39-tap halfband) — psx-spx / DuckStation. */
static const int32_t s_resample_coeff[20] = {
    -0x0001, 0x0002,  -0x000A, 0x0023,  -0x0067, 0x010A,  -0x0268, 0x0534,
    -0x0B90, 0x2806,  0x2806,  -0x0B90, 0x0534,  -0x0268, 0x010A,  -0x0067,
     0x0023, -0x000A, 0x0002,  -0x0001
};

static void process_reverb(int32_t left_in, int32_t right_in,
                           int32_t *left_out, int32_t *right_out,
                           int rev_master) {
    int16_t lin = clamp16(left_in);
    int16_t rin = clamp16(right_in);
    int pos = s_rev_pos;

    s_rev_down[0][pos | 0x00] = s_rev_down[0][pos | 0x40] = lin;
    s_rev_down[1][pos | 0x00] = s_rev_down[1][pos | 0x40] = rin;

    int32_t out[2];

    if (pos & 1) {
        int32_t downsampled[2];
        for (int ch = 0; ch < 2; ch++) {
            /* 39-tap halfband at 44.1 kHz; odd taps are 0 so only even indices
             * (compact table) + centre 0x4000 at src[19]. */
            const int16_t *src = &s_rev_down[ch][(pos - 38) & 0x3F];
            int32_t acc = 0;
            for (int k = 0; k < 20; k++)
                acc += s_resample_coeff[k] * (int32_t)src[k * 2];
            acc += 0x4000 * (int32_t)src[19];
            downsampled[ch] = clamp16(acc >> 15);
        }

        for (int ch = 0; ch < 2; ch++) {
            if (rev_master) {
                int32_t IIR_INPUT_A = clamp16(
                    (((reverb_read(RV_IIR_SRC_A(ch ^ 0), 0) * RV_IIR_COEF) >> 14) +
                     ((downsampled[ch] * RV_IN_COEF(ch)) >> 14)) >> 1);
                int32_t IIR_INPUT_B = clamp16(
                    (((reverb_read(RV_IIR_SRC_B(ch ^ 1), 0) * RV_IIR_COEF) >> 14) +
                     ((downsampled[ch] * RV_IN_COEF(ch)) >> 14)) >> 1);

                int32_t IIR_A = clamp16(
                    (((IIR_INPUT_A * RV_IIR_ALPHA) >> 14) +
                     (reverb_iiasm(reverb_read(RV_IIR_DEST_A(ch), -1)) >> 14)) >> 1);
                int32_t IIR_B = clamp16(
                    (((IIR_INPUT_B * RV_IIR_ALPHA) >> 14) +
                     (reverb_iiasm(reverb_read(RV_IIR_DEST_B(ch), -1)) >> 14)) >> 1);

                reverb_write(RV_IIR_DEST_A(ch), (int16_t)IIR_A);
                reverb_write(RV_IIR_DEST_B(ch), (int16_t)IIR_B);
            }

            int32_t ACC =
                ((reverb_read(RV_ACC_SRC_A(ch), 0) * RV_ACC_COEF_A) >> 14) +
                ((reverb_read(RV_ACC_SRC_B(ch), 0) * RV_ACC_COEF_B) >> 14) +
                ((reverb_read(RV_ACC_SRC_C(ch), 0) * RV_ACC_COEF_C) >> 14) +
                ((reverb_read(RV_ACC_SRC_D(ch), 0) * RV_ACC_COEF_D) >> 14);

            int32_t FB_A = reverb_read((uint16_t)(RV_MIX_DEST_A(ch) - RV_FB_SRC_A), 0);
            int32_t FB_B = reverb_read((uint16_t)(RV_MIX_DEST_B(ch) - RV_FB_SRC_B), 0);
            int32_t MDA = clamp16((ACC + ((FB_A * reverb_neg(RV_FB_ALPHA)) >> 14)) >> 1);
            int32_t MDB = clamp16(
                FB_A + ((((MDA * RV_FB_ALPHA) >> 14) +
                         ((FB_B * reverb_neg(RV_FB_X)) >> 14)) >> 1));

            int16_t samp22050 = clamp16(FB_B + ((MDB * RV_FB_X) >> 15));
            int up_i = (pos >> 1);
            s_rev_up[ch][up_i | 0x20] = s_rev_up[ch][up_i] = samp22050;

            if (rev_master) {
                reverb_write(RV_MIX_DEST_A(ch), (int16_t)MDA);
                reverb_write(RV_MIX_DEST_B(ch), (int16_t)MDB);
            }
        }

        s_reverb_current = (s_reverb_current + 1u) & 0x3FFFFu;
        if (s_reverb_current == 0)
            s_reverb_current = s_reverb_base;

        for (int ch = 0; ch < 2; ch++) {
            const int16_t *src = &s_rev_up[ch][(((pos >> 1) - 19) & 0x1F)];
            int32_t acc = 0;
            /* Upsample FIR: coeffs on even taps of halfband; compact form
             * multiplies consecutive samples in the 32-slot ring. */
            for (int k = 0; k < 20; k++)
                acc += s_resample_coeff[k] * (int32_t)src[k];
            out[ch] = acc >> 14;
            if (out[ch] > 32767) out[ch] = 32767;
            if (out[ch] < -32768) out[ch] = -32768;
        }
    } else {
        int idx = (((pos >> 1) - 19) & 0x1F) + 9;
        out[0] = s_rev_up[0][idx];
        out[1] = s_rev_up[1][idx];
    }

    s_rev_pos = (pos + 1) & 0x3F;
    *left_out  = apply_volume15(out[0], s_vLOUT);
    *right_out = apply_volume15(out[1], s_vROUT);
}

static inline uint32_t reg_index(uint32_t addr) {
    return (addr - 0x1F801C00u) >> 1;
}

static inline uint16_t voice_reg(int voice, int reg) {
    return spu_regs[(uint32_t)voice * 8u + (uint32_t)reg];
}

static inline int16_t direct_volume(uint16_t raw) {
    int32_t v;
    if (raw & 0x8000u) {
        /* Sweep mode is not modeled; use the magnitude as a direct volume. */
        v = (int32_t)(raw & 0x7FFFu);
    } else {
        v = (int32_t)(raw & 0x7FFFu);
        if (v & 0x4000) v -= 0x8000;
    }
    if (v > 0x3FFF) v = 0x3FFF;
    if (v < -0x4000) v = -0x4000;
    return (int16_t)v;
}

static inline int16_t cd_input_volume(uint16_t raw) {
    /* CD input volume registers use signed 16-bit linear gain; games commonly
     * program 0x7FFF for full-scale CD audio. */
    return (int16_t)raw;
}

void spu_cd_audio_reset(void) {
    memset(cd_ring, 0, sizeof(cd_ring));
    cd_read_pos = 0;
    cd_write_pos = 0;
    cd_frame_count = 0;
    cd_push_frames = 0;
    cd_overflow_frames = 0;
    cd_underflow_frames = 0;
}

void spu_cd_audio_push(const int16_t* stereo, int frames) {
    if (!stereo || frames <= 0) return;

    uint32_t in_frames = (uint32_t)frames;
    if (in_frames > SPU_CD_RING_FRAMES) {
        uint32_t skip = in_frames - SPU_CD_RING_FRAMES;
        stereo += skip * 2u;
        cd_overflow_frames += skip;
        in_frames = SPU_CD_RING_FRAMES;
    }

    if (cd_frame_count + in_frames > SPU_CD_RING_FRAMES) {
        uint32_t drop = (cd_frame_count + in_frames) - SPU_CD_RING_FRAMES;
        cd_read_pos = (cd_read_pos + drop) % SPU_CD_RING_FRAMES;
        cd_frame_count -= drop;
        cd_overflow_frames += drop;
    }

    for (uint32_t i = 0; i < in_frames; i++) {
        cd_ring[cd_write_pos * 2u + 0u] = stereo[i * 2u + 0u];
        cd_ring[cd_write_pos * 2u + 1u] = stereo[i * 2u + 1u];
        cd_write_pos = (cd_write_pos + 1u) % SPU_CD_RING_FRAMES;
    }
    cd_frame_count += in_frames;
    cd_push_frames += in_frames;
}

static int cd_audio_pop(int16_t* left, int16_t* right) {
    if (cd_frame_count == 0) return 0;
    *left = cd_ring[cd_read_pos * 2u + 0u];
    *right = cd_ring[cd_read_pos * 2u + 1u];
    cd_read_pos = (cd_read_pos + 1u) % SPU_CD_RING_FRAMES;
    cd_frame_count--;
    return 1;
}

static void decode_block(SpuVoice *v) {
    static const int f0[5] = { 0, 60, 115, 98, 122 };
    static const int f1[5] = { 0, 0, -52, -55, -60 };

    uint32_t addr = v->cur_addr & (SPU_RAM_SIZE - 1u);
    if (addr + 16u > SPU_RAM_SIZE) addr = 0;

    uint8_t header = spu_ram[addr + 0u];
    uint8_t flags = spu_ram[addr + 1u];
    int shift = header & 0x0F;
    int filter = (header >> 4) & 0x0F;
    if (filter > 4) filter = 0;
    if (shift > 12) shift = 12;

    int out = 0;
    for (int b = 0; b < 14; b++) {
        uint8_t packed = spu_ram[addr + 2u + (uint32_t)b];
        for (int n = 0; n < 2; n++) {
            int sample4 = (n == 0) ? (packed & 0x0F) : (packed >> 4);
            if (sample4 & 0x08) sample4 -= 0x10;

            int32_t s = sample4 << 12;
            s >>= shift;
            s += ((int32_t)v->hist1 * f0[filter] +
                  (int32_t)v->hist2 * f1[filter] + 32) >> 6;
            s = clamp16(s);
            v->hist2 = v->hist1;
            v->hist1 = (int16_t)s;
            v->samples[out++] = (int16_t)s;
        }
    }

    if (flags & 0x04u) v->repeat_addr = addr;
    v->flags = flags;
    v->sample_idx = 0;
    v->cur_addr = (addr + 16u) & (SPU_RAM_SIZE - 1u);

    /* Latch end-block-reached so the BIOS music engine sees ENDX[v] = 1
     * when it polls 0x1F801D9C/D9E. Without this latch one-shot music
     * engines never advance, leaving subsequent voices unkeyed. */
    if (flags & 0x01u) {
        int v_idx = (int)(v - voices);
        if (v_idx >= 0 && v_idx < SPU_VOICE_COUNT) {
            endx_latch |= (1u << v_idx);
        }
    }
}

/* ---- Verified-enhancement shadow tap (opt-in; see spu_shadow.{h,c}) ------
 *
 * When the float SPU shadow is enabled, the mix loop records — per output
 * frame, per contributing voice — the EXACT inputs the canon used to produce
 * that voice's contribution: the four decoded samples bracketing the current
 * sub-sample phase, the fractional phase, the envelope level, and the per-voice
 * L/R volumes. The shadow re-renders those in float with cubic interpolation.
 * Sourcing the decoded data + envelope from the canon means the shadow can only
 * differ in HOW a note is resampled, never in WHICH note plays.
 *
 * Inert and zero-cost when the shadow is disabled (s_shadow_tap_on == 0): the
 * mix loop's recording is guarded by that flag, set once from spu_render. */
typedef struct {
    int16_t  s[4];     /* decoded samples at sample_idx-1 .. sample_idx+2 */
    float    frac;     /* fractional phase in [0,1) at this output frame */
    uint16_t env;      /* env_level (0..0x7FFF) */
    int16_t  vol_l;    /* per-voice volume (already direct_volume-decoded) */
    int16_t  vol_r;
    uint8_t  active;
} SpuShadowVoiceTap;

/* One frame's worth of per-voice taps, plus the global scale used this block. */
typedef struct {
    SpuShadowVoiceTap voice[SPU_VOICE_COUNT];
    int16_t main_l;
    int16_t main_r;
    int     enabled;   /* SPU control enable bit this block */
} SpuShadowFrameTap;

/* Sized to the largest spu_render block (main.cpp caps at 2048 frames). */
#define SPU_SHADOW_TAP_FRAMES 2048
static SpuShadowFrameTap s_shadow_tap[SPU_SHADOW_TAP_FRAMES];
static int               s_shadow_tap_on = 0;   /* set by spu_render when enabled */
static int               s_shadow_tap_frame = 0;

/* The shadow reads s_shadow_tap as SpuShadowFrameTapPub[] (spu.h). Guarantee
 * the internal and public layouts are identical so the cast is safe. */
typedef char spu_shadow_tap_voice_size_check[
    (sizeof(SpuShadowVoiceTap) == sizeof(SpuShadowVoiceTapPub)) ? 1 : -1];
typedef char spu_shadow_tap_frame_size_check[
    (sizeof(SpuShadowFrameTap) == sizeof(SpuShadowFrameTapPub)) ? 1 : -1];
typedef char spu_shadow_voice_count_check[
    (SPU_VOICE_COUNT == SPU_SHADOW_MAX_VOICES) ? 1 : -1];

const void* spu_shadow_tap_buffer(void) { return s_shadow_tap; }
int         spu_shadow_tap_count(void)  { return s_shadow_tap_frame; }

static int16_t voice_next_sample(int idx) {
    SpuVoice *v = &voices[idx];
    if (!v->active) return 0;

    if (v->sample_idx >= SPU_BLOCK_SAMPLES) {
        if (v->flags & 0x01u) {
            if (v->flags & 0x02u) {
                v->cur_addr = v->repeat_addr & (SPU_RAM_SIZE - 1u);
                spu_event_record(SPU_EV_END_LOOP, idx, v->repeat_addr);
            } else {
                /* End-without-repeat triggers Release on real hardware
                 * (Beetle's RunDecoder calls ReleaseEnvelope here). The
                 * voice keeps decoding past the end block — whatever
                 * follows in SPU RAM — while env_level decays to 0.
                 * Garbage samples are masked by the dying envelope, so
                 * by the time anything would be audible it's silent. */
                spu_event_record(SPU_EV_END_STOP, idx, v->cur_addr);
                v->adsr_phase = ADSR_RELEASE;
                v->adsr_divider = 0;
                v->flags = 0;  /* don't re-enter this branch */
            }
        }
        decode_block(v);
    }

    int16_t raw_s = v->samples[v->sample_idx];
    /* Apply envelope (0..0x7FFF as a 15-bit gain). */
    int32_t shaped = ((int32_t)raw_s * (int32_t)v->env_level) >> 15;
    if (shaped > 32767)  shaped = 32767;
    if (shaped < -32768) shaped = -32768;

    /* Shadow tap: record the four decoded samples bracketing the current
     * sample position + the fractional phase + envelope, BEFORE the phase
     * advances. The shadow re-interpolates these in float. Cross-block
     * neighbours clamp to the block edge (the canon never interpolates across
     * blocks either; documented in docs/SHADOW_ENHANCEMENTS.md). */
    if (s_shadow_tap_on && s_shadow_tap_frame < SPU_SHADOW_TAP_FRAMES) {
        SpuShadowVoiceTap *t =
            &s_shadow_tap[s_shadow_tap_frame].voice[idx];
        int si = v->sample_idx;
        int i0 = si - 1, i2 = si + 1, i3 = si + 2;
        if (i0 < 0) i0 = 0;
        if (i2 >= SPU_BLOCK_SAMPLES) i2 = SPU_BLOCK_SAMPLES - 1;
        if (i3 >= SPU_BLOCK_SAMPLES) i3 = SPU_BLOCK_SAMPLES - 1;
        t->s[0] = v->samples[i0];
        t->s[1] = v->samples[si];
        t->s[2] = v->samples[i2];
        t->s[3] = v->samples[i3];
        t->frac = (float)v->phase / 4096.0f;
        t->env  = v->env_level;
        t->active = 1;
    }

    /* Step envelope once per output sample (44.1 kHz). */
    adsr_run(idx, v);
    /* Deactivate once Release fully decays to silence. */
    if (v->adsr_phase == ADSR_RELEASE && v->env_level == 0) {
        v->active = 0;
    }

    uint32_t pitch = voice_reg(idx, 2) & 0x3FFFu;
    if (pitch == 0) pitch = 0x1000u;
    v->phase += pitch;
    while (v->phase >= 0x1000u) {
        v->phase -= 0x1000u;
        v->sample_idx++;
        if (v->sample_idx >= SPU_BLOCK_SAMPLES) break;
    }
    return (int16_t)shaped;
}

static void key_on(uint32_t mask) {
    for (int i = 0; i < SPU_VOICE_COUNT; i++) {
        if (!(mask & (1u << i))) continue;
        SpuVoice *v = &voices[i];
        memset(v, 0, sizeof(*v));
        v->active = 1;
        v->cur_addr = ((uint32_t)voice_reg(i, 3) << 3) & (SPU_RAM_SIZE - 1u);
        v->repeat_addr = ((uint32_t)voice_reg(i, 7) << 3) & (SPU_RAM_SIZE - 1u);
        v->sample_idx = SPU_BLOCK_SAMPLES;
        /* Reset ADSR — KEYON starts envelope at 0 in Attack phase
         * (matches Beetle's PS_SPU::ResetEnvelope). */
        v->env_level = 0;
        v->adsr_divider = 0;
        v->adsr_phase = ADSR_ATTACK;
        key_on_count++;
        endx_latch &= ~(1u << i);  /* KEYON clears ENDX bit on real hw */
        spu_event_record(SPU_EV_KEYON, i, v->cur_addr);
    }
}

/* KEYOFF triggers Release phase, NOT immediate silence. The voice
 * keeps voicing while env_level decays from its current value to 0
 * at the configured Release rate. This is the boot-chime fade tail —
 * silencing immediately is what made channels appear to "cut out". */
static void key_off(uint32_t mask) {
    for (int i = 0; i < SPU_VOICE_COUNT; i++) {
        if (!(mask & (1u << i))) continue;
        if (!voices[i].active) continue;
        spu_event_record(SPU_EV_KEYOFF, i, voices[i].cur_addr);
        voices[i].adsr_phase = ADSR_RELEASE;
        voices[i].adsr_divider = 0;
        /* env_level preserved — release decays from wherever we are now. */
    }
}

void spu_init(void) {
    memset(spu_ram, 0, sizeof(spu_ram));
    memset(spu_regs, 0, sizeof(spu_regs));
    memset(voices, 0, sizeof(voices));
    memset(s_events, 0, sizeof(s_events));
    transfer_addr = 0;
    key_on_count = 0;
    render_frames = 0;
    nonzero_frames = 0;
    last_peak = 0;
    peak = 0;
    endx_latch = 0;
    kon_latch = 0;
    koff_latch = 0;
    s_event_idx = 0;
    s_event_seq = 0;
    spu_cd_audio_reset();
    s_shadow_tap_on = 0;
    s_shadow_tap_frame = 0;
    s_out_rpos = s_out_wpos = s_out_count = 0;
    s_spu_cycle_accum = 0;
    s_last_out_l = s_last_out_r = 0;
    s_out_overflow_frames = s_out_underflow_frames = 0;
    s_last_cd_l = s_last_cd_r = 0;
    reverb_reset();
    spu_shadow_reset();
}

/* Mix one stereo sample at guest 44.1 kHz (one SPU step). */
static void spu_mix_one_sample(int16_t* out_l, int16_t* out_r) {
    uint16_t ctrl = spu_regs[reg_index(0x1F801DAAu)];
    int enabled = (ctrl & 0x8000u) != 0;
    int mute_n  = (ctrl & 0x4000u) != 0; /* 0 = muted */
    int16_t main_l = direct_volume(spu_regs[reg_index(0x1F801D80u)]);
    int16_t main_r = direct_volume(spu_regs[reg_index(0x1F801D82u)]);
    int16_t cd_vol_l = cd_input_volume(spu_regs[reg_index(0x1F801DB0u)]);
    int16_t cd_vol_r = cd_input_volume(spu_regs[reg_index(0x1F801DB2u)]);

    int do_shadow = spu_shadow_enabled() ? 1 : 0;
    s_shadow_tap_on = do_shadow;
    s_shadow_tap_frame = 0;
    if (do_shadow) {
        memset(&s_shadow_tap[0], 0, sizeof(s_shadow_tap[0]));
    }

    int32_t mix_l = 0;
    int32_t mix_r = 0;
    int32_t rev_in_l = 0;
    int32_t rev_in_r = 0;
    uint32_t eon = s_eon_latch;

    if (enabled) {
        for (int v = 0; v < SPU_VOICE_COUNT; v++) {
            int16_t s = voice_next_sample(v);
            int16_t vl = direct_volume(voice_reg(v, 0));
            int16_t vr = direct_volume(voice_reg(v, 1));
            if (do_shadow) {
                SpuShadowVoiceTap *t = &s_shadow_tap[0].voice[v];
                t->vol_l = vl;
                t->vol_r = vr;
            }
            if (!s) continue;
            int32_t left  = ((int32_t)s * vl) >> 14;
            int32_t right = ((int32_t)s * vr) >> 14;
            mix_l += left;
            mix_r += right;
            if (eon & (1u << v)) {
                rev_in_l += left;
                rev_in_r += right;
            }
        }
        if (ctrl & 0x0001u) {
            int16_t cd_l = 0;
            int16_t cd_r = 0;
            if (cd_audio_pop(&cd_l, &cd_r)) {
                s_last_cd_l = cd_l;
                s_last_cd_r = cd_r;
            } else if (cd_push_frames != 0) {
                cd_underflow_frames++;
                cd_l = s_last_cd_l;
                cd_r = s_last_cd_r;
            } else {
                cd_l = cd_r = 0;
            }
            int32_t cdl = ((int32_t)cd_l * cd_vol_l) >> 15;
            int32_t cdr = ((int32_t)cd_r * cd_vol_r) >> 15;
            mix_l += cdl;
            mix_r += cdr;
            /* SPUCNT bit2: CD audio to reverb */
            if (ctrl & 0x0004u) {
                rev_in_l += cdl;
                rev_in_r += cdr;
            }
        }
        if (!mute_n) {
            mix_l = mix_r = 0;
            rev_in_l = rev_in_r = 0;
        }

        int32_t rev_out_l = 0, rev_out_r = 0;
        process_reverb(rev_in_l, rev_in_r, &rev_out_l, &rev_out_r,
                       (ctrl & 0x0080u) != 0);
        mix_l += rev_out_l;
        mix_r += rev_out_r;

        mix_l = (clamp16(mix_l) * main_l) >> 14;
        mix_r = (clamp16(mix_r) * main_r) >> 14;
    }

    int16_t ol = clamp16(mix_l);
    int16_t or_ = clamp16(mix_r);

    if (do_shadow) {
        s_shadow_tap[0].main_l = main_l;
        s_shadow_tap[0].main_r = main_r;
        s_shadow_tap[0].enabled = enabled;
        s_shadow_tap_frame = 1;
        int16_t pair[2] = { ol, or_ };
        spu_shadow_process(pair, 1);
        ol = pair[0];
        or_ = pair[1];
    }

    int32_t frame_peak = abs32(ol);
    int32_t right_peak = abs32(or_);
    if (right_peak > frame_peak) frame_peak = right_peak;
    if (frame_peak) nonzero_frames++;
    if (frame_peak > last_peak) last_peak = frame_peak;
    if (frame_peak > peak) peak = frame_peak;

    *out_l = ol;
    *out_r = or_;
    render_frames++;
}

static void spu_out_drop_oldest(void) {
    if (s_out_count == 0) return;
    s_out_rpos = (s_out_rpos + 1u) % SPU_OUT_RING_FRAMES;
    s_out_count--;
    s_out_overflow_frames++;
}

/* Keep the guest→host ring inside the latency budget. */
static void spu_out_trim_lag(void) {
    if (s_out_count <= SPU_OUT_LAG_MAX) return;
    while (s_out_count > SPU_OUT_LAG_TARGET) {
        spu_out_drop_oldest();
    }
}

static void spu_out_push(int16_t l, int16_t r) {
    if (s_out_count >= SPU_OUT_RING_FRAMES) {
        spu_out_drop_oldest();
    }
    s_out_ring[s_out_wpos * 2u + 0u] = l;
    s_out_ring[s_out_wpos * 2u + 1u] = r;
    s_out_wpos = (s_out_wpos + 1u) % SPU_OUT_RING_FRAMES;
    s_out_count++;
    s_last_out_l = l;
    s_last_out_r = r;
    spu_out_trim_lag();
}

void spu_advance(uint32_t cycles) {
    if (cycles == 0) return;
    s_spu_cycle_accum += cycles;
    /* Cap catch-up: at most ~100 ms of new samples per charge. */
    uint32_t max_samples = SPU_OUT_LAG_MAX + (44100u / 30u);
    uint32_t produced = 0;
    while (s_spu_cycle_accum >= SPU_CYCLES_PER_SAMPLE && produced < max_samples) {
        s_spu_cycle_accum -= SPU_CYCLES_PER_SAMPLE;
        int16_t l, r;
        spu_mix_one_sample(&l, &r);
        spu_out_push(l, r);
        produced++;
    }
    if (s_spu_cycle_accum >= SPU_CYCLES_PER_SAMPLE) {
        s_spu_cycle_accum %= SPU_CYCLES_PER_SAMPLE;
    }
}

uint32_t spu_output_frames_ready(void) {
    return s_out_count;
}

void spu_render(int16_t* out_stereo, int frames) {
    if (!out_stereo || frames <= 0) return;

    /* Host consumer only — guest state was already stepped in spu_advance. */
    spu_out_trim_lag();
    for (int f = 0; f < frames; f++) {
        if (s_out_count > 0) {
            out_stereo[f * 2 + 0] = s_out_ring[s_out_rpos * 2u + 0u];
            out_stereo[f * 2 + 1] = s_out_ring[s_out_rpos * 2u + 1u];
            s_out_rpos = (s_out_rpos + 1u) % SPU_OUT_RING_FRAMES;
            s_out_count--;
            s_last_out_l = out_stereo[f * 2 + 0];
            s_last_out_r = out_stereo[f * 2 + 1];
        } else {
            /* Underrun: hold last guest sample (same policy as XA ring). */
            s_out_underflow_frames++;
            out_stereo[f * 2 + 0] = s_last_out_l;
            out_stereo[f * 2 + 1] = s_last_out_r;
        }
    }
}

void spu_debug_info(SpuDebugInfo* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->ctrl = spu_regs[reg_index(0x1F801DAAu)];
    out->main_l = direct_volume(spu_regs[reg_index(0x1F801D80u)]);
    out->main_r = direct_volume(spu_regs[reg_index(0x1F801D82u)]);
    out->cd_l = cd_input_volume(spu_regs[reg_index(0x1F801DB0u)]);
    out->cd_r = cd_input_volume(spu_regs[reg_index(0x1F801DB2u)]);
    for (int i = 0; i < SPU_VOICE_COUNT; i++) {
        if (voices[i].active) out->active_mask |= (1u << i);
    }
    out->key_on_count = key_on_count;
    out->render_frames = render_frames;
    out->nonzero_frames = nonzero_frames;
    out->last_peak = last_peak;
    out->peak = peak;
    out->cd_frames = cd_frame_count;
    out->cd_push_frames = cd_push_frames;
    out->cd_overflow_frames = cd_overflow_frames + s_out_overflow_frames;
    out->cd_underflow_frames = cd_underflow_frames + s_out_underflow_frames;
}

uint32_t spu_read(uint32_t addr) {
    if (addr >= 0x1F801C00u && addr <= 0x1F801DFFu) {
        uint32_t idx = reg_index(addr);
        if (idx < SPU_REG_COUNT) {
            if (addr == 0x1F801DAEu) {
                return 0x0400; /* SPUSTAT: ready */
            }
            /* ENDX (end-block-reached latch). Real hw sets bit v when voice
             * v decodes a block whose flag byte has bit 0; KEYON[v] clears
             * it. Without this latch, music engines that poll ENDX to wait
             * for a sample to finish never advance and downstream voices
             * never get keyed on. */
            if (addr == 0x1F801D9Cu) {
                return endx_latch & 0xFFFFu;
            }
            if (addr == 0x1F801D9Eu) {
                return (endx_latch >> 16) & 0xFFu;
            }
            /* Voice register 6 (byte offset 0x0C) is CURVOL / ADSR_LEVEL —
             * returns the live envelope level. PSX music engines poll this
             * to pick a "free" voice (env_level == 0). Without exposing the
             * real envelope, the BIOS sees every voice as silent and over-
             * recycles voices 0-3 instead of fanning across all 24. */
            if (addr >= 0x1F801C00u && addr < 0x1F801D80u
                && (idx & 7u) == 6u) {
                int v = (int)(idx >> 3);
                if (v >= 0 && v < SPU_VOICE_COUNT)
                    return voices[v].env_level;
            }
            return spu_regs[idx];
        }
    }

    return 0;
}

void spu_write(uint32_t addr, uint32_t value) {
    if (addr >= 0x1F801C00u && addr <= 0x1F801DFFu) {
        uint32_t idx = reg_index(addr);
        if (idx < SPU_REG_COUNT) {
            spu_regs[idx] = (uint16_t)value;

            if (addr == 0x1F801D88u) {
                kon_latch = (kon_latch & 0xFFFF0000u) | (uint32_t)(uint16_t)value;
                key_on((uint32_t)(uint16_t)value);
            }
            if (addr == 0x1F801D8Au) {
                kon_latch = (kon_latch & 0x0000FFFFu) | ((uint32_t)(uint16_t)value << 16);
                key_on((uint32_t)(uint16_t)value << 16);
            }
            if (addr == 0x1F801D8Cu) {
                koff_latch = (koff_latch & 0xFFFF0000u) | (uint32_t)(uint16_t)value;
                key_off((uint32_t)(uint16_t)value);
            }
            if (addr == 0x1F801D8Eu) {
                koff_latch = (koff_latch & 0x0000FFFFu) | ((uint32_t)(uint16_t)value << 16);
                key_off((uint32_t)(uint16_t)value << 16);
            }

            /* EON — per-voice reverb enable (24 bits). */
            if (addr == 0x1F801D98u) {
                s_eon_latch = (s_eon_latch & 0xFFFF0000u) | (uint32_t)(uint16_t)value;
            }
            if (addr == 0x1F801D9Au) {
                s_eon_latch = (s_eon_latch & 0x0000FFFFu) | ((uint32_t)(uint16_t)value << 16);
            }

            /* Reverb output volumes + work-area base. */
            if (addr == 0x1F801D84u) s_vLOUT = (int16_t)(uint16_t)value;
            if (addr == 0x1F801D86u) s_vROUT = (int16_t)(uint16_t)value;
            if (addr == 0x1F801DA2u) {
                s_mBASE = (uint16_t)value;
                s_reverb_base = ((uint32_t)value << 2) & 0x3FFFFu;
                s_reverb_current = s_reverb_base;
            }

            /* Reverb configuration block 1F801DC0..1F801DFF (32 halfwords). */
            if (addr >= 0x1F801DC0u && addr <= 0x1F801DFEu) {
                uint32_t ri = (addr - 0x1F801DC0u) / 2u;
                if (ri < SPU_REVERB_REGS)
                    s_rev[ri] = (uint16_t)value;
            }

            if (addr == 0x1F801DA6u) {
                transfer_addr = ((uint32_t)(uint16_t)value) << 3;
                if (transfer_addr >= SPU_RAM_SIZE) transfer_addr = 0;
            }

            if (addr == 0x1F801DA8u) {
                if (transfer_addr + 1 < SPU_RAM_SIZE) {
                    spu_ram[transfer_addr]     = (uint8_t)(value & 0xFF);
                    spu_ram[transfer_addr + 1] = (uint8_t)((value >> 8) & 0xFF);
                }
                transfer_addr = (transfer_addr + 2) % SPU_RAM_SIZE;
            }
        }
    }
}

void spu_dma_write(uint32_t word) {
    if (transfer_addr + 3 < SPU_RAM_SIZE) {
        spu_ram[transfer_addr]     = (uint8_t)(word & 0xFF);
        spu_ram[transfer_addr + 1] = (uint8_t)((word >> 8) & 0xFF);
        spu_ram[transfer_addr + 2] = (uint8_t)((word >> 16) & 0xFF);
        spu_ram[transfer_addr + 3] = (uint8_t)((word >> 24) & 0xFF);
    }
    transfer_addr = (transfer_addr + 4) % SPU_RAM_SIZE;
}

int spu_dma_ready(void) {
    return 1;
}

const uint8_t* spu_get_ram(void) {
    return spu_ram;
}

void spu_get_voice_state(int idx, SpuVoiceState* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (idx < 0 || idx >= SPU_VOICE_COUNT) return;
    SpuVoice *v = &voices[idx];
    out->active      = v->active;
    out->vol_ctrl_l  = voice_reg(idx, 0);
    out->vol_ctrl_r  = voice_reg(idx, 1);
    out->pitch       = voice_reg(idx, 2);
    out->start_lo    = voice_reg(idx, 3);
    out->adsr_lo     = voice_reg(idx, 4);
    out->adsr_hi     = voice_reg(idx, 5);
    out->loop_lo     = voice_reg(idx, 7);
    out->cur_addr    = v->cur_addr;
    out->repeat_addr = v->repeat_addr;
    out->last_flags  = v->flags;
    out->sample_idx  = (uint8_t)v->sample_idx;
    out->phase       = (uint16_t)v->phase;
}

void spu_get_global_state(SpuGlobalState* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->ctrl       = spu_regs[reg_index(0x1F801DAAu)];
    out->main_vol_l = spu_regs[reg_index(0x1F801D80u)];
    out->main_vol_r = spu_regs[reg_index(0x1F801D82u)];
    out->kon_latch  = kon_latch & 0xFFFFFFu;
    out->koff_latch = koff_latch & 0xFFFFFFu;
    out->pmon = (uint32_t)spu_regs[reg_index(0x1F801D90u)] |
                ((uint32_t)spu_regs[reg_index(0x1F801D92u)] << 16);
    out->non  = (uint32_t)spu_regs[reg_index(0x1F801D94u)] |
                ((uint32_t)spu_regs[reg_index(0x1F801D96u)] << 16);
    out->eon  = s_eon_latch;
    out->endx = endx_latch & 0xFFFFFFu;
    uint32_t am = 0;
    for (int i = 0; i < SPU_VOICE_COUNT; i++)
        if (voices[i].active) am |= (1u << i);
    out->active_mask = am;
}

uint64_t spu_event_total(void) { return s_event_seq; }

uint32_t spu_event_get(SpuEvent* out, uint32_t max_count) {
    if (!out || max_count == 0) return 0;
    uint64_t avail = s_event_seq < (uint64_t)SPU_EVENT_CAP
                     ? s_event_seq : (uint64_t)SPU_EVENT_CAP;
    if ((uint64_t)max_count > avail) max_count = (uint32_t)avail;
    /* Most recent N, oldest first. */
    uint32_t start = (s_event_idx + SPU_EVENT_CAP - max_count) & (SPU_EVENT_CAP - 1u);
    for (uint32_t i = 0; i < max_count; i++) {
        out[i] = s_events[(start + i) & (SPU_EVENT_CAP - 1u)];
    }
    return max_count;
}

void spu_event_reset(void) {
    s_event_idx = 0;
    s_event_seq = 0;
    memset(s_events, 0, sizeof(s_events));
}

/* ---- boot snapshot: complete SPU register + voice state (see boot_state.h) ---- */
#define SPU_SNAP_FIELDS(X) \
    X(spu_regs) X(voices) X(transfer_addr) X(key_on_count) \
    X(endx_latch) X(kon_latch) X(koff_latch)
uint32_t spu_snapshot_bytes(void){ uint32_t n=0;
#define X(f) n += (uint32_t)sizeof(f);
    SPU_SNAP_FIELDS(X)
#undef X
    return n; }
void spu_snapshot_write(uint8_t *p){
#define X(f) memcpy(p,&(f),sizeof(f)); p+=sizeof(f);
    SPU_SNAP_FIELDS(X)
#undef X
}
int spu_snapshot_read(const uint8_t *p, uint32_t len){ if(len!=spu_snapshot_bytes()) return 0;
#define X(f) memcpy(&(f),p,sizeof(f)); p+=sizeof(f);
    SPU_SNAP_FIELDS(X)
#undef X
    return 1; }
uint8_t*  spu_get_ram_ptr(void){ return spu_ram; }
uint32_t  spu_get_ram_bytes(void){ return (uint32_t)sizeof(spu_ram); }
