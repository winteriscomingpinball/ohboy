/*
 * sdl.c
 * sdl interfaces -- based on svga.c 
 *
 * (C) 2001 Damian Gryski <dgryski@uwaterloo.ca>
 * Joystick code contributed by David Lau
 * Sound code added by Laguna
 *
 * Licensed under the GPLv2, or later.
 */

#include <stdlib.h>
#include <stdio.h>

#include <SDL/SDL.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>

#include "SFont.h"
#include "font8px.h"

#include "gnuboy.h"
#include "fb.h"
#include "input.h"
#include "rc.h"

#include "drv_display.h"

#define DEFAULTFBDEV  "/dev/fb0"
#define DEFAULTFBMODE "/etc/fb.modes"
#define displayno 0
#define DISP_DEV "/dev/disp"

#define MODKEY SDLK_UP
#define PRESSEDKEY SDLK_F1
#define REPORTEDKEY SDLK_SPACE

bool keymodstate=0;
bool reportedkeystate=0;

int exitcheck=0;


struct fb fb;

const char *fbdev = DEFAULTFBDEV;
struct fb_var_screeninfo fb_var;
disp_layer_info layerinfo;

static int use_yuv = -1;
static int fullscreen = 1;
static int use_altenter = 1;
static int use_joy = 1, sdl_joy_num;
static SDL_Joystick * sdl_joy = NULL;
static const int joy_commit_range = 3276;
static char Xstatus, Ystatus;

static SDL_Surface *screen;
static SDL_Overlay *overlay;
static SDL_Rect overlay_rect;

static int vmode[3] = { 0, 0, 32 };

/* fps */
static int sdl_showfps = 0; 
static int fps_current_count = 0; 
static int fps_last_count = 0; 
static int fps_last_time = 0; 
static int fps_current_time = 0;
static char fps_str[20] = {0};

static SDL_Surface *font_bitmap_surface=NULL;
static SFont_Font* Font=NULL;

SDL_Rect myrect;
/* fps */


rcvar_t vid_exports[] =
{
	RCV_VECTOR("vmode", &vmode, 3),
	RCV_BOOL("yuv", &use_yuv),
	RCV_BOOL("fullscreen", &fullscreen),
	RCV_BOOL("altenter", &use_altenter),
    
	RCV_INT("sdl_showfps", &sdl_showfps), /* SDL only, show frames per second, if >1 will show FPS in a box */
	RCV_END
};

rcvar_t joy_exports[] =
{
	RCV_BOOL("joy", &use_joy),
	RCV_END
};

/* keymap - mappings of the form { scancode, localcode } - from sdl/keymap.c */
extern int keymap[][2];

static int mapscancode(SDLKey sym)
{
	/* this could be faster:  */
	/*  build keymap as int keymap[256], then ``return keymap[sym]'' */

	int i;
	for (i = 0; keymap[i][0]; i++)
		if (keymap[i][0] == sym)
			return keymap[i][1];
	if (sym >= '0' && sym <= '9')
		return sym;
	if (sym >= 'a' && sym <= 'z')
		return sym;
	return 0;
}


static void joy_init()
{
	int i;
	int joy_count;
	
	/* Initilize the Joystick, and disable all later joystick code if an error occured */
	if (!use_joy) return;
	
	if (SDL_InitSubSystem(SDL_INIT_JOYSTICK))
		return;
	
	joy_count = SDL_NumJoysticks();
	
	if (!joy_count)
		return;

	/* now try and open one. If, for some reason it fails, move on to the next one */
	for (i = 0; i < joy_count; i++)
	{
		sdl_joy = SDL_JoystickOpen(i);
		if (sdl_joy)
		{
			sdl_joy_num = i;
			break;
		}	
	}
	
	/* make sure that Joystick event polling is a go */
	SDL_JoystickEventState(SDL_ENABLE);
}

