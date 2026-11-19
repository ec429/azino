/*
	azino - a QWERTY organ
	Copyright (C) 2010-11, 2026 Edward Cree

	See the file LICENSE for distribution conditions.
*/

#ifndef _GNU_SOURCE
	#define _GNU_SOURCE // 'feature test macro', apparently
#endif

#include <stdio.h>
#include <SDL.h>
#include <math.h>
#include <stdbool.h>
#include <SDL_audio.h>
#include <SDL_ttf.h>
#include "bits.h"

#ifndef SDLK_GRAVE
	#define SDLK_GRAVE 0x60
#endif

#define SX(surf)	((surf)->w)
#define SY(surf)	((surf)->h)

#define BPP	32

typedef enum
{
	SINE,
	TRIANGLE,
	EXPLOG,
	DIST,
	PHASOR,
	VOX,		// PHASOR sharpened by 1.2%
	PHATRI,		// TRIANGLE implemented with phasors
	VOXTRI,		// PHATRI sharpened by 1.2%
	PHAREED,	// even-harmonics
	OVERBLOWN,	// harmonic / overblown pipe
	CHIMNEY,	// half-stopped
}
wave;

typedef struct
{
	char *name;
	char *len;
	char *tc;
	double pitch; // 1 for unison (8')
	unsigned int phapi; // pitch in semitones from 1 (for PHASOR forms)
	double scale; // 0 will be pure, <1 for flute, ~1 for diapason, >1 for string
	wave form;
	bool stopped;
	unsigned char db;
	double vol;
	double vol2; // harmonic volume
	char *group; // groupname if last in group, else NULL
}
stop;

// function protos
SDL_Surface *ginit(int x, int y);
int pset(SDL_Surface *surf, int x, int y, unsigned char r, unsigned char g, unsigned char b);
int line(SDL_Surface *surf, int x0, int y0, int x1, int y1, unsigned char r, unsigned char g, unsigned char b);
void mixaudio(void *unused, Uint8 *stream, int len);
void freqcalc(int transp);
void addstop(stop new);
double szn(int p, double f, int phaf, double vol);
int dtext(SDL_Surface * scrn, int x, int y, char * text, TTF_Font * font, unsigned char r, unsigned char g, unsigned char b);
void write_midi_time(int dt);

int srate=44100;
int t=0;
bool midi=false, mhead=false;
int midchn=0;
unsigned int delts=192;
bool audio=true;
bool pressed[80]; // G to D'' in 4 different octave bases
bool fp,fr,fo;double fv,ff;int fm;
bool released[80]; // used for holding (return)
bool ospc[20], otab[20];
bool on[80], extra[80];
double vol[80];
double freq[80];
double temper[12]={1, 1.05112915, 1.125, 1.20078236, 1.25, 1.33497398, 1.40150557, 1.50097794, 1.6019688, 1.68180672, 1.77996531, 1.8765396};
/* base phasor oscillators */
#define N_PHASORS	24
#define PH_ORDER	32
struct {
	// z is the running e^iwt accumulator
	float z_re[N_PHASORS];
	float z_im[N_PHASORS];
	// w is the step multiplier, (1+eps)e^(iwdt)
	float w_re[N_PHASORS];
	float w_im[N_PHASORS];
	// a[0][n] is the coefficient of z^n in the current control-block's output
	// a[1][n] is the coefficient of z^16n where that doesn't fit in a[0]
	float a_re[N_PHASORS][2][PH_ORDER];
	float a_im[N_PHASORS][2][PH_ORDER];
	// are we updating a[n] this step?
	bool calc_a;
} phasors;
int omode[20];
double env;
double u;
double af=0.7,afs=0.7;
bool wwrite=false;
bool shake=false;
double shakeval=0;
double shakep=0;
double filt=0; // for s_Lp
int high;
bool organ=true;
bool metro=false;
bool m3=false;
int bpm=80;
int nstops=0;
stop *slist=NULL;
bool *stopset=NULL;
bool setting[60];
char *setname[60]={"5h: LShift produces fifths instead of octaves","Si: Shift Invert (toggle Extra Note)","Qe: Quicker entry","Ge: Gradual entry","Or: Override existing note (repeated notes)", "Su: Sustain notes longer","8t: Snap transposer to octaves","3m: Metronome beats compound time","Ns: No sustain","Ch: Chiptune mode", "Lp: Low Pass Filter","Ji: Just Intonation",NULL,NULL,NULL, NULL,NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL,NULL};
char *stext[60];
#define s_5h	setting[0]
#define s_Si	setting[1]
#define s_Qe	setting[2]
#define s_Ge	setting[3]
#define s_Or	setting[4]
#define s_Su	setting[5]
#define s_8t	setting[6]
#define s_3m	setting[7]
#define s_Ns	setting[8]
#define s_Ch	setting[9]
#define s_Lp	setting[10]
#define s_Ji	setting[11]

