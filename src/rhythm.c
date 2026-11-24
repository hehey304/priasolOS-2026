#include <stdint.h>
#include <stdbool.h>
#include "header/stdlib/syscall_nums.h"

#define MAX_NOTES     1200
#define MAX_STEPS     1200
#define MAX_HP        5
#define TRACK_LEFT    19
#define HIT_COL       25
#define TRACK_RIGHT   77

#define WIN_PERFECT   95
#define WIN_GOOD      200
#define WIN_MISS_TAP  240
#define DUAL_SPAM_WIN 110

#define NOTE_TAP      0
#define NOTE_DUAL     1
#define NOTE_HOLD     2
#define NOTE_SPAM     3
#define NOTE_HEART    4

#define AUDIO_REST    0
#define AUDIO_TAP     1
#define AUDIO_CHORD   2
#define AUDIO_HOLD    3
#define AUDIO_BASS    4

#define JUDGE_NONE    0
#define JUDGE_PERFECT 1
#define JUDGE_GOOD    2
#define JUDGE_MISS    3
#define JUDGE_HEAL    4
#define JUDGE_BURST   5
#define JUDGE_MASH    6
#define JUDGE_EMPTY   7

#define SC_1          0x02
#define SC_2          0x03
#define SC_3          0x04
#define SC_4          0x05
#define SC_Q          0x10
#define SC_R          0x13
#define SC_A          0x1E
#define SC_D          0x20
#define SC_F          0x21
#define SC_G          0x22
#define SC_J          0x24
#define SC_V          0x2F
#define SC_M          0x32

#define COL_BG        0x07
#define COL_WHITE     0x0F
#define COL_GRAY      0x08
#define COL_CYAN      0x0B
#define COL_PINK      0x0D
#define COL_YELLOW    0x0E
#define COL_GREEN     0x0A
#define COL_RED       0x0C
#define COL_NOTE_TOP  0x3F
#define COL_NOTE_BOT  0x5F
#define COL_NOTE_DUAL 0x2F
#define COL_NOTE_HRT  0x4F
#define COL_HOLD_ON   0x6F
#define COL_HIT_ON    0xE0
#define COL_FEVER_BG  0x6E

#define N_REST 0
#define N_A2   110
#define N_B2   123
#define N_C3   131
#define N_D3   147
#define N_E3   165
#define N_F3   175
#define N_G3   196
#define N_A3   220
#define N_B3   247
#define N_C4   262
#define N_D4   294
#define N_E4   330
#define N_F4   349
#define N_FS4  370
#define N_G4   392
#define N_A4   440
#define N_AS4  466
#define N_B4   494
#define N_C5   523
#define N_CS5  554
#define N_D5   587
#define N_E5   659
#define N_F5   698
#define N_FS5  740
#define N_G5   784
#define N_A5   880
#define N_B5   988
#define N_C6   1047
#define N_D6   1175
#define N_E6   1319

#define R(b)      { '.', (b), 0 }
#define TOP(f)    { '<', (f), 0 }
#define BOT(f)    { '>', (f), 0 }
#define DUAL(f,a) { '=', (f), (a) }
#define HT(f)     { 'H', (f), 0 }
#define HB(f)     { 'h', (f), 0 }
#define HLDT(f,s) { '[', (f), (s) }
#define HLDB(f,s) { ']', (f), (s) }
#define HLDD(f,s) { 'B', (f), (s) }
#define SPMT(f,k) { 'T', (f), (k) }
#define SPMB(f,k) { 'b', (f), (k) }
#define SPMD(f,k) { 'S', (f), (k) }

struct SongStep {
    char     token;
    uint16_t freq;
    uint16_t param;
};

#include "robin_chart.h"

struct BeatAudio {
    uint16_t freq;
    uint16_t alt_freq;
    uint8_t  style;
    int16_t  note_idx;
};

struct Note {
    uint32_t time_ms;
    uint32_t duration_ms;
    uint16_t base_freq;
    uint8_t  type;
    uint8_t  lane;
    uint8_t  hits_total;
    uint8_t  hits_left;
    uint8_t  state;
    uint8_t  chord_mask;
    uint32_t sub_tick;
};

static const struct SongStep chart_normal[] = {
    DUAL(N_C5, N_G5), R(0), BOT(N_E5), R(N_C3), TOP(N_G5), R(0), BOT(N_E5), R(N_G3),
    TOP(N_A5), R(0), TOP(N_G5), R(N_F3), HLDT(N_E5, 3), R(0), R(0), R(N_G3),
    BOT(N_D5), R(N_G3), BOT(N_E5), R(0), TOP(N_G5), R(N_G3), BOT(N_E5), R(0),
    DUAL(N_C5, N_E5), R(0), BOT(N_D5), R(N_C3), HLDB(N_C5, 3), R(0), R(0), R(N_G3),

    TOP(N_A5), R(N_A2), TOP(N_G5), R(0), BOT(N_E5), R(N_A2), HT(N_A5), R(0),
    SPMT(N_C5, 3), R(0), R(N_F3), R(0), R(N_G3), R(0), R(0), R(0),
    TOP(N_G5), BOT(N_E5), TOP(N_G5), R(N_C3), TOP(N_A5), BOT(N_E5), TOP(N_G5), R(N_G3),
    DUAL(N_G5, N_C6), R(0), HLDT(N_A5, 3), R(0), R(0), R(N_F3), BOT(N_G5), R(0),

    BOT(N_E5), R(N_C3), TOP(N_G5), R(0), BOT(N_D5), R(N_G3), HB(N_E5), R(0),
    SPMD(N_C5, 4), R(0), R(N_C3), R(0), R(N_G3), R(0), R(N_A2), R(0),
    TOP(N_G5), BOT(N_E5), TOP(N_A5), BOT(N_G5), DUAL(N_C5, N_C6), R(0), HLDD(N_C6, 3), R(0),
    R(0), R(0)
};

static const struct SongStep chart_hard[] = {
    DUAL(N_D5, N_A5), R(N_D3), TOP(N_A5), BOT(N_F5), TOP(N_G5), BOT(N_E5), TOP(N_F5), R(N_A2),
    BOT(N_D5), TOP(N_F5), TOP(N_A5), R(N_D3), HLDT(N_C6, 3), R(0), R(0), R(N_C3),
    DUAL(N_AS4, N_F5), R(0), BOT(N_E5), TOP(N_G5), TOP(N_A5), BOT(N_F5), HT(N_A5), R(N_A2),
    HLDB(N_D5, 3), R(0), R(0), R(N_A2), BOT(N_E5), TOP(N_F5), TOP(N_G5), R(0),

    SPMB(N_D5, 4), R(0), R(N_D3), R(0), R(N_A2), R(0), R(N_C3), R(0),
    DUAL(N_F5, N_A5), BOT(N_E5), TOP(N_G5), BOT(N_F5), DUAL(N_G5, N_C6), BOT(N_E5), TOP(N_A5), R(N_D3),
    TOP(N_A5), BOT(N_F5), HB(N_G5), BOT(N_E5), HLDD(N_A5, 3), R(0), R(0), R(N_A2),
    SPMD(N_D5, 5), R(0), R(N_D3), R(0), R(N_F3), R(0), R(N_G3), R(0), R(N_A2), R(0),

    TOP(N_A5), BOT(N_F5), TOP(N_G5), BOT(N_E5), TOP(N_F5), BOT(N_D5), DUAL(N_E5, N_A5), R(0),
    HLDT(N_A5, 2), R(0), HLDB(N_F5, 2), R(0), DUAL(N_D5, N_A5), R(0), DUAL(N_D5, N_D6), R(0)
};

static const struct SongStep chart_overdrive[] = {
    DUAL(N_E5, N_B5), BOT(N_G5), TOP(N_B5), BOT(N_E5), DUAL(N_FS5, N_B5), BOT(N_G5), TOP(N_A5), BOT(N_FS5),
    TOP(N_B5), BOT(N_G5), TOP(N_E6), BOT(N_B5), HLDT(N_D6, 3), R(0), R(0), R(N_B2),
    DUAL(N_G5, N_B5), BOT(N_FS5), TOP(N_A5), BOT(N_E5), TOP(N_G5), BOT(N_FS5), HT(N_B5), BOT(N_G5),
    HLDB(N_E5, 2), R(0), DUAL(N_FS5, N_A5), R(0), DUAL(N_G5, N_B5), BOT(N_A5), TOP(N_B5), R(0),

    SPMD(N_E5, 5), R(0), R(N_E3), R(0), R(N_G3), R(0), R(N_A3), R(0), R(N_B2), R(0), R(0),
    DUAL(N_E5, N_E6), BOT(N_B5), TOP(N_D6), BOT(N_B5), TOP(N_A5), BOT(N_G5), TOP(N_B5), BOT(N_FS5),
    HB(N_G5), TOP(N_A5), DUAL(N_B5, N_E6), R(0), HLDD(N_E6, 3), R(0), R(0), R(N_B2),
    SPMD(N_E5, 6), R(0), R(N_E3), R(0), R(N_G3), R(0), R(N_A3), R(0), R(N_B2), R(0), R(0), R(0),

    TOP(N_B5), BOT(N_G5), TOP(N_A5), BOT(N_FS5), DUAL(N_G5, N_B5), BOT(N_E5), HT(N_B5), BOT(N_G5),
    SPMD(N_E5, 8), R(0), R(N_E3), R(0), R(N_G3), R(0), R(N_A3), R(0), R(N_B2), R(0), R(N_E3), R(0), R(N_B2), R(0), R(0),
    DUAL(N_G5, N_B5), R(0), DUAL(N_A5, N_D6), R(0), DUAL(N_B5, N_E6), R(0), HLDD(N_E6, 3), R(0), R(0)
};

