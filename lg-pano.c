/* TODO:
 *      -- Support xoffset
 *      -- Constrain movement
 *      -- Handle directories of images gracefully
 */

/* #include <freeglut.h> */
#include <GL/gl.h>
#include <SDL/SDL.h>
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
#include <jpeglib.h>
#include <setjmp.h>
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
int screen_width, screen_height;
float texture_aspect;
unsigned int texture_width, texture_height;
float tex_min_x, tex_min_y, tex_max_x, tex_max_y;   /* Texture coordinates */
int *send_sockets;
int num_sockets = 0;
int has_slaves = 0;
int redraw = 1;

struct slavehost_s {
    char addr[ADDR_LEN];
    int port, broadcast;
    int socket;
    LIST_ENTRY(slavehost_s) entries;
};
LIST_HEAD(slavelisthead, slavehost_s) slave_list;

/* List of image names, initialized from argv */
struct image_s {
    char *filename;
    TAILQ_ENTRY(image_s) entries;
};
TAILQ_HEAD(imagelisthead, image_s) image_list;

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
    0       /* multicast */
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

            redraw = 1;
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
    char *image_file;
    int i;
    struct image_s *img;
    
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
        /* Setup images tail queue */
        TAILQ_INIT(&image_list);

        num_images = argc - optind;
        for (i = optind; i < argc; i++) {
            image_file = (char *) malloc(strlen(argv[i]));
            if (!image_file) {
                perror("Couldn't allocate memory for image name");
                exit(1);
            }
            strcpy(image_file, argv[i]);
            img = (struct image_s *) malloc(sizeof(struct image_s));
            if (!img) {
                perror("Couldn't allocate memory for image structure");
                exit(1);
            }
            img->filename = image_file;
            TAILQ_INSERT_TAIL(&image_list, img, entries);
        }

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
    /* XXX If zooming out would be blocked because of constraints, can I
     * translate some and still zoom? */

    /* Ratio of x size to y size, in normalized texture coordinates */
    float x2y = 1.0 * screen_width * texture_height / screen_height / texture_width;
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
        fprintf(stderr, "Screen: (%d, %d)\timage: (%d, %d)\n",
             screen_width, screen_height,
             texture_width, texture_height);
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
    redraw = 1;
}

/* render the image */
static void render_scene(void) {
    redraw = 0;
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

    SDL_GL_SwapBuffers();
}

/* This struct, my_error_exit, and read_JPEG_file were lifted wholesale from
 * libjpeg-turbo's example.c */

struct my_error_mgr {
  struct jpeg_error_mgr pub;	/* "public" fields */

  jmp_buf setjmp_buffer;	/* for return to caller */
};

typedef struct my_error_mgr * my_error_ptr;

METHODDEF(void)
my_error_exit (j_common_ptr cinfo)
{
    /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
    my_error_ptr myerr = (my_error_ptr) cinfo->err;
    
    /* Always display the message. */
    /* We could postpone this until after returning, if we chose. */
    (*cinfo->err->output_message) (cinfo);
    
    /* Return control to the setjmp point */
    longjmp(myerr->setjmp_buffer, 1);
}