static void overlay_init()
{
	if (!use_yuv) return;
	
	if (use_yuv < 0)
		if (vmode[0] < 320 || vmode[1] < 288)
			return;
	
	overlay = SDL_CreateYUVOverlay(320, 144, SDL_YUY2_OVERLAY, screen);

	if (!overlay) return;

#ifndef GNUBOY_HACK_ALWAYS_USE_YUV_IF_REQUESTED
	/* check if there is hardware support, use regular RGB mode if there is no HW support */
	if (!overlay->hw_overlay || overlay->planes > 1)
	{
		SDL_FreeYUVOverlay(overlay);
		overlay = 0;
		return;
	}
#endif /* GNUBOY_HACK_ALWAYS_USE_YUV_IF_REQUESTED */

	SDL_LockYUVOverlay(overlay);
	
	fb.w = 160;
	fb.h = 144;
	fb.pelsize = 4;
	fb.pitch = overlay->pitches[0];
	fb.ptr = overlay->pixels[0];
	fb.yuv = 1;
	fb.cc[0].r = fb.cc[1].r = fb.cc[2].r = fb.cc[3].r = 0;
	fb.dirty = 1;
	fb.enabled = 1;
	
	overlay_rect.x = 0;
	overlay_rect.y = 0;
	overlay_rect.w = vmode[0];
	overlay_rect.h = vmode[1];

	/* Color channels are 0=Y, 1=U, 2=V, 3=Y1 */
	switch (overlay->format)
	{
		/* FIXME - support more formats */
	case SDL_YUY2_OVERLAY:
	default:
		fb.cc[0].l = 0;
		fb.cc[1].l = 24;
		fb.cc[2].l = 8;
		fb.cc[3].l = 16;
		break;
	}
	
	SDL_UnlockYUVOverlay(overlay);
}