static struct Note notes[MAX_NOTES];
static struct BeatAudio beat_audio[MAX_STEPS];
static int total_notes = 0;
static uint32_t total_steps = 0;
static bool sound_enabled = true;

static uint32_t rng_state = 0x12345678u;
static uint32_t best_endless_score = 0;
static uint32_t best_endless_wave  = 0;

static uint32_t sfx_freq = 0;
static uint32_t sfx_until_ms = 0;
static uint32_t last_spk_freq = 0xFFFFFFFF;

static void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("mov %0, %%ebx" : : "r"(ebx));
    __asm__ volatile("mov %0, %%ecx" : : "r"(ecx));
    __asm__ volatile("mov %0, %%edx" : : "r"(edx));
    __asm__ volatile("mov %0, %%eax" : : "r"(eax));
    __asm__ volatile("int $0x30");
}

static uint32_t get_ticks(void) {
    uint32_t t = 0;
    syscall(SYSCALL_GET_TICKS, (uint32_t) &t, 0, 0);
    return t;
}

static uint32_t rng_next(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (rng_state >> 16) & 0x7FFFu;
}

static void poll_matrix(uint8_t *down, uint8_t *pressed) {
    syscall(SYSCALL_KEY_MATRIX, (uint32_t) down, (uint32_t) pressed, 0);
}

static void speaker_set(uint32_t freq) {
    if (!sound_enabled) freq = 0;
    if (freq != last_spk_freq) {
        last_spk_freq = freq;
        syscall(SYSCALL_SPEAKER, freq, 0, 0);
    }
}

static void trigger_sfx(uint32_t freq, uint32_t now, uint32_t dur_ms) {
    if (!sound_enabled) return;
    sfx_freq = freq;
    sfx_until_ms = now + dur_ms;
}

static void play_jingle(const uint16_t *freqs, const uint16_t *durs, int count) {
    if (!sound_enabled) return;
    for (int i = 0; i < count; i++) {
        speaker_set(freqs[i]);
        uint32_t start = get_ticks();
        while (get_ticks() - start < durs[i]) {}
    }
    speaker_set(0);
}

static void puts_at_len(uint8_t row, uint8_t col, const char *s, uint32_t len, uint8_t color) {
    if (len == 0) return;
    uint32_t attr = ((uint32_t) row << 16) | ((uint32_t) col << 8) | (uint32_t) color;
    syscall(SYSCALL_PUTS_AT, (uint32_t) s, len, attr);
}

static void puts_at(uint8_t row, uint8_t col, const char *s, uint8_t color) {
    uint32_t len = 0;
    while (s[len] != '\0') len++;
    puts_at_len(row, col, s, len, color);
}

static void clear_game_area(void) {
    char blank[80];
    for (int i = 0; i < 80; i++) blank[i] = ' ';
    for (uint8_t r = 0; r < 24; r++) {
        puts_at_len(r, 0, blank, 80, COL_BG);
    }
}

static void format_uint(uint32_t val, char *buf, int digits) {
    for (int i = digits - 1; i >= 0; i--) {
        buf[i] = (char) ('0' + (val % 10));
        val /= 10;
    }
    buf[digits] = '\0';
}

static uint16_t spam_step_pitch(uint16_t base, uint8_t progress) {
    static const uint16_t num[9] = { 1, 9, 5, 4, 3, 5, 15, 2, 9 };
    static const uint16_t den[9] = { 1, 8, 4, 3, 2, 3,  8, 1, 4 };
    uint8_t idx = progress > 8 ? 8 : progress;
    return (uint16_t) (((uint32_t) base * num[idx]) / den[idx]);
}

static uint32_t compile_song_steps(const struct SongStep *src, uint32_t len,
                                   uint32_t origin_ms, uint32_t step_ms,
                                   bool remix, uint32_t wave, int cur_hp,
                                   uint32_t *max_score_out) {
    if (len > MAX_STEPS) len = MAX_STEPS;
    total_steps = len;

    for (uint32_t i = 0; i < MAX_STEPS; i++) {
        beat_audio[i].freq = 0;
        beat_audio[i].alt_freq = 0;
        beat_audio[i].style = AUDIO_REST;
        beat_audio[i].note_idx = -1;
    }

    total_notes = 0;
    uint32_t last_end = origin_ms;
    uint32_t max_score = 0;
    bool phrase_mirror = false;
    uint32_t dual_chance = (wave > 1) ? ((wave - 1) * 12) : 0;
    if (dual_chance > 45) dual_chance = 45;

    for (uint32_t i = 0; i < len; i++) {
        if (remix && (i % 16 == 0)) {
            phrase_mirror = (wave > 1) && ((rng_next() % 100) < 45);
        }

        char ch = src[i].token;
        uint16_t freq = src[i].freq;
        uint16_t param = src[i].param;

        if (ch == '.') {
            if (beat_audio[i].style == AUDIO_REST && freq != 0) {
                beat_audio[i].freq = freq;
                beat_audio[i].style = AUDIO_BASS;
                beat_audio[i].note_idx = -1;
            }
            continue;
        }

        if (total_notes >= MAX_NOTES - 1) break;

        if (remix) {
            if (phrase_mirror) {
                if (ch == '<') ch = '>';
                else if (ch == '>') ch = '<';
                else if (ch == '[') ch = ']';
                else if (ch == ']') ch = '[';
            }
            if ((ch == '<' || ch == '>') && (i % 4 == 0) && (rng_next() % 100) < dual_chance) {
                ch = '=';
                param = (uint16_t) (((uint32_t) freq * 3) / 2);
            } else if ((ch == '[' || ch == ']') && wave >= 3 && (rng_next() % 100) < 35) {
                ch = 'B';
            }
        }

        struct Note *n = &notes[total_notes];
        n->time_ms     = origin_ms + i * step_ms;
        n->duration_ms = 0;
        n->base_freq   = freq;
        n->hits_total  = 0;
        n->hits_left   = 0;
        n->state       = 0;
        n->chord_mask  = 0;
        n->sub_tick    = 0;

        if (ch == '<' || ch == '>') {
            n->type = NOTE_TAP;
            n->lane = (ch == '<') ? 0 : 1;
            beat_audio[i].freq = freq;
            beat_audio[i].style = AUDIO_TAP;
            beat_audio[i].note_idx = (int16_t) total_notes;
            max_score += 300;
        } else if (ch == '=') {
            n->type = NOTE_DUAL;
            n->lane = 2;
            beat_audio[i].freq = freq;
            beat_audio[i].alt_freq = param ? param : (uint16_t) (((uint32_t) freq * 3) / 2);
            beat_audio[i].style = AUDIO_CHORD;
            beat_audio[i].note_idx = (int16_t) total_notes;
            max_score += 400;
        } else if (ch == 'H' || ch == 'h') {
            n->type = NOTE_HEART;
            n->lane = (ch == 'H') ? 0 : 1;
            beat_audio[i].freq = freq;
            beat_audio[i].style = AUDIO_TAP;
            beat_audio[i].note_idx = (int16_t) total_notes;
            max_score += 350;
        } else if (ch == '[' || ch == ']' || ch == 'B') {
            uint32_t span_steps = param > 0 ? param : 2;
            n->type = NOTE_HOLD;
            n->lane = (ch == '[') ? 0 : (ch == ']') ? 1 : 2;
            n->duration_ms = span_steps * step_ms;
            for (uint32_t k = 0; k < span_steps && (i + k) < len; k++) {
                beat_audio[i + k].freq = freq;
                beat_audio[i + k].style = AUDIO_HOLD;
                beat_audio[i + k].note_idx = (int16_t) total_notes;
            }
            max_score += 200 + span_steps * 90;
        } else if (ch == 'T' || ch == 'b' || ch == 'S') {
            uint8_t cnt = (uint8_t) (param > 0 ? param : 4);
            n->type = NOTE_SPAM;
            n->lane = (ch == 'T') ? 0 : (ch == 'b') ? 1 : 2;
            n->hits_total = cnt;
            n->hits_left = cnt;
            n->duration_ms = (n->lane == 2)
                ? (450u + (uint32_t) cnt * 250u)
                : (350u + (uint32_t) cnt * 240u);
            max_score += 500u + (uint32_t) cnt * 75u;
        } else {
            continue;
        }

        uint32_t end_t = n->time_ms + n->duration_ms;
        if (end_t > last_end) last_end = end_t;
        total_notes++;
    }

    /* Endless Mode: sisipkan Heart Note dinamis + Finisher Spam Note di akhir wave */
    if (remix && total_notes > 4) {
        int heart_quota = (cur_hp <= 2) ? 2 : 1;
        for (int h = 0; h < heart_quota; h++) {
            for (int tries = 0; tries < 24; tries++) {
                int idx = (int) (rng_next() % (uint32_t) total_notes);
                if (notes[idx].type == NOTE_TAP) {
                    notes[idx].type = NOTE_HEART;
                    break;
                }
            }
        }

        if (total_notes < MAX_NOTES) {
            struct Note *fin = &notes[total_notes++];
            uint8_t cnt = (uint8_t) (2 + wave);
            if (cnt > 8) cnt = 8;
            fin->time_ms     = last_end + step_ms * 2;
            fin->base_freq   = N_C5;
            fin->type        = NOTE_SPAM;
            fin->lane        = (wave == 1) ? 0 : (wave == 2) ? 1 : 2;
            fin->hits_total  = cnt;
            fin->hits_left   = cnt;
            fin->state       = 0;
            fin->chord_mask  = 0;
            fin->sub_tick    = 0;
            fin->duration_ms = (fin->lane == 2)
                ? (450u + (uint32_t) cnt * 250u)
                : (350u + (uint32_t) cnt * 230u);
            last_end = fin->time_ms + fin->duration_ms;
        }
    }

    if (max_score_out != 0) *max_score_out = max_score;
    return last_end + (remix ? 700u : 1500u);
}