unsigned char *read_JPEG_file (char * filename, unsigned int *width, unsigned int *height)
{
    struct jpeg_decompress_struct cinfo;
    /* We use our private extension JPEG error handler.
     * Note that this struct must live as long as the main JPEG parameter
     * struct, to avoid dangling-pointer problems.
     */
    struct my_error_mgr jerr;
    /* More stuff */
    FILE * infile;		/* source file */
    JSAMPARRAY buffer;		/* Output row buffer */
    int row_stride;		/* physical row width in output buffer */
    unsigned char *pixels;

    /* In this example we want to open the input file before doing anything else,
     * so that the setjmp() error recovery below can assume the file is open.
     * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
     * requires it in order to read binary files.
     */

    if ((infile = fopen(filename, "rb")) == NULL) {
        fprintf(stderr, "can't open %s\n", filename);
        return NULL;
    }

    /* Step 1: allocate and initialize JPEG decompression object */

    /* We set up the normal JPEG error routines, then override error_exit. */
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    /* Establish the setjmp return context for my_error_exit to use. */
    if (setjmp(jerr.setjmp_buffer)) {
        /* If we get here, the JPEG code has signaled an error.
         * We need to clean up the JPEG object, close the input file, and return.
         */
        jpeg_destroy_decompress(&cinfo);
        fclose(infile);
        return NULL;
    }
    /* Now we can initialize the JPEG decompression object. */
    jpeg_create_decompress(&cinfo);

    /* Step 2: specify data source (eg, a file) */

    jpeg_stdio_src(&cinfo, infile);

    /* Step 3: read file parameters with jpeg_read_header() */

    (void) jpeg_read_header(&cinfo, TRUE);
    /* We can ignore the return value from jpeg_read_header since
     *     (a) suspension is not possible with the stdio data source, and
     *     (b) we passed TRUE to reject a tables-only JPEG file as an error.
     * See libjpeg.txt for more info.
     */

    /* Step 4: set parameters for decompression */

    /* In this example, we don't need to change any of the defaults set by
     * jpeg_read_header(), so we do nothing here.
     */

    /* Step 5: Start decompressor */

    (void) jpeg_start_decompress(&cinfo);
    /* We can ignore the return value since suspension is not possible
     * with the stdio data source.
     */

    /* We may need to do some setup of our own at this point before reading
     * the data.    After jpeg_start_decompress() we have the correct scaled
     * output image dimensions available, as well as the output colormap
     * if we asked for color quantization.
     * In this example, we need to make an output work buffer of the right size.
     */ 
    /* JSAMPLEs per row in output buffer */
    row_stride = cinfo.output_width * cinfo.output_components;

    *width = cinfo.output_width;
    *height = cinfo.output_height;

    /* Make a one-row-high sample array that will go away when done with image */
    buffer = (*cinfo.mem->alloc_sarray)
		((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);
    pixels = (unsigned char *) malloc(row_stride * cinfo.output_height);
    if (!pixels) {
        perror("Allocate memory for image");
        exit(1);
    }

    /* Step 6: while (scan lines remain to be read) */
    /*                     jpeg_read_scanlines(...); */

    /* Here we use the library's state variable cinfo.output_scanline as the
     * loop counter, so that we don't have to keep track ourselves.
     */
    while (cinfo.output_scanline < cinfo.output_height) {
        /* jpeg_read_scanlines expects an array of pointers to scanlines.
         * Here the array is only one element long, but you could ask for
         * more than one scanline at a time if that's more convenient.
         */
        (void) jpeg_read_scanlines(&cinfo, buffer, 1);
        /* Assume put_scanline_someplace wants a pointer and sample count. */
        memcpy(pixels + row_stride * (cinfo.output_scanline - 1), buffer[0], row_stride);
    }

    /* Step 7: Finish decompression */

    (void) jpeg_finish_decompress(&cinfo);
    /* We can ignore the return value since suspension is not possible
     * with the stdio data source.
     */

    /* Step 8: Release JPEG decompression object */

    /* This is an important step since it will release a good deal of memory. */
    jpeg_destroy_decompress(&cinfo);

    /* After finish_decompress, we can close the input file.
     * Here we postpone it until after no more JPEG errors are possible,
     * so as to simplify the setjmp error logic above.    (Actually, I don't
     * think that jpeg_destroy can do an error exit, but why assume anything...)
     */
    fclose(infile);

    /* At this point you may want to check to see whether any corrupt-data
     * warnings occurred (test whether jerr.pub.num_warnings is nonzero).
     */

    /* And we're done! */
    return pixels;
}

char *image_at(int i) {
    struct image_s *image;
    int p = 0;

    TAILQ_FOREACH(image, &image_list, entries) {
        if (p == i) break;
        p++;
    }

    return (image->filename);
}

void setup_texture(void) {
    /* MagickWand *wand; */

    /* wand = NewMagickWand();
    MagickReadImage(wand, images[image_index]);

    texture_width = MagickGetImageWidth(wand);
    texture_height = MagickGetImageHeight(wand); */
    if (tex_buffer != NULL)
        free(tex_buffer);

    tex_buffer = read_JPEG_file(image_at(image_index), &texture_width, &texture_height);
    texture_aspect = 1.0 * texture_width / texture_height;

/*    tex_buffer = (unsigned char *) malloc(texture_height * texture_width * 3);
    if (!tex_buffer) {
        perror("Out of memory trying to allocate texture");
        exit(-1);
    } */
    /* MagickExportImagePixels(wand, 0, 0, texture_width, texture_height, "RGB", CharPixel, tex_buffer); */

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

void handle_keyboard(SDL_keysym* keysym ) {
    switch(keysym->sym) {
        case SDLK_q:
            quit_main_loop = 1;
            break;
        case SDLK_a:
            translate(0.1, 0, 0);
            break;
        case SDLK_d:
            translate(-0.1, 0, 0);
            break;
        case SDLK_w:
            translate(0, 0.1, 0);
            break;
        case SDLK_s:
            translate(0, -0.1, 0);
            break;
        case SDLK_z:
            translate(0, 0, -0.05);
            break;
        case SDLK_c:
            translate(0, 0, 0.05);
            break;
        case SDLK_x:
            next_image();
            setup_texture();
        default:
            break;
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
    const SDL_VideoInfo* info = NULL;

    /* BEGIN */
    /* Information about the current video settings. */
    /* Dimensions of our window. */
    /* Color depth in bits of our window. */
    int bpp = 0;
    /* Flags we will pass into SDL_SetVideoMode. */
    int flags = 0;

    /* END */

    spnav_event spev;

    struct pollfd fds[1];
    int retval;
    int recv_socket = 0;

    SDL_Event event;

    get_options(argc, argv);

    /* SDL BEGIN */
    /* First, initialize SDL's video subsystem. */
    if( SDL_Init( SDL_INIT_VIDEO ) < 0 ) {
        /* Failed, exit. */
        fprintf( stderr, "Video initialization failed: %s\n",
             SDL_GetError( ) );
        exit( 1 );
    }

    /* Let's get some video information. */
    info = SDL_GetVideoInfo( );

    if( !info ) {
        /* This should probably never happen. */
        fprintf( stderr, "Video query failed: %s\n",
             SDL_GetError( ) );
        exit( 1 );
    }

    /* XXX Set our width/height to 640/480 (you would of course let the user
     * decide this in a normal app). We get the bpp we will request from the
     * display. On X11, VidMode can't change resolution, so this is probably being
     * overly safe. Under Win32, ChangeDisplaySettings can change the bpp.  */

    screen_width = info->current_w;
    screen_height = info->current_h;

    bpp = info->vfmt->BitsPerPixel;

    /*
     * Now, we want to setup our requested window attributes for our OpenGL
     * window.  We want *at least* 5 bits of red, green and blue. We also want
     * at least a 16-bit depth buffer.
     *
     * The last thing we do is request a double buffered window. '1' turns on
     * double buffering, '0' turns it off.
     *
     * Note that we do not use SDL_DOUBLEBUF in the flags to SDL_SetVideoMode.
     * That does not affect the GL attribute state, only the standard 2D
     * blitting setup.  */

    SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 5 );
    SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 5 );
    SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 5 );
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 16 );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

    /*
     * We want to request that SDL provide us with an OpenGL window, in a
     * fullscreen video mode.
     *
     * EXERCISE:
     * Make starting windowed an option, and handle the resize events properly
     * with glViewport.  */

    flags = SDL_OPENGL | SDL_FULLSCREEN;

    /*
     * Set the video mode
     */
    if( SDL_SetVideoMode( screen_width, screen_height, bpp, flags ) == 0 ) {
        /* 
         * This could happen for a variety of reasons,
         * including DISPLAY not being set, the specified
         * resolution not being available, etc.  */

        fprintf( stderr, "Video mode set failed: %s\n",
             SDL_GetError( ) );
        exit(1);
    }
    /* SDL END */

    /* XXX pay attention to options.fullscreen */

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
        if (redraw)
            render_scene();
        while( SDL_PollEvent( &event ) ) {
            switch (event.type) {
                case SDL_KEYDOWN:
                    /* Handle key presses. */
                    handle_keyboard(&event.key.keysym);
                    break;
                case SDL_QUIT:
                    quit_main_loop = 1;
                    break;
                default:
                    /* fprintf(stderr, "Unknown event type"); */
                    break;
            }
        }
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