int main(int argc, char *argv[])
{
	int arg;
	int transp=0;
	high=80;
	int buflen=128;
	for(arg=1;arg<argc;arg++)
	{
		if(strcmp(argv[arg], "-Ow")==0) // output WAV
		{
			wwrite=true;
		}
		else if(strcmp(argv[arg], "-Om")==0) // output MIDI file
		{
			midi=true;
			mhead=true;
		}
		else if(strcmp(argv[arg], "-Oms")==0) // output MIDI Stream
		{
			midi=true;
			mhead=false;
		}
		else if(strncmp(argv[arg], "-ch=", 4)==0) // MIDI channel
		{
			int ch=-1;
			sscanf(argv[arg]+4, "%d", &ch);
			if((ch>=0)&&(ch<16))
				midchn=ch;
			else
				fprintf(stderr, "-ch bad value\n");
		}
		else if(strcmp(argv[arg], "-a")==0)
		{
			audio=false;
		}
		else if(strcmp(argv[arg], "-r")==0)
		{
			organ=false;
		}
		else if(strcmp(argv[arg], "-3")==0)
		{
			m3=true;
		}
		else if(strncmp(argv[arg], "-m=", 3)==0)
		{
			metro=true;
			sscanf(argv[arg]+3, "%d", &bpm);
			if(bpm>200)
			{
				fprintf(stderr, "-m parm may not exceed 200\n");
				bpm=200;
			}
			else if(bpm<20)
			{
				fprintf(stderr, "-m parm must be at least 20\n");
				bpm=20;
			}
		}
		else if(strncmp(argv[arg], "-t=", 3)==0)
		{
			sscanf(argv[arg]+3, "%d", &transp);
		}
		else if(strncmp(argv[arg], "-h=", 3)==0)
		{
			sscanf(argv[arg]+3, "%d", &high);
			if(high>80)
			{
				fprintf(stderr, "-h parm may not exceed 80\n");
				high=80;
			}
		}
		else if(strncmp(argv[arg], "-s=", 3)==0)
		{
			sscanf(argv[arg]+3, "%d", &srate);
		}
		else if(strncmp(argv[arg], "-b=", 3)==0)
		{
			sscanf(argv[arg]+3, "%d", &buflen);
		}
	}
	
	#ifdef WINDOWS
		if(wwrite)
		{
			freopen("out.wav", "wb", stdout);
		}
		else if(midi)
		{
			freopen("out.mid", "wb", stdout);
		}
	#endif
	
	FILE *stops=fopen("stops", "r");
	if(!stops)
	{
		fprintf(stderr, "Failed to open 'stops' file; full organ unavailable\n");
		addstop((stop){.name=strdup("8' Diapason"),.pitch=1,.scale=1,.form=SINE});
	}
	else
	{
		while(!feof(stops))
		{
			char *line=fgetl(stops);
			if(!line) break;
			if((*line) && (*line!='#'))
			{
				char *lline=strdup(line);
				char *name=strtok(line, ":");
				if(name)
				{
					char *pitch=strtok(NULL, ":");
					if(pitch)
					{
						char *scale=strtok(NULL, ":");
						if(scale)
						{
							char *wf=strtok(NULL, ":");
							if(wf)
							{
								char *st=strtok(NULL, ":");
								if(st)
								{
									char *mm=strtok(NULL, ":");
									if(mm)
									{
										char *gn=strtok(NULL, "");
										stop new;
										new.name=strdup(name);
										new.len=strndup(name, strcspn(name, " "));
										if(strchr(name, ' ')) new.tc=strndup(strchr(name, ' ')+1, 3); else new.tc=strdup("");
										sscanf(wf, "%d", (int *)&new.form);
										if (new.form >= PHASOR)
											sscanf(pitch, "%u", &new.phapi);
										else
											sscanf(pitch, "%lg", &new.pitch);
										sscanf(scale, "%lg", &new.scale);
										sscanf(st, "%d", (int *)&new.stopped);
										sscanf(mm, "%hhu", &new.db);
										new.vol=exp(0.1-new.pitch*0.15)*(nstops<3?(0.5-nstops*0.15):0.1);
										switch (new.form) {
										case OVERBLOWN:
										case CHIMNEY:
											new.vol*=2.0;
											break;
										case EXPLOG:
											new.vol*=exp(1.1-new.pitch*0.2);
											// fallthrough
										default:
											new.vol2=new.vol*new.scale;
											new.vol/=new.scale;
											break;
										}
										if(gn) new.group=strdup(gn);
										else new.group=NULL;
										addstop(new);
									}
									else
									{
										fprintf(stderr, "stops: bad line (no mm)\n\t%s\n", lline);
									}
								}
								else
								{
									fprintf(stderr, "stops: bad line (no st)\n\t%s\n", lline);
								}
							}
							else
							{
								fprintf(stderr, "stops: bad line (no wf)\n\t%s\n", lline);
							}
						}
						else
						{
							fprintf(stderr, "stops: bad line (no scale)\n\t%s\n", lline);
						}
					}
					else
					{
						fprintf(stderr, "stops: bad line (no pitch)\n\t%s\n", lline);
					}
				}
				else
				{
					fprintf(stderr, "stops: bad line (no name)\n\t%s\n", lline);
				}
				free(lline);
			}
			free(line);
		}
		fclose(stops);
	}
	
	stopset=malloc(nstops*sizeof(bool));
	memset(stopset, 0, nstops*sizeof(bool));
	if(nstops>4) stopset[4]=true;
	bool *stopmem[9];
	for(int i=0;i<9;i++)
	{
		stopmem[i]=malloc(nstops*sizeof(bool));
		memset(stopmem[i], 0, nstops*sizeof(bool));
		if(i<8)
		{
			for(int s=0;s<nstops;s++)
			{
				stopmem[i][s]=slist[s].db&(1<<i);
			}
		}
	}
	for(int s=0;s<nstops;s++)
	{
		stopset[s]=stopmem[3][s];
	}
	bool tosave=false;
	memset(setting, 0, sizeof(setting));
	s_Qe=true;
	s_8t=true;
	s_3m=m3;
	for(int i=0;i<60;i++)
	{
		if(setname[i])
			stext[i]=strndup(setname[i], 2);
		else
			stext[i]=NULL;
	}
	
	// SDL stuff
	SDL_Surface * screen=ginit(640,320);
	SDL_Rect cls;
	cls.x=0;
	cls.y=0;
	cls.w=SX(screen);
	cls.h=SY(screen);
	SDL_FillRect(screen, &cls, SDL_MapRGB(screen->format, 191, 0, 0));
	SDL_Flip(screen);
	
	// Set up vars, data
	u=env=1;
	bool spc=false, tab=false, retn=false;
	int mode=0;
	
	int j;
	for(j=0;j<80;j++)
	{
		pressed[j]=on[j]=released[j]=otab[j%20]=ospc[j%20]=false;
		vol[j]=0;
	}
	fp=fo=fr=false;
	fv=0;fm=0;
	freqcalc(transp);
	for(int j=0;j<N_PHASORS;j++) {
		phasors.z_re[j] = 1.0f;
		phasors.z_im[j] = 0.0f;
	}
	
	// More SDL stuff
	TTF_Font *font=NULL, *minifont=NULL;
	if(!TTF_Init())
	{
		font=TTF_OpenFont("Vera.ttf", 12);
		minifont=TTF_OpenFont("Vera.ttf", 9);
	}
	if(!font)
	{
		fprintf(stderr, "No 12pt font\n");
	}
	if(!minifont)
	{
		fprintf(stderr, "No 9pt font\n");
	}
	SDL_WM_SetCaption("Azino", "Azino");
	SDL_EnableUNICODE(1);
	SDL_EnableKeyRepeat(0, 0);
	SDL_Event event;
	int errupt = 0;
	SDL_AudioSpec fmt;
	fmt.freq = srate;
	fmt.format = AUDIO_S16;
	fmt.channels = 1;
	fmt.samples = buflen;
	fmt.callback = mixaudio;
	fmt.userdata = NULL;
	
	if(wwrite) // Put the WAV header on stdout
	{
		printf("RIFF%c%c%c%cWAVE", 255, 255, 255, 255);
		printf("fmt ");
		unsigned long int t;
		putchar(16);
		for(t=0;t<3;t++)
		{
			putchar(0);
		}
		putchar(1);
		putchar(0);
		putchar(1);
		putchar(0);
		unsigned long int rrate=srate;
		for(t=0;t<4;t++)
		{
			putchar((char)rrate);
			rrate=rrate>>8;
		}
		rrate=srate*16/8;
		for(t=0;t<4;t++)
		{
			putchar((char)rrate);
			rrate=rrate>>8;
		}
		putchar(16/8);
		putchar(0);
		putchar(16);
		putchar(0);
		printf("data%c%c%c%c", 255, 255, 255, 255);
	}
	else if(midi)
	{
		if(mhead) // Put MIDI header on stdout
		{
			//4D 54 68 64 00 00 00 06 ff ff nn nn dd dd
			printf("MThd");
			putchar(0);
			putchar(0);
			putchar(0);
			putchar(6);
			putchar(0);
			putchar(0);
			putchar(0);
			putchar(1);
			putchar(delts>>8);
			putchar(delts);
			// 4D 54 72 6B xx xx xx xx
			printf("MTrk");
			putchar(0xFF); // using 0xFFFFFFFF to signify 'length unknown and unbounded'
			putchar(0xFF);
			putchar(0xFF);
			putchar(0xFF);
		}
		// Set up
		// tempo
		if(mhead) putchar(0);
		putchar(0xFF);
		putchar(0x51);
		putchar(3);
		int us=floor(6e7/bpm);
		putchar(us>>16);
		putchar(us>>8);
		putchar(us);
	}
	int last_event=0; // used for MIDI delta-time
	
	SDL_AudioSpec obt;
	
	// Open the audio device
	if(SDL_OpenAudio(&fmt, &obt)<0)
	{
		fprintf(stderr, "Unable to open audio: %s\n", SDL_GetError());
		return(1);
	}
	
	if(obt.format != fmt.format)
	{
		fprintf(stderr, "The desired format was not obtained!\n");
		return(2);
	}
	
	SDL_PauseAudio(!audio);
	
	SDL_FillRect(screen, &cls, SDL_MapRGB(screen->format, 0, 127, 0));
	SDL_Flip(screen);
	
	bool pause=false;
	bool clicks=true;
	
	// Main program loop
	while(!errupt)
	{
		if(clicks)
		{
			int lx=0,ly=0;
			for(int i=0;i<nstops;i++)
			{
				int x,y,my;
				x=i%15;
				y=i/15;
				my=(nstops-1)/15+1;
				int sx,sy;
				sx=x*40+22;
				sy=y*((SY(screen)-20)/my)+10;
				SDL_Rect btn=(SDL_Rect){.x=sx, .y=sy, .w=36, .h=(SY(screen)-20)*.75/my-8};
				if(!ly)
				{
					lx=sx+18;
					ly=sy+btn.h+2;
				}
				else if(slist[i].group)
				{
					line(screen, lx, ly, lx, ly+7, 0x3f, 0x3f, 0x3f);
					line(screen, sx+18, ly+7, lx, ly+7, 0x3f, 0x3f, 0x3f);
					line(screen, sx+18, ly+8, lx, ly+8, 0x3f, 0x3f, 0x3f);
					line(screen, sx+18, ly+7, sx+18, ly, 0x3f, 0x3f, 0x3f);
					SDL_Surface *sText = TTF_RenderText_Solid(font, slist[i].group, (SDL_Color){0x00, 0x00, 0x07, 0});
					SDL_Rect rcDest = {(sx+18+lx-SX(sText))/2, ly, SX(sText), SY(sText)};
					SDL_FillRect(screen, &rcDest, SDL_MapRGB(screen->format, 0x3f, 0x3f, 0x3f));
					SDL_BlitSurface(sText, NULL, screen, &rcDest);
					SDL_FreeSurface(sText);
					ly=0;
				}
				SDL_FillRect(screen, &btn, stopset[i]?SDL_MapRGB(screen->format, 224, 224, 224):SDL_MapRGB(screen->format, 47, 47, 63));
				dtext(screen, sx+4, sy+4, slist[i].len, font, 0, 0, 240);
				dtext(screen, sx+4, sy+20, slist[i].tc, font, 0, 0, 160);
			}
			for(int i=0;i<60;i++)
			{
				if(!setname[i]) continue;
				int x,y,my;
				x=i%30;
				y=i/30;
				my=(nstops-1)/15+1;
				int sx,sy;
				sx=x*20+22;
				sy=(y+1)*((SY(screen)-20)/my)-3;
				SDL_Rect btn={.x=sx, .y=sy, .w=16, .h=8};
				SDL_FillRect(screen, &btn, setting[i]?SDL_MapRGB(screen->format, 224, 224, 224):SDL_MapRGB(screen->format, 47, 47, 63));
				if(screen&&minifont&&stext[i])
				{
					SDL_Color clrFg = {127, 63, 0, 0};
					SDL_Rect rcDest = {sx+2, sy-2, 12, 8};
					SDL_FillRect(screen, &rcDest, setting[i]?SDL_MapRGB(screen->format, 224, 224, 224):SDL_MapRGB(screen->format, 47, 47, 63));
					SDL_Surface *sText = TTF_RenderText_Solid(minifont, stext[i], clrFg);
					SDL_BlitSurface(sText, NULL, screen, &rcDest);
					SDL_FreeSurface(sText);
				}
			}
		}
		clicks=false;
		for(int i=0;i<80;i++)
		{
			if(released[i]&&(vol[i]<0.005)) {released[i]=false; vol[i]=0;};
			SDL_Rect note={.x=3+((i%20)<<1), .y=(i/20)<<1, .w=2, .h=2};
			SDL_FillRect(screen, &note, SDL_MapRGB(screen->format, released[i]?255:0, pressed[i]?255:0, 127));
		}
		{
			SDL_Rect row={.x=43, .y=0, .w=3, .h=8};
			SDL_FillRect(screen, &row, SDL_MapRGB(screen->format, 0, 63, 224));
			int y=(tab?0:1)+spc*(tab?3:1);
			row.y=y<<1;
			row.h=2;
			SDL_FillRect(screen, &row, SDL_MapRGB(screen->format, 255, 255, 0));
			row=(SDL_Rect){.x=0, .y=0, .w=3, .h=8};
			SDL_FillRect(screen, &row, SDL_MapRGB(screen->format, 0, 63, 224));
			row.y=y<<1;
			row.h=2;
			SDL_FillRect(screen, &row, SDL_MapRGB(screen->format, 255, 255, 0));
		}
		{
			SDL_FillRect(screen, &(SDL_Rect){.x=2, .y=35, .w=8, .h=141}, SDL_MapRGB(screen->format, 96, 48, 0));
			SDL_FillRect(screen, &(SDL_Rect){.x=2, .y=32, .w=8, .h=3}, SDL_MapRGB(screen->format, 127, 96, 31));
			SDL_FillRect(screen, &(SDL_Rect){.x=2, .y=68, .w=8, .h=3}, SDL_MapRGB(screen->format, 127, 72, 31));
			SDL_FillRect(screen, &(SDL_Rect){.x=2, .y=104, .w=8, .h=3}, SDL_MapRGB(screen->format, 127, 96, 31));
			SDL_FillRect(screen, &(SDL_Rect){.x=2, .y=140, .w=8, .h=3}, SDL_MapRGB(screen->format, 127, 72, 31));
			SDL_FillRect(screen, &(SDL_Rect){.x=2, .y=176, .w=8, .h=3}, SDL_MapRGB(screen->format, 127, 96, 31));
			if(abs(transp)<=24) SDL_FillRect(screen, &(SDL_Rect){.x=2, .y=104+3*transp, .w=8, .h=3}, SDL_MapRGB(screen->format, 191, 160, 0));
		}
		{
			SDL_FillRect(screen, &(SDL_Rect){.x=2, .y=204, .w=8, .h=32}, SDL_MapRGB(screen->format, 63, 63, 63));
			if(shakeval>0.005) SDL_FillRect(screen, &(SDL_Rect){.x=2, .y=236-32*shakeval, .w=8, .h=1+32*shakeval}, SDL_MapRGB(screen->format, 31, 191, 63));
		}
		SDL_Flip(screen);
		while(SDL_PollEvent(&event))
		{
			switch (event.type)
			{
				case SDL_QUIT:
					errupt++;
				break;
				case SDL_KEYDOWN:
					if(event.key.type==SDL_KEYDOWN)
					{
						SDL_keysym key=event.key.keysym;
						//fprintf(stderr, "%04x\n", key.sym);
						if(key.sym==SDLK_q)
						{
							errupt++;
						}
						else if(key.sym==SDLK_PAUSE)
						{
							if(pause)
							{
								SDL_PauseAudio(!audio);
								switch(mode)
								{
									case 0:
										SDL_FillRect(screen, &cls, SDL_MapRGB(screen->format, 0, 127, 0));
									break;
									case 1:
										SDL_FillRect(screen, &cls, SDL_MapRGB(screen->format, 0, 127, 159));
									break;
									case 2:
										SDL_FillRect(screen, &cls, SDL_MapRGB(screen->format, 127, 0, 159));
									break;
								}
								SDL_Flip(screen);
								pause=false;
							}
							else
							{
								SDL_PauseAudio(1);
								SDL_FillRect(screen, &cls, SDL_MapRGB(screen->format, 159, 127, 0));
								SDL_Flip(screen);
								pause=true;
							}
							clicks=true;
						}
						else if(key.sym==SDLK_GRAVE)
						{
							shake=true;
						}
						else if(key.sym==SDLK_KP_MULTIPLY)
						{
							tosave=true;
						}
						else if(key.sym==SDLK_KP_DIVIDE)
						{
							tosave=false;
						}
						else if((key.sym&0xFFF0)==0x100) // SDLK_KP_foo
						{
							int tomem=key.sym&0xF;
							if(tomem==14) // KP_PLUS
								organ=true;
							else if(tomem==13) // KP_MINUS
								organ=false;
							else if(tomem==15) // KP_ENTER
							{
								int j;
								for(j=0;j<high;j++)
								{
									vol[j]=on[j]=pressed[j]=released[j]=false;
								}
								fv=fo=fp=fr=false;
							}
							else
							{
								if(tomem>9) tomem=0;
								if(tosave)
								{
									fprintf(stderr, "Save to bank %d\n", tomem);
									if(tomem) memcpy(stopmem[tomem-1], stopset, nstops*sizeof(bool));
									else fprintf(stderr, "Can't save to bank 0; 1-9 only\n");
									tosave=false;
								}
								else
								{
									fprintf(stderr, tomem?"Load from bank %d\n":"Clear all stops\n", tomem);
									if(tomem<8) memcpy(stopmem[8], stopset, nstops*sizeof(bool));
									if(tomem) memcpy(stopset, stopmem[tomem-1], nstops*sizeof(bool));
									else memset(stopset, 0, nstops*sizeof(bool));
								}
							}
							clicks=true;
						}
						else if(key.sym==SDLK_SPACE)
						{
							spc=true;
						}
						else if(key.sym==SDLK_TAB)
						{
							tab=true;
						}
						else if(key.sym==SDLK_LEFT)
						{
							bpm=max(bpm-(key.mod&&SDLK_LSHIFT?1:5), 20);
							fprintf(stderr, "%d bpm\n", bpm);
							if(midi)
							{
								if(mhead)
								{
									int dt=t-last_event;
									last_event+=dt;
									write_midi_time(dt);
								}
								putchar(0xFF);
								putchar(0x51);
								putchar(3);
								int us=floor(6e7/bpm);
								putchar(us>>16);
								putchar(us>>8);
								putchar(us);
							}
						}
						else if(key.sym==SDLK_RIGHT)
						{
							bpm=min(bpm+(key.mod&&SDLK_LSHIFT?1:5), 200);
							fprintf(stderr, "%d bpm\n", bpm);
							if(midi)
							{
								if(mhead)
								{
									int dt=t-last_event;
									last_event+=dt;
									write_midi_time(dt);
								}
								putchar(0xFF);
								putchar(0x51);
								putchar(3);
								int us=floor(6e7/bpm);
								putchar(us>>16);
								putchar(us>>8);
								putchar(us);
							}
						}
						else if(key.sym==SDLK_UP)
						{
							metro=!metro;
						}
						else if((key.sym==SDLK_END)||(key.sym==SDLK_DOWN))
						{
							int j;
							for(j=0;j<high;j++)
							{
								pressed[j]=pressed[j]&&!released[j];
								released[j]=false;
							}
							fp=fp&&!fr;
							fr=false;
							if(midi)
							{
								for(unsigned int k=0;k<128;k++)
								{
									if(mhead)
									{
										int dt=t-last_event;
										last_event+=dt; // by only reading t once, we prevent /some/ of the errors that could happen if mixaudio is writing to it
										write_midi_time(dt);
									}
									if(!k) putchar(0x80|(midchn&0xF));
									putchar(k);
									putchar(0);
								}
							}
						}
						else if(key.sym==SDLK_RETURN)
						{
							retn=true;
						}
						else if(key.sym==SDLK_PAGEUP)
						{
							mode=max(mode-1, 0);
							if(!pause)
							{
								switch(mode)
								{
									case 0:
										SDL_FillRect(screen, &cls, SDL_MapRGB(screen->format, 0, 127, 0));
									break;
									case 1:
										SDL_FillRect(screen, &cls, SDL_MapRGB(screen->format, 0, 127, 159));
									break;
									case 2:
										SDL_FillRect(screen, &cls, SDL_MapRGB(screen->format, 127, 0, 159));
									break;
								}
								SDL_Flip(screen);
							}
							clicks=true;
						}
						else if(key.sym==SDLK_PAGEDOWN)
						{
							mode=min(mode+1, 2);
							if(!pause)
							{
								switch(mode)
								{
									case 0:
										SDL_FillRect(screen, &cls, SDL_MapRGB(screen->format, 0, 127, 0));
									break;
									case 1:
										SDL_FillRect(screen, &cls, SDL_MapRGB(screen->format, 0, 127, 159));
									break;
									case 2:
										SDL_FillRect(screen, &cls, SDL_MapRGB(screen->format, 127, 0, 159));
									break;
								}
								SDL_Flip(screen);
							}
							clicks=true;
						}
						else if(((key.unicode & 0xFFF0)==0x30)&&(key.unicode<0x3A))
						{
							if(key.unicode=='0')
							{
								afs=1.0;
							}
							else if(isdigit(key.unicode))
							{
								afs=0.1*(key.unicode-'0');
							}
						}
						else if((key.unicode & 0xFF80) == 0)
						{
							if(key.unicode=='+')
								organ=true;
							if(key.unicode=='-')
								organ=false;
							int n=-1;
							if(key.sym==SDLK_a) // G
								n=0;
							if(key.sym==SDLK_w) // G#/Ab
								n=1;
							if(key.sym==SDLK_s) // A
								n=2;
							if(key.sym==SDLK_e) // A#/Bb
								n=3;
							if(key.sym==SDLK_d) // B
								n=4;
							if(key.sym==SDLK_f) // C'
								n=5;
							if(key.sym==SDLK_t) // C#'/Db'
								n=6;
							if(key.sym==SDLK_g) // D'
								n=7;
							if(key.sym==SDLK_y) // D#'/Eb'
								n=8;
							if(key.sym==SDLK_h) // E'
								n=9;
							if(key.sym==SDLK_j) // F'
								n=10;
							if(key.sym==SDLK_i) // F#'/Gb'
								n=11;
							if(key.sym==SDLK_k) // G'
								n=12;
							if(key.sym==SDLK_o) // G#'/Ab'
								n=13;
							if(key.sym==SDLK_l) // A'
								n=14;
							if(key.sym==SDLK_p) // A#'/Bb'
								n=15;
							if((key.sym==SDLK_SEMICOLON)||(key.sym==SDLK_COLON)) // B'
								n=16;
							if(key.sym==SDLK_QUOTE) // C''
								n=17;
							if(key.sym==SDLK_RIGHTBRACKET) // C#''/Db''
								n=18;
							if(key.sym==SDLK_HASH) // D''
								n=19;
							if(n>=0)
							{
								int note=n+30+(spc?10:-10)*(tab?3:1);
								pressed[note]=true;
								released[note]=false;
								ospc[n]=spc;
								otab[n]=tab;
								on[note]=true;
								extra[note]=((key.mod&KMOD_LSHIFT)!=s_Si);
								if(midi)
								{
									int block=note/20, posn=note%20;
									int mn=block*12+posn+43+transp;
									if((mn>=0)&&(mn<=127))
									{
										if(mhead)
										{
											int dt=t-last_event;
											last_event+=dt;
											write_midi_time(dt);
										}
										putchar(0x90|(midchn&0xF));
										putchar(mn);
										putchar(floor(afs*127));
										if(extra[note]&&(mn+12<=127))
										{
											if(mhead)
												write_midi_time(0);
											putchar(mn+12);
											putchar(floor(afs*127));
										}
									}
								}
							}
							else
							{
								bool mswap=key.mod&KMOD_LCTRL;
								int n=-1;
								if(key.sym==SDLK_BACKSLASH)
									n=-2;
								if(key.sym==SDLK_z)
									n=0;
								if(key.sym==SDLK_x)
									n=2;
								if(key.sym==SDLK_c)
									n=4;
								if(key.sym==SDLK_v)
									n=5;
								if(key.sym==SDLK_b)
									n=7;
								if(key.sym==SDLK_n)
									n=9;
								if(key.sym==SDLK_m)
									n=10;
								if(key.sym==SDLK_COMMA)
									n=12;
								if(key.sym==SDLK_PERIOD)
									n=14;
								if(key.sym==SDLK_SLASH)
									n=16;
								if(key.sym==SDLK_RSHIFT)
									n=17;
								if(n==-2)
								{
									fm=mode+(mswap?3:0);
									int j;
									for(j=0;j<20;j++)
									{
										if(pressed[j]&&released[j])
										{
											pressed[j]=released[j]=false;
										}
									}
									if(midi)
									{
										int mn=63+transp;
										for(int k=0;k<min(mn,128);k++)
										{
											if(mhead)
											{
												int dt=t-last_event;
												last_event+=dt;
												write_midi_time(dt);
											}
											if(!k) putchar(0x80|(midchn&0xF));
											putchar(k);
											putchar(0);
										}
										mn=41+transp;
										if((mn>=0)&&(mn<128))
										{
											if(mhead)
											{
												int dt=t-last_event;
												last_event+=dt;
												write_midi_time(dt);
											}
											putchar(0x90|(midchn&0xF));
											putchar(mn);
											putchar(floor(afs*127));
										}
									}
									fp=fo=true;fr=false;
									if(!(key.mod&KMOD_LALT))
									{
										int a=2,b=5;
										if(((mode==1)&&!mswap)||((mode==2)&&mswap))
											a--;
										pressed[a]=on[a]=true;
										released[a]=extra[a]=false;
										pressed[b]=on[b]=true;
										released[b]=extra[b]=false;
										if(midi)
										{
											int ma=43+a+transp, mb=43+b+transp;
											if((ma>=0)&&(ma<128))
											{
												if(mhead)
												{
													int dt=t-last_event;
													last_event+=dt;
													write_midi_time(dt);
												}
												putchar(0x90|(midchn&0xF));
												putchar(ma);
												putchar(floor(afs*127));
											}
											if((mb>=0)&&(mb<128))
											{
												if(mhead)
												{
													int dt=t-last_event;
													last_event+=dt;
													write_midi_time(dt);
												}
												putchar(0x90|(midchn&0xF));
												putchar(mb);
												putchar(floor(afs*127));
											}
										}
									}
								}
								if(n>=0)
								{
									omode[n]=mode+(mswap?3:0);
									if(((mode==1)&&!mswap)||((mode==2)&&mswap))
									{
										switch(n)
										{
											case 2:
											case 9:
											case 14:
												n--;
											break;
										}
									}
									if(((mode==1)&&mswap)||((mode==2)&&!mswap))
									{
										switch(n)
										{
											case 4:
											case 9:
											case 16:
												n--;
											break;
										}
									}
									bool minor=mode;
									int j;
									for(j=0;j<19;j++)
									{
										if(pressed[j]&&released[j])
										{
											pressed[j]=released[j]=false;
										}
									}
									if(fp&&fr)
										fp=fr=false;
									if(midi)
									{
										int mn=63+transp;
										for(int k=0;k<min(mn,128);k++)
										{
											if(mhead)
											{
												int dt=t-last_event;
												last_event+=dt;
												write_midi_time(dt);
											}
											if(!k) putchar(0x80|(midchn&0xF));
											putchar(k);
											putchar(0);
										}
									}
									pressed[n]=on[n]=true;released[n]=extra[n]=false;
									if(midi)
									{
										int mn=n+43+transp;
										if((mn>=0)&&(mn<=127))
										{
											if(mhead)
											{
												int dt=t-last_event;
												last_event+=dt;
												write_midi_time(dt);
											}
											putchar(0x90|(midchn&0xF));
											putchar(mn);
											putchar(floor(afs*127));
										}
									}
									if(!(key.mod&KMOD_LALT))
									{
										int a=-1,b=-1,c=-1;
										switch(n)
										{
											case 0: // G B D
												a=((mode==2)!=mswap)?3:4;b=7;
											break;
											case 1: // Ab C Eb
												a=5;b=8;
											break;
											case 2: // A C E (Eb modal)
												a=5;b=((mode==2)!=mswap)?8:9;
											break;
											case 3: // Bb D F
											case 4: // B D F
											case 15:
											case 16:
												a=7;b=10;
											break;
											case 5: // C E G (Eb minor|modal)
											case 17:
												if(minor==mswap)
													c=0;
												a=(minor!=mswap)?8:9;b=12;
											break;
											case 7: // D F A (Ab minor)
												a=10;
												b=((mode==1)!=mswap)?13:14;
											break;
											case 8: // Eb G Bb
												a=12;b=mswap?16:15;
											break;
											case 9: // E G B
												a=12;b=16;
											break;
											case 10: // F A C (Ab minor)
												a=((mode==1)!=mswap)?13:14;
												b=17;
											break;
											case 12: // G B D (Bb modal)
												a=((mode==2)!=mswap)?15:16;
												b=7;
											break;
											case 13: // Ab C Eb
												a=17;b=8;
											break;
											case 14: // A C E
												a=17;b=9;
											break;
										}
										if(a>=0)
										{
											pressed[a]=on[a]=true;
											released[a]=extra[a]=false;
											if(midi)
											{
												int ma=43+a+transp;
												if((ma>=0)&&(ma<128))
												{
													if(mhead)
													{
														int dt=t-last_event;
														last_event+=dt;
														write_midi_time(dt);
													}
													putchar(0x90|(midchn&0xF));
													putchar(ma);
													putchar(floor(afs*127));
												}
											}
										}
										if(b>=0)
										{
											pressed[b]=on[b]=true;
											released[b]=extra[b]=false;
											if(midi)
											{
												int ma=43+b+transp;
												if((ma>=0)&&(ma<128))
												{
													if(mhead)
													{
														int dt=t-last_event;
														last_event+=dt;
														write_midi_time(dt);
													}
													putchar(0x90|(midchn&0xF));
													putchar(ma);
													putchar(floor(afs*127));
												}
											}
										}
										if(c>=0)
										{
											pressed[c]=on[c]=true;
											released[c]=extra[c]=false;
											if(midi)
											{
												int ma=43+c+transp;
												if((ma>=0)&&(ma<128))
												{
													if(mhead)
													{
														int dt=t-last_event;
														last_event+=dt;
														write_midi_time(dt);
													}
													putchar(0x90|(midchn&0xF));
													putchar(ma);
													putchar(floor(afs*127));
												}
											}
										}
									}
								}
							}
						}
					}
				break;
				case SDL_KEYUP:
					if(event.key.type==SDL_KEYUP)
					{
						SDL_keysym key=event.key.keysym;
						if(key.sym==SDLK_RETURN)
						{
							retn=false;
						}
						else if(key.sym==SDLK_TAB)
						{
							tab=false;
						}
						else if(key.sym==SDLK_SPACE)
						{
							spc=false;
						}
						else if(key.sym==SDLK_GRAVE)
						{
							shake=false;
						}
						else
						{
							int n=-1;
							if(key.sym==SDLK_a) // G
								n=0;
							if(key.sym==SDLK_w) // G#/Ab
								n=1;
							if(key.sym==SDLK_s) // A
								n=2;
							if(key.sym==SDLK_e) // A#/Bb
								n=3;
							if(key.sym==SDLK_d) // B
								n=4;
							if(key.sym==SDLK_f) // C'
								n=5;
							if(key.sym==SDLK_t) // C#'/Db'
								n=6;
							if(key.sym==SDLK_g) // D'
								n=7;
							if(key.sym==SDLK_y) // D#'/Eb'
								n=8;
							if(key.sym==SDLK_h) // E'
								n=9;
							if(key.sym==SDLK_j) // F'
								n=10;
							if(key.sym==SDLK_i) // F#'/Gb'
								n=11;
							if(key.sym==SDLK_k) // G'
								n=12;
							if(key.sym==SDLK_o) // G#'/Ab'
								n=13;
							if(key.sym==SDLK_l) // A'
								n=14;
							if(key.sym==SDLK_p) // A#'/Bb'
								n=15;
							if((key.sym==SDLK_SEMICOLON)||(key.sym==SDLK_COLON)) // B'
								n=16;
							if(key.sym==SDLK_QUOTE) // C''
								n=17;
							if(key.sym==SDLK_RIGHTBRACKET) // C#''/Db''
								n=18;
							if(key.sym==SDLK_HASH) // D''
								n=19;
							if(n>=0)
							{
								int note=n+30+(ospc[n]?10:-10)*(otab[n]?3:1);
								if(!retn)
								{
									pressed[note]=false;
									if(midi)
									{
										int block=note/20, posn=note%20;
										int mn=block*12+posn+43+transp;
										if((mn>=0)&&(mn<=127))
										{
											if(mhead)
											{
												int dt=t-last_event;
												last_event+=dt;
												write_midi_time(dt);
											}
											putchar(0x80|(midchn&0xF));
											putchar(mn);
											putchar(0);
											if(extra[note]&&(mn+12<=127))
											{
												if(mhead) write_midi_time(0);
												putchar(0x80|(midchn&0xF));
												putchar(mn+12);
												putchar(0);
											}
										}
									}
								}
								released[note]=true;/*
								released[n]=true;
								released[n+20]=true;
								released[n+40]=true;
								released[n+60]=true;*/
							}
							else
							{
								n=-1;
								if(key.sym==SDLK_BACKSLASH)
									n=-2;
								if(key.sym==SDLK_z)
									n=0;
								if(key.sym==SDLK_x)
									n=2;
								if(key.sym==SDLK_c)
									n=4;
								if(key.sym==SDLK_v)
									n=5;
								if(key.sym==SDLK_b)
									n=7;
								if(key.sym==SDLK_n)
									n=9;
								if(key.sym==SDLK_m)
									n=10;
								if(key.sym==SDLK_COMMA)
									n=12;
								if(key.sym==SDLK_PERIOD)
									n=14;
								if(key.sym==SDLK_SLASH)
									n=16;
								if(key.sym==SDLK_RSHIFT)
									n=17;
								if(n==-2)
								{
									fr=true;
									int a=2,b=5;
									int mode=fm%3;
									bool mswap=fm>2;
									if(((mode==1)&&!mswap)||((mode==2)&&mswap))
										a--;
									released[a]=released[b]=true;
								}
								if(n>=0)
								{
									int mode=omode[n]%3;
									bool mswap=omode[n]>2;
									bool minor=mode;
									if(((mode==1)&&!mswap)||((mode==2)&&mswap))
									{
										switch(n)
										{
											case 2:
											case 9:
											case 14:
												n--;
											break;
										}
									}
									if(((mode==1)&&mswap)||((mode==2)&&!mswap))
									{
										switch(n)
										{
											case 4:
											case 9:
											case 16:
												n--;
											break;
										}
									}
									released[n]=true;
									int a=-1,b=-1,c=-1;
									switch(n)
									{
										case 0: // G B D
											a=((mode==2)!=mswap)?3:4;b=7;
										break;
										case 1: // Ab C Eb
											a=5;b=8;
										break;
										case 2: // A C E (Eb modal)
											a=5;b=((mode==2)!=mswap)?8:9;
										break;
										case 3: // Bb D F
										case 4: // B D F
										case 15:
										case 16:
											a=7;b=10;
										break;
										case 5: // C E G (Eb minor|modal)
										case 17:
											if(minor==mswap)
												c=0;
											a=(minor!=mswap)?8:9;b=12;
										break;
										case 7: // D F A (Ab minor)
											a=10;
											b=((mode==1)!=mswap)?13:14;
										break;
										case 8: // Eb G Bb
											a=12;b=mswap?16:15;
										break;
										case 9: // E G B
											a=12;b=16;
										break;
										case 10: // F A C (Ab minor)
											a=((mode==1)!=mswap)?13:14;
											b=17;
										break;
										case 12: // G B D (Bb modal)
											a=((mode==2)!=mswap)?15:16;
											b=7;
										break;
										case 13: // Ab C Eb
											a=17;b=8;
										break;
										case 14: // A C E
											a=17;b=9;
										break;
									}
									if(a>=0)
									{
										released[a]=true;
									}
									if(b>=0)
									{
										released[b]=true;
									}
									if(c>=0)
									{
										released[c]=true;
									}
								}
							}
						}
					}
				break;
				case SDL_MOUSEBUTTONDOWN:
					if(event.button.type==SDL_MOUSEBUTTONDOWN)
					{
						int my=(nstops-1)/15+1;
						int sx=event.button.x-22, sy=event.button.y-10;
						int x=sx/40, y=sy/((SY(screen)-20)/my);
						int dx=sx-(x*40), dy=sy-y*((SY(screen)-20)/my);
						if((dx>=0)&&(dy>=0)&&(dx<36)&&(dy<(SY(screen)-20)*.75/my))
						{
							if(x>=0 && x<16)
							{
								int i=x+(y*15);
								if(i>=0 && i<nstops)
								{
									switch(event.button.button)
									{
										case SDL_BUTTON_LEFT:
											stopset[i]=!stopset[i];
											clicks=true;
										break;
										case SDL_BUTTON_RIGHT:;
											int k=0;
											for(int j=0;j<8;j++)
											{
												if(stopmem[j][i]) k|=(1<<j);
											}
											fprintf(stderr, "%s\t%hhu\n", slist[i].name, k);
										break;
									}
								}
							}
						}
						sx=event.button.x-22;sy=event.button.y+4;
						x=sx/20;y=sy/((SY(screen)-20)/my)-1;
						dx=sx-(x*20);dy=sy-(y+1)*((SY(screen)-20)/my);
						if((dx>=0)&&(dy>=0)&&(dx<16)&&(dy<8))
						{
							if(x>=0 && x<31)
							{
								int i=x+(y*30);
								if(i>=0 && i<60)
								{
									switch(event.button.button)
									{
										case SDL_BUTTON_LEFT:
											setting[i]=!setting[i];
											clicks=true;
										break;
										case SDL_BUTTON_RIGHT:
											fprintf(stderr, "%s\n", setname[i]);
										break;
									}
								}
							}
						}
						sx=event.button.x-3;sy=event.button.y-32;
						y=sy/3;
						if((sy>=0)&&(y<=48)&&(0<=sx)&&(sx<12))
						{
							transp=y-24;
							if(s_8t)
							{
								transp=floor((transp+6.0)/12)*12;
							}
						}
						freqcalc(transp);
					}
				break;
			}
		}
		SDL_Delay(1);
	}
	return(0);
}

