//
// i_oplmusic_evo.h - EVO OPL music backend (MUS -> Nuked-OPL3 -> PCM).
// Transport-agnostic: the caller supplies the GENMIDI lump and the MUS data,
// and pulls rendered stereo S16 PCM to mix into its own output.
//
#ifndef I_OPLMUSIC_EVO_H
#define I_OPLMUSIC_EVO_H

// Initialise at the given output sample rate; genmidi_lump points at the raw
// "GENMIDI" lump (8-byte "#OPL_II#" header + instrument tables).
void EvoMusic_Init(int samplerate, const void *genmidi_lump);

// Register a song (raw MUS lump). Returns 1 on success, 0 on failure.
int  EvoMusic_Register(const void *mus_data);

// Start / stop / pause the registered song.
void EvoMusic_Play(int looping);
void EvoMusic_Stop(void);
void EvoMusic_Pause(int paused);

// Music volume, 0..127.
void EvoMusic_SetVolume(int volume);

// Render `frames` interleaved stereo S16 samples of the current song
// (silence when nothing is playing). Safe to call from the mixer thread.
void EvoMusic_Render(short *stereo, int frames);

void EvoMusic_Shutdown(void);

#endif
