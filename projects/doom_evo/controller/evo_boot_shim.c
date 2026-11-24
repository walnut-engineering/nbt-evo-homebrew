/* evo_boot_shim - generic early-boot sniffer launcher. Installed in place of an
 * enabled, long-lived, IGNORE_ALL starter process (host = OnOffHuOmapApp #0).
 * Derives the real binary as argv[0] + ".real", logs to /dev/shmem, spawns the
 * boot sniffer launcher (sda0, reachable early), then exec()s the real binary
 * with the original argv so the host runs normally. IGNORE_ALL host => if this
 * ever failed, the starter just skips it (no brick). */
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
static void logln(const char *s){
    int fd=open("/dev/shmem/evo_shim.log",O_WRONLY|O_CREAT|O_APPEND,0666);
    if(fd>=0){ write(fd,s,strlen(s)); write(fd,"\n",1); close(fd); }
}
int main(int argc, char **argv){
    (void)argc;
    char real[512];
    logln("shim: entered");
    logln(argv[0]?argv[0]:"(null argv0)");
    if(fork()==0){
        setsid();
        logln("shim: child -> exec launcher");
        execl("/bin/sh","sh","/opt/car/bin/veom_capture.sh",(char*)0);
        logln("shim: child exec FAILED");
        _exit(127);
    }
    real[0]=0;
    if(argv[0]){ strncpy(real,argv[0],sizeof(real)-6); real[sizeof(real)-6]=0; strcat(real,".real"); }
    logln("shim: parent -> exec real");
    logln(real);
    execv(real, argv);
    logln("shim: parent exec real FAILED");
    _exit(127);
}