SDL_Surface *ginit(int x, int y)
{
	SDL_Surface *surf;
	if(SDL_Init(SDL_INIT_VIDEO)<0)
	{
		perror("SDL_Init");
		return(NULL);
	}
	atexit(SDL_Quit);
	if((surf = SDL_SetVideoMode(x, y, BPP, SDL_HWSURFACE))==0)
	{
		perror("SDL_SetVideoMode");
		SDL_Quit();
		return(NULL);
	}
	if(SDL_MUSTLOCK(surf) && SDL_LockSurface(surf) < 0)
	{
		perror("SDL_LockSurface");
	}
	return(surf);
}

int pset(SDL_Surface *surf, int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
	if(!surf)
		return(1);
	if((x<0)||(y<0))
		return(2);
	if((x>=SX(surf))||(y>=SY(surf)))
		return(3);
	unsigned long int s_off = (y*surf->pitch) + (x*surf->format->BytesPerPixel);
	unsigned long int pixval = SDL_MapRGB(surf->format, r, g, b);
	*(unsigned long int *)((char *)surf->pixels + s_off)=pixval;
	return(0);
}

int line(SDL_Surface *surf, int x0, int y0, int x1, int y1, unsigned char r, unsigned char g, unsigned char b)
{
	// Bresenham's line algorithm, based on http://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm
	int e=0;
	if((e=pset(surf, x0, y0, r, g, b)))
		return(e);
	bool steep = abs(y1 - y0) > abs(x1 - x0);
	int tmp;
	if(steep)
	{
		tmp=x0;x0=y0;y0=tmp;
		tmp=x1;x1=y1;y1=tmp;
	}
	if(x0>x1)
	{
		tmp=x0;x0=x1;x1=tmp;
		tmp=y0;y0=y1;y1=tmp;
	}
	int dx=x1-x0,dy=abs(y1-y0);
	int ey=dx>>1;
	int dely=(y0<y1?1:-1),y=y0;
	int x;
	for(x=x0;x<x1;x++)
	{
		if((e=pset(surf, steep?y:x, steep?x:y, r, g, b)))
			return(e);
		ey-=dy;
		if(ey<0)
		{
			y+=dely;
			ey+=dx;
		}
	}
	return(0);
}