static uint32_t build_standard_chart(int mode, uint32_t *step_ms_out, uint32_t *approach_ms_out,
                                     uint32_t *origin_ms_out, uint32_t *max_score_out) {
    const struct SongStep *src;
    uint32_t len;
    uint32_t step_ms;

    if (mode == 0) {
        src = chart_normal;
        len = (uint32_t) (sizeof(chart_normal) / sizeof(chart_normal[0]));
        step_ms = 250;
        *approach_ms_out = 1750;
    } else if (mode == 1) {
        src = chart_hard;
        len = (uint32_t) (sizeof(chart_hard) / sizeof(chart_hard[0]));
        step_ms = 220;
        *approach_ms_out = 1380;
    } else {
        src = chart_overdrive;
        len = (uint32_t) (sizeof(chart_overdrive) / sizeof(chart_overdrive[0]));
        step_ms = 190;
        *approach_ms_out = 1100;
    }

    *step_ms_out = step_ms;
    *origin_ms_out = step_ms * 8;
    return compile_song_steps(src, len, *origin_ms_out, step_ms, false, 1, MAX_HP, max_score_out);
}

static uint32_t build_endless_wave(uint32_t wave, uint32_t cur_elapsed, int cur_hp,
                                   uint32_t *step_ms_out, uint32_t *approach_ms_out,
                                   uint32_t *origin_ms_out) {
    uint32_t base_step = ROBIN_BASE_STEP_MS;
    uint32_t dec = (wave - 1) * 10;
    uint32_t step_ms = (base_step > 135 + dec) ? (base_step - dec) : 135;

    uint32_t app_dec = (wave - 1) * 70;
    uint32_t approach_ms = (1550 > 900 + app_dec) ? (1550 - app_dec) : 900;

    uint32_t origin_ms = (wave == 1) ? (step_ms * 8) : (cur_elapsed + approach_ms);

    *step_ms_out = step_ms;
    *approach_ms_out = approach_ms;
    *origin_ms_out = origin_ms;

    uint32_t len = (uint32_t) (sizeof(chart_robin) / sizeof(chart_robin[0]));
    return compile_song_steps(chart_robin, len, origin_ms, step_ms, true, wave, cur_hp, 0);
}

static void update_music(uint32_t elapsed, uint32_t wave_origin, uint32_t step_ms,
                         uint32_t wave, uint32_t now) {
    if (!sound_enabled) {
        speaker_set(0);
        return;
    }
    if (now < sfx_until_ms) {
        speaker_set(sfx_freq);
        return;
    }

    if (elapsed < wave_origin) {
        if (wave == 1) {
            uint32_t beat_ms = step_ms * 2;
            uint32_t beat_idx = elapsed / beat_ms;
            uint32_t beat_pos = elapsed % beat_ms;
            if (beat_idx == 1 && beat_pos < 45)      speaker_set(440);
            else if (beat_idx == 2 && beat_pos < 45) speaker_set(440);
            else if (beat_idx == 3 && beat_pos < 60) speaker_set(880);
            else speaker_set(0);
        } else {
            speaker_set(0);
        }
        return;
    }

    for (int i = 0; i < total_notes; i++) {
        struct Note *n = &notes[i];
        if (n->type == NOTE_SPAM && (n->state == 0 || n->state == 4) &&
            elapsed >= n->time_ms && elapsed <= n->time_ms + n->duration_ms) {
            uint32_t pos = (elapsed - n->time_ms) % step_ms;
            speaker_set(pos < 32 ? N_D3 : 0);
            return;
        }
    }

    uint32_t rel = elapsed - wave_origin;
    uint32_t step = rel / step_ms;
    uint32_t pos = rel % step_ms;

    if (step >= total_steps) {
        speaker_set(0);
        return;
    }

    struct BeatAudio *ba = &beat_audio[step];
    if (ba->style == AUDIO_REST || ba->freq == 0) {
        speaker_set(0);
        return;
    }

    if (ba->note_idx >= 0 && notes[ba->note_idx].state == 2) {
        speaker_set(0);
        return;
    }

    if (ba->style == AUDIO_BASS) {
        speaker_set(pos < 32 ? ba->freq : 0);
    } else if (ba->style == AUDIO_TAP) {
        speaker_set(pos < (step_ms * 72) / 100 ? ba->freq : 0);
    } else if (ba->style == AUDIO_CHORD) {
        if (pos < (step_ms * 82) / 100) {
            speaker_set(((pos / 28) % 2 == 0) ? ba->freq : ba->alt_freq);
        } else {
            speaker_set(0);
        }
    } else if (ba->style == AUDIO_HOLD) {
        uint16_t vib = ((elapsed / 45) % 2 == 0) ? ba->freq : (uint16_t) (ba->freq + 6);
        speaker_set(vib);
    }
}

static void draw_static_frame(void) {
    clear_game_area();
    puts_at(1,  0, "================================================================================", COL_CYAN);
    puts_at(2,  0, "+----------------+-------------------------------------------------------------+", COL_CYAN);
    for (uint8_t r = 3; r <= 8; r++) {
        puts_at(r, 0,  "|", COL_CYAN);
        puts_at(r, 17, "|", COL_CYAN);
        puts_at(r, 79, "|", COL_CYAN);
    }
    puts_at(9,  0, "+----------------+-------------------------------------------------------------+", COL_CYAN);
    for (uint8_t r = 10; r <= 18; r++) {
        puts_at(r, 0,  "|", COL_PINK);
        puts_at(r, 79, "|", COL_PINK);
    }
    puts_at(19, 0, "+==============================================================================+", COL_PINK);
    puts_at(21, 1, "[A/F]:<  [D/J]:>  [A+D]:Dual  [V]:Sound  [R]:Restart  [Q]:Quit", COL_WHITE);
    puts_at(22, 1, "Cyan:<  Pink:>  Green:Both  ===:Hold  [N]:Spam  Red<3:Heal(+1 HP)", COL_GRAY);
}

