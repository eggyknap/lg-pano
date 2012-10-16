#include <freeglut.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*#include <wand/wand_api.h> */
#include "wand/magick_wand.h"

unsigned char *tex_buffer;

static void render_scene(void) {
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 1);
    glBegin(GL_QUADS);
        /* The biggest thing I need to do is change these texture coordinates
         * around based on spacenav input, ensuring they keep the proper aspect
         * ratio at all times. I don't yet know how to make h360 work... */
        glTexCoord2f(0, 0);  glVertex3f(-1, 1, 0);
        glTexCoord2f(0.6, 0);  glVertex3f(1, 1, 0);
        glTexCoord2f(0.6, 0.5);  glVertex3f(1, -1, 0);
        glTexCoord2f(0, 0.3);  glVertex3f(-1, -1, 0);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    glutSwapBuffers();
}

void setupTexture(char *imgFile) {
    MagickWand *wand;
    unsigned long width, height;

    wand = NewMagickWand();
    MagickReadImage(wand, imgFile);
    width = MagickGetImageWidth(wand);
    height = MagickGetImageHeight(wand);
    tex_buffer = (unsigned char *) malloc(height * width * 3);
    if (!tex_buffer) {
        perror("Out of memory trying to allocate texture");
        exit(-1);
    }
    MagickGetImagePixels(wand, 0, 0, width, height, "RGB", CharPixel, tex_buffer);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, tex_buffer);
}

void handle_keyboard(unsigned char key, int x, int y) {
    if (key == 'q') {
        glutLeaveMainLoop();
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Please give the program a texture file name as its only argument\n");
        exit(-1);
    }

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(1024, 768);
    glutInitWindowPosition(100, 100);
    glutCreateWindow("test");
    glutDisplayFunc(render_scene);
    glutKeyboardFunc(handle_keyboard);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    setupTexture(argv[1]);
    glutMainLoop();
    free(tex_buffer);
    return 0;
}
