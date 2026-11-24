//
// i_oplmusic_evo.c - DOOM music for the EVO head unit.
//
// A self-contained MUS -> OPL player: it plays the DMX MUS lumps directly
// (no MUS->MIDI conversion, no temp files) through the Nuked-OPL3 FM
// synthesiser, producing stereo PCM that i_sound_evo.c mixes into the same
// QSA output as the sound effects.
//
// The OPL voice/GENMIDI machinery (instrument loading, operator programming,
// voice allocation, the frequency and volume tables, pitch bend) is the
// battle-tested logic from Chocolate Doom's i_oplmusic.c (Simon Howard, GPL),
// adapted to be driven by a MUS front-end and a sample-clock instead of
// Chocolate's MIDI sequencer + SDL timer. The FM core is Nuked-OPL3 (Nuke.YKT).
//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2013-2020 Nuke.YKT
// Copyright(C) 2026 EVO Music Integration
// GNU General Public License v2+ (see the project COPYING).
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "opl3.h"
#include "i_oplmusic_evo.h"

// ---- minimal types (avoid pulling the whole DOOM header web) --------------
typedef unsigned char u8;
#ifdef PACKEDATTR
#undef PACKEDATTR
#endif
#define PACKEDATTR __attribute__((packed))
// SHORT: the data is little-endian; the unit and host are little-endian.
#define RD_SHORT(x) ((short)(x))

// ---- OPL register map (from Chocolate's opl.h) ----------------------------
#define OPL_NUM_OPERATORS   21
#define OPL_NUM_VOICES      9
#define OPL_REG_WAVEFORM_ENABLE 0x01
#define OPL_REG_TIMER_CTRL      0x04
#define OPL_REG_FM_MODE         0x08
#define OPL_REG_NEW             0x105
#define OPL_REGS_TREMOLO   0x20
#define OPL_REGS_LEVEL     0x40
#define OPL_REGS_ATTACK    0x60
#define OPL_REGS_SUSTAIN   0x80
#define OPL_REGS_WAVEFORM  0xE0
#define OPL_REGS_FREQ_1    0xA0
#define OPL_REGS_FREQ_2    0xB0
#define OPL_REGS_FEEDBACK  0xC0

#define GENMIDI_NUM_INSTRS     128
#define GENMIDI_NUM_PERCUSSION 47
#define GENMIDI_HEADER         "#OPL_II#"
#define GENMIDI_FLAG_FIXED     0x0001
#define GENMIDI_FLAG_2VOICE    0x0004

#define MIDI_CHANNELS   16
#define MUS_PERCUSSION_CHAN 15

typedef struct PACKEDATTR {
    u8 tremolo, attack, sustain, waveform, scale, level;
} genmidi_op_t;

typedef struct PACKEDATTR {
    genmidi_op_t modulator;
    u8 feedback;
    genmidi_op_t carrier;
    u8 unused;
    short base_note_offset;
} genmidi_voice_t;

typedef struct PACKEDATTR {
    unsigned short flags;
    u8 fine_tuning;
    u8 fixed_note;
    genmidi_voice_t voices[2];
} genmidi_instr_t;

typedef struct {
    genmidi_instr_t *instrument;
    int volume;
    int volume_base;
    int pan;
    int bend;
} opl_channel_data_t;

typedef struct opl_voice_s {
    int index, op1, op2, array;
    genmidi_instr_t *current_instr;
    unsigned int current_instr_voice;
    opl_channel_data_t *channel;
    unsigned int key, note, freq, note_volume, car_volume, mod_volume, reg_pan, priority;
} opl_voice_t;

// ---- state ----------------------------------------------------------------
static opl3_chip        opl_chip;
static int              opl_opl3mode = 1;          // use OPL3 (18 voices, stereo)
static int              num_opl_voices = OPL_NUM_VOICES * 2;
static int              out_rate = 44100;
static int              music_inited = 0;

static genmidi_instr_t *main_instrs, *percussion_instrs;

static opl_voice_t      voices[OPL_NUM_VOICES * 2];
static opl_voice_t     *voice_free_list[OPL_NUM_VOICES * 2];
static opl_voice_t     *voice_alloced_list[OPL_NUM_VOICES * 2];
static int              voice_free_num, voice_alloced_num;
static opl_channel_data_t channels[MIDI_CHANNELS];

static int              current_music_volume = 127;
static int              start_music_volume = 127;

