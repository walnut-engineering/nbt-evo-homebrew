#include "evo_compat.h"
#include <string.h>
#include <ctype.h>

// Safe case-insensitive string comparison
int evo_strcasecmp(const char *s1, const char *s2) {
    if (s1 == NULL || s2 == NULL) {
        return (s1 == s2) ? 0 : (s1 == NULL) ? -1 : 1;
    }
    
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        
        if (c1 != c2) {
            return c1 - c2;
        }
        
        s1++;
        s2++;
    }
    
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}