void vid_init()
{
	int flags;
	
	int fh; 
	int dispfile;
	int ret;
	int args[4] = {displayno, 3, (unsigned long)(&layerinfo), 0};
	
	
	fh = open(fbdev, O_RDONLY);
	
	if ( fh < 0 )
  {
    puts("open fb0 failed");
  }
  else
  {
    ioctl(fh, 0x4600u, &fb_var);
	
	
	
	printf("Current screen xres_virtual = %d\n", fb_var.xres_virtual);
		 
		  printf("Current screen yres_virtual = %d\n", fb_var.yres_virtual);
		 
		  printf("Current screen xres = %d\n", fb_var.xres);
		
		  printf("Current screen yres = %d\n", fb_var.yres);
		
		  printf("Current screen yoffset = %d\n", fb_var.yoffset);
		  
		  
		  printf("Current screen bits_per_pixel = %d\n", fb_var.bits_per_pixel);
		  
		  
		  
		  
    fb_var.bits_per_pixel=32;
	printf("setting bits per pixel to %d\n",fb_var.bits_per_pixel);
	
	fb_var.yres_virtual=180;
	fb_var.xres_virtual=240;
	
	fb_var.yres=180;
	fb_var.xres=240;
	
	/* //use this to set framebuffer */
	ioctl(fh, 0x4601, &fb_var);/* //FBIOPUT_VSCREENINFO */
	
	
		  
  }
  
  
  dispfile = open(DISP_DEV, O_RDWR, 0);
  if (dispfile < 0) {
		perror("open");
		
	}else{
  
  if (ioctl(dispfile, DISP_CMD_LAYER_GET_INFO, &args) < 0)
		perror("ioctl: 0x43 - DISP_CMD_LAYER_GET_INFO");
	    
		
		puts("Got layer 3 info");
	
	
	layerinfo.fb.size.width=144;
	layerinfo.fb.size.height=160;
	
	layerinfo.fb.src_win.height=240;
	layerinfo.fb.src_win.height=180;
	
	layerinfo.screen_win.width=240;
	layerinfo.screen_win.height=180;
	layerinfo.screen_win.y=38;
	
	
	layerinfo.alpha_mode=0;
	layerinfo.mode=DISP_LAYER_WORK_MODE_SCALER;
	
	if (ioctl(dispfile, DISP_CMD_LAYER_SET_INFO, &args) < 0)/* //DISP_CMD_LAYER_SET_INFO */
		perror("ioctl: 0x42 - DISP_CMD_LAYER_SET_INFO");
    puts("set layer 3 info...");
	
	
	args[1]=0;
	if (ioctl(dispfile, DISP_CMD_LAYER_DISABLE, &args) < 0)/* //DISP_CMD_LAYER_DISABLE  */
			perror("ioctl: 0x41 - DISP_CMD_LAYER_DISABLE");	
			puts("disabled layer 0");
	args[1]=1;		
	if (ioctl(dispfile, DISP_CMD_LAYER_DISABLE, &args) < 0)/* //DISP_CMD_LAYER_DISABLE  */
			perror("ioctl: 0x41 - DISP_CMD_LAYER_DISABLE");	
			puts("disabled layer 1");
	args[1]=2;
	if (ioctl(dispfile, DISP_CMD_LAYER_DISABLE, &args) < 0)/* //DISP_CMD_LAYER_DISABLE  */
			perror("ioctl: 0x41 - DISP_CMD_LAYER_DISABLE");	
			puts("disabled layer 2");
		
		
		
		
	
	}
	
	close(dispfile);
	

	if (!vmode[0] || !vmode[1])
	{
		int scale = rc_getint("scale");
		if (scale < 1) scale = 1;
		//vmode[0] = 160 * scale;
		//vmode[1] = 144 * scale;
		vmode[0] = 240;
		vmode[1] = 180;
		
	}
	
	flags = SDL_ANYFORMAT | SDL_HWPALETTE | SDL_HWSURFACE;

	if (fullscreen)
		flags |= SDL_FULLSCREEN;

	if (SDL_Init(SDL_INIT_VIDEO))
		die("SDL: Couldn't initialize SDL: %s\n", SDL_GetError());
	atexit(SDL_Quit); /* gnuboy uses exit() alot, so need to clear SDL correctly */

	if (!(screen = SDL_SetVideoMode(vmode[0], vmode[1], vmode[2], flags)))
		die("SDL: can't set video mode: %s\n", SDL_GetError());
    
    /* fps */
    font_bitmap_surface = get_default_data_font();
    Font = SFont_InitFont(font_bitmap_surface);
    if(!Font)
    {
        die("An error occured while setting up font.");
    }
    myrect.x = 0;
    myrect.y = 0;
    myrect.w = 240;
    myrect.h = SFont_TextHeight(Font);
    /* fps */

	SDL_ShowCursor(0);

	joy_init();

	overlay_init();
	
	if (fb.yuv) return;
	
	SDL_LockSurface(screen);
	
	fb.w = 240;//screen->w;
	fb.h = 180;//screen->h;
	fb.pelsize = screen->format->BytesPerPixel;
	fb.pitch = screen->pitch;
	fb.indexed = fb.pelsize == 1;
	fb.ptr = screen->pixels;
	fb.cc[0].r = screen->format->Rloss;
	fb.cc[0].l = screen->format->Rshift;
	fb.cc[1].r = screen->format->Gloss;
	fb.cc[1].l = screen->format->Gshift;
	fb.cc[2].r = screen->format->Bloss;
	fb.cc[2].l = screen->format->Bshift;

	SDL_UnlockSurface(screen);

	fb.enabled = 1;
	fb.dirty = 0;
	
}


