#include <freeglut.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/queue.h>
#include "wand/magick_wand.h"
#include "read-event.h"
#define ADDR_LEN 500

const char VERSION[] = "0.1";

unsigned char *tex_buffer;
float
    zoom_factor = 1,    /* 1 == "normal size" */
    horiz_disp = 0,     /* disp == displacement */
    vert_disp = 0;
int quit_main_loop = 0;     /* Flag to exit the program */
int image_index = 0,        /* Which image are we supposed to be looking at now? */
    num_images = 0;
char **images;              /* Array of image names, initialized from argv */
float screen_width, screen_height;
float texture_aspect;
long double texture_width, texture_height;
float tex_min_x, tex_min_y, tex_max_x, tex_max_y;   /* Texture coordinates */
int *send_sockets;
int num_sockets = 0;
int has_slaves = 0;

struct slavehost_s {
    char addr[ADDR_LEN];
    int port, broadcast;
    int socket;
    LIST_ENTRY(slavehost_s) entries;
};
LIST_HEAD(slavelisthead, slavehost_s) slave_list;

struct {
    int verbose, fullscreen;
    int use_spacenav, swapaxes;
    float sensitivity;
    char *spacenav_dev;
    char listenaddr[ADDR_LEN];
    int valid_listenaddr;
    int listenport;
    int multicast;
} options = {
    0,      /* verbose */
    0,      /* fullscreen */
    0,      /* use_spacenav */
    1,      /* swapaxes */
    0.2,    /* sensitivity */
    NULL,   /* spacenav_dev */
    "",     /* listenaddr */
    0,      /* valid listenaddr */
    -1,     /* listenport */
    0       /* multicast */ /* XXX add this to getoptions */
};

void setup_texture(void);

void usage(const char *pname) {
    fprintf(stderr, "%s%s\n\n%s%s%s\n",
"Liquid Galaxy Panoramic Image Viewer, version ", VERSION,
"USAGE: ", pname, " <options> image_file[, image_file, ...]\n\n"
"OPTIONS:\n"
"\t-v, --verbose\n"
"\t\tInclude extra output\n"
"\t-f, --fullscreen\n"
"\t\tMake the window full screen\n"
"\t-s, --spacenav[=device_name]\n"
"\t\tAccept input from the space navigator. Optionally, allows a custom device name\n"
"\t\tto replace the default, /dev/input/spacenavigator\n"
"\t--sensitivity=value\n"
"\t\tChange the space navigator's sensitivity. Larger numbers make the device more\n"
"\t\tsensitive. The default is 0.02\n"
"\t-w, --swapaxes\n"
"\t\tReverse the direction the image moves on input from the space navigator.\n"
"\t-h, --help\n"
"\t\tDisplay this help text including version information.\n"
"\t--listen=[addr:]port\n"
"\t\tListen for UDP synchronization traffic on addr:port. Addr defaults to INADDR_ANY\n"
"\t--multicast\n"
"\t\tUsed only when --listen is specified; indicates that the listen addr is a multicast\n"
"\t\tgroup we need to join\n"
"\t--slave=addr:port, --bcastslave=addr:port\n"
"\t\tAdds addr:port as a slave to receive UDP synchronization traffic. The bcastslave\n"
"\t\toption indicates that the slave's address is a broadcast address\n"
    );
}

typedef struct {
    int flag, img_idx;
    int horiz_disp, vert_disp;
    float tex_min_x, tex_max_x, tex_min_y, tex_max_y;
} sync_struct;

