#ifndef _read_event_h_
#define _read_event_h_

#include <linux/input.h>
#include <X11/Xlib.h>

#define SPNAV_MOTION = 0;
#define SPNAV_BUTTON = 1;

typedef struct {
    int type;
    int button1, button2;
} spnav_button;

typedef struct {
    int type;
    int x, y, z, yaw, pitch, roll, button;
} spnav_motion;

typedef union {
    spnav_motion motion;
    spnav_button button;
} spnav_event;

int init_spacenav(const char *dev_name);
int get_spacenav_event(spnav_event *);

#endif