// MUS score state
static const u8        *mus_data;                  // whole lump
static const u8        *score_base;                // start of event stream
static int              score_len;
static int              score_pos;
static int              song_looping;
static int              song_playing;
static int              song_paused;
static int              delay_samples;             // samples until next event batch
static int              samples_per_tick;          // out_rate / 140

static pthread_mutex_t  music_lock = PTHREAD_MUTEX_INITIALIZER;

static const int voice_operators[2][OPL_NUM_VOICES] = {
    { 0x00, 0x01, 0x02, 0x08, 0x09, 0x0a, 0x10, 0x11, 0x12 },
    { 0x03, 0x04, 0x05, 0x0b, 0x0c, 0x0d, 0x13, 0x14, 0x15 }
};

static const unsigned short frequency_curve[] = {
    0x133,0x133,0x134,0x134,0x135,0x136,0x136,0x137,0x137,0x138,0x138,0x139,0x139,0x13a,0x13b,0x13b,
    0x13c,0x13c,0x13d,0x13d,0x13e,0x13f,0x13f,0x140,0x140,0x141,0x142,0x142,0x143,0x143,0x144,0x144,
    0x145,0x146,0x146,0x147,0x147,0x148,0x149,0x149,0x14a,0x14a,0x14b,0x14c,0x14c,0x14d,0x14d,0x14e,
    0x14f,0x14f,0x150,0x150,0x151,0x152,0x152,0x153,0x153,0x154,0x155,0x155,0x156,0x157,0x157,0x158,
    0x158,0x159,0x15a,0x15a,0x15b,0x15b,0x15c,0x15d,0x15d,0x15e,0x15f,0x15f,0x160,0x161,0x161,0x162,
    0x162,0x163,0x164,0x164,0x165,0x166,0x166,0x167,0x168,0x168,0x169,0x16a,0x16a,0x16b,0x16c,0x16c,
    0x16d,0x16e,0x16e,0x16f,0x170,0x170,0x171,0x172,0x172,0x173,0x174,0x174,0x175,0x176,0x176,0x177,
    0x178,0x178,0x179,0x17a,0x17a,0x17b,0x17c,0x17c,0x17d,0x17e,0x17e,0x17f,0x180,0x181,0x181,0x182,
    0x183,0x183,0x184,0x185,0x185,0x186,0x187,0x188,0x188,0x189,0x18a,0x18a,0x18b,0x18c,0x18d,0x18d,
    0x18e,0x18f,0x18f,0x190,0x191,0x192,0x192,0x193,0x194,0x194,0x195,0x196,0x197,0x197,0x198,0x199,
    0x19a,0x19a,0x19b,0x19c,0x19d,0x19d,0x19e,0x19f,0x1a0,0x1a0,0x1a1,0x1a2,0x1a3,0x1a3,0x1a4,0x1a5,
    0x1a6,0x1a6,0x1a7,0x1a8,0x1a9,0x1a9,0x1aa,0x1ab,0x1ac,0x1ad,0x1ad,0x1ae,0x1af,0x1b0,0x1b0,0x1b1,
    0x1b2,0x1b3,0x1b4,0x1b4,0x1b5,0x1b6,0x1b7,0x1b8,0x1b8,0x1b9,0x1ba,0x1bb,0x1bc,0x1bc,0x1bd,0x1be,
    0x1bf,0x1c0,0x1c0,0x1c1,0x1c2,0x1c3,0x1c4,0x1c4,0x1c5,0x1c6,0x1c7,0x1c8,0x1c9,0x1c9,0x1ca,0x1cb,
    0x1cc,0x1cd,0x1ce,0x1ce,0x1cf,0x1d0,0x1d1,0x1d2,0x1d3,0x1d3,0x1d4,0x1d5,0x1d6,0x1d7,0x1d8,0x1d8,
    0x1d9,0x1da,0x1db,0x1dc,0x1dd,0x1de,0x1de,0x1df,0x1e0,0x1e1,0x1e2,0x1e3,0x1e4,0x1e5,0x1e5,0x1e6,
    0x1e7,0x1e8,0x1e9,0x1ea,0x1eb,0x1ec,0x1ed,0x1ed,0x1ee,0x1ef,0x1f0,0x1f1,0x1f2,0x1f3,0x1f4,0x1f5,
    0x1f6,0x1f6,0x1f7,0x1f8,0x1f9,0x1fa,0x1fb,0x1fc,0x1fd,0x1fe,0x1ff,0x200,0x201,0x201,0x202,0x203,
    0x204,0x205,0x206,0x207,0x208,0x209,0x20a,0x20b,0x20c,0x20d,0x20e,0x20f,0x210,0x210,0x211,0x212,
    0x213,0x214,0x215,0x216,0x217,0x218,0x219,0x21a,0x21b,0x21c,0x21d,0x21e,0x21f,0x220,0x221,0x222,
    0x223,0x224,0x225,0x226,0x227,0x228,0x229,0x22a,0x22b,0x22c,0x22d,0x22e,0x22f,0x230,0x231,0x232,
    0x233,0x234,0x235,0x236,0x237,0x238,0x239,0x23a,0x23b,0x23c,0x23d,0x23e,0x23f,0x240,0x241,0x242,
    0x244,0x245,0x246,0x247,0x248,0x249,0x24a,0x24b,0x24c,0x24d,0x24e,0x24f,0x250,0x251,0x252,0x253,
    0x254,0x256,0x257,0x258,0x259,0x25a,0x25b,0x25c,0x25d,0x25e,0x25f,0x260,0x262,0x263,0x264,0x265,
    0x266,0x267,0x268,0x269,0x26a,0x26c,0x26d,0x26e,0x26f,0x270,0x271,0x272,0x273,0x275,0x276,0x277,
    0x278,0x279,0x27a,0x27b,0x27d,0x27e,0x27f,0x280,0x281,0x282,0x284,0x285,0x286,0x287,0x288,0x289,
    0x28b,0x28c,0x28d,0x28e,0x28f,0x290,0x292,0x293,0x294,0x295,0x296,0x298,0x299,0x29a,0x29b,0x29c,
    0x29e,0x29f,0x2a0,0x2a1,0x2a2,0x2a4,0x2a5,0x2a6,0x2a7,0x2a9,0x2aa,0x2ab,0x2ac,0x2ae,0x2af,0x2b0,
    0x2b1,0x2b2,0x2b4,0x2b5,0x2b6,0x2b7,0x2b9,0x2ba,0x2bb,0x2bd,0x2be,0x2bf,0x2c0,0x2c2,0x2c3,0x2c4,
    0x2c5,0x2c7,0x2c8,0x2c9,0x2cb,0x2cc,0x2cd,0x2ce,0x2d0,0x2d1,0x2d2,0x2d4,0x2d5,0x2d6,0x2d8,0x2d9,
    0x2da,0x2dc,0x2dd,0x2de,0x2e0,0x2e1,0x2e2,0x2e4,0x2e5,0x2e6,0x2e8,0x2e9,0x2ea,0x2ec,0x2ed,0x2ee,
    0x2f0,0x2f1,0x2f2,0x2f4,0x2f5,0x2f6,0x2f8,0x2f9,0x2fb,0x2fc,0x2fd,0x2ff,0x300,0x302,0x303,0x304,
    0x306,0x307,0x309,0x30a,0x30b,0x30d,0x30e,0x310,0x311,0x312,0x314,0x315,0x317,0x318,0x31a,0x31b,
    0x31c,0x31e,0x31f,0x321,0x322,0x324,0x325,0x327,0x328,0x329,0x32b,0x32c,0x32e,0x32f,0x331,0x332,
    0x334,0x335,0x337,0x338,0x33a,0x33b,0x33d,0x33e,0x340,0x341,0x343,0x344,0x346,0x347,0x349,0x34a,
    0x34c,0x34d,0x34f,0x350,0x352,0x353,0x355,0x357,0x358,0x35a,0x35b,0x35d,0x35e,0x360,0x361,0x363,
    0x365,0x366,0x368,0x369,0x36b,0x36c,0x36e,0x370,0x371,0x373,0x374,0x376,0x378,0x379,0x37b,0x37c,
    0x37e,0x380,0x381,0x383,0x384,0x386,0x388,0x389,0x38b,0x38d,0x38e,0x390,0x392,0x393,0x395,0x397,
    0x398,0x39a,0x39c,0x39d,0x39f,0x3a1,0x3a2,0x3a4,0x3a6,0x3a7,0x3a9,0x3ab,0x3ac,0x3ae,0x3b0,0x3b1,
    0x3b3,0x3b5,0x3b7,0x3b8,0x3ba,0x3bc,0x3bd,0x3bf,0x3c1,0x3c3,0x3c4,0x3c6,0x3c8,0x3ca,0x3cb,0x3cd,
    0x3cf,0x3d1,0x3d2,0x3d4,0x3d6,0x3d8,0x3da,0x3db,0x3dd,0x3df,0x3e1,0x3e3,0x3e4,0x3e6,0x3e8,0x3ea,
    0x3ec,0x3ed,0x3ef,0x3f1,0x3f3,0x3f5,0x3f6,0x3f8,0x3fa,0x3fc,0x3fe,0x36c,
};