void ev_poll()
{
	event_t ev;
	SDL_Event event;
	int axisval;

	while (SDL_PollEvent(&event))
	{
		switch(event.type)
		{
		case SDL_ACTIVEEVENT:
			if (event.active.state == SDL_APPACTIVE)
				fb.enabled = event.active.gain;
			break;
		case SDL_KEYDOWN:
			if ((event.key.keysym.sym == SDLK_RETURN) && (event.key.keysym.mod & KMOD_ALT))
				SDL_WM_ToggleFullScreen(screen);
			ev.type = EV_PRESS;
				switch(event.key.keysym.sym){
					case SDLK_DOWN:
					case SDLK_F1:
					case SDLK_RSHIFT:
					case SDLK_KP_MULTIPLY:
						exitcheck++;
					default:
					break;
				}
			if ((event.key.keysym.sym == SDLK_RETURN) && (event.key.keysym.mod & KMOD_ALT)){
				SDL_WM_ToggleFullScreen(screen);
				ev.code = mapscancode(event.key.keysym.sym);
			}
			else if ((event.key.keysym.sym == MODKEY)){
					/* puts("mod key down..."); */
					keymodstate=1;
					ev.code = mapscancode(event.key.keysym.sym);
			} else if (keymodstate==1 && event.key.keysym.sym== PRESSEDKEY){
				/* puts("got pressed key - need to report a different one... and report that mod key is not pressed."); */
			    ev.type = EV_RELEASE;
				ev.code = mapscancode(MODKEY);
				keymodstate=0;
				ev_postevent(&ev);
				
				ev.type = EV_PRESS;
				ev.code = mapscancode(REPORTEDKEY);
				reportedkeystate=1;
			}else{
			
			
			ev.code = mapscancode(event.key.keysym.sym);
			
			}
			ev_postevent(&ev);
				
			break;
		case SDL_KEYUP:
			ev.type = EV_RELEASE;
			switch(event.key.keysym.sym){
				case SDLK_DOWN:
				case SDLK_F1:
				case SDLK_RSHIFT:
				case SDLK_KP_MULTIPLY:
					exitcheck--;
				default:
				break;
			}
			
				if ((event.key.keysym.sym == MODKEY)){
					/* puts("mod key up..."); */
					keymodstate=0;
					ev.code = mapscancode(event.key.keysym.sym);
					
				}else if(reportedkeystate==1 && event.key.keysym.sym == PRESSEDKEY){
					reportedkeystate=0;
					keymodstate=0;
					ev.code = mapscancode(REPORTEDKEY);
				}else{

					ev.code = mapscancode(event.key.keysym.sym);
				}
			ev_postevent(&ev);
			break;
		case SDL_JOYAXISMOTION:
			switch (event.jaxis.axis)
			{
			case 0: /* X axis */
				axisval = event.jaxis.value;
				if (axisval > joy_commit_range)
				{
					if (Xstatus==2) break;
					
					if (Xstatus==0)
					{
						ev.type = EV_RELEASE;
						ev.code = K_JOYLEFT;
        			  		ev_postevent(&ev);				 		
					}
					
					ev.type = EV_PRESS;
					ev.code = K_JOYRIGHT;
					ev_postevent(&ev);
					Xstatus=2;
					break;
				}	   				   
				
				if (axisval < -(joy_commit_range))
				{
					if (Xstatus==0) break;
					
					if (Xstatus==2)
					{
						ev.type = EV_RELEASE;
						ev.code = K_JOYRIGHT;
        			  		ev_postevent(&ev);				 		
					}
					
					ev.type = EV_PRESS;
					ev.code = K_JOYLEFT;
					ev_postevent(&ev);
					Xstatus=0;
					break;
				}	   				   
				
				/* if control reaches here, the axis is centered,
				 * so just send a release signal if necisary */
				
				if (Xstatus==2)
				{
					ev.type = EV_RELEASE;
					ev.code = K_JOYRIGHT;
					ev_postevent(&ev);
				}
				
				if (Xstatus==0)
				{
					ev.type = EV_RELEASE;
					ev.code = K_JOYLEFT;
					ev_postevent(&ev);
				}	       
				Xstatus=1;
				break;
				
			case 1: /* Y axis*/ 
				axisval = event.jaxis.value;
				if (axisval > joy_commit_range)
				{
					if (Ystatus==2) break;
					
					if (Ystatus==0)
					{
						ev.type = EV_RELEASE;
						ev.code = K_JOYUP;
        			  		ev_postevent(&ev);				 		
					}
					
					ev.type = EV_PRESS;
					ev.code = K_JOYDOWN;
					ev_postevent(&ev);
					Ystatus=2;
					break;
				}	   				   
				
				if (axisval < -joy_commit_range)
				{
					if (Ystatus==0) break;
					
					if (Ystatus==2)
					{
						ev.type = EV_RELEASE;
						ev.code = K_JOYDOWN;
        			  		ev_postevent(&ev);
					}
					
					ev.type = EV_PRESS;
					ev.code = K_JOYUP;
					ev_postevent(&ev);
					Ystatus=0;
					break;
				}	   				   
				
				/* if control reaches here, the axis is centered,
				 * so just send a release signal if necisary */
				
				if (Ystatus==2)
				{
					ev.type = EV_RELEASE;
					ev.code = K_JOYDOWN;
					ev_postevent(&ev);
				}
				
				if (Ystatus==0)
				{
					ev.type = EV_RELEASE;
					ev.code = K_JOYUP;
					ev_postevent(&ev);
				}
				Ystatus=1;
				break;
			}
			break;
		case SDL_JOYBUTTONUP:
			if (event.jbutton.button>15) break;
			ev.type = EV_RELEASE;
			ev.code = K_JOY0 + event.jbutton.button;
			ev_postevent(&ev);
			break;
		case SDL_JOYBUTTONDOWN:
			if (event.jbutton.button>15) break;
			ev.type = EV_PRESS;
			ev.code = K_JOY0+event.jbutton.button;
			ev_postevent(&ev);
			break;
		case SDL_QUIT:
			exit(1);
			break;
		default:
			break;
		}
	}
	
	if(exitcheck==4){
		raise(SIGINT);
	}
}