static void draw_hud(uint32_t score, uint32_t max_score, uint32_t rem_ms,
                     uint32_t fever, bool fever_on, bool endless, uint32_t wave) {
    if (fever_on) {
        puts_at(0, 1, " [O] FEVER x2! ", COL_FEVER_BG);
    } else {
        puts_at(0, 1, " [O] FEVER     ", COL_YELLOW);
    }

    char fbar[13];
    fbar[0] = '[';
    uint32_t filled = fever / 10;
    if (filled > 10) filled = 10;
    for (uint32_t i = 0; i < 10; i++) {
        fbar[1 + i] = (i < filled) ? '#' : '.';
    }
    fbar[11] = ']';
    fbar[12] = '\0';
    puts_at(0, 16, fbar, fever_on ? COL_YELLOW : COL_GREEN);

    if (endless) {
        char wbuf[4];
        format_uint(wave > 99 ? 99 : wave, wbuf, 2);
        puts_at(0, 29, "WAVE ", COL_PINK);
        puts_at(0, 34, wbuf,  COL_YELLOW);
        puts_at(0, 36, "  ",  COL_BG);
    } else {
        uint32_t sec = rem_ms / 1000;
        char tbuf[6];
        tbuf[0] = (char) ('0' + (sec / 60) % 10);
        tbuf[1] = ':';
        format_uint(sec % 60, &tbuf[2], 2);
        puts_at(0, 29, "TIME ", COL_GRAY);
        puts_at(0, 34, tbuf, COL_WHITE);
    }

    char sbuf[7];
    format_uint(score > 999999 ? 999999 : score, sbuf, 6);
    puts_at(0, 41, "SCORE ", COL_GRAY);
    puts_at(0, 47, sbuf, COL_YELLOW);

    uint32_t pct = endless
        ? ((score >= 25000) ? 90 : (score >= 12000) ? 70 : (score >= 5000) ? 45 : 0)
        : ((max_score > 0) ? (score * 100) / max_score : 0);
    puts_at(0, 55, "[B]", pct >= 40 ? COL_CYAN   : COL_GRAY);
    puts_at(0, 59, "[A]", pct >= 65 ? COL_PINK   : COL_GRAY);
    puts_at(0, 63, "[S]", pct >= 85 ? COL_YELLOW : COL_GRAY);

    puts_at(0, 68, sound_enabled ? "[V] SND:ON " : "[V] SND:OFF", sound_enabled ? COL_GREEN : COL_GRAY);
}

static void draw_left_panel(int judge, uint32_t elapsed, uint32_t wave_origin, uint32_t step_ms,
                            uint32_t judge_ms, int hp, bool fever_on, bool endless, uint32_t wave) {
    bool active = (judge != JUDGE_NONE && elapsed - judge_ms < 550);

    if (elapsed < wave_origin && !active) {
        if (wave == 1) {
            uint32_t beat_idx = elapsed / (step_ms * 2);
            const char *cd = "  GET READY!  ";
            uint8_t cdc = COL_CYAN;
            if (beat_idx == 1)      { cd = "    -- 3 --   "; cdc = COL_YELLOW; }
            else if (beat_idx == 2) { cd = "    -- 2 --   "; cdc = COL_YELLOW; }
            else if (beat_idx == 3) { cd = "   -- 1 GO! --"; cdc = COL_GREEN; }
            puts_at(3, 2, cd, cdc);
        } else {
            char wmsg[15] = "  WAVE 00 !!  ";
            wmsg[7] = (char) ('0' + (wave / 10) % 10);
            wmsg[8] = (char) ('0' + (wave % 10));
            puts_at(3, 2, wmsg, COL_YELLOW);
        }
        puts_at(4, 2, "    /\\__/\\    ", COL_PINK);
        puts_at(5, 2, wave > 1 ? "   ( >w<  )   " : "   ( o_o  )   ", COL_WHITE);
        puts_at(6, 2, wave > 1 ? "  SPEED UP!!  " : "   /|    |\\   ", wave > 1 ? COL_GREEN : COL_CYAN);
    } else if (active && judge == JUDGE_PERFECT) {
        puts_at(3, 2, "  PERFECT!!   ", COL_YELLOW);
        puts_at(4, 2, "    /\\__/\\    ", COL_PINK);
        puts_at(5, 2, "   ( *w*  )   ", COL_YELLOW);
        puts_at(6, 2, "   /|    |\\   ", COL_CYAN);
    } else if (active && judge == JUDGE_GOOD) {
        puts_at(3, 2, "    GOOD!     ", COL_CYAN);
        puts_at(4, 2, "    /\\__/\\    ", COL_PINK);
        puts_at(5, 2, "   ( ^v^  )   ", COL_CYAN);
        puts_at(6, 2, "   /|    |\\   ", COL_CYAN);
    } else if (active && judge == JUDGE_HEAL) {
        puts_at(3, 2, "  HEAL +1 <3  ", COL_RED);
        puts_at(4, 2, "   */\\__/\\*   ", COL_RED);
        puts_at(5, 2, "   ( ^w^  )   ", COL_PINK);
        puts_at(6, 2, "   /| <3 |\\   ", COL_RED);
    } else if (active && judge == JUDGE_BURST) {
        puts_at(3, 2, " ** BURST! ** ", COL_YELLOW);
        puts_at(4, 2, "   \\/\\__/\\/   ", COL_YELLOW);
        puts_at(5, 2, "   ( >w<  )   ", COL_GREEN);
        puts_at(6, 2, "   /| ** |\\   ", COL_PINK);
    } else if (active && judge == JUDGE_MASH) {
        puts_at(3, 2, "  MASH FAST!  ", COL_WHITE);
        puts_at(4, 2, "    /\\__/\\    ", COL_YELLOW);
        puts_at(5, 2, "   ( >o<  )   ", COL_WHITE);
        puts_at(6, 2, "   /| !! |\\   ", COL_CYAN);
    } else if (active && judge == JUDGE_EMPTY) {
        puts_at(3, 2, "  EMPTY TAP!  ", COL_GRAY);
        puts_at(4, 2, "    /\\__/\\    ", COL_GRAY);
        puts_at(5, 2, "   ( -_-  )   ", COL_GRAY);
        puts_at(6, 2, "    |    |    ", COL_GRAY);
    } else if (active && judge == JUDGE_MISS) {
        puts_at(3, 2, "  MISS -1 HP  ", COL_RED);
        puts_at(4, 2, "    /\\__/\\    ", COL_GRAY);
        puts_at(5, 2, "   ( ;_;  )   ", COL_RED);
        puts_at(6, 2, "    |    |    ", COL_GRAY);
    } else {
        puts_at(3, 2, fever_on ? "  * FEVER! *  " : (endless ? "  ROBIN LIVE! " : "  ON BEAT!    "),
                fever_on ? COL_YELLOW : (endless ? COL_PINK : COL_WHITE));
        puts_at(4, 2, "    /\\__/\\    ", COL_PINK);
        puts_at(5, 2, "   ( o_o  )   ", COL_WHITE);
        puts_at(6, 2, "   /|    |\\   ", COL_CYAN);
    }

    puts_at(7, 2, "HP:", COL_WHITE);
    for (int i = 0; i < MAX_HP; i++) {
        uint8_t col = (uint8_t) (5 + i * 2);
        if (i < hp) {
            puts_at(7, col, "<3", COL_RED);
        } else {
            puts_at(7, col, "--", COL_GRAY);
        }
    }
    puts_at(8, 2, "[A/F]< [D/J]>", COL_CYAN);
}