static const unsigned int volume_mapping_table[] = {
    0,1,3,5,6,8,10,11,13,14,16,17,19,20,22,23,25,26,27,29,30,32,33,34,36,37,39,41,43,45,47,49,
    50,52,54,55,57,59,60,61,63,64,66,67,68,69,71,72,73,74,75,76,77,79,80,81,82,83,84,84,85,86,87,88,
    89,90,91,92,92,93,94,95,96,96,97,98,99,99,100,101,101,102,103,103,104,105,105,106,107,107,108,109,109,110,110,111,
    112,112,113,113,114,114,115,115,116,117,117,118,118,119,119,120,120,121,121,122,122,123,123,123,124,124,125,125,126,126,127,127
};

// ---- OPL register write -> Nuked ------------------------------------------
static void OPL_WriteRegister(int reg, int value)
{
    if (reg == OPL_REG_NEW)
        opl_opl3mode = value & 0x01;
    OPL3_WriteRegBuffered(&opl_chip, (unsigned short) reg, (unsigned char) value);
}

// ======================= cribbed OPL voice machinery =======================
static opl_voice_t *GetFreeVoice(void)
{
    opl_voice_t *result; int i;
    if (voice_free_num == 0) return NULL;
    result = voice_free_list[0];
    voice_free_num--;
    for (i = 0; i < voice_free_num; i++) voice_free_list[i] = voice_free_list[i + 1];
    voice_alloced_list[voice_alloced_num++] = result;
    return result;
}

