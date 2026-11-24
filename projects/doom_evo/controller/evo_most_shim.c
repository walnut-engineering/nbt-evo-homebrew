/* evo_most_shim - tiny ELF the starter execs in place of /opt/sys/bin/most
 * (starter.cfg Process #105-107, IGNORE_ALL, no respawn).  It execs the real
 * binary (most.real) with the original argv, so most runs with full MoCCA
 * integration (same pid, same starter parent, same environment), and adds
 * LD_PRELOAD=evo_tee.so ONLY when that is provably safe:
 *
 *   - a failed LD_PRELOAD is FATAL on this QNX (ldqnx refuses to run the host),
 *     so the tee is dlopen()-tested first; if it does not load, most runs clean;
 *   - /fs/sda1 might not be mounted yet when most starts: missing .so -> clean;
 *   - kill switch: create /fs/sda1/evo/most_tee.off -> clean, no sda0 remount
 *     needed to back out (rename/remove the .so works too).
 *
 * It ALSO starts the DOOM-tile autostart supervisor (/fs/sda1/evo/evo_autostart.sh,
 * see that script) in a detached child BEFORE the tee's LD_PRELOAD is set, so the
 * supervisor and the daemons it runs never get the tee. Kill switch for that:
 * /fs/sda1/evo/autostart.off (or remove the script). The child is a new session
 * with stdio on /dev/null + a log, so the starter never waits on it.
 *
 * `evo_most_shim --evo-check` reports what it would do and exits 0/1 (bench test).
 * `evo_most_shim --evo-spawn` runs ONLY the autostart spawn (test without a reboot).
 * If the exec of most.real fails the shim exits 127 - most simply is not running,
 * recoverable over SSH (independent of the starter): remount /fs/sda0 rw, copy
 * most.real back over most, reboot.
 */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>

#define REAL "/opt/sys/bin/most.real"
#define TEE  "/fs/sda1/evo/evo_tee_most.so"
#define OFF  "/fs/sda1/evo/most_tee.off"
#define AUTO     "/fs/sda1/evo/evo_autostart.sh"
#define AUTO_OFF "/fs/sda1/evo/autostart.off"
#define AUTO_LOG "/fs/sda1/evo/autostart_spawn.log"

/* Why the autostart would (not) run; also used by --evo-check. */
static const char *autostart_state(int *ok) {
    *ok = 0;
    if (access(AUTO_OFF, F_OK) == 0) return "kill switch " AUTO_OFF " present";
    if (access(AUTO, R_OK) != 0)     return "script " AUTO " not readable (sda1 not mounted?)";
    *ok = 1; return "ok";
}

/* Detached child: new session, stdio -> /dev/null + log, LD_PRELOAD cleared, exec sh. */
static void autostart_spawn(void) {
    pid_t pid = fork();
    if (pid != 0) return;                       /* parent (or fork failed): carry on */
    setsid();
    int nul = open("/dev/null", O_RDWR);
    int log = open(AUTO_LOG, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (nul >= 0) dup2(nul, 0);
    if (log >= 0) { dup2(log, 1); dup2(log, 2); }
    if (nul > 2) close(nul);
    if (log > 2) close(log);
    unsetenv("LD_PRELOAD");
    execl("/bin/sh", "sh", AUTO, (char *)0);
    _exit(127);
}

int main(int argc, char **argv) {
    const char *why = "ok";
    int ok = 0, auto_ok = 0;
    const char *auto_why = autostart_state(&auto_ok);
    int check = (argc > 1 && strcmp(argv[1], "--evo-check") == 0);
    if (argc > 1 && strcmp(argv[1], "--evo-spawn") == 0) {
        printf("autostart %s: %s\n", auto_ok ? "SPAWNING" : "DISABLED", auto_why);
        if (auto_ok) autostart_spawn();
        return auto_ok ? 0 : 1;
    }
    if (auto_ok && !check) autostart_spawn();   /* before LD_PRELOAD is set */
    if (access(OFF, F_OK) == 0) why = "kill switch " OFF " present";
    else if (access(TEE, R_OK) != 0) why = "tee .so not readable (sda1 not mounted?)";
    else {
        void *h = dlopen(TEE, RTLD_NOW);
        if (!h) why = dlerror();
        else { dlclose(h); ok = 1; }
    }
    if (ok) { setenv("LD_PRELOAD", TEE, 1); setenv("EVO_TEE_TAG", "most", 1); }
    if (check) {
        printf("tee %s: %s\n", ok ? "ENABLED" : "DISABLED", why);
        printf("autostart %s: %s\n", auto_ok ? "ENABLED" : "DISABLED", auto_why);
        printf("real binary %s: %s\n", REAL, access(REAL, X_OK) == 0 ? "ok" : "MISSING");
        return ok ? 0 : 1;
    }
    execv(REAL, argv);
    _exit(127);
}