void vid_setpal(int i, int r, int g, int b)
{
	SDL_Color col;

	col.r = r; col.g = g; col.b = b;

	SDL_SetColors(screen, &col, i, 1);
}

void vid_preinit()
{
}

void vid_close()
{
	if (overlay)
	{
		SDL_UnlockYUVOverlay(overlay);
		SDL_FreeYUVOverlay(overlay);
	}
	else SDL_UnlockSurface(screen);
	SDL_Quit();
	fb.enabled = 0;
}

void vid_settitle(char *title)
{
	SDL_WM_SetCaption(title, title);
}

void vid_begin()
{
	if (overlay)
	{
		SDL_LockYUVOverlay(overlay);
		fb.ptr = overlay->pixels[0];
		return;
	}
	SDL_LockSurface(screen);
	fb.ptr = screen->pixels;
}

void vid_end()
{
	if (overlay)
	{
		SDL_UnlockYUVOverlay(overlay);
        
#ifdef SDL_YUV_SHOW_FPS_TO_STDOUT
        /* Hack to dump FPS to stdout when YUV mode is enabled */
        if (sdl_showfps)
        {
            fps_current_time = SDL_GetTicks();
            fps_current_count++;
            if ( fps_current_time - fps_last_time >= 1000 )
            {
                printf("%d FPS\n", fps_last_count);
                /* reset fps count every second (not every frame) */
                fps_last_count = fps_current_count;
                fps_current_count = 0;
                fps_last_time = fps_current_time;
            }
        }
#endif /* SDL_YUV_SHOW_FPS_TO_STDOUT */

		if (fb.enabled)
			SDL_DisplayYUVOverlay(overlay, &overlay_rect);
		return;
	}
	SDL_UnlockSurface(screen);
    
    if (sdl_showfps)
    {
        fps_current_time = SDL_GetTicks();
        fps_current_count++;
        snprintf(fps_str, 19, "%d FPS", fps_last_count);
        if (sdl_showfps > 1 )
        {
            myrect.w = SFont_TextWidth(Font, fps_str);
            SDL_FillRect(screen, &myrect, 0 );
        }
        SFont_Write(screen, Font, 0,0, fps_str);
        if ( fps_current_time - fps_last_time >= 1000 )
        {
            /* reset fps count every second (not every frame) */
            fps_last_count = fps_current_count;
            fps_current_count = 0;
            fps_last_time = fps_current_time;
        }
    }

	if (fb.enabled) SDL_Flip(screen);
}

#ifndef GNUBOY_NO_SCREENSHOT
/*
**  TakeScreenShot of SDL surface, only needs SDL lib
**      surface is optional, if NULL is specified use the main video
**      filename is optional, if NULL is specified use SCREENSHOT_DEFAULT_FILENAME
**
**  Saves BMP files (SDL has built in support for BMP).
**  If no filename is provided a default name is used (which may
**  overwrite existing files).
*/
#define SCREENSHOT_DEFAULT_FILENAME "screenshot_gnuboy.bmp"
void TakeScreenShot(SDL_Surface *screen_to_save, char *filename)
{
    char *local_filename=NULL;
    SDL_Surface *local_screen=NULL;
    
    local_screen = screen_to_save;
    local_filename = filename;
    
    if (local_screen == NULL)
    {
        local_screen = SDL_GetVideoSurface();
        
        /*
        or if there is global screen ref go grab it ...
        extern SDL_Surface *screen;
        
        local_screen = screen;
        */
    }
    if (local_filename == NULL)
    {
        local_filename = SCREENSHOT_DEFAULT_FILENAME;
        /* TODO scan local dir and derive name? Use timestamp (note Dingoo has no clock). */
    }
    
    SDL_SaveBMP(local_screen, local_filename);
}

