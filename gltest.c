#include <freeglut.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*#include <wand/wand_api.h> */
#include "wand/magick_wand.h"

unsigned char *tex_buffer;
float
    zoom_factor = 1,    /* 1 == "normal size" */
    horiz_disp = 0,     /* disp == displacement */
    vert_disp = 0;
int quit_main_loop = 0;
int screen_width, screen_height;
float screen_aspect;
unsigned long texture_width, texture_height;

static void render_scene(void) {
    float tex_min_x, tex_min_y, tex_max_x, tex_max_y;

    tex_min_x = horiz_disp / screen_width;
    tex_min_y = vert_disp / screen_height;
    tex_max_x = tex_min_x + screen_width / texture_width * zoom_factor;
    tex_max_y = tex_min_y + screen_height / texture_height * zoom_factor;

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

void setup_texture(char *imgFile) {
    MagickWand *wand;

    wand = NewMagickWand();
    MagickReadImage(wand, imgFile);
    texture_width = MagickGetImageWidth(wand);
    texture_height = MagickGetImageHeight(wand);
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
}

void handle_keyboard(unsigned char key, int x, int y) {
    switch(key) {
        case 'q':
            quit_main_loop = 1;
            break;
        case 'a':
            horiz_disp += 5;
            break;
        case 'd':
            horiz_disp -= 5;
            break;
        case 'w':
            vert_disp += 5;
            break;
        case 's':
            vert_disp -= 5;
            break;
        case 'z':
            zoom_factor -= 0.05;
            break;
        case 'c':
            zoom_factor += 0.05;
            break;
    }

    /* Manually redraw, because glut apparently won't otherwise. XXX Can we
     * signal glut somehow? Is it better to do so? */
    if (! quit_main_loop) render_scene();
}

int main(int argc, char **argv) {
    /* XXX Set initial zoom factor to something that shows most of the image,
     * and displacement to center the image */
    /* XXX Constrain vertical movement */
    /* XXX Copy lg-xiv options, where needed, and use getopt */

    if (argc != 2) {
        fprintf(stderr, "Please give the program a texture file name as its only argument\n");
        exit(-1);
    }

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);

    screen_width = glutGet(GLUT_SCREEN_WIDTH);
    screen_height = glutGet(GLUT_SCREEN_HEIGHT);
    screen_aspect = screen_width / screen_height;

    /* XXX Make this a decent pixel size, if necessary */
    glutInitWindowSize(screen_width, screen_height);

    /* XXX Do I need this? */
    glutInitWindowPosition(100, 100);
    /* XXX If I use glutGameMode, do I need this window? */
    glutCreateWindow("test");

    /* XXX Make this an option */
    /* glutFullScreen();  <-- Why doesn't this work (in my notion WM)? */

    /* XXX is glutEnterGameMode() superior / inferior to glutFullScreen()? */
/*    glutGameModeString("1920x1200@32");
    glutEnterGameMode(); */

    glutDisplayFunc(render_scene);
    glutKeyboardFunc(handle_keyboard);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

    setup_texture(argv[1]);

    while (!quit_main_loop) {
        glutMainLoopEvent();
    }
    free(tex_buffer);
    return 0;
}