static void draw_track(uint32_t elapsed, uint32_t approach_ms, uint32_t flash0, uint32_t flash1,
                       bool left_down, bool right_down) {
    char empty[61];
    char divline[61];
    for (int i = 0; i < 61; i++) {
        empty[i]   = ' ';
        divline[i] = '-';
    }

    puts_at_len(3, 18, empty, 61, COL_BG);
    puts_at_len(4, 18, empty, 61, COL_BG);
    puts_at_len(5, 18, empty, 61, COL_BG);
    puts_at_len(6, 18, divline, 61, COL_GRAY);
    puts_at_len(7, 18, empty, 61, COL_BG);
    puts_at_len(8, 18, empty, 61, COL_BG);

    bool f0 = left_down  || (flash0 > 0 && elapsed - flash0 < 110);
    bool f1 = right_down || (flash1 > 0 && elapsed - flash1 < 110);

    puts_at(3, HIT_COL - 2, "+---+", (f0 || f1) ? COL_YELLOW : COL_WHITE);
    puts_at(4, HIT_COL - 2, "|   |", f0 ? COL_HIT_ON : COL_CYAN);
    puts_at(5, HIT_COL - 2, "|   |", f0 ? COL_HIT_ON : COL_CYAN);
    puts_at(6, HIT_COL - 2, "|<3 |", (f0 || f1) ? COL_YELLOW : COL_PINK);
    puts_at(7, HIT_COL - 2, "|   |", f1 ? COL_HIT_ON : COL_PINK);
    puts_at(8, HIT_COL - 2, "|   |", f1 ? COL_HIT_ON : COL_PINK);

    static const uint8_t rainbow_cols[5] = { 0x70, 0x3F, 0x5F, 0x2F, 0x6E };
    int32_t span = TRACK_RIGHT - HIT_COL;

    for (int i = 0; i < total_notes; i++) {
        struct Note *n = &notes[i];
        if (n->state == 1 || n->state == 2) continue;

        int32_t dt = (int32_t) n->time_ms - (int32_t) elapsed;
        int32_t end_dt = (int32_t) (n->time_ms + n->duration_ms) - (int32_t) elapsed;

        if (dt > (int32_t) approach_ms || end_dt < -220) continue;

        int32_t col = HIT_COL + (dt * span) / (int32_t) approach_ms;
        if (n->state == 3 || (n->type == NOTE_SPAM && (n->state == 4 || elapsed >= n->time_ms))) {
            col = HIT_COL;
        }

        if (n->type == NOTE_HOLD || n->type == NOTE_SPAM) {
            int32_t end_col = HIT_COL + (end_dt * span) / (int32_t) approach_ms;
            int32_t t_start = col + 2;
            if (t_start < TRACK_LEFT) t_start = TRACK_LEFT;
            int32_t t_end = end_col;
            if (t_end > TRACK_RIGHT) t_end = TRACK_RIGHT;

            if (t_end >= t_start) {
                char tail[64];
                int tlen = t_end - t_start + 1;
                if (tlen > 60) tlen = 60;
                char tc = (n->type == NOTE_SPAM) ? '~' : (n->state == 3 ? '#' : '=');
                for (int k = 0; k < tlen; k++) tail[k] = tc;
                if (t_end == end_col && tlen > 0) tail[tlen - 1] = ']';

                uint8_t tcol = (n->state == 3) ? COL_HOLD_ON
                             : (n->type == NOTE_SPAM) ? COL_WHITE
                             : (n->lane == 0) ? COL_CYAN
                             : (n->lane == 1) ? COL_PINK
                             : COL_GREEN;

                if (n->lane == 0 || n->lane == 2) {
                    puts_at_len(4, (uint8_t) t_start, tail, (uint32_t) tlen, tcol);
                    puts_at_len(5, (uint8_t) t_start, tail, (uint32_t) tlen, tcol);
                }
                if (n->lane == 1 || n->lane == 2) {
                    puts_at_len(7, (uint8_t) t_start, tail, (uint32_t) tlen, tcol);
                    puts_at_len(8, (uint8_t) t_start, tail, (uint32_t) tlen, tcol);
                }
            }
        }

        if (col - 1 < TRACK_LEFT || col + 1 > TRACK_RIGHT) continue;
        uint8_t hcol = (uint8_t) (col - 1);

        if (n->type == NOTE_TAP) {
            if (n->lane == 0) {
                puts_at(4, hcol, "[<]", COL_NOTE_TOP);
                puts_at(5, hcol, "[<]", COL_NOTE_TOP);
            } else {
                puts_at(7, hcol, "[>]", COL_NOTE_BOT);
                puts_at(8, hcol, "[>]", COL_NOTE_BOT);
            }
        } else if (n->type == NOTE_HEART) {
            if (n->lane == 0) {
                puts_at(4, hcol, "[<]", COL_NOTE_HRT);
                puts_at(5, hcol, "<3 ", COL_NOTE_HRT);
            } else {
                puts_at(7, hcol, "[>]", COL_NOTE_HRT);
                puts_at(8, hcol, "<3 ", COL_NOTE_HRT);
            }
        } else if (n->type == NOTE_DUAL) {
            puts_at(4, hcol, "[<]", COL_NOTE_DUAL);
            puts_at(5, hcol, "| |", COL_NOTE_DUAL);
            puts_at(6, hcol, "<=>", COL_NOTE_DUAL);
            puts_at(7, hcol, "| |", COL_NOTE_DUAL);
            puts_at(8, hcol, "[>]", COL_NOTE_DUAL);
        } else if (n->type == NOTE_HOLD) {
            uint8_t hc = (n->state == 3) ? COL_HOLD_ON
                       : (n->lane == 0) ? COL_NOTE_TOP
                       : (n->lane == 1) ? COL_NOTE_BOT
                       : COL_NOTE_DUAL;
            if (n->lane == 0) {
                puts_at(4, hcol, "[<=", hc);
                puts_at(5, hcol, "[<=", hc);
            } else if (n->lane == 1) {
                puts_at(7, hcol, "[>=", hc);
                puts_at(8, hcol, "[>=", hc);
            } else {
                puts_at(4, hcol, "[<=", hc);
                puts_at(5, hcol, "| |", hc);
                puts_at(6, hcol, "<=>", hc);
                puts_at(7, hcol, "| |", hc);
                puts_at(8, hcol, "[>=", hc);
            }
        } else if (n->type == NOTE_SPAM) {
            uint8_t rc = rainbow_cols[(elapsed / 70 + (uint32_t) i) % 5];
            char tag[4];
            tag[0] = '[';
            tag[1] = (char) ('0' + (n->hits_left > 9 ? 9 : n->hits_left));
            tag[2] = ']';
            tag[3] = '\0';

            if (n->lane == 0) {
                puts_at(4, hcol, tag, rc);
                puts_at(5, hcol, "<<<", rc);
            } else if (n->lane == 1) {
                puts_at(7, hcol, tag, rc);
                puts_at(8, hcol, ">>>", rc);
            } else {
                puts_at(4, hcol, "<<<", rc);
                puts_at(5, hcol, tag, rc);
                puts_at(6, hcol, "<=>", rc);
                puts_at(7, hcol, tag, rc);
                puts_at(8, hcol, ">>>", rc);
            }
        }
    }
}

static void draw_stage(uint32_t combo, uint32_t beat_frame, bool fever_on, bool endless) {
    uint8_t spot_col = fever_on ? COL_YELLOW : COL_CYAN;
    if (beat_frame % 2 == 0) {
        puts_at(10, 2, "     \\   /             \\   /             \\   /             \\   /         ", spot_col);
    } else {
        puts_at(10, 2, "   \\   /             \\   /             \\   /             \\   /           ", COL_PINK);
    }

    char cbuf[4];
    format_uint(combo > 999 ? 999 : combo, cbuf, 3);
    puts_at(12, 3, "+---------+", fever_on ? COL_YELLOW : COL_CYAN);
    puts_at(13, 3, "|   ",        fever_on ? COL_YELLOW : COL_CYAN);
    puts_at(13, 7, cbuf,         fever_on ? COL_YELLOW : COL_WHITE);
    puts_at(13, 10, "   |",      fever_on ? COL_YELLOW : COL_CYAN);
    puts_at(14, 3, "|  COMBO  |", COL_PINK);
    puts_at(15, 3, "+---------+", fever_on ? COL_YELLOW : COL_CYAN);
    puts_at(16, 3, fever_on ? " * FEVER! * " : (endless ? "  PENACONY  " : "  DELUSION  "),
            fever_on ? COL_YELLOW : (endless ? COL_PINK : COL_GRAY));

    static const char *idol1[4][5] = {
        { "   /^^^\\   ", "  ( ^w^ )  ", "  /|===|\\  ", "   /   \\   ", "  ~~o~~o~~ " },
        { "   /^^^\\   ", " \\( ^o^ )  ", "   |===|\\  ", "   |   \\   ", "  ~~o~~o~~ " },
        { "   /^^^\\   ", "  ( >w< )  ", "  /|===|/  ", "   /   |   ", "  ~~o~~o~~ " },
        { "  \\/^^^\\/  ", "  ( *v* )  ", "   |===|   ", "   /   \\   ", "  **o**o** " }
    };
    static const char *idol2[4][5] = {
        { "  ,(@_@),  ", "  ( ^v^ )  ", "  /|=#=|\\  ", "   /   \\   ", "  ======== " },
        { "  ,(@_@),  ", "  ( >o< )/ ", "  /|=#=|   ", "   /   |   ", "  ======== " },
        { "  ,(@_@),  ", " \\( ^w^ )  ", "   |=#=|\\  ", "   |   \\   ", "  ======== " },
        { " \\,(@_@),/ ", "  ( *w* )  ", "   |=#=|   ", "   /   \\   ", "  **====** " }
    };
    static const char *idol3[4][5] = {
        { "  d/^^^\\b  ", "  ( -w- )  ", "  /|+++|\\  ", "   /   \\   ", "  ~~*~~*~~ " },
        { "  d/^^^\\b  ", " \\( ^-^ )  ", "   |+++|\\  ", "   /   |   ", "  ~~*~~*~~ " },
        { "  d/^^^\\b  ", "  ( >_< )/ ", "  /|+++|   ", "   |   \\   ", "  ~~*~~*~~ " },
        { " \\d/^^^\\b/ ", "  ( *o* )  ", "   |+++|   ", "   /   \\   ", "  **~**~** " }
    };

    uint32_t f = fever_on ? ((beat_frame % 2 == 0) ? 3 : 1) : (beat_frame % 4);
    for (uint8_t r = 0; r < 5; r++) {
        puts_at(13 + r, 21, idol1[f][r], (r < 2) ? COL_YELLOW : COL_CYAN);
        puts_at(13 + r, 40, idol2[f][r], (r < 2) ? COL_PINK   : COL_WHITE);
        puts_at(13 + r, 59, idol3[f][r], (r < 2) ? COL_RED    : COL_PINK);
    }
}