static void VoiceKeyOff(opl_voice_t *voice)
{
    OPL_WriteRegister((OPL_REGS_FREQ_2 + voice->index) | voice->array, voice->freq >> 8);
}

static void ReleaseVoice(int index)
{
    opl_voice_t *voice; int i;
    if (index >= voice_alloced_num) { voice_alloced_num = 0; voice_free_num = 0; return; }
    voice = voice_alloced_list[index];
    VoiceKeyOff(voice);
    voice->channel = NULL; voice->note = 0;
    voice_alloced_num--;
    for (i = index; i < voice_alloced_num; i++) voice_alloced_list[i] = voice_alloced_list[i + 1];
    voice_free_list[voice_free_num++] = voice;
}

static void LoadOperatorData(int op, genmidi_op_t *data, int max_level, unsigned int *volume)
{
    int level = data->scale;
    if (max_level) level |= 0x3f; else level |= data->level;
    *volume = level;
    OPL_WriteRegister(OPL_REGS_LEVEL + op, level);
    OPL_WriteRegister(OPL_REGS_TREMOLO + op, data->tremolo);
    OPL_WriteRegister(OPL_REGS_ATTACK + op, data->attack);
    OPL_WriteRegister(OPL_REGS_SUSTAIN + op, data->sustain);
    OPL_WriteRegister(OPL_REGS_WAVEFORM + op, data->waveform);
}

static void SetVoiceInstrument(opl_voice_t *voice, genmidi_instr_t *instr, unsigned int instr_voice)
{
    genmidi_voice_t *data; unsigned int modulating;
    if (voice->current_instr == instr && voice->current_instr_voice == instr_voice) return;
    voice->current_instr = instr;
    voice->current_instr_voice = instr_voice;
    data = &instr->voices[instr_voice];
    modulating = (data->feedback & 0x01) == 0;
    LoadOperatorData(voice->op2 | voice->array, &data->carrier, 1, &voice->car_volume);
    LoadOperatorData(voice->op1 | voice->array, &data->modulator, !modulating, &voice->mod_volume);
    OPL_WriteRegister((OPL_REGS_FEEDBACK + voice->index) | voice->array, data->feedback | voice->reg_pan);
    voice->priority = 0x0f - (data->carrier.attack >> 4) + 0x0f - (data->carrier.sustain & 0x0f);
}

