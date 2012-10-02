#include <stdio.h>
#include "read-event.h"

int main(int argc, char **argv) {
    spnav_event spev;
    int flush;
    int x, y, z, yaw, pitch, roll;

    x = 0; y = 0; z = 0;
    yaw = 0; pitch = 0; roll = 0;

    init_spacenav("/dev/input/spacenavigator");
    printf("X\tY\tZ\tYaw\tpitch\troll\n");

    while (1) {
        if (get_spacenav_event(&spev, &flush)) {
            if (flush) {
                if (x != 0 || y != 0 || z != 0) {
                    printf("%d\t%d\t%d\t%d\t%d\t%d\n", x, y, z, yaw, pitch, roll);
                }
                x = 0; y = 0; z = 0;
                yaw = 0; pitch = 0; roll = 0;
            }
            else if (spev.type == SPNAV_MOTION) {
                x += spev.x;
                y += spev.y;
                z += spev.z;
                yaw += spev.yaw;
                pitch += spev.pitch;
                roll += spev.roll;
            }
        }
    }
}