static void draw_progress(uint32_t elapsed, uint32_t seg_start, uint32_t seg_end, bool endless) {
    char bar[64];
    uint32_t dur = (seg_end > seg_start) ? (seg_end - seg_start) : 1;
    uint32_t cur = (elapsed > seg_start) ? (elapsed - seg_start) : 0;
    uint32_t pct = (cur * 100) / dur;
    if (pct > 100) pct = 100;
    uint32_t pos = (pct * 56) / 100;

    bar[0] = '[';
    for (uint32_t i = 0; i < 56; i++) {
        if (i < pos) bar[1 + i] = '=';
        else if (i == pos) bar[1 + i] = '>';
        else bar[1 + i] = '-';
    }
    bar[57] = ']';
    bar[58] = ' ';
    format_uint(pct, &bar[59], 3);
    bar[62] = '%';
    bar[63] = '\0';
    puts_at(20, 8, bar, endless ? COL_PINK : COL_CYAN);
}

static void show_guidebook(void) {
    uint8_t down[128];
    uint8_t pressed[128];

    clear_game_area();
    puts_at(1,  12, "========================================================", COL_CYAN);
    puts_at(2,  12, "            GUIDEBOOK :: CARA MAIN ZZZ RHYTHM           ", COL_YELLOW);
    puts_at(3,  12, "========================================================", COL_CYAN);

    puts_at(5,  4, "[<]", COL_NOTE_TOP);
    puts_at(5,  9, "NOTE ATAS (Cyan)  : Tekan [A] atau [F] (Nada Tinggi)", COL_WHITE);

    puts_at(7,  4, "[>]", COL_NOTE_BOT);
    puts_at(7,  9, "NOTE BAWAH (Pink) : Tekan [D] atau [J] (Nada Rendah)", COL_WHITE);

    puts_at(9,  4, "<=>", COL_NOTE_DUAL);
    puts_at(9,  9, "NOTE DUAL (Hijau) : Tekan KIRI + KANAN bersamaan (A+D / F+J)", COL_GREEN);

    puts_at(11, 4, "[<==]", COL_NOTE_TOP);
    puts_at(11, 10, "NOTE HOLD (Ekor) : TAHAN tombol sampai ekor ==== habis!", COL_CYAN);

    puts_at(13, 4, "[5]", 0x70);
    puts_at(13, 9, "NOTE SPAM (Angka) : Spam sesuai angka (Dual = spam A+D bareng!)", COL_YELLOW);

    puts_at(15, 4, "<3 ", COL_NOTE_HRT);
    puts_at(15, 9, "NOTE HEART (Merah): Hit untuk memulihkan +1 Heart (Max 5)", COL_RED);

    puts_at(17, 4, "ATURAN & ENDLESS:", COL_PINK);
    puts_at(18, 6, "- Mulai 5 Heart. Miss = -1 HP. HP 0 = GAME OVER!", COL_WHITE);
    puts_at(19, 6, "- Spam tombol kosong = Combo reset & Fever turun (HP aman).", COL_GRAY);
    puts_at(20, 6, "- ENDLESS (Robin): Lagu loop + remix pola & speed naik tiap Wave!", COL_YELLOW);

    puts_at(22, 20, "Tekan [G], [M], atau [Q] untuk kembali ke Menu...", COL_CYAN);

    while (true) {
        poll_matrix(down, pressed);
        if (pressed[SC_G] || pressed[SC_M] || pressed[SC_Q]) return;
    }
}

static int show_title_screen(void) {
    uint8_t down[128];
    uint8_t pressed[128];
    speaker_set(0);

    while (true) {
        clear_game_area();
        puts_at(1,  12, "=============================================================", COL_CYAN);
        puts_at(2,  12, "   ZENLESS ZONE ZERO :: ANGELS OF DELUSION RHYTHM INSPIRED  ", COL_YELLOW);
        puts_at(3,  12, "=============================================================", COL_CYAN);

        puts_at(5,  22, "  /^^^\\      ,(@_@),     d/^^^\\b  ", COL_PINK);
        puts_at(6,  22, " ( *v* )    ( ^w^ )     ( *o* )  ", COL_YELLOW);
        puts_at(7,  22, " /|===|\\    /|=#=|\\     /|+++|\\  ", COL_CYAN);

        puts_at(9,  14, "Pilih Mode:", COL_WHITE);
        puts_at(11, 16, "[1] NORMAL    - 120 BPM (Idol Pop Synced Chart)", COL_CYAN);
        puts_at(12, 16, "[2] HARD      - 136 BPM (Electro Drive & Dual Spam)", COL_PINK);
        puts_at(13, 16, "[3] OVERDRIVE - 158 BPM (Full Rave & Fast Mash!)", COL_RED);
        puts_at(14, 16, "[4] ENDLESS   - Robin: Sway to My Beat (Remix Loop!)", COL_YELLOW);

        if (best_endless_score > 0) {
            char bbuf[7];
            char wbuf[3];
            format_uint(best_endless_score > 999999 ? 999999 : best_endless_score, bbuf, 6);
            format_uint(best_endless_wave > 99 ? 99 : best_endless_wave, wbuf, 2);
            puts_at(15, 20, "Best Endless: ", COL_GRAY);
            puts_at(15, 34, bbuf, COL_YELLOW);
            puts_at(15, 41, " (Wave ", COL_GRAY);
            puts_at(15, 48, wbuf, COL_PINK);
            puts_at(15, 50, ")", COL_GRAY);
        }

        puts_at(17, 16, "[G] Guidebook (Cara Main & Jenis Note)", COL_CYAN);
        puts_at(18, 16, sound_enabled ? "[V] Musik 8-Bit: ON " : "[V] Musik 8-Bit: OFF", sound_enabled ? COL_GREEN : COL_GRAY);
        puts_at(19, 16, "[Q] Keluar ke Shell", COL_GRAY);

        puts_at(22, 13, "Kontrol: [A/F] Atas (<) | [D/J] Bawah (>) | [A+D] Dual", COL_WHITE);

        while (true) {
            poll_matrix(down, pressed);
            if (pressed[SC_1]) return 0;
            if (pressed[SC_2]) return 1;
            if (pressed[SC_3]) return 2;
            if (pressed[SC_4]) return 3;
            if (pressed[SC_Q]) return -1;
            if (pressed[SC_G]) {
                show_guidebook();
                break;
            }
            if (pressed[SC_V]) {
                sound_enabled = !sound_enabled;
                speaker_set(0);
                puts_at(18, 16, sound_enabled ? "[V] Musik 8-Bit: ON " : "[V] Musik 8-Bit: OFF", sound_enabled ? COL_GREEN : COL_GRAY);
            }
        }
    }
}