static void SetVoiceVolume(opl_voice_t *voice, unsigned int volume)
{
    genmidi_voice_t *opl_voice;
    unsigned int midi_volume, full_volume, car_volume, mod_volume;
    voice->note_volume = volume;
    opl_voice = &voice->current_instr->voices[voice->current_instr_voice];
    midi_volume = 2 * (volume_mapping_table[voice->channel->volume] + 1);
    full_volume = (volume_mapping_table[voice->note_volume] * midi_volume) >> 9;
    car_volume = 0x3f - full_volume;
    if (car_volume != (voice->car_volume & 0x3f)) {
        voice->car_volume = car_volume | (voice->car_volume & 0xc0);
        OPL_WriteRegister((OPL_REGS_LEVEL + voice->op2) | voice->array, voice->car_volume);
        if ((opl_voice->feedback & 0x01) != 0 && opl_voice->modulator.level != 0x3f) {
            mod_volume = opl_voice->modulator.level;
            if (mod_volume < car_volume) mod_volume = car_volume;
            mod_volume |= voice->mod_volume & 0xc0;
            if (mod_volume != voice->mod_volume) {
                voice->mod_volume = mod_volume;
                OPL_WriteRegister((OPL_REGS_LEVEL + voice->op1) | voice->array,
                                  mod_volume | (opl_voice->modulator.scale & 0xc0));
            }
        }
    }
}

static void SetVoicePan(opl_voice_t *voice, unsigned int pan)
{
    genmidi_voice_t *opl_voice;
    voice->reg_pan = pan;
    opl_voice = &voice->current_instr->voices[voice->current_instr_voice];
    OPL_WriteRegister((OPL_REGS_FEEDBACK + voice->index) | voice->array, opl_voice->feedback | pan);
}

static void InitVoices(void)
{
    int i;
    voice_free_num = num_opl_voices;
    voice_alloced_num = 0;
    for (i = 0; i < num_opl_voices; ++i) {
        voices[i].index = i % OPL_NUM_VOICES;
        voices[i].op1 = voice_operators[0][i % OPL_NUM_VOICES];
        voices[i].op2 = voice_operators[1][i % OPL_NUM_VOICES];
        voices[i].array = (i / OPL_NUM_VOICES) << 8;
        voices[i].current_instr = NULL;
        voice_free_list[i] = &voices[i];
    }
}

static unsigned int FrequencyForVoice(opl_voice_t *voice)
{
    genmidi_voice_t *gm_voice; signed int freq_index; unsigned int octave, sub_index; signed int note;
    note = voice->note;
    gm_voice = &voice->current_instr->voices[voice->current_instr_voice];
    if ((RD_SHORT(voice->current_instr->flags) & GENMIDI_FLAG_FIXED) == 0)
        note += (signed short) RD_SHORT(gm_voice->base_note_offset);
    while (note < 0) note += 12;
    while (note > 95) note -= 12;
    freq_index = 64 + 32 * note + voice->channel->bend;
    if (voice->current_instr_voice != 0)
        freq_index += (voice->current_instr->fine_tuning / 2) - 64;
    if (freq_index < 0) freq_index = 0;
    if (freq_index < 284) return frequency_curve[freq_index];
    sub_index = (freq_index - 284) % (12 * 32);
    octave = (freq_index - 284) / (12 * 32);
    if (octave >= 7) octave = 7;
    return frequency_curve[sub_index + 284] | (octave << 10);
}

static void UpdateVoiceFrequency(opl_voice_t *voice)
{
    unsigned int freq = FrequencyForVoice(voice);
    if (voice->freq != freq) {
        OPL_WriteRegister((OPL_REGS_FREQ_1 + voice->index) | voice->array, freq & 0xff);
        OPL_WriteRegister((OPL_REGS_FREQ_2 + voice->index) | voice->array, (freq >> 8) | 0x20);
        voice->freq = freq;
    }
}

static void VoiceKeyOn(opl_channel_data_t *channel, genmidi_instr_t *instrument,
                       unsigned int instrument_voice, unsigned int note,
                       unsigned int key, unsigned int volume)
{
    opl_voice_t *voice = GetFreeVoice();
    if (voice == NULL) return;
    voice->channel = channel;
    voice->key = key;
    if ((RD_SHORT(instrument->flags) & GENMIDI_FLAG_FIXED) != 0) voice->note = instrument->fixed_note;
    else voice->note = note;
    voice->reg_pan = channel->pan;
    SetVoiceInstrument(voice, instrument, instrument_voice);
    SetVoiceVolume(voice, volume);
    voice->freq = 0;
    UpdateVoiceFrequency(voice);
}

