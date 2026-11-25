/* sync_shim.c - the GCC atomic builtin snes9x2010 uses. gcc 4.4.2 for QNX/ARM emits a
 * call to a libgcc helper that does not exist on this target, so provide it with the
 * ARMv7 exclusive-access instructions. */
unsigned __sync_fetch_and_add_4(volatile unsigned *p, unsigned v)
{
    unsigned old, sum, fail;
    do {
        __asm__ __volatile__("ldrex %0, [%3]\n\tadd %1, %0, %4\n\tstrex %2, %1, [%3]"
                             : "=&r"(old), "=&r"(sum), "=&r"(fail) : "r"(p), "r"(v) : "cc", "memory");
    } while (fail);
    return old;
}
