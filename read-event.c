// Released into the public domain, 4 Mar 2011
// Google, Inc. Jason E. Holt <jholt [at] google.com>
//
// Simple example of how to read and parse input_event structs from device
// files like those found in /dev/input/event* for multi-axis devices such
// as the 3dconnexion Space Navigator.
//
// Our navigator shows up as:
// Bus 007 Device 004: ID 0510:1004 Sejin Electron, Inc.

#include <sys/ioctl.h>
#include <error.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>
#include <unistd.h>

int spacenav_fd;

int init_spacenav(const char *dev_name) {
    if ((spacenav_fd = open(dev_name, O_RDONLY | O_NONBLOCK)) < 0) {
        perror("opening the file you specified");
        return -1;
    }
    return 1;
}

struct input_event *get_spacenav_event() {
    int x, y, z, yaw, pitch, roll;
    x = y = z = yaw = pitch = roll = 0;
    struct input_event ev;
    struct input_event *event_data = &ev;

    int num_read = read(spacenav_fd, event_data, sizeof(ev));

    if (sizeof(ev) != num_read) {
        return NULL;
    }

    if (event_data->type == EV_KEY) {
        fprintf(stderr, "button press\n");
    } else if (event_data->type == EV_SYN) {
    // EV_SYN type may be quite useful for some devices
    // identifies the sent data complete and therefore apply-able
//      fprintf(stderr, "sync event\n");
    } else if (event_data->type == EV_REL || event_data->type == EV_ABS) {
        int axis = event_data->code;
        int amount = event_data->value;

        switch(axis) {
            case 0:
                x = amount;
                break;
            case 1:
                y = amount;
                break;
            case 2:
                z = amount;
                break;
            case 3:
                pitch = amount;
                break;
            case 4:
                roll = amount;
                break;
            case 5:
                yaw = amount;
                break;
            default:
                fprintf(stderr, "unknown axis event\n");
                break;
        }

            printf("{\"topic\":\"spacenav\",\"x\":%d,\"y\":%d,\"z\":%d,\"yaw\":%d,\"pitch\":%d,\"roll\":%d}\n",
                x,y,z,yaw,pitch,roll);
    } else {
        int evtype = event_data->type;

        fprintf(stderr, "Unknown event type \"%d\".\n", evtype);
    }
    return NULL;
}