static void ReplaceExistingVoice(void)
{
    int i, result = 0;
    for (i = 0; i < voice_alloced_num; i++)
        if (voice_alloced_list[i]->current_instr_voice != 0
         || voice_alloced_list[i]->channel >= voice_alloced_list[result]->channel)
            result = i;
    ReleaseVoice(result);
}

// ======================= MUS event handlers ================================
static opl_channel_data_t *ChannelFor(int ch) { return &channels[ch & 15]; }

static void KeyOff(int ch, int key)
{
    opl_channel_data_t *channel = ChannelFor(ch);
    int i;
    for (i = 0; i < voice_alloced_num; i++)
        if (voice_alloced_list[i]->channel == channel && (int)voice_alloced_list[i]->key == key) {
            ReleaseVoice(i); i--;
        }
}

static void KeyOn(int ch, int key, int volume)
{
    genmidi_instr_t *instrument;
    opl_channel_data_t *channel;
    int note = key;
    int double_voice;
    if (volume <= 0) { KeyOff(ch, key); return; }
    channel = ChannelFor(ch);
    if (ch == MUS_PERCUSSION_CHAN) {
        if (key < 35 || key > 81) return;
        instrument = &percussion_instrs[key - 35];
        note = 60;
    } else {
        instrument = channel->instrument;
    }
    if (instrument == NULL) return;
    double_voice = (RD_SHORT(instrument->flags) & GENMIDI_FLAG_2VOICE) != 0;
    if (voice_free_num == 0) ReplaceExistingVoice();
    VoiceKeyOn(channel, instrument, 0, note, key, volume);
    if (double_voice) VoiceKeyOn(channel, instrument, 1, note, key, volume);
}

static void ProgramChange(int ch, int instrument)
{
    if (instrument < 0 || instrument >= GENMIDI_NUM_INSTRS) return;
    ChannelFor(ch)->instrument = &main_instrs[instrument];
}

static void SetChannelVolume(opl_channel_data_t *channel, int volume, int clip_start)
{
    int i;
    channel->volume_base = volume;
    if (volume > current_music_volume) volume = current_music_volume;
    if (clip_start && volume > start_music_volume) volume = start_music_volume;
    channel->volume = volume;
    for (i = 0; i < num_opl_voices; ++i)
        if (voices[i].channel == channel) SetVoiceVolume(&voices[i], voices[i].note_volume);
}

static void SetChannelPan(opl_channel_data_t *channel, int pan)
{
    unsigned int reg_pan; int i;
    if (!opl_opl3mode) return;
    if (pan >= 96) reg_pan = 0x10; else if (pan <= 48) reg_pan = 0x20; else reg_pan = 0x30;
    if (channel->pan != (int)reg_pan) {
        channel->pan = reg_pan;
        for (i = 0; i < num_opl_voices; i++)
            if (voices[i].channel == channel) SetVoicePan(&voices[i], reg_pan);
    }
}

static void AllNotesOff(opl_channel_data_t *channel)
{
    int i;
    for (i = 0; i < voice_alloced_num; i++)
        if (voice_alloced_list[i]->channel == channel) { ReleaseVoice(i); i--; }
}

static void PitchBend(int ch, int bendbyte)   // MUS pitch 0..255, 128 = centre
{
    opl_channel_data_t *channel = ChannelFor(ch);
    int i;
    channel->bend = (bendbyte >> 1) - 64;
    for (i = 0; i < voice_alloced_num; ++i)
        if (voice_alloced_list[i]->channel == channel)
            UpdateVoiceFrequency(voice_alloced_list[i]);
}

static void ControllerEvent(int ch, int controller, int value)
{
    opl_channel_data_t *channel = ChannelFor(ch);
    switch (controller) {
        case 0:  ProgramChange(ch, value); break;           // instrument / program
        case 3:  SetChannelVolume(channel, value, 1); break; // volume
        case 4:  SetChannelPan(channel, value); break;       // pan
        default: break;                                      // bank/mod/expr/reverb/... ignore
    }
}

static void SystemEvent(int ch, int controller)
{
    // 10 = all sounds off, 11 = all notes off, 14 = reset all controllers
    if (controller == 10 || controller == 11 || controller == 14)
        AllNotesOff(ChannelFor(ch));
}

