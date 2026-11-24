/* evo_tcd_shim - tiny ELF that the starter execs in place of touchComDevice.
 * It sets LD_PRELOAD to our tee .so (on persistent /fs/sda1) and exec()s the
 * real binary with the original argv, so touchComDevice runs with full MoCCA
 * integration (same pid, same starter parent, same environment) plus the tee.
 *
 * A compiled ELF (not a shell script) so ANY spawn/exec path the starter uses
 * will launch it.  If exec of the real binary fails, exit non-zero (touchCom
 * simply won't start - recoverable over SSH, which is independent of the starter).
 */
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    (void)argc;
    setenv("LD_PRELOAD", "/fs/sda1/evo/evo_tee.so", 1);
    execv("/opt/car/bin/touchComDevice.real", argv);
    _exit(127);
}