double szn(int p, double f, int phaf, double vol)
{
	if(organ)
	{
		double r=0;
		for(int s=0;s<nstops;s++)
		{
			if(stopset[s])
			{
				if (!s_Ch && slist[s].form>=PHASOR) {
					if(!phasors.calc_a)
						continue;
					// Can't have 64' pedal F
					if ((int)slist[s].phapi + phaf < 0)
						continue;
					unsigned int pitch = slist[s].phapi + phaf;
					unsigned int base = pitch % 12;
					unsigned int shift = pitch / 12;
					double v;
					switch (slist[s].form) {
					case VOX:
						// use the detuned ranks
						base+=12;
						// fallthrough
					case PHASOR:
					diap:
						// For now we are oversimplified and have only real coefficients
						if (1<<shift <= PH_ORDER)
							phasors.a_re[base][0][(1<<shift)-1] += slist[s].vol * vol;
						else if (1<<(shift-4) <= PH_ORDER)
							phasors.a_re[base][1][(1<<(shift-4))-1] += slist[s].vol * vol;
						int over = slist[s].stopped?3:2;
						if (over<<shift <= PH_ORDER)
							phasors.a_re[base][0][(over<<shift)-1] += slist[s].vol2 * vol;
						else if (over<<(shift-4) <= PH_ORDER)
							phasors.a_re[base][1][(over<<(shift-4))-1] += slist[s].vol2 * vol;
						break;
					case VOXTRI:
						// use the detuned ranks
						base+=12;
						// fallthrough
					case PHATRI:
						for(int j=0;;j++) {
							int over = j*2 + 1;
							v = ((j&1)?-1.0:1.0)/(over*over);
							if (2*over<<shift <= PH_ORDER)
								phasors.a_re[base][0][(2*over<<shift)-1] += slist[s].vol2 * vol * v;
							else if ((over<<shift)/8 <= PH_ORDER)
								phasors.a_re[base][1][((over<<shift)/8)-1] += slist[s].vol2 * vol * v;
							if (over<<shift <= PH_ORDER)
								phasors.a_re[base][0][(over<<shift)-1] += slist[s].vol * vol * v;
							else if ((over<<shift)/16 <= PH_ORDER)
								phasors.a_re[base][1][((over<<shift)/16)-1] += slist[s].vol * vol * v;
							else
								break;
						}
						break;
					case PHAREED:
						for(int j=0;;j++) {
							int over = j?j*2:1;
							v = 1.0/((j+1)*(j+1));
							if (2*over<<shift <= PH_ORDER)
								phasors.a_re[base][0][(2*over<<shift)-1] += slist[s].vol2 * vol * v;
							else if ((over<<shift)/8 <= PH_ORDER)
								phasors.a_re[base][1][((over<<shift)/8)-1] += slist[s].vol2 * vol * v;
							if (over<<shift <= PH_ORDER)
								phasors.a_re[base][0][(over<<shift)-1] += slist[s].vol * vol * v;
							else if ((over<<shift)/16 <= PH_ORDER)
								phasors.a_re[base][1][((over<<shift)/16)-1] += slist[s].vol * vol * v;
							else
								break;
						}
						break;
					case OVERBLOWN:
						if (pitch < 48) // lower ranks use single-length flutes
							goto diap;
						v = 1.0;
						if (1<<(shift-1) <= PH_ORDER)
							phasors.a_re[base][0][(1<<(shift-1))-1] += slist[s].vol * vol * 0.025;
						else if ((1<<(shift-1))/16 <= PH_ORDER)
							phasors.a_re[base][1][((1<<(shift-1))/16)-1] += slist[s].vol * vol * 0.025;
						for(int j=1;;j++) {
							if (j<<shift <= PH_ORDER)
								phasors.a_re[base][0][(j<<shift)-1] += slist[s].vol * vol * v;
							else if ((j<<shift)/16 <= PH_ORDER)
								phasors.a_re[base][1][((j<<shift)/16)-1] += slist[s].vol * vol * v;
							else
								break;
							v *= slist[s].scale;
						}
						break;
					case CHIMNEY:
						if (pitch < 48) // chimneys are usually omitted in the lower pitches
							goto diap;
						for(int j=1;;j++) {
							v = 1.0/(j*j*j);
							if (!(j&1))
								v*=slist[s].scale;
							if (j<<shift <= PH_ORDER)
								phasors.a_re[base][0][(j<<shift)-1] += slist[s].vol * vol * v;
							else if ((j<<shift)/16 <= PH_ORDER)
								phasors.a_re[base][1][((j<<shift)/16)-1] += slist[s].vol * vol * v;
							else
								break;
						}
						break;
					default:
						fprintf(stderr, "Bad slist[%d].form %d\n", s, slist[s].form);
						return 0.0;
					}
					continue;
				}
				for(int i=1;i<3;i++)
				{
					double pitch=(slist[s].form>=PHASOR)?pow(2, slist[s].pitch/12.0):slist[s].pitch;
					double ph=fmod(p*f*(slist[s].stopped?(i<<1)-1:i)*pitch, M_PI*2);
					double v=0;
					switch(slist[s].form)
					{
						case SINE:
						case PHASOR:
						case VOX:
						case CHIMNEY:
							v=s_Ch?(ph>M_PI?1:0):sin(ph);
						break;
						case TRIANGLE:
						case PHATRI:
						case VOXTRI:
							if(s_Ch)
							{
								int part=ph*16/M_PI;
								v=(part>15?12-part:part-4)*.25;
							}
							else
							{
								v=ph>M_PI?M_PI*1.5-ph:ph-M_PI*0.5;
								v=((fabs(v)+0.01)/(fabs(v)+1.01)-(1.0/101))*(v>0?1:-1);
							}
						break;
						case EXPLOG:
						case PHAREED:
							if(s_Ch)
							{
								ph=fmod(p*f*(slist[s].stopped?(i<<1)-1:i)*slist[s].pitch, M_PI*512);
								int step=(ph/M_PI);
								if(step>256) step=512-step;
								v=(fmod(ph/M_PI/2, 1)*256>step)?-1:1;
							}
							else
								v=ph>M_PI?0.5-log2(ph/M_PI):exp(ph-M_PI)/(1-exp(-M_PI))-exp(-M_PI)-0.5;
						break;
						case DIST:
						case OVERBLOWN:
							v=s_Ch?(ph>1?1:0):3.0*sin(sin(ph)*3.2);
						break;
					}
					r+=v*(i&2?slist[s].vol2:slist[s].vol);
				}
			}
		}
		return(r * vol);
	}
	else
		return sin(p*f) * vol;
}