// ======================= MUS parser / sequencer ============================
static void InitChannel(opl_channel_data_t *channel)
{
    channel->instrument = &main_instrs[0];
    channel->volume = current_music_volume;
    channel->volume_base = 100;
    if (channel->volume > channel->volume_base) channel->volume = channel->volume_base;
    channel->pan = 0x30;
    channel->bend = 0;
}

static void OPL_InitRegisters(int opl3)
{
    int r;
    for (r = OPL_REGS_LEVEL; r <= OPL_REGS_LEVEL + OPL_NUM_OPERATORS; ++r) OPL_WriteRegister(r, 0x3f);
    for (r = OPL_REGS_ATTACK; r <= OPL_REGS_WAVEFORM + OPL_NUM_OPERATORS; ++r) OPL_WriteRegister(r, 0x00);
    for (r = 1; r < OPL_REGS_LEVEL; ++r) OPL_WriteRegister(r, 0x00);
    OPL_WriteRegister(OPL_REG_TIMER_CTRL, 0x60);
    OPL_WriteRegister(OPL_REG_TIMER_CTRL, 0x80);
    OPL_WriteRegister(OPL_REG_WAVEFORM_ENABLE, 0x20);
    if (opl3) {
        OPL_WriteRegister(OPL_REG_NEW, 0x01);
        for (r = OPL_REGS_LEVEL; r <= OPL_REGS_LEVEL + OPL_NUM_OPERATORS; ++r) OPL_WriteRegister(r | 0x100, 0x3f);
        for (r = OPL_REGS_ATTACK; r <= OPL_REGS_WAVEFORM + OPL_NUM_OPERATORS; ++r) OPL_WriteRegister(r | 0x100, 0x00);
        for (r = 1; r < OPL_REGS_LEVEL; ++r) OPL_WriteRegister(r | 0x100, 0x00);
    }
    OPL_WriteRegister(OPL_REG_FM_MODE, 0x40);
    if (opl3) OPL_WriteRegister(OPL_REG_NEW, 0x01);
}

// Process events until one carries the "last" flag; return the delay (in ticks)
// that follows. Returns -1 at end-of-score (after handling loop/stop).
static int ProcessEvents(void)
{
    for (;;) {
        int ev, type, ch, last, delay;
        if (score_pos >= score_len) { return -1; }
        ev = score_base[score_pos++];
        type = (ev >> 4) & 7;
        ch = ev & 0x0f;
        last = ev & 0x80;

        switch (type) {
            case 0:   // release note
                KeyOff(ch, score_base[score_pos++] & 0x7f);
                break;
            case 1: { // play note
                int note = score_base[score_pos++];
                int vol = -1;
                if (note & 0x80) vol = score_base[score_pos++] & 0x7f;
                KeyOn(ch, note & 0x7f, vol < 0 ? 127 : vol);
                break;
            }
            case 2:   // pitch bend
                PitchBend(ch, score_base[score_pos++]);
                break;
            case 3:   // system event
                SystemEvent(ch, score_base[score_pos++] & 0x7f);
                break;
            case 4: {  // change controller
                int c = score_base[score_pos++] & 0x7f;
                int v = score_base[score_pos++] & 0x7f;
                ControllerEvent(ch, c, v);
                break;
            }
            case 6:   // score end
                return -1;
            default:  // 5,7 unused
                break;
        }

        if (last) {
            delay = 0;
            for (;;) {
                int b;
                if (score_pos >= score_len) return -1;
                b = score_base[score_pos++];
                delay = (delay * 128) + (b & 0x7f);
                if ((b & 0x80) == 0) break;
            }
            return delay;
        }
    }
}

static void ResetAll(void)
{
    int i;
    for (i = 0; i < MIDI_CHANNELS; i++) InitChannel(&channels[i]);
    OPL_InitRegisters(opl_opl3mode);
    InitVoices();
}

