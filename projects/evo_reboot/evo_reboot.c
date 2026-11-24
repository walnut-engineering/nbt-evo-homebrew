/* evo_reboot - reboot the NBT EVO head unit (QNX has no shutdown/reboot in PATH).
 * Uses the QNX sysmgr reboot. Run as root: /fs/sda1/evo/evo_reboot */
#include <stdio.h>
#include <sys/sysmgr.h>
int main(void) {
    printf("evo_reboot: requesting system reboot...\n"); fflush(stdout);
    int rc = sysmgr_reboot();
    printf("evo_reboot: sysmgr_reboot() returned %d (errno-ish)\n", rc);
    return rc;
}