void mixaudio(__attribute__((unused)) void *unused, Uint8 *stream, int len)
{
	int i;
	double hit=(organ?50.0:400.0)/srate;
	if(s_Qe)
	{
		hit=750.0/srate;
	}
	if(s_Ge)
		hit*=0.2;
	memset(phasors.a_re, 0, sizeof(phasors.a_re));
	memset(phasors.a_im, 0, sizeof(phasors.a_im));
	for(i=0;i<len;i+=2)
	{
		phasors.calc_a = !i;
		t=(t+1);
		/* Spin the fundamental oscillators */
		for(int j=0;j<N_PHASORS;j++) {
			float re = phasors.z_re[j] * phasors.w_re[j] -
				   phasors.z_im[j] * phasors.w_im[j];
			float im = phasors.z_re[j] * phasors.w_im[j] +
				   phasors.z_im[j] * phasors.w_re[j];
			/* clip phasors to [-1, 1] bounding box.
			 * Symmetric for now, later each phasor will have its
			 * own clip values specified.
			 */
			re = re > 1.0f ? 1.0f : (re < -1.0f ? -1.0f : re);
        		im = im > 1.0f ? 1.0f : (im < -1.0f ? -1.0f : im);
        		phasors.z_re[j] = re;
        		phasors.z_im[j] = im;
		}
		if(shake||(shakeval>0.005))
		{
			if(!(t%25))
			{
				if(!shake) shakeval*=(1-30.0/srate);
				else shakeval=(shakeval*(1-30.0/srate))+(30.0/srate);
			}
		}
		else
			shakeval=0;
		double x=0;
		if(fv>0.005) // code for bottom F
		{
			x+=szn(t, ff, -2, fv)*7.5;
			if(!fp&&!(t%25))
			{
				if(s_Ns)
					fv=(fv*(1-1440.0/srate));
				else
					fv=(fv*(1-(s_Su?30.0:120.0)/srate));
			}
		}
		if(fp&&!(t%5))
		{
			if(fo&&!s_Or)
			{
				fv=(fv*(1-100.0/srate));
				if(fv<0.2)
					fo=false;
			}
			else
			{
				fv=hit+(fv*(1-hit));
			}
		}
		for(int j=0;j<high;j++)
		{
			int pit = (j/20)*12+(j%20);
			if(vol[j]>0.005)
			{
				x+=szn(t, freq[j], pit, vol[j])*7.5;
				if(extra[j])
					x+=szn(t, freq[j]*(s_5h?1.5:2.0), pit + (s_5h?7:12), vol[j])*7.5;
				/*if(s_Ns)
					fv=(fv*(1-5000.0/srate));
				else
					fv=(fv*(1-(s_Su?30.0:120.0)/srate));*/
				if(!pressed[j]&&!(t%25))
				{
					if(s_Ns)
						vol[j]=(vol[j]*(1-1440.0/srate));
					else
						vol[j]=(vol[j]*(1-(s_Su?30.0:120.0)/srate));
				}

			}
			if(pressed[j]&&!(t%5))
			{
				if(on[j]&&!s_Or)
				{
					vol[j]=(vol[j]*(1-hit));
					if(vol[j]<0.2)
						on[j]=false;
				}
				else
				{
					vol[j]=hit+(vol[j]*(1-hit));
				}
			}
		}
		// accumulate phasor polynomials
		for(int j=0;j<N_PHASORS;j++) {
			float re = phasors.z_re[j], im = phasors.z_im[j];
			float sq_re, sq_im;
			float qq_re, qq_im;
			float re16, im16;
			x += (re*phasors.a_re[j][0][0] - im*phasors.a_im[j][0][0])*7.5;
			for(int k=1;k<PH_ORDER;k++) {
				float next_re, next_im;
				if (k>=16) {
					// only powers of z^4 beyond this point
					if ((k+1)&3) continue;
					next_re = re*qq_re - im*qq_im;
					next_im = re*qq_im + im*qq_re;
				} else if (k>=8) {
					// only powers of z^2 beyond this point
					if ((k+1)&1) continue;
					next_re = re*sq_re - im*sq_im;
					next_im = re*sq_im + im*sq_re;
				} else {
					next_re = re*phasors.z_re[j] - im*phasors.z_im[j];
					next_im = re*phasors.z_im[j] + im*phasors.z_re[j];
				}
				re = next_re;
				im = next_im;
				if (k==1) {
					sq_re = re;
					sq_im = im;
				} else if (k==3) {
					qq_re = re;
					qq_im = im;
				} else if (k==15) {
					re16 = re;
					im16 = im;
				}
				x += (re*phasors.a_re[j][0][k] - im*phasors.a_im[j][0][k])*7.5;
			}
			// a_re[j][1][0] is always zero, don't need to x+=.
			re=re16;
			im=im16;
			for(int k=1;k<PH_ORDER;k++) {
				float next_re, next_im;
				if (k>=16) {
					// only powers of z^64 beyond this point
					if ((k+1)&3) continue;
					next_re = re*qq_re - im*qq_im;
					next_im = re*qq_im + im*qq_re;
				} else if (k>=8) {
					// only powers of z^32 beyond this point
					if ((k+1)&1) continue;
					next_re = re*sq_re - im*sq_im;
					next_im = re*sq_im + im*sq_re;
				} else {
					next_re = re*re16 - im*im16;
					next_im = re*im16 + im*re16;
				}
				re = next_re;
				im = next_im;
				if (k==1) {
					sq_re = re;
					sq_im = im;
				} else if (k==3) {
					qq_re = re;
					qq_im = im;
				}
				x += (re*phasors.a_re[j][1][k] - im*phasors.a_im[j][1][k])*7.5;
			}
		}
		//double v=abs(x);
		//u=max(v,u*(1-5.0/srate));
		//env*=pow(200.0/((u*env)+160.0), 0.4);
		//env=min(max(env, 0.0005), 400);
		if(s_Lp) filt=x=filt*(1-1000.0/srate)+x*1000.0/srate;
		double r=fabs((double)x*(double)1*(exp(4.0*af-3.0)-exp(-2.6))*2.0);
		if(shakeval>0.05)
		{
			shakep+=shakeval*M_PI*24.0/srate;
			r*=1.0+(0.25*shakeval*sin(shakep));
		}
		if(!(t%25))
		{
			if(af>afs) af-=2.5/srate;
			else if(af<afs) af+=5.0/srate;
		}
		if(r>100.0)
			r=100.0+27.5*((r-100.0)/(r-70.0));
		//stream[i]=min(max(floor(127.5+(((x*env)>0)?(r):(-r))), 0), 255);
		unsigned short int w=min(max(floor(((x>0)?((double)r*257.0):(-(double)r*257.0))), -32768.0), 32767.0);
		stream[i]=(w%256);
		stream[i+1]=(w>>8);
		if(wwrite)
		{
			printf("%c%c", stream[i], stream[i+1]);
		}
		if(metro)
		{
			int per=floor(srate*30.0/bpm);
			if((t%per)<srate/400)
			{
				if(t%2)
				{
					w=0x4FFF;
				}
				else
				{
					w=0x3000;
				}
				if(s_3m?(t%(per*3))>=srate/400:(t%(per<<1))<srate/400)
				{
					w*=2;
				}
				stream[i]=(w%256);
				stream[i+1]=(w>>8);
			}
		}
	}
	if(wwrite)
		fflush(stdout);
}