// ======================= public API ========================================
void EvoMusic_Init(int samplerate, const void *genmidi_lump)
{
    const u8 *lump = (const u8 *) genmidi_lump;
    if (music_inited) return;
    out_rate = samplerate > 0 ? samplerate : 44100;
    samples_per_tick = out_rate / 140;
    if (samples_per_tick < 1) samples_per_tick = 1;

    num_opl_voices = opl_opl3mode ? (OPL_NUM_VOICES * 2) : OPL_NUM_VOICES;

    // GENMIDI lump: 8-byte header "#OPL_II#", then 128 instrs + 47 percussion.
    main_instrs = (genmidi_instr_t *) (lump + strlen(GENMIDI_HEADER));
    percussion_instrs = main_instrs + GENMIDI_NUM_INSTRS;

    OPL3_Reset(&opl_chip, (unsigned) out_rate);
    ResetAll();
    music_inited = 1;
    printf("MUSIC: EVO OPL (Nuked-OPL3) ready: %d Hz, %d voices, GENMIDI loaded\n",
           out_rate, num_opl_voices);
}

int EvoMusic_Register(const void *data)
{
    const u8 *d = (const u8 *) data;
    int score_start, slen;
    if (!music_inited || d == NULL) return 0;
    if (memcmp(d, "MUS\x1a", 4) != 0) {
        printf("MUSIC: not a MUS lump (bad header)\n");
        return 0;
    }
    slen        = d[4] | (d[5] << 8);          // score length
    score_start = d[6] | (d[7] << 8);          // score start offset
    pthread_mutex_lock(&music_lock);
    mus_data = d;
    score_base = d + score_start;
    score_len = slen;
    score_pos = 0;
    song_playing = 0;
    pthread_mutex_unlock(&music_lock);
    return 1;
}

void EvoMusic_Play(int looping)
{
    if (!music_inited || mus_data == NULL) return;
    pthread_mutex_lock(&music_lock);
    start_music_volume = current_music_volume;
    ResetAll();
    score_pos = 0;
    delay_samples = 0;
    song_looping = looping;
    song_paused = 0;
    song_playing = 1;
    pthread_mutex_unlock(&music_lock);
}

void EvoMusic_Stop(void)
{
    if (!music_inited) return;
    pthread_mutex_lock(&music_lock);
    song_playing = 0;
    { int i; for (i = 0; i < MIDI_CHANNELS; i++) AllNotesOff(&channels[i]); }
    pthread_mutex_unlock(&music_lock);
}

void EvoMusic_Pause(int paused)
{
    pthread_mutex_lock(&music_lock);
    song_paused = paused;
    if (paused) { int i; for (i = 0; i < MIDI_CHANNELS; i++) AllNotesOff(&channels[i]); }
    pthread_mutex_unlock(&music_lock);
}

void EvoMusic_SetVolume(int volume)
{
    int i;
    if (volume < 0) volume = 0; if (volume > 127) volume = 127;
    pthread_mutex_lock(&music_lock);
    if (current_music_volume != volume) {
        current_music_volume = volume;
        for (i = 0; i < MIDI_CHANNELS; ++i) {
            if (i == MUS_PERCUSSION_CHAN) SetChannelVolume(&channels[i], volume, 0);
            else SetChannelVolume(&channels[i], channels[i].volume_base, 0);
        }
    }
    pthread_mutex_unlock(&music_lock);
}

// Fill `frames` stereo S16 samples with the current song (silence if stopped).
void EvoMusic_Render(short *stereo, int frames)
{
    int done = 0;
    if (!music_inited) { memset(stereo, 0, frames * 2 * sizeof(short)); return; }

    pthread_mutex_lock(&music_lock);
    if (!song_playing || song_paused) {
        memset(stereo, 0, frames * 2 * sizeof(short));
        pthread_mutex_unlock(&music_lock);
        return;
    }
    while (done < frames) {
        int n;
        if (delay_samples <= 0) {
            int ticks = ProcessEvents();
            if (ticks < 0) {                    // end of score
                if (song_looping) {
                    score_pos = 0;
                    { int i; for (i = 0; i < MIDI_CHANNELS; i++) AllNotesOff(&channels[i]); }
                    continue;
                }
                song_playing = 0;
                memset(stereo + done * 2, 0, (frames - done) * 2 * sizeof(short));
                break;
            }
            delay_samples = ticks * samples_per_tick;
            if (delay_samples == 0) continue;   // zero-tick delay: process more events
        }
        n = frames - done;
        if (n > delay_samples) n = delay_samples;
        OPL3_GenerateStream(&opl_chip, stereo + done * 2, (unsigned) n);
        done += n;
        delay_samples -= n;
    }
    pthread_mutex_unlock(&music_lock);
}

void EvoMusic_Shutdown(void)
{
    if (!music_inited) return;
    EvoMusic_Stop();
    music_inited = 0;
}