void udp_handler(int recv_socket) {
    sync_struct data;

    if (read(recv_socket, &data, sizeof(sync_struct)) >= (ssize_t) sizeof(sync_struct)) {
        if ( data.flag == 1234) {
            if (options.verbose) {
                fprintf(stderr, "%d, %d, %d, %d, %f, %f, %f, %f\n",
                    data.flag, data.img_idx,
                    data.horiz_disp, data.vert_disp,
                    data.tex_min_x, data.tex_min_y,
                    data.tex_max_x, data.tex_max_y);
            }

            if (image_index != data.img_idx) {
                if (data.img_idx >= num_images || data.img_idx < 0) {
                    fprintf(stderr, "ERROR: Tried to cycle past the end of the image list (image_index = %d, num_images = %d). Is the list of images on your command line identical to the master, and do all the images actually exist?\n", data.img_idx, num_images);
                    exit(1);
                }
                setup_texture();
            }
            horiz_disp = data.horiz_disp;
            vert_disp = data.vert_disp;
            tex_min_x = data.tex_min_x;
            tex_min_y = data.tex_min_y;
            tex_max_x = data.tex_max_x;
            tex_max_y = data.tex_max_y;

            glutPostRedisplay();
        }
        else {
            fprintf(stderr, "Wrong flag value\n");
        }
    }
}

int get_addr_port(char *addr, int *port, char *arg) {
    char *p;
    int ret = 0;

    p = strchr(arg, ':');
    if (p) {
        ret = 1;
        *port = atoi(p+1);
        if (p - arg > ADDR_LEN) {
            fprintf(stderr, "Warning: I can't deal with addresses longer than %d characters\n", ADDR_LEN);
            ret = -1;
            p = arg + ADDR_LEN - 1;
        }
        strncpy(addr, arg, p - arg); 
    }
    else {
        *port = atoi(arg);
    }
    return ret;
}

