#ifndef EVO_COMPAT_H
#define EVO_COMPAT_H

// EVO compatibility header
// Provides safe implementations to avoid library compatibility issues

// Safe case-insensitive string comparison
int evo_strcasecmp(const char *s1, const char *s2);

// Override problematic functions
#define strcasecmp evo_strcasecmp
#define strcmpi evo_strcasecmp

#endif // EVO_COMPAT_H
