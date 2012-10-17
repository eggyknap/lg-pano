#include <freeglut.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wand/magick_wand.h"
#include <getopt.h>

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

struct {
    int verbose, fullscreen;
    int use_spacenav;
    char *spacenav_dev;
} options = {
    0,      /* verbose */
    0,      /* fullscreen */
    0,      /* use_spacenav */
    NULL,   /* spacenav_dev */
};

void usage(const char *pname) {
    fprintf(stderr, "%s%s%s\n",
"USAGE: ", pname, " <options> image_file[, image_file, ...]"
    );
}

void get_options(const int argc, char * const argv[]) {
    int opt_index, c;
    
    while (1) {
        static struct option long_options[] = {
            { "verbose",    no_argument,        NULL, 'v' },
            { "fullscreen", no_argument,        NULL, 'f' },
            { "spacenav",   optional_argument,  NULL, 's' },
            { 0,            0,                  0,    0   }
        };

        c = getopt_long(argc, argv, "vfs::", long_options, &opt_index);
        if (c == -1) break;

        switch (c) {
            case 'v':
                options.verbose++;
                break;
            case 'f':
                options.fullscreen = 1;
                break;
            case 's':
                options.use_spacenav = 1;
                if (optarg != NULL) options.spacenav_dev = optarg;
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

    /* Ratio of x size to y size, in normalized texture coordinates */
    float x2y = screen_width * texture_height / screen_height / texture_width;
    float xmax, xmin, ymax, ymin;
    float oz;

    /* Constrain zooming */
    oz = zoom_factor;
    if (zoom_factor + z >= 1)
        zoom_factor += z;
    else if (options.verbose) {
        fprintf(stderr, "Violated zoom constraint (val: %f, delta: %f)\n", zoom_factor, z);
    }

    xmin = (h + horiz_disp) + 0.5 - 1.0 / 2.0 / zoom_factor * x2y;
    xmax = (h + horiz_disp) + 0.5 + 1.0 / 2.0 / zoom_factor * x2y;

    ymin = (v + vert_disp) + 0.5 - 1.0 / 2.0 / zoom_factor;
    ymax = (v + vert_disp) + 0.5 + 1.0 / 2.0 / zoom_factor;

    /* Constrain vertical movement */
    if (ymin < 0 || ymax > 1) {
        zoom_factor = oz;
        if (options.verbose)
            fprintf(stderr, "Vertical zoom / movement constraints violated\n\tdisp/zoom: (%f,%f,%f) delta: (%f,%f,%f), new vals: X(%f, %f), Y(%f, %f)\n",
                horiz_disp, vert_disp, zoom_factor, h, v, z, xmin, xmax, ymin, ymax
            );
        return;
    }

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
    zoom_factor = screen_height / texture_height;
    zoom_factor = 1.0;

    /* Set texture coordinates */
    translate(0, 0, 0);
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
            image_index++;
            if (image_index >= num_images)
                image_index = 0;
            setup_texture();
    }
}

int main(int argc, char * argv[]) {
    /* XXX Copy lg-xiv options, where needed */

    char modestring[300];

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

    while (!quit_main_loop) {
        glutMainLoopEvent();
    }
    free(tex_buffer);
    return 0;
}