static int show_result_screen(uint32_t score, uint32_t max_score, uint32_t max_combo,
                              uint32_t perfects, uint32_t goods, uint32_t misses,
                              bool failed, bool endless, uint32_t wave) {
    uint8_t down[128];
    uint8_t pressed[128];
    speaker_set(0);

    if (endless) {
        if (score > best_endless_score) best_endless_score = score;
        if (wave  > best_endless_wave)  best_endless_wave  = wave;
    }

    if (failed) {
        static const uint16_t f_fail[4] = { 392, 349, 311, 220 };
        static const uint16_t d_fail[4] = { 140, 140, 140, 320 };
        play_jingle(f_fail, d_fail, 4);
    } else {
        static const uint16_t f_win[4] = { 523, 659, 784, 1047 };
        static const uint16_t d_win[4] = { 100, 100, 100, 260 };
        play_jingle(f_win, d_win, 4);
    }

    clear_game_area();
    uint32_t pct = endless
        ? ((score >= 25000) ? 90 : (score >= 12000) ? 70 : (score >= 5000) ? 45 : 20)
        : ((max_score > 0) ? (score * 100) / max_score : 0);
    const char *rank = (failed && !endless) ? "F" : "C";
    uint8_t rank_col = COL_RED;
    if (!failed || endless) {
        if (pct >= 85)      { rank = "S"; rank_col = COL_YELLOW; }
        else if (pct >= 65) { rank = "A"; rank_col = COL_PINK; }
        else if (pct >= 40) { rank = "B"; rank_col = COL_CYAN; }
    }

    puts_at(3,  18, "============================================", failed ? COL_RED : COL_CYAN);
    puts_at(4,  18, endless ? "          ENDLESS RUN :: FINISHED!          "
                  : (failed ? "         GAME OVER :: HEARTS DEPLETED       "
                            : "              STAGE CLEAR !!                "),
            endless ? COL_YELLOW : (failed ? COL_RED : COL_YELLOW));
    puts_at(5,  18, "============================================", failed ? COL_RED : COL_CYAN);

    puts_at(7,  33, "RANK : [ ", COL_WHITE);
    puts_at(7,  42, rank, rank_col);
    puts_at(7,  43, " ]", COL_WHITE);

    char num[7];
    format_uint(score > 999999 ? 999999 : score, num, 6);
    puts_at(10, 26, "FINAL SCORE : ", COL_GRAY);
    puts_at(10, 40, num, COL_YELLOW);

    format_uint(max_combo > 999 ? 999 : max_combo, num, 3);
    puts_at(11, 26, "MAX COMBO   : ", COL_GRAY);
    puts_at(11, 40, num, COL_PINK);

    if (endless) {
        format_uint(wave > 99 ? 99 : wave, num, 2);
        puts_at(12, 26, "WAVE REACHED: ", COL_CYAN);
        puts_at(12, 40, num, COL_YELLOW);
    }

    format_uint(perfects > 999 ? 999 : perfects, num, 3);
    puts_at(14, 26, "PERFECT/OK  : ", COL_YELLOW);
    puts_at(14, 40, num, COL_WHITE);

    format_uint(goods > 999 ? 999 : goods, num, 3);
    puts_at(15, 26, "GOOD        : ", COL_CYAN);
    puts_at(15, 40, num, COL_WHITE);

    format_uint(misses > 999 ? 999 : misses, num, 3);
    puts_at(16, 26, "MISS        : ", COL_RED);
    puts_at(16, 40, num, COL_WHITE);

    puts_at(19, 22, "[R] Main Lagi    [M] Menu    [Q] Keluar", COL_WHITE);

    poll_matrix(down, pressed);
    while (true) {
        poll_matrix(down, pressed);
        if (pressed[SC_R]) return 1;
        if (pressed[SC_M]) return 2;
        if (pressed[SC_Q]) return 0;
    }
}

