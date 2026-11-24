/*
 * DOOM Sound Interface - Stub Implementation
 * 
 * This is a no-op sound implementation for when audio is not needed
 * or to simplify the build process.
 */

#include <stdio.h>
#include <stdlib.h>

#include "doomstat.h"
#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

// DOOM sound interface stubs

void I_SetChannels() {
    // Stub
}

int I_GetSfxLumpNum(sfxinfo_t* sfx) {
    return 0;
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority) {
    return 0;
}

void I_StopSound(int handle) {
    // Stub  
}

int I_SoundIsPlaying(int handle) {
    return 0;
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch) {
    // Stub
}

void I_UpdateSound(void) {
    // Stub
}

void I_SubmitSound(void) {
    // Stub
}

void I_InitSound() {
    printf("DOOM: Sound system disabled (using stub implementation)\n");
}

void I_ShutdownSound(void) {
    // Stub
}

void I_SetMusicVolume(int volume) {
    // Stub
}

void I_SetSfxVolume(int volume) {
    // Stub
}

void I_InitMusic(void) {
    printf("DOOM: Music system disabled (using stub implementation)\n");
}

void I_ShutdownMusic(void) {
    // Stub
}

void I_PlaySong(int handle, int looping) {
    // Stub
}

void I_PauseSong(int handle) {
    // Stub
}

void I_ResumeSong(int handle) {
    // Stub
}

void I_StopSong(int handle) {
    // Stub
}

void I_UnRegisterSong(int handle) {
    // Stub
}

int I_RegisterSong(void* data) {
    return 1;
}

int I_QrySongPlaying(int handle) {
    return 0;
}
