#ifndef _read_event_h_
#define _read_event_h_

#include <linux/input.h>
#include <X11/Xlib.h>

typedef struct {
    int x, y, z, yaw, pitch, roll, button;
} spnav_event;

int init_spacenav(const char *dev_name);
int get_spacenav_event(XEvent *);

#endif