int vid_screenshot(char *filename)
{
    TakeScreenShot(screen, filename);
    return 0;
}
#endif /*GNUBOY_NO_SCREENSHOT */



#ifndef GNUBOY_DISABLE_SDL_SOUND

#include "pcm.h"


struct pcm pcm;


static int sound = 1;
static int samplerate = 44100;
static int stereo = 1;
static volatile int audio_done;

rcvar_t pcm_exports[] =
{
	RCV_BOOL("sound", &sound),
	RCV_INT("stereo", &stereo),
	RCV_INT("samplerate", &samplerate),
	RCV_END
};


static void audio_callback(void *blah, byte *stream, int len)
{
	(void) blah; /* avoid warning about unused parameter */

	memcpy(stream, pcm.buf, len);
	audio_done = 1;
}


void pcm_init()
{
	int i;
	SDL_AudioSpec as;

	if (!sound) return;
	
	SDL_InitSubSystem(SDL_INIT_AUDIO);
	as.freq = samplerate;
	as.format = AUDIO_U8;
	as.channels = 1 + stereo;
	as.samples = samplerate / 60;
	for (i = 1; i < as.samples; i<<=1);
	as.samples = i;
	as.callback = audio_callback;
	as.userdata = 0;
	if (SDL_OpenAudio(&as, 0) == -1)
		return;
	
	pcm.hz = as.freq;
	pcm.stereo = as.channels - 1;
	pcm.len = as.size;
	pcm.buf = malloc(pcm.len);
	pcm.pos = 0;
	memset(pcm.buf, 0, pcm.len);
	
	SDL_PauseAudio(0);
}

int pcm_submit()
{
	if (!pcm.buf) return 0;
	if (pcm.pos < pcm.len) return 1;
	while (!audio_done)
		SDL_Delay(4);
	audio_done = 0;
	pcm.pos = 0;
	return 1;
}

void pcm_close()
{
	if (sound) SDL_CloseAudio();
}
#endif /* GNUBOY_DISABLE_SDL_SOUND */


#ifdef GNUBOY_USE_SDL_TIMERS
/*
**  Return timer suitable for use with sys_elapsed()
**  Timer is initialized with the current time.
*/
void *sys_timer()
{
	Uint32 *tv;
	
	tv = malloc(sizeof *tv);
	*tv = SDL_GetTicks() * 1000;
	return (void *) tv;
}

/*
**  Return number of microseconds since input timer was last updated.
**  Also update the input timer with the current time.
*/
int sys_elapsed(void *in_ptr)
{
	Uint32 *cl;
	Uint32 now;
	Uint32 usecs;

	cl = (Uint32 *) in_ptr;
	now = SDL_GetTicks() * 1000;
	usecs = now - *cl;
	*cl = now;
	return (int) usecs;
}

void sys_sleep(int us)
{
	/*
	**  "us" is microseconds, SDL timers are all milliseconds
	**  1 second [s]  == 1000 millisecond [ms] == 1000000 microsecond [us]
	**  1 millisecond [ms] == 1000 microsecond [us]
	**  1000 microsecond [us] == 0.001 millisecond [ms]
	*/

	/* dbk: for some reason 2000 works..
	   maybe its just compensation for the time it takes for SDL_Delay to
	   execute, or maybe sys_timer is too slow */

	/*
	** if zero or negative,
	** do NOT perform math for microsecond to millisecond conversion
	*/
	if (us > 0)
		SDL_Delay(us/1000); /* NOTE signed input to an unsigned input function. */
}

#endif /* GNUBOY_USE_SDL_TIMERS */