void freqcalc(int transp)
{
	for(int j=0;j<80;j++)
	{
		int t=j+transp-(8.0*((int)j/20))-5.0;
		int ti=((t%12)+12)%12;
		freq[j]=
			s_Ji?M_PI*256.0*(double)exp2(floor(t/12.0))*temper[ti]/(double)srate
			:M_PI*256.0*(double)exp2(t/12.0)/(double)srate;
	}
	int t=transp-7.0;
	int ti=((t%12)+12)%12;
	ff=
		s_Ji?M_PI*256.0*(double)exp2(floor(t/12.0))*temper[ti]/(double)srate
		:M_PI*256.0*(double)exp2(t/12.0)/(double)srate;
	for(int j=0;j<N_PHASORS;j++) {
		// Slightly longer than 1 so that it stays on the clip and we don't have to normalise
		// Float ulp at 1 is about 1.19e-7
		double detune = j < 12 ? 1 : 1.012;
		phasors.w_re[j] = cos(freq[j % 12] * detune / 8) * (1 + 2e-7);
		phasors.w_im[j] = sin(freq[j % 12] * detune / 8) * (1 + 2e-7);
	}
}

void addstop(stop new)
{
	int n=nstops++;
	stop *nlist=realloc(slist, nstops*sizeof(stop));
	if(!nlist)
	{
		nstops=n;
		perror("addstop: realloc");
		return; // fail
	}
	(slist=nlist)[n]=new;
}

