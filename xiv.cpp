/*
  Copyright (c) 2010, Gilles BERNARD lordikc at free dot fr
  All rights reserved.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
  * Neither the name of the Author nor the
  names of its contributors may be used to endorse or promote products
  derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h> 
#include <stdint.h>
#include <X11/Xlib.h> 
#include <X11/Xutil.h> 
#include <X11/cursorfont.h>
#include <X11/Xatom.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <libgen.h>
#include "xiv.h"
#include "xiv_utils.h"
#include "xiv_readers.h"
#include "read-event.h"

// X11 global variables
pthread_mutex_t mutexWin = PTHREAD_MUTEX_INITIALIZER; // Mutex protecting the window
Display *display; 
Window  window; 
Visual *visual; 
int screen;
int depth; 
GC gc;
XImage* image = NULL;
Cursor watch; // Wait cursor
Cursor normal; // Normal cursor
Atom wmDeleteMessage;
bool fullscreen=false;

// Threads
pthread_t th; // Drawing thread
pthread_t *thFill=0; // Sub drawing threads
pthread_t thFifo; // Pipe control thread
pthread_t thSpacenav; // Spacenav control thread
pthread_t thPreload; // Preload image thread


// Drawing 
pthread_mutex_t mutexData = PTHREAD_MUTEX_INITIALIZER; // Mutex protecting the data array
int w,h,wref,href;
int ox,oy;
unsigned char* data=NULL;

// Display characteristics
float dx=0; // Translation
float dy=0;
float z=1; // Zoom
float a=0; // Rotation
bool autorot=true;
int lu=0;  // Luminosity
int cr=255; // Contrast
float gm=1; // Gamma
int osdSize=256;

// Current image
Image* imgCurrent=0;

// Image cache
int CACHE_NBIMAGES=5;
pthread_mutex_t mutexCache = PTHREAD_MUTEX_INITIALIZER; // Mutex protecting the cache
Image** imgCache;
int idxCache=0;

// FIFO file name
char* fifo=NULL;

bool revert=false; // Use reverse video
bool bilin=false;  // Use bilinear interpolation (true) or nearest neighbour (false)
bool bilinMove=false;
bool displayHist=false; // Display histogram
bool displayQuickview=false; // Display overview
bool refresh=false; // Need a window refresh
bool run=true; // Keeping running while it's true

// Histogram of current image
int histMax;
int histr[256];
int histg[256];
int histb[256];

// File list
#define MAX_NBFILES 32768
char** files=0;
int nbfiles=0;
int idxfile=0;
bool shuffle=false;
bool spacenav=false;

// values of powf(x,powe) for x between 0 and 1 to speed up calculation.
int powv[256];
float powe=0;

// Nb of cores available
int ncores=0;
int* fillBounds=NULL;

// Zoom on zone
int zx1=0,zx2=0,zy1=0,zy2=0;
bool displayZone=false;

// Points
float pts[20];
const char* ptsNames[]={"1","2","3","4","5","6","7","8","9","10"};
bool displayPts=true;
int crossSize=4;

bool displayGrid=false;
int ncells=12;

bool verbose=false;

bool displayAbout=false;
const char* about=" xiv " VERSION " (c) Gilles BERNARD lordikc@free.fr ";

void usage(const char* prog)
{
  char* progn=basename(strdup(prog));
  fprintf(stderr,"%s v%s\n",progn,VERSION);
  fprintf(stderr,"Usage %s [options] file1 file2...\n",progn);
  fprintf(stderr,"Options:\n");
  fprintf(stderr,"   -geometry widthxheight+ox+oy, default is screen size\n");
  fprintf(stderr,"   -threads # threads, default is to auto-detect # of cores.\n");
  fprintf(stderr,"   -cache # images (default 5).\n");
  fprintf(stderr,"   -no-autorot Disable auto rotate according to EXIF tags.\n");
  fprintf(stderr,"   -overview Display overview.\n");
  fprintf(stderr,"   -fullscreen.\n");
  fprintf(stderr,"   -histogram Display histogram.\n");
  fprintf(stderr,"   -grid Display grid.\n");
  fprintf(stderr,"   -browse expand the list of files by browsing the directory of the first file.\n");
  fprintf(stderr,"   -shuffle file list.\n");
  fprintf(stderr,"   -bilinear Turn on bilinear interpolation.\n");
  fprintf(stderr,"   -fifo filename for incoming commands, default is no command file.\n");
  fprintf(stderr,"   -spacenav use space navigator at /dev/input/spacenavigator for direction\n");
  fprintf(stderr,"   -v verbose.\n");
  fprintf(stderr,"       Commands are:\n");
  fprintf(stderr,"         o l filename: load a new image\n");
  fprintf(stderr,"         o z zoom_level: if zoom_level <0 fit image in window\n");
  fprintf(stderr,"         o c x y: Center view on (x,y) (image pixel coordinates system)\n");
  fprintf(stderr,"         o m x y: Move view of (x,y) (image pixel coordinates system)\n");
  fprintf(stderr,"         o q: quit\n");
  fprintf(stderr,"%s is a very simple and lightweight image viewer without UI but a X11 window and only controled by keys and mouse.\n",progn);
  fprintf(stderr,"As opposed to most of the image viewers, it does not rely on scrollbar for image panning.\n");
  fprintf(stderr,"It is a powerful tool to analyse huge images.\n");
  fprintf(stderr,"The Window is a view of the image in which you can zoom, pan, rotate...\n");
  fprintf(stderr,"%s reads natively 8 and 16 bits binary PPM and TIFF and JPEG images. It uses ImageMagick to convert other formats.\n",progn);
  fprintf(stderr,"Image drawing is performed in several threads for a better image analysis experience.\n");
  fprintf(stderr,"Next image is preloaded during current image analysis.\n");
  fprintf(stderr,"Shortcuts are:\n");
  fprintf(stderr,"   - Key based:\n");
  fprintf(stderr,"      o q/Q Quit\n");
  fprintf(stderr,"      o n/p Next/previous image in the list\n");
  fprintf(stderr,"      o D Delete current image. \n");
  fprintf(stderr,"      o d The current image is renamed to file.jpg.del. You'llcan delete it manually afterward.\n");
  fprintf(stderr,"      o Shift+n/p Jump 10 images forward/backward.\n");
  fprintf(stderr,"      o ' '/. Center view on pointer\n");
  fprintf(stderr,"      o z/Z/+/i Zoom/Unzoom\n");
  fprintf(stderr,"      o c/C Contrast +/-\n");
  fprintf(stderr,"      o g/G Gamma +/-\n");
  fprintf(stderr,"      o l/L Luminosity +/-\n");
  fprintf(stderr,"      o v   Reset Luminosity/Contrast\n");
  fprintf(stderr,"      o i   Invert colors\n");
  fprintf(stderr,"      o Fn  Memorize current pixel coordinate as nth point.\n");
  fprintf(stderr,"      o s   Show/hide points.\n");
  fprintf(stderr,"      o a   Show/hide about message.\n");
  fprintf(stderr,"      o f   Toggle Full Screen.\n");
  fprintf(stderr,"      o h   Toggle display histogram\n");
  fprintf(stderr,"      o b   Toggle bilinear interpolation\n");
  fprintf(stderr,"      o o   Toggle display overview\n");
  fprintf(stderr,"      o m   Toggle display grid\n");
  fprintf(stderr,"      o r/=/0 Reset view\n");
  fprintf(stderr,"      o 1-9 Set zoom level to 1/1..9\n");
  fprintf(stderr,"      o [Alt+]1-9 Set zoom level to 1..9\n");
  fprintf(stderr,"      o Left/Right/Up/Down pan\n");
  fprintf(stderr,"      o Shift+Left/Right/Up/Down fine pan\n");
  fprintf(stderr,"      o / or * rotate around center of window by 90° increments rounding angle to n x 90°.\n");
  fprintf(stderr,"      o Alt+Left/Right rotate around center of window\n");
  fprintf(stderr,"      o Shift+Alt+Left/Right fine rotate around center of window\n");
  fprintf(stderr,"   - Mouse based:\n");
  fprintf(stderr,"      o Left button+Drag Pan\n");
  fprintf(stderr,"      o Shift+Left button+Drag Upper-Left -> Lower Right : Zoom on zone, Lower-Right -> Upper Left Unzoom from zone.\n");
  fprintf(stderr,"      o Wheel Zoom/Unzoom keeping pointer position\n");
  fprintf(stderr,"      o Shift+Wheel Fine Zoom/Unzoom keeping pointer position\n");
  fprintf(stderr,"      o Alt+Wheel Rotate around pointer\n");
  fprintf(stderr,"      o Shift+Alt+Wheel Fine rotate around pointer\n");
  fprintf(stderr,"      o Button middle Previous image\n");
  fprintf(stderr,"      o Button right Next image\n");
  fprintf(stderr,"Points input:\n");
  fprintf(stderr,"   You can set up to 10 points using keys F1 to F10. If points are displayed (which is the default) you'll see them on top of the image.\n");
  fprintf(stderr,"   At the end of the image viewing, the points are written to stdout (before switching to another image or quitting).\n");
  fprintf(stderr,"Examples:\n");
  fprintf(stderr,"  %s -browse /images/image1.jpg: opens images1.jpg as well as every files in the /images directory.\n",progn);
  fprintf(stderr,"  %s -shuffle /images/*: opens every files in /images in random order.\n",progn);
  fprintf(stderr,"Capabilities: ");
  fprintf(stderr,"PPM ");
#ifdef HAVE_LIBJPEG
  fprintf(stderr,"JPEG ");
#endif
#ifdef HAVE_LIBTIFF
  fprintf(stderr,"TIFF ");
#endif
#ifdef HAVE_LIBEXIF
  fprintf(stderr,"EXIF ");
#endif
  fprintf(stderr,"\n");
}

void write_points(const char* file)
{
  for(int p=0;p<10;p++)
    {
      float xp=pts[2*p];
      float yp=pts[2*p+1];
      if(xp>=0)
	{
	  printf("%s %d %f %f\n",file,p,xp,yp);
	}
    }
}

// Return a pixel r,g and b value according to geometric and radiometric transformation.
// r,g and b are between 0 and 255 even if the input image is 16 bits.
// contrast, luminosity and gamma take advantage of the 16bits wide input to best convert to 8 bits.
inline void pixel2(int ii, int ji, int&r, int&g, int&b)
{
  if(imgCurrent && ji>=0 && ji<imgCurrent->w && ii>=0 && ii<imgCurrent->h)
    {
      int idx=3*(imgCurrent->w*ii+ji);
      int val=0;

      if(imgCurrent->nb==1)
	val=imgCurrent->buf[idx];
      else
	val=*(((unsigned short*)imgCurrent->buf)+idx);
      
      if(gm==1)
	{
	  val*=cr;
	  val>>=imgCurrent->nbits;
	}
      else
	{
	  val=(int)(cr*powv[(val*255)>>imgCurrent->nbits])>>8;
	}
	  
      val+=lu;

      if(val>255) val=255;
      if(val<0) val=0;

      if(revert)
	val=255-val;

      b=val;

      if(imgCurrent->nb==1)
	val=imgCurrent->buf[idx+1];
      else
	val=*(((unsigned short*)imgCurrent->buf)+idx+1);

      if(gm==1)
	{
	  val*=cr;
	  val>>=imgCurrent->nbits;
	}
      else
	{
	  val=(int)(cr*powv[(val*255)>>imgCurrent->nbits])>>8;
	}
	  
      val+=lu;

      if(val>255) val=255;
      if(val<0) val=0;

      if(revert)
	val=255-val;

      g=val;

      if(imgCurrent->nb==1)
	val=imgCurrent->buf[idx+2];
      else
	val=*(((unsigned short*)imgCurrent->buf)+idx+2);

      if(gm==1)
	{
	  val*=cr;
	  val>>=imgCurrent->nbits;
	}
      else
	{
	  val=(int)(cr*powv[(val*255)>>imgCurrent->nbits])>>8;
	}
	  
      val+=lu;

      if(val>255) val=255;
      if(val<0) val=0;

      if(revert)
	val=255-val;

      r=val;
    }
  else
    {
      // Outside of the image, the world is white...
      r=g=b=255;
    }
}

inline void pixel_gm_nb2(int ii, int ji, int&r, int&g, int&b)
{
  if(imgCurrent && ji>=0 && ji<imgCurrent->w && ii>=0 && ii<imgCurrent->h)
    {
      int idx=3*(imgCurrent->w*ii+ji);
      int val=0;

      val=*(((unsigned short*)imgCurrent->buf)+idx);
      
      val=(int)(cr*powv[(val*255)>>imgCurrent->nbits])>>8;
	  
      val+=lu;

      if(val>255) val=255;
      if(val<0) val=0;

      if(revert)
	val=255-val;

      b=val;

      val=*(((unsigned short*)imgCurrent->buf)+idx+1);

      val=(int)(cr*powv[(val*255)>>imgCurrent->nbits])>>8;
	  
      val+=lu;

      if(val>255) val=255;
      if(val<0) val=0;

      if(revert)
	val=255-val;

      g=val;

      val=*(((unsigned short*)imgCurrent->buf)+idx+2);

      val=(int)(cr*powv[(val*255)>>imgCurrent->nbits])>>8;
	  
      val+=lu;

      if(val>255) val=255;
      if(val<0) val=0;

      if(revert)
	val=255-val;

      r=val;
    }
  else
    {
      // Outside of the image, the world is white...
      r=g=b=255;
    }
}

inline void pixel_gm_nb1(int ii, int ji, int&r, int&g, int&b)
{
  if(imgCurrent && ji>=0 && ji<imgCurrent->w && ii>=0 && ii<imgCurrent->h)
    {
      int idx=3*(imgCurrent->w*ii+ji);
      int val=0;

      val=imgCurrent->buf[idx];
      
      val=(int)(cr*powv[val])>>8;
	  
      val+=lu;

      if(val>255) val=255;
      if(val<0) val=0;

      if(revert)
	val=255-val;

      b=val;

      val=imgCurrent->buf[idx+1];

      val=(int)(cr*powv[val])>>8;
	  
      val+=lu;

      if(val>255) val=255;
      if(val<0) val=0;

      if(revert)
	val=255-val;

      g=val;

      val=imgCurrent->buf[idx+2];

      val=(int)(cr*powv[val])>>8;
	  
      val+=lu;

      if(val>255) val=255;
      if(val<0) val=0;

      if(revert)
	val=255-val;

      r=val;
    }
  else
    {
      // Outside of the image, the world is white...
      r=g=b=255;
    }
}

inline void pixel_gm1_nb2(int ii, int ji, int&r, int&g, int&b)
{
  if(imgCurrent && ji>=0 && ji<imgCurrent->w && ii>=0 && ii<imgCurrent->h)
    {
      int idx=3*(imgCurrent->w*ii+ji);
      int val=0;

      val=*(((unsigned short*)imgCurrent->buf)+idx);
      
      val*=cr;
      val>>=imgCurrent->nbits;
	  
      val+=lu;

      if(val>255) val=255;
      if(val<0) val=0;

      if(revert)
	val=255-val;

      b=val;

      val=*(((unsigned short*)imgCurrent->buf)+idx+1);

      val*=cr;
      val>>=imgCurrent->nbits;
	  
      val+=lu;

      if(val>255) val=255;
      if(val<0) val=0;

      if(revert)
	val=255-val;

      g=val;

      val=*(((unsigned short*)imgCurrent->buf)+idx+2);

      val*=cr;
      val>>=imgCurrent->nbits;
	  
      val+=lu;

      if(val>255) val=255;
      if(val<0) val=0;

      if(revert)
	val=255-val;

      r=val;
    }
  else
    {
      // Outside of the image, the world is white...
      r=g=b=255;
    }
}

inline void pixel_gm1_nb1(int ii, int ji, int&r, int&g, int&b)
{
  if(imgCurrent && ji>=0 && ji<imgCurrent->w && ii>=0 && ii<imgCurrent->h)
    {
      int idx=3*(imgCurrent->w*ii+ji);
      int val=0;

      val=imgCurrent->buf[idx];
      
      val*=cr;
      val>>=imgCurrent->nbits;
	  
      val+=lu;

      if(val>255) val=255;
      if(val<0) val=0;

      if(revert)
	val=255-val;

      b=val;

      val=imgCurrent->buf[idx+1];

      val*=cr;
      val>>=imgCurrent->nbits;

      val+=lu;

      if(val>255) val=255;
      if(val<0) val=0;

      if(revert)
	val=255-val;

      g=val;

      val=imgCurrent->buf[idx+2];

      val*=cr;
      val>>=imgCurrent->nbits;
	  
      val+=lu;

      if(val>255) val=255;
      if(val<0) val=0;

      if(revert)
	val=255-val;

      r=val;
    }
  else
    {
      // Outside of the image, the world is white...
      r=g=b=255;
    }
}

inline void pixel(int ii, int ji, int&r, int&g, int&b)
{
  if(gm==1)
    {
      if(imgCurrent->nb==1)
	pixel_gm1_nb1(ii,ji,r,g,b);
      else
	pixel_gm1_nb2(ii,ji,r,g,b);
    }
  else
    {
      if(imgCurrent->nb==1)
	pixel_gm_nb1(ii,ji,r,g,b);
      else
	pixel_gm_nb2(ii,ji,r,g,b);
    }
}

// Fill a part of the drawing area.
// Part is delimited by bounds int[2] with starting row and ending row.
void* async_fill_part(void* bounds)
{
  float zca=z*cos(a);
  float zsa=z*sin(a);
  int* p=(int*)bounds;
  for(int i=p[0];i<p[1];i++)
    {
      int idx=4*w*i;
      float mix=dx-zsa*i;
      float miy=zca*i+dy;
      float x=mix;
      float y=miy;
      for(int j=0;j<w;j++)
	{
	  int r=0,g=0,b=0;
	  x+=zca;
	  y+=zsa;
	  int ji=(int)x;
	  int ii=(int)y;

	  // If bilinear interpolation is not requested or useful
	  if(!bilin||((z>=1)&&(a==0)))
	    {
	      pixel(ii,ji,r,g,b);
	    }
	  else // Use bilinear interpolation
	    {
	      if(x<0) ji--;
	      if(y<0) ii--;
	      int r1=0,g1=0,b1=0;
	      int r2=0,g2=0,b2=0;
	      int r3=0,g3=0,b3=0;
	      int r4=0,g4=0,b4=0;
	      pixel(ii,ji,r1,g1,b1);
	      pixel(ii,ji+1,r2,g2,b2);
	      pixel(ii+1,ji,r3,g3,b3);
	      pixel(ii+1,ji+1,r4,g4,b4);

	      float u=x-ji;
	      float v=y-ii;
	      float u1=1-u;
	      float v1=1-v;
	      float uv=u*v;
	      float u1v1=u1*v1;
	      float uv1=u*v1;
	      float u1v=u1*v;
	      r=(int)(r1*u1v1+r2*uv1+r3*u1v+r4*uv);
	      g=(int)(g1*u1v1+g2*uv1+g3*u1v+g4*uv);
	      b=(int)(b1*u1v1+b2*uv1+b3*u1v+b4*uv);
	    }

	  data[idx]=(unsigned char)r;
	  data[idx+1]=(unsigned char)g;
	  data[idx+2]=(unsigned char)b;

	  if(displayZone && 
	     ( ( ((j==zx1)||(j==zx2)) && ( ((i>=zy1) && (i<=zy2))||((i>=zy2) && (i<=zy1)) ) ) ||
	       ( ((i==zy1)||(i==zy2)) && ( ((j>=zx1) && (j<=zx2))||((j>=zx2) && (j<=zx1)) ) ) ))
	    {
	      data[idx]^=-1;
	      data[idx+1]^=-1;
	      data[idx+2]^=-1;
	    }

	  idx+=4;
	}
    }
  return 0;
}


// Fill data with image according to zoom, angle and translation
void fill()
{
  if(imgCurrent==0)
    return;

  if(gm!=powe)
    {
      powe=gm;
      for(int i=0;i<256;i++)
	powv[i]=(int)(255*powf((float)i/(float)255,gm));
    }

  // If we have several cores available, split filling into several threads.
  if(ncores>1)
    {
      for(int i=0;i<ncores;i++)
	fillBounds[i]=i*(h/ncores);
      fillBounds[ncores]=h;

      for (int i=0;i<ncores;i++)
	pthread_create(thFill+i,NULL,async_fill_part,fillBounds+i);
      
      void*r;
      for (int i=0;i<ncores;i++)
	pthread_join(*(thFill+i),&r);

    }
  else // Or directly fill the buffer in the main thread.
    {
      int bounds[2];
      bounds[0]=0;
      bounds[1]=h;
      async_fill_part(bounds);
    }

  // Display the grid
  if(displayGrid)
    draw_grid(w,h,ncells,data);

  // Display the points
  if(displayPts)
    {
      float caz=cos(a)/z;
      float saz=sin(a)/z;

      for(int p=0;p<10;p++)
	{
	  float xp=pts[2*p];
	  float yp=pts[2*p+1];
	  if(xp>=0)
	    {
	      int wx=(int)(caz*(xp-dx)+saz*(yp-dy));
	      int wy=(int)(-saz*(xp-dx)+caz*(yp-dy));
	      
	      for(int dwy=-crossSize;dwy<=crossSize;dwy++)
		{
		  for(int dwx=-crossSize;dwx<=crossSize;dwx++)
		    {
		      if((wx+dwx)>=0 && (wy+dwy)>=0 && (wx+dwx)<w && (wy+dwy)<h)
			{
			  int idx=4*((wy+dwy)*w+(wx+dwx));
			  if (displayPts &&(dwx==0||dwy==0))
			    {
			      data[idx]=data[idx]>128?0:255;idx++;
			      data[idx]=data[idx]>128?0:255;idx++;
			      data[idx]=data[idx]>128?0:255;
			    }
			}
		    }
		}
	    }
	}
    }

  // Display histogram & radiometry transformation
  if(displayHist&&histMax==0)
    compute_histogram(imgCurrent,histr,histg,histb,histMax);
  if(displayHist && osdSize && osdSize<h && histMax)
    draw_histogram(imgCurrent,w,h,data,histr,histg,histb,histMax,osdSize,lu,cr,powv);

  // Display overview un the lower right corner
  if(displayQuickview  && osdSize && osdSize<h )
    {
      int zn=max(imgCurrent->w,imgCurrent->h)/osdSize;
      if(zn==0) zn=1;
      int dw=(osdSize-(imgCurrent->w/zn));
      int dh=h-osdSize+(osdSize-(imgCurrent->h/zn));

      float caz=cos(a)/z;
      float saz=sin(a)/z;

      for(int i=0;i<imgCurrent->h/zn;i++)
	{
	  int idx1=w*(i+dh);
	  int cst1=w-osdSize+dw;
	  int in=i*zn;
	  for(int j=0;j<imgCurrent->w/zn;j++)
	    {
	      int idx2=4*(idx1+cst1+j);
	      int jn=j*zn;
	      int r=0,g=0,b=0;
	      pixel(in,j*zn,r,g,b);

	      int xw = (int)((jn-dx)*caz+(in-dy)*saz);
	      int yw = (int)((dx-jn)*saz+(in-dy)*caz);
	      // Draker when outside of viewed area
	      // Blue if z<1, Green if z=1 and Red if z>1
	      if(xw<0 || xw>=w || yw<0 || yw>=h)
		{
		  if(z>=1) r/=2; else r/=3;
		  if(z==1) g/=2; else g/=3;
		  if(z>=1) b/=3; else b/=2;
		}

	      data[idx2++]=r;
	      data[idx2++]=g;
	      data[idx2]=b;
	    }
	}
    }
}

// Asynchronous image filling
void* async_fill(void*)
{
  float za=0,aa=0,dxa=0,dya=0;
  float gma=0;
  int la=0;
  int ca=0;
  bool ra=false;
  bool bilina=false;
  //  bool dqa=false;
  //  bool dha=false;
  //  bool dza=false;
  int zx1a=0,zx2a=0,zy1a=0,zy2a=0;
  int delay=20000;

  while(run)
    {
      // If something changed we need to redraw
      if(za!=z||aa!=a||dx!=dxa||dy!=dya||
	 la!=lu||ca!=cr||ra!=revert||gma!=gm||bilina!=bilin||
	 zx1a!=zx1||zx2a!=zx2||zy1a!=zy1||zy2a!=zy2||
	 refresh)
	{
	  delay=20000;
	  MutexProtect mp(&mutexData);
	  za=z;aa=a;dxa=dx;dya=dy;la=lu;ca=cr;
	  bilina=bilin;
	  gma=gm;
	  ra=revert;
	  zx1a=zx1;zx2a=zx2;zy1a=zy1;zy2a=zy2;
	  refresh=false;
	  if(data!=NULL&&image!=NULL)
	    {
	      MutexProtect mwin(&mutexWin);
	      fill();

	      XPutImage(display,window,gc,image,0,0,0,0,w,h);

	      // Add labels to points
	      if(displayPts)
		{
		  float caz=cos(a)/z;
		  float saz=sin(a)/z;
		  
		  for(int p=0;p<10;p++)
		    {
		      float xp=pts[2*p];
		      float yp=pts[2*p+1];
		      if(xp>=0)
			{
			  int wx=(int)(caz*(xp-dx)+saz*(yp-dy));
			  int wy=(int)(-saz*(xp-dx)+caz*(yp-dy));
			  XDrawImageString(display,window,gc,wx+8,wy-8,ptsNames[p],p==9?2:1);
			}
		    }
		}
	      if(displayAbout)
		{
		  XDrawImageString(display,window,gc,8,(3*h)/4,about,strlen(about));
		}
	      XFlush(display);
	    }
	}
      else // Otherwise, just wait ...
	{
	  // ... and gently increase wait state
	  delay*=102;
	  delay/=100;
	  if(delay>200000)
	    delay=200000;
	  usleep(delay);
	}
    }
  return 0;
}

// Set parameters so that image fit into window
void full_extend()
{
  if(!imgCurrent) return;
  a=0;
  lu=0;
  gm=1;
  revert=false;
  z=(float)imgCurrent->w/(float)w;
  float z2=(float)imgCurrent->h/(float)h;
  if(z2>z)
    z=z2;

  dx=imgCurrent->w/2-(z*cos(a)*(w/2)-z*sin(a)*(h/2));
  dy=imgCurrent->h/2-(z*sin(a)*(w/2)+z*cos(a)*(h/2));
  
  if(verbose) fprintf(stderr,"Full extend %f %f %f\n",z,dx,dy);
}

Image* get_image_from_cache(const char* file)
{
  MutexProtect mp(&mutexCache);
  for(int i=0;i<CACHE_NBIMAGES;i++)
    {
      if(imgCache[i] && 0==strcmp(file,imgCache[i]->name))
	{
	  return imgCache[i];
	}
    }
  return 0;
}

// Load an image, tries to open as ppm, then jpeg, then tiff and if fails, use imagemagick to convert to ppm
Image* load_image(const char* file)
{
  Image* img = get_image_from_cache(file);
  if (img) 
    {
      // We are already loading the file from another thread
      // Wait for loading is done
      while(img && img->state==IN_PROGRESS) 
	{
	  usleep(50000);
	  img = get_image_from_cache(file);
	}
      if(img->state==READY)
	return img;
      if(img) // Error occured
	return 0;
      // Image was removed from cache, reload it
    }

  img = new Image(0,0,0,0,0,file,0);

  // Add image to cache
  if(img)
    {
      MutexProtect mp(&mutexCache);
      if(imgCache[idxCache])
	{
	  delete imgCache[idxCache];
	}
      imgCache[idxCache]=img;
      idxCache++;
      idxCache%=CACHE_NBIMAGES;
    }
  else
    return 0;

  int wi,hi,nbBytes,valMax;
  // Try PPM -> JPEG -> TIFF -> convert
  struct stat statBuf;
  unsigned char* buf = 0;
  if(0==stat(file,&statBuf)) // File exist
    {
      // Try ppm
      buf = read_ppm(file,wi,hi,nbBytes,valMax);
      if(buf==0) // No success, try jpeg
	{
	  buf=read_jpeg(file,wi,hi);
	  if(verbose&&buf) fprintf(stderr,"Success reading jpeg file %s\n",file);
	  nbBytes=1;
	  valMax=255;
	}
      if(buf==0) // No success, try tiff
	{
	  buf=read_tiff(file,wi,hi,nbBytes,valMax);
	  if(verbose&&buf) fprintf(stderr,"Success reading tiff file %s\n",file);
	}
      if(buf==0)  // No success, convert with ImageMagick
	{
	  if (verbose) fprintf(stderr,"Converting image with ImageMagick\n");
	  char cmd[2048];
	  char tmp[32];
	  char *tmpdir=getenv("TMP");
	  sprintf(tmp,"%s/xiv_XXXXXX",tmpdir==NULL?"/tmp":tmpdir);
	  int ret = mkstemp(tmp);
	  if(ret==-1)
	    {
	      fprintf(stderr,"Error creating tmp file %s\n",tmpdir==NULL?"/tmp":tmpdir);
	    }
	  close(ret);
	  unlink(tmp);
	  strcat(tmp,".ppm");
	  sprintf(cmd,"convert \"%s\" -quiet %s",file,tmp);
	  if(system(cmd)==-1)
	    {
	      fprintf(stderr,"Error calling ImageMagick convert\n");
	    }
	  buf = read_ppm(tmp,wi,hi,nbBytes,valMax);
	  if(!buf)
	    {
	      fprintf(stderr,"Unable to read converted image\n");
	    }
	  else if (verbose) fprintf(stderr,"Success reading converted file %s\n",file);
	  unlink(tmp);
	}
    }

  if(buf)
    {
      int nbits=(int)round(log(valMax)/log(2));
      img->nb=nbBytes;
      img->max=valMax;
      img->nbits=nbits;
      // Perform autorotate if requested
      int ai=autorot?orientation(file):0;
      if(verbose) fprintf(stderr,"Orientation %d\n",ai);
      if(ai==0)
	{
	  img->w=wi;
	  img->h=hi;
	  img->buf=buf;
	  img->state=READY;
	}
      else if(ai==1) // 90
	{
	  unsigned char* buf2 = (unsigned char*)malloc(wi*hi*3);
	  if(!buf2) {free(buf);return 0;}
	  for(int i=0;i<hi;i++)
	    {
	      int jn=i;
	      for(int j=0;j<wi;j++)
		{
		  int in=wi-j-1;
		  buf2[3*(in*hi+jn)+0] = buf[3*(i*wi+j)+0];
		  buf2[3*(in*hi+jn)+1] = buf[3*(i*wi+j)+1];
		  buf2[3*(in*hi+jn)+2] = buf[3*(i*wi+j)+2];
		}
	    }
	  ai=0;
	  img->w=hi;
	  img->h=wi;
	  img->buf=buf2;
	  img->state=READY;
	  free(buf);
	}
      else if(ai==2) //180
	{
	  unsigned char* buf2 = (unsigned char*)malloc(wi*hi*3);
	  if(!buf2) {free(buf);return 0;}
	  for(int i=0;i<hi;i++)
	    {
	      int in=hi-i-1;
	      for(int j=0;j<wi;j++)
		{
		  int jn=wi-j-1;
		  buf2[3*(in*hi+jn)+0] = buf[3*(i*wi+j)+0];
		  buf2[3*(in*hi+jn)+1] = buf[3*(i*wi+j)+1];
		  buf2[3*(in*hi+jn)+2] = buf[3*(i*wi+j)+2];
		}
	    }
	  ai=0;
	  img->w=wi;
	  img->h=hi;
	  img->buf=buf2;
	  img->state=READY;
	  free(buf);
	}
      else if(ai==3)//270
	{
	  unsigned char* buf2 = (unsigned char*)malloc(wi*hi*3);
	  if(!buf2) {free(buf);return 0;}
	  for(int i=0;i<hi;i++)
	    {
	      int jn=i;
	      for(int j=0;j<wi;j++)
		{
		  int in=j;
		  buf2[3*(in*hi+jn)+0] = buf[3*(i*wi+j)+0];
		  buf2[3*(in*hi+jn)+1] = buf[3*(i*wi+j)+1];
		  buf2[3*(in*hi+jn)+2] = buf[3*(i*wi+j)+2];
		}
	    }
	  ai=0;
	  img->w=hi;
	  img->h=wi;
	  img->buf=buf2;
	  img->state=READY;
	  free(buf);
	}
    }
  else
    {
      img->state=ERROR;
      return 0;
    }

  return img;
}

// Set the title of the window
void set_title(const char* file)
{
  // Tell the WM what is the name of the window.
  char title[1024];
  if(imgCurrent)
    sprintf(title,"%s - %d x %d - %db - %d / %d",imgCurrent->name,imgCurrent->w,imgCurrent->h,(int)round(log(imgCurrent->max)/log(2)),idxfile+1,nbfiles);
  else
    sprintf(title,"%s - %d x %d - %db - %d / %d",file,0,0,0,idxfile+1,nbfiles);
  XStoreName(display, window, title);
}

// Display the image
void display_image(const char* file)
{
  MutexProtect mwin(&mutexWin);
  if(verbose) fprintf(stderr,"display_image %s\n",file);
  for(int i=0;i<20;i++)
    pts[i]=-1;
  // Set the wait cursor
  XDefineCursor(display,window,watch);
  XFlush(display);

  histMax=0;
  imgCurrent=load_image(file);
  if(imgCurrent==0)
    imgCurrent=load_image(PREFIX "/share/xiv/xiv.ppm");
  if(imgCurrent==0)
    fprintf(stderr,"Can't open default file %s, there's something wrong with the installation\n",PREFIX "/share/xiv/xiv.ppm");
  // Init to image fitting in window
  full_extend();

  cr=255;
  refresh=true;
  // Restore normal cursor
  XDefineCursor(display,window,normal);
  set_title(file);
}

void* async_load(void* )
{
  while(nbfiles>1)
    {
      // Ensure the next image in the list is preloaded in the cache
      for(int s=0;s<=1;s++)
	{
	  bool found=false;
	  int next=0;
	  {
	    MutexProtect mp(&mutexCache);
	    // Preload next file
	    if(nbfiles>0)
	      {
		next=(idxfile+s)%nbfiles;
		// Search if it's already in the cache
		for(int i=0;i<CACHE_NBIMAGES;i++)
		  {
		    if(imgCache[i] && 0==strcmp(imgCache[i]->name,files[next]))
		      {
			found=true;
			break;
		      }
		  }
	      }      
	  }
	  
	  if(!found)
	    {
	      if(verbose) fprintf(stderr,"Preload next image %s\n",files[next]);
	      load_image(files[next]);
	      if(verbose) fprintf(stderr,"Preload done\n");
	    }
	  usleep(200000);
	}
    }
  return 0;
}

void rotate(float da)
{
  float xp=z*cos(a)*w/2-z*sin(a)*h/2+dx;
  float yp=z*sin(a)*w/2+z*cos(a)*h/2+dy;
  a=da;
  dx=xp-(z*cos(a)*w/2-z*sin(a)*h/2);
  dy=yp-(z*sin(a)*w/2+z*cos(a)*h/2);
}

void translate(float stepX, float stepY)
{
  dx=dx-(-z*cos(a)*stepX-z*sin(a)*stepY);
  dy=dy-(-z*sin(a)*stepX+z*cos(a)*stepY);
}

void zoom(float zf)
{
  float xp=z*cos(a)*w/2-z*sin(a)*h/2+dx;
  float yp=z*sin(a)*h/2+z*cos(a)*h/2+dy;
  z=zf;
  dx=xp-(z*cos(a)*w/2-z*sin(a)*h/2);
  dy=yp-(z*sin(a)*w/2+z*cos(a)*h/2);
}

// Thread for spacenav
void *spacenav_handler(void*) {
    spnav_event spev;
    
    // XXX make this udev link not hardcoded. this really doesn't matter for
    // now, though, because this is LG-specific, and each LG will have one of
    // these links
    if (! init_spacenav("/dev/input/spacenavigator")) {
        pthread_exit(0);
    }

    while (1) {
        if (get_spacenav_event(&spev)) {
            // XXX Don't hardcode the spacenav sensitivity factor (here, it's
            // 3, and larger numbers decrease the sensitivity)
            translate(-1 * spev.motion.x / 3, spev.motion.y / 3);
            float zf = z - z * spev.motion.z / 350;
            if (zf < 0 || zf > 10) {
                zf = z;
            }
            else
                zoom(zf);
        }
    }
    return 0;
}

// Thread for fifo listening
void* async_fifo(void*)
{
  while(fifo!=NULL)
    {
      int fd = open(fifo,O_RDONLY);
      if(fd==-1)
	{
	  fprintf(stderr,"Can't open fifo %s for reading\n",fifo);
	  fifo=NULL;
	}
      else
	{
	  char msg[2048];
	  int ret=read(fd,msg,2048);
	  if(ret>0)
	    {
	      msg[ret-1]=0;
	      printf("msg: [%s]\n",msg);
	      if(strstr(msg,"l ")==msg)
		{
		  MutexProtect mp(&mutexData);
		  display_image(msg+2);
		}
	      else if(strstr(msg,"z")==msg)
		{
		  float zc=atof(msg+2);
		  if(zc<=0)
		    {
		      full_extend();
		    }
		  else
		    {
		      zoom(zc);
		    }
		}
	      else if(strstr(msg,"c")==msg)
		{
		  int xp,yp;
		  sscanf(msg,"c %d %d\n",&xp,&yp);
		  dx=xp-(z*cos(a)*(w/2)-z*sin(a)*(h/2));
		  dy=yp-(z*sin(a)*(w/2)+z*cos(a)*(h/2));
		}
	      else if(strstr(msg,"m")==msg)
		{
		  int dxp,dyp;
		  sscanf(msg,"m %d %d\n",&dxp,&dyp);
		  translate(dxp,dyp);
		}
	      else if(strstr(msg,"q")==msg)
		{
		  close(fd);
		  unlink(fifo);
		  exit(1);
		  run=false;
		}
	      close(fd);
	    }
	}
    }
  return 0;
}

void quit()
{
  void *r;
  nbfiles=0;
  if(fifo!=NULL)
    {
      fifo=NULL;
      pthread_cancel(thFifo);
      pthread_join(thFifo,&r);
    }
  if (spacenav) {
    pthread_cancel(thSpacenav);
    pthread_join(thSpacenav, &r);
  }
  pthread_join(thPreload,&r);
  pthread_join(th,&r);
}

// Display next image
void next_image(int step)
{
  idxfile+=step;

  if(idxfile>=nbfiles) idxfile=0;
  if(idxfile<0) idxfile=nbfiles-1;
  display_image(files[idxfile]);
}

// Destroy current window
void destroy_window()
{
  XDestroyWindow(display,window);
}

// Create the window
void create_window(bool fs)
{
  // Create the 
  Window root = DefaultRootWindow (display);
  int ww=wref;
  int wh=href;
  if(fs)
    {
      ww=XDisplayWidth(display,screen);
      wh=XDisplayHeight(display,screen);
    }
  window= XCreateSimpleWindow(display, root,
			      ox,oy, ww,wh,0, 
			      BlackPixel(display, screen), 
			      WhitePixel(display, screen));

  XSelectInput(display,window,ExposureMask | ButtonPressMask | ButtonReleaseMask | KeyPressMask | PointerMotionMask|StructureNotifyMask );

  // Tell the window manager to call us when user close the window
  XSetWMProtocols(display, window, &wmDeleteMessage, 1);

  // Tell the window manager to go fullscreen (if compatible)...
  if(fs)
    {
      Atom first, second;
      
      first = XInternAtom(display, "_NET_WM_STATE", False);
      second = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
      XChangeProperty(display, window, first, XA_ATOM, 32,
		      PropModeReplace, (unsigned char *)&second, 1);
    }
  set_title("");

  // Display the window
  XMapWindow(display,window); 
}


int main (int argc, char** argv) 
{ 

  if(argc>1 &&( 0==strcmp(argv[1],"-h") || 0==strcmp(argv[1],"--help")))
    {
      usage(argv[0]);
      exit(1);
    }

  XEvent event; 
  
  display = XOpenDisplay(NULL); 
  screen = DefaultScreen(display);
  visual = DefaultVisual(display,screen); 
  depth  = DefaultDepth(display,screen); 

  w=XDisplayWidth(display,screen);
  h=XDisplayHeight(display,screen);
  wref=w;
  href=h;

  watch = XCreateFontCursor (display, XC_watch) ;
  normal = XCreateFontCursor (display, XC_left_ptr) ;

  bool browse=false;

  // Allocate file liste
  files=(char**)malloc(sizeof(char*)*MAX_NBFILES);
  if(files==NULL)
    {
      fprintf(stderr,"Not enough memory\n");
      exit(1);
    }

  // Analyse arguments
  for(int i=1;i<argc;i++)
    {
      if(0==strcmp(argv[i],"-geometry"))
	{
	  if((i+1)<argc)
	    {
	      sscanf(argv[++i],"%dx%d+%d+%d",&w,&h,&ox,&oy);
	      wref=w;
	      href=h;
	    }
	  else
	    {
	      usage(argv[0]);
	      exit(1);
	    }
	}
      else if(0==strcmp(argv[i],"-no-autorot"))
	{
	  autorot=false;
	}
      else if(0==strcmp(argv[i],"-fullscreen"))
	{
	  fullscreen=true;
	}
      else if(0==strcmp(argv[i],"-overview"))
	{
	  displayQuickview=true;
	}
      else if(0==strcmp(argv[i],"-histogram"))
	{
	  displayHist=true;
	}
      else if(0==strcmp(argv[i],"-grid"))
	{
	  displayGrid=true;
	}
      else if(0==strcmp(argv[i],"-browse"))
	{
	  browse=true;
	}
      else if(0==strcmp(argv[i],"-shuffle"))
	{
	  shuffle=true;
    }
      else if (0 == strcmp(argv[i], "-spacenav"))
    {
      spacenav = true;
	}
      else if(0==strcmp(argv[i],"-bilinear"))
	{
	  bilin=true;
	}
      else if(0==strcmp(argv[i],"-v"))
	{
	  verbose=true;
	}
      else if(0==strcmp(argv[i],"-threads"))
	{
	  if((i+1)<argc)
	    sscanf(argv[++i],"%d",&ncores);
	  else
	    {
	      usage(argv[0]);
	      exit(1);
	    }
	}
      else if(0==strcmp(argv[i],"-cache"))
	{
	  if((i+1)<argc)
	    sscanf(argv[++i],"%d",&CACHE_NBIMAGES);
	  else
	    {
	      usage(argv[0]);
	      exit(1);
	    }
	}
      else if(0==strcmp(argv[i],"-fifo"))
	{
	  if((i+1)<argc)
	    fifo=argv[++i];
	}
      else
	{
	  struct stat statbuf;
	  if(lstat(argv[i],&statbuf)!=-1)
	    {
	      // If a dir was passed, set browse to true
	      if (S_ISDIR(statbuf.st_mode))
		{
		  browse=true;
		}	      
	      // Add file to list
	      if(nbfiles<MAX_NBFILES)
		files[nbfiles++]=strdup(argv[i]);
	    }
	}
    }

  if (spacenav)
    {
      pthread_create(&thSpacenav, NULL, spacenav_handler, 0);
    }

  // Create the image cache.
  {
    MutexProtect mp(&mutexCache);
    imgCache=(Image**)malloc(CACHE_NBIMAGES*sizeof(Image*));
    if(imgCache==NULL)
      {
	fprintf(stderr,"Not enough memory\n");
	exit(1);
      }
    for(int i=0;i<CACHE_NBIMAGES;i++)
      {
	imgCache[i]=0;
      }
  }

  // No files and no fifo, display usage and exit
  if(nbfiles==0 && fifo==NULL)
    {
      usage(argv[0]);
      exit(1);
    }

  // Expand file list
  if(nbfiles==1 && browse)
    {
      struct stat statbuf;
      if(lstat(files[0],&statbuf)==-1)
	{
	  fprintf(stderr,"Unable to open %s\n",files[0]);
	  exit(1);
	}
      char* basepart=NULL;
      char* dirpart=NULL;
      char* start=NULL;
      if (S_ISDIR(statbuf.st_mode))
	{
	  dirpart=strdup(files[0]); // Leaked
	  free(files[0]); // Remove the dir
	  nbfiles=0;
	}
      else
	{
	  // These strings will be leaked
	  basepart=basename(strdup(files[0]));
	  dirpart=dirname(strdup(files[0]));
	  if(start==NULL) start=files[0];
	}
      DIR* dir = opendir(dirpart);
      if(dir!=NULL)
	{
	  // Add every file in the directory
	  for (struct dirent *dirent; (dirent = readdir(dir)) != NULL; ) 
	    {
	      if (strncmp(dirent->d_name, ".",1) == 0 || // Ignores everything starting with "."
		  (basepart!=NULL && strcmp(dirent->d_name, basepart) == 0))
		continue;
	      
	      files[nbfiles]=(char*)malloc(strlen(dirpart)+2+strlen(dirent->d_name));
	      sprintf(files[nbfiles],"%s/%s",dirpart,dirent->d_name);
	      // Add only regular files
	      if(is_file(files[nbfiles]))
		nbfiles++;
	      else
		free(files[nbfiles]);
	    }
	  
	  closedir(dir);
	}
      
      // Sort files
      qsort(files,nbfiles,sizeof(char*),cmpstr);
      // Start with the requested file
      if(start!=NULL)
	{
	  for(idxfile=0;idxfile<nbfiles;idxfile++)
	    if (0==strcmp(start,files[idxfile]))
	      break;
	}

    }

  // Shuffle files
  if(shuffle && nbfiles)
    {
      if (verbose) fprintf(stderr,"Shuffling files\n");
      srand(time(NULL));
      for(int i=0;i<nbfiles;i++)
	{
	  int i1=rand()%nbfiles;
	  int i2=rand()%nbfiles;
	  char* tmp=files[i1];
	  files[i1]=files[i2];
	  files[i2]=tmp;
	}
    }

  // Try to detect number of cores
  if(ncores==0)
    {
      FILE*f =fopen("/proc/cpuinfo","r");
      char tmp[1024];
      fgets(tmp,1024,f);
      while(!feof(f))
	{
	  if(strstr(tmp,"processor")==tmp)
	    ncores++;
	  fgets(tmp,1024,f);
	}
      fclose(f);
      if(ncores==0)
	ncores=1;
    }

  // If several cores are available, create variables for multithreaded drawing
  if(ncores>1)
    {
      thFill=(pthread_t*)malloc(ncores*sizeof(pthread_t));
      fillBounds=(int*)malloc((ncores+1)*sizeof(int));
      if(thFill==NULL||fillBounds==NULL)
	{
	  fprintf(stderr,"Not enough memory\n");
	  exit(1);
	}
    }
  if(verbose) fprintf(stderr,"%d core(s).\n",ncores);

  // Open the fifo if requested.
  if(fifo!=NULL)
    {
      if(mkfifo(fifo,0700))
	{
	  fprintf(stderr,"Can't create fifo %s\n",fifo);
	  exit(1);
	}
      pthread_create(&thFifo,NULL,async_fifo,0);
    }
  float xp=0,yp=0;

  //#define PERF
#ifdef PERF
  printf("Load image %s\n",files[0]);
  imgCurrent=load_image(files[0]);
  printf("done\n");
  time_t debut=time(NULL);
  w=2048;
  h=2048;
  data=(unsigned char*)malloc(4*w*h);
  full_extend();
  xp=z*cos(a)*w/2-z*sin(a)*h/2+dx;
  yp=z*sin(a)*h/2+z*cos(a)*h/2+dy;
  
  z/=3;
  a=0.1;
  
  dx=xp-(z*cos(a)*w/2-z*sin(a)*h/2);
  dy=yp-(z*sin(a)*w/2+z*cos(a)*h/2);

  bilin=true;

  gm=1.1;
  for(int i=0;i<30;i++)
    {
      fill();
    }

  printf("GM=%f NB=%d %ds\n",gm,imgCurrent->nb,time(NULL)-debut);
  debut=time(NULL);
  gm=1;

  for(int i=0;i<30;i++)
    {
      fill();
    }

  printf("GM=%f NB=%d %ds\n",gm,imgCurrent->nb,time(NULL)-debut);

  exit(1);
#endif

  gc = DefaultGC(display,screen);
  XSetForeground(display, gc, BlackPixel(display, screen));
  XSetBackground(display, gc, 0xFFA000);
    
  wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
  create_window(fullscreen);

  // If no requested files, add the default one (for fifo to load one)
  if(nbfiles==0)
    {
      files[nbfiles++]=strdup(PREFIX "/share/xiv/xiv.ppm");
    }

  pthread_create(&th,NULL,async_fill,0);
  pthread_create(&thPreload,NULL,async_load,0);

  bool leftdown=false;

  run=true;

  // Event Loop
  do{ 
    XNextEvent(display,&event); 

    MutexProtect mp(&mutexData);
    if (event.type==Expose && event.xexpose.count<1 && image!=NULL)
      {
	MutexProtect mwin(&mutexWin);
	if (verbose) fprintf(stderr,"Expose\n");
	XPutImage(display,window,gc,image,0,0,0,0,w,h);
	XFlush(display);
      }
    else if (event.type==ConfigureNotify) // Handle window resizing
      {
	if (verbose) fprintf(stderr,"Configure %d %d %d\n",event.xconfigure.width,event.xconfigure.height,image==NULL);
	if(w!=event.xconfigure.width||h!=event.xconfigure.height||image==NULL)
	  {	   
	    if(image!=NULL)
	      {
		XDestroyImage(image); // This destroys the data pointer as well.
	      }
	    else
	      {
		display_image(files[idxfile]);
	      }
	    
	    // Keep image centered
	    xp=z*cos(a)*w/2-z*sin(a)*h/2+dx;
	    yp=z*sin(a)*w/2+z*cos(a)*h/2+dy;
	    w=event.xconfigure.width;
	    osdSize=w/7; // Adjust OSD size
	    h=event.xconfigure.height;
	    data=(unsigned char*)malloc(w*h*4); // Allocate new drawing area
	    if(data==NULL)
	      {
		fprintf(stderr,"Not enougth memory\n");
		exit(1);
	      }
	    image = XCreateImage (display,visual,depth,ZPixmap,0,(char*)data,w,h,32,0);
	    // Keep image centered
	    dx=xp-(z*cos(a)*(w/2)-z*sin(a)*(h/2));
	    dy=yp-(z*sin(a)*(w/2)+z*cos(a)*(h/2));
	  }
      }
    else if (event.type==ButtonPress && event.xbutton.button==Button2)
      {
	next_image(-1);
      }
    else if (event.type==ButtonPress && event.xbutton.button==Button3)
      {
	next_image(1);
      }
    else if (event.type==ButtonPress && event.xbutton.button==Button4)
      {
	// Wheel Forward
	Window r,wr;
	int wx, wy, rx, ry;
	unsigned int m;
	MutexProtect mwin(&mutexWin);
	XQueryPointer (display, window, &r, &wr, &rx, &ry, &wx, &wy, &m);

	// Zoom/Rotate on current position
	xp=z*cos(a)*wx-z*sin(a)*wy+dx;
	yp=z*sin(a)*wx+z*cos(a)*wy+dy;
		
	if(m&Mod1Mask) // Alt is pressed -> Rotate
	  {
	    if(m&ShiftMask) 
	      a-=0.5*M_PI/180;
	    else
	      a-=5*M_PI/180;
	  }
	else // No Alt -> Zoom
	  {
	    if(m&ShiftMask)
	      z/=1.05;
	    else
	      z/=1.5;
	  }
		
	dx=xp-(z*cos(a)*wx-z*sin(a)*wy);
	dy=yp-(z*sin(a)*wx+z*cos(a)*wy);
      }
    else if (event.type==ButtonPress && event.xbutton.button==Button5)
      {
	// Wheel Backward
	Window r,wr;
	int wx, wy, rx, ry;
	unsigned int m;
	MutexProtect mwin(&mutexWin);
	XQueryPointer (display, window, &r, &wr, &rx, &ry, &wx, &wy, &m);

	// Unzoom from current position
	xp=z*cos(a)*wx-z*sin(a)*wy+dx;
	yp=z*sin(a)*wx+z*cos(a)*wy+dy;
		
	if(m&Mod1Mask)
	  {
	    if(m&ShiftMask)
	      a+=0.2*M_PI/180;
	    else
	      a+=5*M_PI/180;
	  }
	else
	  {
	    if(m&ShiftMask)
	      z*=1.05;
	    else
	      z*=1.5;
	  }
	    
	dx=xp-(z*cos(a)*wx-z*sin(a)*wy);
	dy=yp-(z*sin(a)*wx+z*cos(a)*wy);
      }
    else if (event.type==ButtonPress && event.xbutton.button==Button1)
      {
	// Left is down
	leftdown=true;
	bilinMove=bilin;
	bilin=false;
	Window r,wr;
	int wx, wy, rx, ry;
	unsigned int m;
	MutexProtect mwin(&mutexWin);
	XQueryPointer (display, window, &r, &wr, &rx, &ry, &wx, &wy, &m);
	if (m&ShiftMask)
	  {
	    displayZone=true;
	    zx1=zx2=wx;
	    zy1=zy2=wy;
	  }
	else
	  {
	    xp=z*cos(a)*wx-z*sin(a)*wy+dx;
	    yp=z*sin(a)*wx+z*cos(a)*wy+dy;
	  }
      }
    else if (event.type==ButtonRelease && event.xbutton.button==Button1)
      {
	// Left is up
	leftdown=false;
	bilin=bilinMove;
	if(displayZone)
	  {
	    Window r,wr;
	    int wx, wy, rx, ry;
	    unsigned int m;
	    MutexProtect mwin(&mutexWin);
	    XQueryPointer (display, window, &r, &wr, &rx, &ry, &wx, &wy, &m);
	    displayZone=false;
	    if(m&ShiftMask && zx1<zx2 && zy1<zy2)
	      {
		float xp1=z*cos(a)*zx1-z*sin(a)*zy1+dx;
		float yp1=z*sin(a)*zx1+z*cos(a)*zy1+dy;
		float xp2=z*cos(a)*zx2-z*sin(a)*zy2+dx;
		float yp2=z*sin(a)*zx2+z*cos(a)*zy2+dy;
		xp=(xp1+xp2)/2;
		yp=(yp1+yp2)/2;
		// Compute new zoom
		z=max(fabsf((xp2-xp1)/w), fabsf((yp2-yp1)/h));
		// Center view on center of zone
		dx=xp-(z*cos(a)*(w/2)-z*sin(a)*(h/2));
		dy=yp-(z*sin(a)*(w/2)+z*cos(a)*(h/2));
	      }
	    else if(m&ShiftMask && zx1>zx2 && zy1>zy2)
	      {
		float xp1=z*cos(a)*zx1-z*sin(a)*zy1+dx;
		float yp1=z*sin(a)*zx1+z*cos(a)*zy1+dy;
		float xp2=z*cos(a)*zx2-z*sin(a)*zy2+dx;
		float yp2=z*sin(a)*zx2+z*cos(a)*zy2+dy;
		xp=(xp1+xp2)/2;
		yp=(yp1+yp2)/2;
		// Compute new zoom
		z/=max(fabsf((xp2-xp1)/w), fabsf((yp2-yp1)/h));
		// Center view on center of zone
		dx=xp-(z*cos(a)*(w/2)-z*sin(a)*(h/2));
		dy=yp-(z*sin(a)*(w/2)+z*cos(a)*(h/2));
	      }
	  }
      }
    else if (event.type==KeyPress)
      {
	char c[11];
	KeySym ks;
	XComposeStatus cs;
	int nc=XLookupString(&(event.xkey),c,10,&ks,&cs);
	c[nc]=0;

	Window r,wr;
	int wx, wy, rx, ry;
	unsigned int m;
	{
	  MutexProtect mwin(&mutexWin);
	  XQueryPointer (display, window, &r, &wr, &rx, &ry, &wx, &wy, &m);
	}
		
	if(0==strcmp(c,"1") || 0==strcmp(c,"2") || 0==strcmp(c,"3") || 0==strcmp(c,"4") || 0==strcmp(c,"5") || 
	   0==strcmp(c,"6") || 0==strcmp(c,"7") || 0==strcmp(c,"8") || 0==strcmp(c,"9") ) // Zoom level keep center view
	  {
	    xp=z*cos(a)*w/2-z*sin(a)*h/2+dx;
	    yp=z*sin(a)*h/2+z*cos(a)*h/2+dy;
		
	    z=1/atof(c);
	    if( m&Mod1Mask )
	      z=1/z;
		
	    dx=xp-(z*cos(a)*w/2-z*sin(a)*h/2);
	    dy=yp-(z*sin(a)*w/2+z*cos(a)*h/2);
	  }
	if(0==strcmp(c,"+") || 0==strcmp(c,"z")) // Zoom keep center view
	  {
	    zoom(z/1.5);
	  }
	else if(0==strcmp(c,"-") || 0==strcmp(c,"Z")) // Unzoom keep center view
	  {
	    zoom(z*1.5);
	  }
	else if(0==strcmp(c,"/") || 0==strcmp(c,"*")) // Rotate PI/2
	  {
	    float fa=0;
	    int n=(int)(a/(M_PI/2));
	    if(n>3) n=0;
	    if(n<-3) n=0;
	    if(0==strcmp(c,"/"))
	      fa=(n+1)*M_PI/2;
	    else
	      fa=(n-1)*M_PI/2;
	    rotate(fa);
	  }
	else if(0==strcmp(c," ")||0==strcmp(c,".")) // Center on current pointer position
	  {
	    xp=z*cos(a)*wx-z*sin(a)*wy+dx;
	    yp=z*sin(a)*wx+z*cos(a)*wy+dy;
		
	    dx=xp-(z*cos(a)*(w/2)-z*sin(a)*(h/2));
	    dy=yp-(z*sin(a)*(w/2)+z*cos(a)*(h/2));
	  }
	else if(0==strcmp(c,"s")) 
	  {
	    displayPts=!displayPts;
	    refresh=true;
	  }
	else if(0==strcmp(c,"a")) 
	  {
	    displayAbout=!displayAbout;
	    refresh=true;
	  }
	else if(0==strcmp(c,"f")) 
	  {
	    fullscreen=!fullscreen;
	    MutexProtect mwin(&mutexWin);
	    destroy_window();
	    create_window(fullscreen);
	  }
	else if(0==strcmp(c,"c") || 0==strcmp(c,"C")) // Contrast +/-
	  {
	    if(0==strcmp(c,"C"))
	      cr-=8;
	    else
	      cr+=8;
	    if(cr<=0) cr=1;
	  }
	else if(0==strcmp(c,"g") || 0==strcmp(c,"G")) // Contrast +/-
	  {
	    if(0==strcmp(c,"g"))
	      gm*=1.1;
	    else
	      gm/=1.1;
	  }
	else if(0==strcmp(c,"h")) // Toggle display histogram
	  {
	    displayHist=!displayHist;
	    refresh=true;
	  }
	else if(0==strcmp(c,"m")) // Toggle display grid
	  {
	    displayGrid=!displayGrid;
	    refresh=true;
	  }
	else if(0==strcmp(c,"o")) // Toggle display overview
	  {
	    displayQuickview=!displayQuickview;
	    refresh=true;
	  }
	else if(0==strcmp(c,"b")) // Toggle bilinear interpolation
	  {
	    bilin=!bilin;
	  }
	else if(0==strcmp(c,"l") || 0==strcmp(c,"L")) // Luminosity +/-
	  {
	    if(0==strcmp(c,"l"))
	      lu+=8;
	    else
	      lu-=8;
	  }
	else if(0==strcmp(c,"v")) // Reset Luminosity/Contrast
	  {
	    lu=0;
	    cr=255;
	    gm=1;
	  }
	else if(0==strcmp(c,"i")) // Invert radiometry
	  {
	    revert=!revert;
	  }
	else if(0==strcmp(c,"=") || 0==strcmp(c,"r") || 0==strcmp(c,"0")) // Reset view
	  {
	    full_extend();
	  }
	else if(0==strcmp(c,"n") || 0==strcmp(c,"p") || 0==strcmp(c,"N") || 0==strcmp(c,"P")) // next/previous image
	  {
	    if(0==strcmp(c,"n"))
	      next_image(1);
	    else if(0==strcmp(c,"N"))
	      next_image(nbfiles/20);
	    else if(0==strcmp(c,"p"))
	      next_image(-1);
	    else
	      next_image(-nbfiles/20);
	  }
	else if(0==strcmp(c,"D")) // Delete image 
	  {
	    if(nbfiles>0)
	      {
		unlink(files[idxfile]);
		free(files[idxfile]);
		nbfiles--;
		for(int i=idxfile;i<nbfiles;i++)
		  files[i]=files[i+1];
		next_image(0);
	      }
	  }
	else if(0==strcmp(c,"d")) // Move image to file.jpg.del so that you can undelete it.
	  {
	    if(nbfiles>0)
	      {
		// Append .del to filename and rename file to it
		// The file is not actually deleted.
		char* tmp=(char*)malloc(strlen(files[idxfile])+5);
		tmp[0]=0;
		sprintf(tmp,"%s.del",files[idxfile]);
		rename(files[idxfile],tmp);
		free(tmp);
		free(files[idxfile]);
		nbfiles--;
		for(int i=idxfile;i<nbfiles;i++)
		  files[i]=files[i+1];
		next_image(0);
	      }
	  }
	else if(0==strcmp(c,"q") || 0==strcmp(c,"Q")) // Quit
	  {
	    write_points(files[idxfile]);
	    run=false;
	  }
	else if (ks==XK_Left) // Key based Pan / Rotate
	  {
	    if(m&Mod1Mask) 
	      {
		if(m&ShiftMask)
		  rotate(a+0.2*M_PI/180);
		else
		  rotate(a+5*M_PI/180);
	      }
	    else
	      {
		if(m&ShiftMask)
		  translate(-w/20,0);
		else
		  translate(-w/5,0);
	      }
	  }
	else if (ks==XK_Right) // Key based Pan / Rotate
	  {
	    if(m&Mod1Mask) 
	      {
		if(m&ShiftMask)
		  rotate(a-0.2*M_PI/180);
		else
		  rotate(a-5*M_PI/180);
	      }
	    else
	      {
		if(m&ShiftMask)
		  translate(w/20,0);
		else
		  translate(w/5,0);
	      }
	  }
	else if (ks==XK_Up) // Key based Pan Up
	  {
	    if(m&ShiftMask)
	      translate(0,h/20);
	    else
	      translate(0,h/5);
	  }
	else if (ks==XK_Down) // Key based Pan Down
	  {
	    if(m&ShiftMask)
	      translate(0,-h/20);
	    else
	      translate(0,-h/5);
	  }
	else if (ks==XK_F1||ks==XK_F2||ks==XK_F3||ks==XK_F4||ks==XK_F5||ks==XK_F6||ks==XK_F7||ks==XK_F8||ks==XK_F9||ks==XK_F10) 
	  {
	    xp=z*cos(a)*wx-z*sin(a)*wy+dx;
	    yp=z*sin(a)*wx+z*cos(a)*wy+dy;
	    
	    int idxp=0;
	    switch(ks)
	      {
	      case XK_F1:idxp=1;break;
	      case XK_F2:idxp=2;break;
	      case XK_F3:idxp=3;break;
	      case XK_F4:idxp=4;break;
	      case XK_F5:idxp=5;break;
	      case XK_F6:idxp=6;break;
	      case XK_F7:idxp=7;break;
	      case XK_F8:idxp=8;break;
	      case XK_F9:idxp=9;break;
	      case XK_F10:idxp=10;break;
	      }
		
	    if(verbose) fprintf(stderr,"%d:%f %f\n",idxp,xp,yp);
	    pts[2*(idxp-1)+0]=xp;
	    pts[2*(idxp-1)+1]=yp;
	    refresh=true;
	  }
      }
    else if(leftdown) // Handle mouse based pan
      {
	Window r,wr;
	int wx, wy, rx, ry;
	unsigned int m;

	MutexProtect mwin(&mutexWin);
	XQueryPointer (display, window, &r, &wr, &rx, &ry, &wx, &wy, &m);
	if(displayZone)
	  {
	    zx2=wx;
	    zy2=wy;
	  }
	else
	  {
	    dx=xp-(z*cos(a)*wx-z*sin(a)*wy);
	    dy=yp-(z*sin(a)*wx+z*cos(a)*wy);
	  }
      }
    else if (event.type == ClientMessage &&
	     (Atom)event.xclient.data.l[0] == wmDeleteMessage) 
      {
	run=false;
      }

  } while (run); 
  
  // Cleanup before leaving
  {
    MutexProtect mwin(&mutexWin);
    MutexProtect mp(&mutexData);
    if(image!=NULL)
      XDestroyImage(image);
    XDestroyWindow(display,window);
    XCloseDisplay(display); 
  }

  // Remove the fifo file if need be
  if (fifo!=NULL)
    {
      unlink(fifo);
      fifo=NULL;
    }

  quit();

  if(thFill!=NULL) free(thFill);
  if(fillBounds!=NULL) free(fillBounds);

  for(int i=0;i<nbfiles;i++)
    free(files[i]);
  free(files);

  MutexProtect mp(&mutexCache);
  for(int i=0;i<CACHE_NBIMAGES;i++)
    {
      delete imgCache[i];
    }
  free(imgCache);

  return 0;
}