int setup_slave(struct slavehost_s *slave, const int broadcast, char *args) {
    struct sockaddr_in addr;
    struct hostent *server;
    int dummy = 1;

    if (!get_addr_port(slave->addr, &slave->port, args)) {
        fprintf(stderr, "ERROR: You must include a host in --listen=%s", args);
        exit(1);
    }

    if (options.verbose) {
        fprintf(stderr, "Opening socket to %s:%d\n", slave->addr, slave->port);
    }
    slave->socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (slave->socket == 0) {
        perror("Couldn't open socket");
        exit(0);
    }
    if (broadcast)
        setsockopt(slave->socket, SOL_SOCKET, SO_BROADCAST, &dummy, sizeof(int));
    server = gethostbyname(slave->addr);
    if (server == NULL) {
        perror("Couldn't figure out host");
        exit(0);
    }

    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
    addr.sin_port = htons(slave->port);
    if (connect(slave->socket, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("Error connecting UDP client");
        return 0;
    }
    return 1;
}


void get_options(const int argc, char * const argv[]) {
    int opt_index, c, broadcast;
    struct slavehost_s *new_slave;
    
    while (1) {
        broadcast = 0;

        static struct option long_options[] = {
            { "bcastslave",  required_argument,  NULL, 'B' },
            { "slave",       required_argument,  NULL, 'S' },
            { "sensitivity", required_argument,  NULL, 'e' },
            { "fullscreen",  no_argument,        NULL, 'f' },
            { "help",        no_argument,        NULL, 'h' },
            { "listen",      required_argument,  NULL, 'l' },
            { "multicast",   no_argument,        NULL, 'm' },
            { "spacenav",    optional_argument,  NULL, 's' },
            { "verbose",     no_argument,        NULL, 'v' },
            { "swapaxes",    no_argument,        NULL, 'w' },
            { 0,             0,                  0,     0  }
        };

        c = getopt_long(argc, argv, "vfs::e:whl:", long_options, &opt_index);
        if (c == -1) break;

        switch (c) {
            case 'm':
                options.multicast = 1;
                break;
            case 'B':
                broadcast = 1;
                /* The missing break here is intentional */
            case 'S':
                if (! has_slaves) {
                    has_slaves = 1;
                    LIST_INIT(&slave_list);
                }

                new_slave = (struct slavehost_s *) malloc(sizeof(struct slavehost_s));
                if (!new_slave) {
                    perror("Couldn't allocate memory for new slave");
                    exit(1);
                }

                if (setup_slave(new_slave, broadcast, optarg))
                    LIST_INSERT_HEAD(&slave_list, new_slave, entries);

                break;
            case 'l':
                if (get_addr_port(options.listenaddr, &options.listenport, optarg) != 0)
                    options.valid_listenaddr = 1;
                break;
            case 'h':
                usage(argv[0]);
                exit(1);
            case 'v':
                options.verbose++;
                break;
            case 'f':
                options.fullscreen = 1;
                break;
            case 'e':
                options.sensitivity = atof(optarg);
                break;
            case 's':
                options.use_spacenav = 1;
                if (optarg != NULL) options.spacenav_dev = optarg;
                break;
            case 'w':
                options.swapaxes = -1;
                break;
            default:
                /* Unrecognized option */
                usage(argv[0]);
                exit(-1);
        }
    }

    if (optind < argc) {
        /* Setup images array */
        num_images = argc - optind;
        images = malloc(num_images * sizeof(char *));

        if (!images) {
            fprintf(stderr, "ERROR: Couldn't allocate memory for image array.\n");
            exit(-1);
        }
        memcpy(images, argv + optind, sizeof(char *) * num_images);
        /* XXX Perhaps make this preload the images, so I don't have to reload
         * them each time I cycle into one */
    }
    else {
        fprintf(stderr, "ERROR: No images found on the command line\n");
        usage(argv[0]);
    }
}

void translate(float h, float v, float z) {
    /* Calculate texture coordinates */
    /* XXX One bad constraint shouldn't invalidate all other translations in one call */
    /* XXX If zooming out would be blocked because of constraints, can I
     * translate some and still zoom? */

    /* Ratio of x size to y size, in normalized texture coordinates */
    float x2y = screen_width * texture_height / screen_height / texture_width;
    float xmax, xmin, ymax, ymin;

    struct slavehost_s *slave;
    sync_struct sync;

    /* Constrain zooming */
    if (zoom_factor + z >= 1)
        zoom_factor += z;
    else if (options.verbose) {
        fprintf(stderr, "Violated zoom constraint (val: %f, delta: %f)\n", zoom_factor, z);
    }

    xmin = (h + horiz_disp) + 0.5 - 1.0 / 2.0 / zoom_factor * x2y;
    xmax = (h + horiz_disp) + 0.5 + 1.0 / 2.0 / zoom_factor * x2y;

    ymin = (v + vert_disp) + 0.5 - 1.0 / 2.0 / zoom_factor;
    ymax = (v + vert_disp) + 0.5 + 1.0 / 2.0 / zoom_factor;

    /* XXX Constrain vertical movement */
//        if (ymax - ymin > 1) {
//            /* Change zoom factor */
//            vert_disp = 0;
//            zoom_factor = 1.0;
//            //done++;
//            continue;
//        }
//        if (ymin < 0) {
//            fprintf(stderr, "Changing vert_disp from %f to ", vert_disp);
//            vert_disp = 1.0 / 2.0 / zoom_factor - 0.5 - v;
//            vert_disp -= (vert_disp * 0.01);
//            fprintf(stderr, "%f\n", vert_disp);
//            done++;
//            continue;
//        }
//        else if (ymin > 1) {
//            fprintf(stderr, "Changing vert_disp from %f to ", vert_disp);
//            vert_disp = 1.0 / 2.0 / zoom_factor + 0.5 - v;
//            vert_disp += (vert_disp * 0.01);
//            fprintf(stderr, "%f\n", vert_disp);
//            done++;
//            continue;
//        }
//        done = 10;


    horiz_disp += h;
    vert_disp += v;

    tex_min_x = xmin;
    tex_max_x = xmax;
    tex_min_y = ymin;
    tex_max_y = ymax;

    if (options.verbose) {
        fprintf(stderr, "Texture coords: (%f, %f) -> (%f, %f)\n",
            tex_min_x, tex_min_y, tex_max_x, tex_max_y);
        fprintf(stderr, "Screen: (%d, %d)\timage: (%ld, %ld)\n",
            (int) screen_width, (int) screen_height,
            (unsigned long int) texture_width, (unsigned long int) texture_height);
        fprintf(stderr, "Disp/zoom: (%f, %f, %f)\n\n", horiz_disp, vert_disp, zoom_factor);
    }

    /* Notify slaves */
    sync.flag = 1234;
    sync.img_idx = image_index;
    sync.horiz_disp = horiz_disp;
    sync.vert_disp = vert_disp;
    sync.tex_min_x = tex_min_x;
    sync.tex_min_y = tex_min_y;
    sync.tex_max_x = tex_max_x;
    sync.tex_max_y = tex_max_y;

    LIST_FOREACH(slave, &slave_list, entries) {
        if (write(slave->socket, &sync, sizeof(sync)) <= 0 && options.verbose) {
            fprintf(stderr, "Write returned 0 or -1; writing to %s:%d may have failed\n", slave->addr, slave->port);
        }
    }

    /* Make sure we redraw */
    glutPostRedisplay();
}

/* GLUT's callback to render the image */
static void render_scene(void) {
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 1);
    glBegin(GL_QUADS);
        /* The biggest thing I need to do is change these texture coordinates
         * around based on spacenav input, ensuring they keep the proper aspect
         * ratio at all times. <strike>I don't yet know how to make h360
         * work...</strike> OpenGL lets me tile textures, and even does it by
         * default. */
        glTexCoord2f(tex_min_x, tex_min_y);  glVertex3f(-1, 1, 0);
        glTexCoord2f(tex_max_x, tex_min_y);  glVertex3f(1, 1, 0);
        glTexCoord2f(tex_max_x, tex_max_y);  glVertex3f(1, -1, 0);
        glTexCoord2f(tex_min_x, tex_max_y);  glVertex3f(-1, -1, 0);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    glutSwapBuffers();
}