int dtext(SDL_Surface * scrn, int x, int y, char * text, TTF_Font * font, unsigned char r, unsigned char g, unsigned char b)
{
	if(!scrn) return(1);
	if(!font) return(1);
	if(!text) return(1);
	SDL_Color clrFg = {r, g, b, 0};
	SDL_Rect rcDest = {x, y, 28, 14};
	SDL_FillRect(scrn, &rcDest, SDL_MapRGB(scrn->format, 127, 127, 95));
	int s=4-strlen(text);
	if(strchr(text, 'm')) s--;
	rcDest.x+=3*s;
	rcDest.w-=6*s;
	SDL_Surface *sText = TTF_RenderText_Solid(font, text, clrFg);
	SDL_BlitSurface(sText, NULL, scrn, &rcDest);
	SDL_FreeSurface(sText);
	return(0);
}

void write_midi_time(int dt)
{
	int t=(dt*delts*bpm)/(srate*60);
	unsigned char bytes[4];
	bytes[0]=t&0x7f;
	bytes[1]=(t>>7)&0x7f;
	bytes[2]=(t>>14)&0x7f;
	bytes[3]=(t>>21)&0x7f;
	int m=3;
	while(m&&!bytes[m]) m--;
	for(;m>=0;m--)
	{
		putchar(bytes[m]|(m?0x80:0));
	}
}
