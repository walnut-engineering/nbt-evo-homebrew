/* evo_trace_shim v2 - replaces /opt/car/bin/trace-recorder-app (proc #9/#10,
 * IGNORE_ALL => cannot brick boot). Logs each step to /dev/shmem (RAM, always
 * available even at early boot), spawns the boot sniffer launcher from sda0
 * (reachable early, unlike /fs/sda1), then exec()s the real binary. */
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
    logln("shim: entered");
    if(fork()==0){
        setsid();
        logln("shim: child -> exec launcher");
        execl("/bin/sh","sh","/opt/car/bin/veom_capture.sh",(char*)0);
        logln("shim: child exec FAILED");
        _exit(127);
    }
    logln("shim: parent -> exec real");
    execv("/opt/car/bin/trace-recorder-app.real", argv);
    logln("shim: parent exec real FAILED");
    _exit(127);
}