void setup_texture(void) {
    MagickWand *wand;

    wand = NewMagickWand();
    MagickReadImage(wand, images[image_index]);

    texture_width = MagickGetImageWidth(wand);
    texture_height = MagickGetImageHeight(wand);
    texture_aspect = 1.0 * texture_width / texture_height;

    tex_buffer = (unsigned char *) malloc(texture_height * texture_width * 3);
    if (!tex_buffer) {
        perror("Out of memory trying to allocate texture");
        exit(-1);
    }
    MagickGetImagePixels(wand, 0, 0, texture_width, texture_height, "RGB", CharPixel, tex_buffer);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    /* wrap horizontally and vertically */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    /* Linear texture processing for zooming */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture_width, texture_height, 0, GL_RGB, GL_UNSIGNED_BYTE, tex_buffer);

    horiz_disp = vert_disp = 0;

    /* Initial zoom factor is whatever makes the image fill the screen vertically */
    zoom_factor = 1.0;

    /* Set texture coordinates */
    translate(0, 0, 0);
}

void next_image(void) {
    image_index++;
    if (image_index >= num_images)
        image_index = 0;
}

void handle_keyboard(unsigned char key, int x, int y) {
    switch(key) {
        case 'q':
            quit_main_loop = 1;
            break;
        case 'a':
            translate(0.1, 0, 0);
            break;
        case 'd':
            translate(-0.1, 0, 0);
            break;
        case 'w':
            translate(0, 0.1, 0);
            break;
        case 's':
            translate(0, -0.1, 0);
            break;
        case 'z':
            translate(0, 0, -0.05);
            break;
        case 'c':
            translate(0, 0, 0.05);
            break;
        case 'x':
            next_image();
            setup_texture();
    }
}