static int run_song(int mode) {
    uint8_t down[128];
    uint8_t pressed[128];

    bool endless = (mode == 3);
    uint32_t wave = 1;
    uint32_t step_ms = 250;
    uint32_t approach_ms = 1750;
    uint32_t wave_origin = 2000;
    uint32_t max_score = 1000;
    uint32_t song_end = 0;

    rng_state = get_ticks() ^ 0xA5A55A5Au;

    if (endless) {
        song_end = build_endless_wave(wave, 0, MAX_HP, &step_ms, &approach_ms, &wave_origin);
    } else {
        song_end = build_standard_chart(mode, &step_ms, &approach_ms, &wave_origin, &max_score);
    }

    poll_matrix(down, pressed);
    uint32_t start_ms = get_ticks();
    uint32_t last_render = 0;

    int      hp = MAX_HP;
    uint32_t score = 0;
    uint32_t combo = 0;
    uint32_t max_combo = 0;
    uint32_t perfects = 0;
    uint32_t goods = 0;
    uint32_t misses = 0;
    uint32_t fever = 0;
    bool     fever_on = false;
    uint32_t fever_end_ms = 0;
    uint32_t spam_grace_until = 0;

    int      last_judge = JUDGE_NONE;
    uint32_t judge_ms = 0;
    uint32_t flash0 = 0;
    uint32_t flash1 = 0;

    draw_static_frame();

    while (true) {
        uint32_t now = get_ticks();
        uint32_t elapsed = now - start_ms;

        if (hp <= 0) {
            return show_result_screen(score, max_score, max_combo, perfects, goods, misses, true, endless, wave);
        }

        if (elapsed >= song_end) {
            if (endless) {
                wave++;
                score += 500 * wave;
                rng_state ^= (now + wave * 2654435761u);
                song_end = build_endless_wave(wave, elapsed, hp, &step_ms, &approach_ms, &wave_origin);
                last_judge = JUDGE_NONE;
                trigger_sfx(N_E6, now, 110);
            } else {
                return show_result_screen(score, max_score, max_combo, perfects, goods, misses, false, false, 1);
            }
        }

        if (fever_on) {
            if (elapsed >= fever_end_ms) {
                fever_on = false;
                fever = 0;
            } else {
                fever = ((fever_end_ms - elapsed) * 100) / 6000;
            }
        }

        update_music(elapsed, wave_origin, step_ms, wave, now);

        poll_matrix(down, pressed);
        if (pressed[SC_Q]) { speaker_set(0); return 0; }
        if (pressed[SC_R]) { speaker_set(0); return 1; }
        if (pressed[SC_V]) {
            sound_enabled = !sound_enabled;
            if (!sound_enabled) speaker_set(0);
        }

        bool left_down  = (down[SC_A] != 0) || (down[SC_F] != 0);
        bool right_down = (down[SC_D] != 0) || (down[SC_J] != 0);
        uint32_t left_tap  = (uint32_t) pressed[SC_A] + (uint32_t) pressed[SC_F];
        uint32_t right_tap = (uint32_t) pressed[SC_D] + (uint32_t) pressed[SC_J];

        if (left_tap > 0) flash0 = elapsed;
        if (right_tap > 0) flash1 = elapsed;

        bool consumed_left = false;
        bool consumed_right = false;
        uint32_t mult = fever_on ? 2 : 1;

        for (int i = 0; i < total_notes; i++) {
            struct Note *n = &notes[i];
            if (n->type != NOTE_SPAM || (n->state != 0 && n->state != 4)) continue;

            if (n->lane == 2 && n->chord_mask != 0 && elapsed - n->sub_tick > DUAL_SPAM_WIN) {
                n->chord_mask = 0;
            }

            if (elapsed + WIN_GOOD < n->time_ms) continue;
            if (elapsed >= n->time_ms && n->state == 0) n->state = 4;

            uint32_t hits = 0;
            if (n->lane == 0 && left_tap > 0 && !consumed_left) {
                hits = left_tap;
                consumed_left = true;
            } else if (n->lane == 1 && right_tap > 0 && !consumed_right) {
                hits = right_tap;
                consumed_right = true;
            } else if (n->lane == 2 && (left_tap > 0 || right_tap > 0)) {
                uint32_t lt = consumed_left ? 0 : left_tap;
                uint32_t rt = consumed_right ? 0 : right_tap;

                while ((lt > 0 || rt > 0) && hits < n->hits_left) {
                    bool used = false;
                    if (lt > 0 && !(n->chord_mask & 1)) {
                        if (n->chord_mask == 0) n->sub_tick = elapsed;
                        n->chord_mask |= 1;
                        lt--;
                        consumed_left = true;
                        used = true;
                    }
                    if (rt > 0 && !(n->chord_mask & 2)) {
                        if (n->chord_mask == 0) n->sub_tick = elapsed;
                        n->chord_mask |= 2;
                        rt--;
                        consumed_right = true;
                        used = true;
                    }
                    if (n->chord_mask == 3) {
                        hits++;
                        n->chord_mask = 0;
                    } else if (!used) {
                        if (lt > 0) { lt = 0; consumed_left = true; }
                        if (rt > 0) { rt = 0; consumed_right = true; }
                        break;
                    }
                }
            }

            if (hits > 0) {
                n->state = 4;
                if (hits >= n->hits_left) {
                    n->hits_left = 0;
                    n->state = 1;
                    score += 500 * mult;
                    combo += hits;
                    perfects++;
                    last_judge = JUDGE_BURST;
                    judge_ms = elapsed;
                    spam_grace_until = elapsed + 180;
                    if (!fever_on) fever += 14;
                    trigger_sfx(spam_step_pitch(n->base_freq, n->hits_total), now, 85);
                } else {
                    n->hits_left = (uint8_t) (n->hits_left - hits);
                    uint8_t prog = (uint8_t) (n->hits_total - n->hits_left);
                    score += 75 * hits * mult;
                    combo += hits;
                    last_judge = JUDGE_MASH;
                    judge_ms = elapsed;
                    if (!fever_on) fever += 2;
                    trigger_sfx(spam_step_pitch(n->base_freq, prog), now, 65);
                }
                if (combo > max_combo) max_combo = combo;
                break;
            }
        }

        for (int i = 0; i < total_notes; i++) {
            struct Note *n = &notes[i];
            if (n->type != NOTE_HOLD || n->state != 3) continue;

            bool held = (n->lane == 0) ? left_down
                      : (n->lane == 1) ? right_down
                      : (left_down && right_down);

            if (elapsed + 120 >= n->time_ms + n->duration_ms) {
                if (held) {
                    n->state = 1;
                    score += 250 * mult;
                    combo++;
                    perfects++;
                    last_judge = JUDGE_PERFECT;
                    judge_ms = elapsed;
                    if (!fever_on) fever += 8;
                    if (combo > max_combo) max_combo = combo;
                } else {
                    n->state = 2;
                    hp--;
                    combo = 0;
                    misses++;
                    last_judge = JUDGE_MISS;
                    judge_ms = elapsed;
                    trigger_sfx(155, now, 75);
                }
            } else if (!held && elapsed > n->sub_tick + 60) {
                n->state = 2;
                hp--;
                combo = 0;
                misses++;
                last_judge = JUDGE_MISS;
                judge_ms = elapsed;
                if (!fever_on && fever >= 5) fever -= 5;
                trigger_sfx(155, now, 75);
            } else if (held && elapsed - n->sub_tick >= 120) {
                n->sub_tick = elapsed;
                score += 45 * mult;
                combo++;
                if (!fever_on) fever += 2;
                if (combo > max_combo) max_combo = combo;
            }
        }

        if ((left_tap > 0 && !consumed_left) || (right_tap > 0 && !consumed_right)) {
            int best_dual = -1;
            int32_t best_abs = 99999;
            for (int i = 0; i < total_notes; i++) {
                struct Note *n = &notes[i];
                if (n->state != 0 || n->lane != 2 || n->type == NOTE_SPAM) continue;
                int32_t diff = (int32_t) n->time_ms - (int32_t) elapsed;
                int32_t adiff = diff < 0 ? -diff : diff;
                if (adiff <= WIN_MISS_TAP && adiff < best_abs) {
                    best_abs = adiff;
                    best_dual = i;
                }
            }

            if (best_dual != -1) {
                struct Note *n = &notes[best_dual];
                if (left_tap > 0 && !consumed_left) {
                    n->chord_mask |= 1;
                    consumed_left = true;
                    if (right_down) n->chord_mask |= 2;
                }
                if (right_tap > 0 && !consumed_right) {
                    n->chord_mask |= 2;
                    consumed_right = true;
                    if (left_down) n->chord_mask |= 1;
                }

                if (n->chord_mask == 3) {
                    if (n->type == NOTE_HOLD) {
                        n->state = 3;
                        n->sub_tick = elapsed;
                        score += 200 * mult;
                        combo++;
                        last_judge = (best_abs <= WIN_PERFECT) ? JUDGE_PERFECT : JUDGE_GOOD;
                        judge_ms = elapsed;
                    } else {
                        n->state = 1;
                        if (best_abs <= WIN_PERFECT) {
                            score += 400 * mult;
                            perfects++;
                            last_judge = JUDGE_PERFECT;
                            if (!fever_on) fever += 10;
                        } else {
                            score += 200 * mult;
                            goods++;
                            last_judge = JUDGE_GOOD;
                            if (!fever_on) fever += 5;
                        }
                        combo++;
                        judge_ms = elapsed;
                    }
                    if (combo > max_combo) max_combo = combo;
                }
            }
        }

        for (int lane = 0; lane < 2; lane++) {
            bool has_tap = (lane == 0) ? (left_tap > 0 && !consumed_left) : (right_tap > 0 && !consumed_right);
            if (!has_tap) continue;

            int best_idx = -1;
            int32_t best_abs = 99999;
            for (int i = 0; i < total_notes; i++) {
                struct Note *n = &notes[i];
                if (n->state != 0 || n->lane != (uint8_t) lane || n->type == NOTE_SPAM) continue;
                int32_t diff = (int32_t) n->time_ms - (int32_t) elapsed;
                int32_t adiff = diff < 0 ? -diff : diff;
                if (adiff < best_abs) {
                    best_abs = adiff;
                    best_idx = i;
                }
            }

            if (best_idx != -1 && best_abs <= WIN_MISS_TAP) {
                struct Note *n = &notes[best_idx];
                if (lane == 0) consumed_left = true;
                else consumed_right = true;

                if (n->type == NOTE_HOLD) {
                    if (best_abs <= WIN_GOOD) {
                        n->state = 3;
                        n->sub_tick = elapsed;
                        score += 150 * mult;
                        combo++;
                        last_judge = (best_abs <= WIN_PERFECT) ? JUDGE_PERFECT : JUDGE_GOOD;
                        judge_ms = elapsed;
                    } else {
                        n->state = 2;
                        hp--;
                        combo = 0;
                        misses++;
                        last_judge = JUDGE_MISS;
                        judge_ms = elapsed;
                        trigger_sfx(155, now, 70);
                    }
                } else {
                    n->state = 1;
                    if (best_abs <= WIN_GOOD) {
                        if (n->type == NOTE_HEART) {
                            if (hp < MAX_HP) hp++;
                            score += 350 * mult;
                            perfects++;
                            combo++;
                            last_judge = JUDGE_HEAL;
                            if (!fever_on) fever += 10;
                            trigger_sfx(N_E6, now, 65);
                        } else if (best_abs <= WIN_PERFECT) {
                            score += 300 * mult;
                            perfects++;
                            combo++;
                            last_judge = JUDGE_PERFECT;
                            if (!fever_on) fever += 8;
                        } else {
                            score += 150 * mult;
                            goods++;
                            combo++;
                            last_judge = JUDGE_GOOD;
                            if (!fever_on) fever += 4;
                        }
                    } else {
                        hp--;
                        combo = 0;
                        misses++;
                        last_judge = JUDGE_MISS;
                        if (!fever_on && fever >= 5) fever -= 5;
                        trigger_sfx(155, now, 70);
                    }
                    judge_ms = elapsed;
                }
                if (combo > max_combo) max_combo = combo;
            }
        }

        if (((left_tap > 0 && !consumed_left) || (right_tap > 0 && !consumed_right)) &&
            elapsed > spam_grace_until && elapsed + WIN_MISS_TAP >= wave_origin) {
            combo = 0;
            if (!fever_on && fever >= 5) fever -= 5;
            last_judge = JUDGE_EMPTY;
            judge_ms = elapsed;
        }

        if (!fever_on && fever >= 100) {
            fever = 100;
            fever_on = true;
            fever_end_ms = elapsed + 6000;
            trigger_sfx(N_D6, now, 75);
        }

        for (int i = 0; i < total_notes; i++) {
            struct Note *n = &notes[i];
            if (n->type == NOTE_SPAM) {
                if ((n->state == 0 || n->state == 4) && elapsed > n->time_ms + n->duration_ms) {
                    n->state = 2;
                    hp--;
                    combo = 0;
                    misses++;
                    last_judge = JUDGE_MISS;
                    judge_ms = elapsed;
                    if (!fever_on && fever >= 5) fever -= 5;
                    trigger_sfx(155, now, 75);
                }
            } else if (n->state == 0 && (int32_t) elapsed - (int32_t) n->time_ms > WIN_GOOD) {
                n->state = 2;
                hp--;
                combo = 0;
                misses++;
                last_judge = JUDGE_MISS;
                judge_ms = elapsed;
                if (!fever_on && fever >= 5) fever -= 5;
                trigger_sfx(155, now, 75);
            }
        }

        if (now - last_render >= 33) {
            last_render = now;
            uint32_t rem = (song_end > elapsed) ? (song_end - elapsed) : 0;
            uint32_t beat_frame = elapsed / (step_ms * 2);

            draw_hud(score, max_score, rem, fever, fever_on, endless, wave);
            draw_left_panel(last_judge, elapsed, wave_origin, step_ms, judge_ms, hp, fever_on, endless, wave);
            draw_track(elapsed, approach_ms, flash0, flash1, left_down, right_down);
            draw_stage(combo, beat_frame, fever_on, endless);
            draw_progress(elapsed, endless ? wave_origin : 0, song_end, endless);
        }
    }
}

int main(void) {
    syscall(SYSCALL_KB_FOCUS, 1, 0, 0);

    int mode = show_title_screen();
    while (mode >= 0) {
        int next = run_song(mode);
        if (next == 0) break;
        if (next == 2) mode = show_title_screen();
    }

    speaker_set(0);
    clear_game_area();
    puts_at(0, 0, "ZZZ Rhythm selesai. Tekan Enter untuk kembali ke shell...", COL_YELLOW);
    syscall(SYSCALL_KB_FOCUS, 0, 0, 0);
    return 0;
}