int setup_listen_port(void) {
    int recv_socket = 0, so_reuseaddr = 1;
    struct sockaddr_in addr;
    struct hostent *server;
    struct ip_mreq mreq;

    recv_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (recv_socket == 0) {
        perror("Couldn't open receiving socket");
        exit(0);
    }
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;

    if (options.valid_listenaddr) {
        if (! options.multicast) {
            server = gethostbyname(options.listenaddr);
            if (server == NULL) {
                fprintf(stderr, "Couldn't figure out host to bind to");
                exit(0);
            }
            memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
        }
        else {
            // In multicast mode, we need to listen on INADDR_ANY. We'll use listenaddr later on
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
        }
    }
    else {
        addr.sin_addr.s_addr = INADDR_ANY;
    }
    addr.sin_port = htons(options.listenport);
    if (bind(recv_socket, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("Couldn't bind socket");
        exit(0);
    }

    if (setsockopt(recv_socket, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof so_reuseaddr) == -1) {
        perror("Couldn't turn on SO_REUSEADDR");
    }

    if (options.multicast) {
        mreq.imr_multiaddr.s_addr = inet_addr(options.listenaddr);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(recv_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            perror("Problem joining multicast group");
            exit(1);
        }
    }

    return recv_socket;
}

int main(int argc, char * argv[]) {
    /* XXX Copy lg-xiv options, where needed */

    spnav_event spev;
    char modestring[300];

    struct pollfd fds[1];
    int retval;
    int recv_socket = 0;

    get_options(argc, argv);

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);

    screen_width = (float) glutGet(GLUT_SCREEN_WIDTH);
    screen_height = (float) glutGet(GLUT_SCREEN_HEIGHT);

    /* XXX Make this a decent pixel size, if necessary */
    glutInitWindowSize((int) screen_width, (int) screen_height);

    /* XXX is glutEnterGameMode() superior / inferior to glutFullScreen()?
     * Aside from that glutFullScreen() doesn't work on my system? :) */
    /* glutFullScreen();  <-- Why doesn't this work (in my notion WM)? */
    if (options.fullscreen) {
        snprintf(modestring, 300, "%dx%d@32", (int) screen_width, (int) screen_height);
        glutGameModeString(modestring);
        glutEnterGameMode();
    }
    else {
        /* XXX Do I need this? */
        glutInitWindowPosition(100, 100);
        glutCreateWindow("test");
    }

    glutDisplayFunc(render_scene);
    glutKeyboardFunc(handle_keyboard);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

    setup_texture();

    if (options.use_spacenav) {
        if (!init_spacenav((options.spacenav_dev ? options.spacenav_dev : "/dev/input/spacenavigator"), 1)) {
            fprintf(stderr, "ERROR: Couldn't initialize space navigator on %s\n",
                (options.spacenav_dev ? options.spacenav_dev : "/dev/input/spacenavigator"));
        }
        if (options.verbose)
            fprintf(stderr, "Successfully initialized the spacenav\n");
    }

    if (options.listenport != -1) {
        recv_socket = setup_listen_port();
        fds[0].fd = recv_socket;
        fds[0].events = POLLIN;
    }

    while (!quit_main_loop) {
        glutMainLoopEvent();
        if (options.use_spacenav && get_spacenav_event(&spev, NULL)) {
            if (spev.type == SPNAV_MOTION) {
                // Raw spacenav values range from -350 to 350
                if (abs(spev.x) + abs(spev.y) + abs(spev.z) != 0) {
                    translate(-1.0 * options.swapaxes * spev.x * options.sensitivity / 350.0,
                                     options.swapaxes * spev.y * options.sensitivity / 350.0,
                                                        spev.z * options.sensitivity / 350.0);
                }
            } else {
                // value == 0  means the button is coming up. Without this, it
                // would cycle images both on press *and* on release, which
                // gets irritating.
                if (spev.type == SPNAV_BUTTON && spev.value == 0) {
                    // Left spnav button goes to previous image, right one goes to next image
                    image_index += spev.button * 2 - 1;
                }
            }
        }
        if (options.listenport != -1) {
            retval = poll(fds, 1, 0);
            if (retval == -1)
                perror("Poll UDP socket");
            else if (retval) {
                if (options.verbose > 1)
                    printf("We received something!\n");
                udp_handler(recv_socket);
            }
        }
        usleep(200);
    }
    free(tex_buffer);
    return 0;
}
