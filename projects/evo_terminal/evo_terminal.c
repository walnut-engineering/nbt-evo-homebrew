/*
 * evo_terminal - a terminal emulator for the BMW NBT EVO head unit (QNX 6.5).
 *
 * Renders a text grid onto the car display using the shared EVO graphics
 * library (the same Screen + ILM layer machinery DOOM uses), runs a real shell
 * on a pseudo-terminal, and drives it from the USB keyboard.
 *
 *   USB keyboard (/dev/usbkbd0, PS/2 set-1 scancodes)
 *        -> decode to ASCII  -> write pty master
 *   shell (ksh on pty slave) -> pty master -> VT100 parse -> text grid
 *        -> 8x8 bitmap font (scaled) -> framebuffer -> present
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <stdint.h>
#include <spawn.h>

#include "evo_graphics_lib.h"
#include "evo_context.h"   /* evo_context_get_display */
#include "evo_display.h"   /* evo_display_enable_presentation_mode */
#include "font8x8_basic.h"
#include "evo_appinput.h"  /* iDrive BACK button -> quit (no keyboard needed) */

/* ---- display / grid geometry ---- */
#define DISPLAY_W 1440
#define DISPLAY_H 540
#define SCALE     2                 /* each 8x8 glyph drawn at SCALE */
#define CELL_W    (8 * SCALE)       /* 16 */
#define CELL_H    (8 * SCALE)       /* 16 */
#define COLS      (DISPLAY_W / CELL_W)   /* 90 */
#define ROWS      (DISPLAY_H / CELL_H)   /* 33 */

/* ARGB8888.  If red/blue look swapped on the panel, build with -DSWAP_RB. */
#ifdef SWAP_RB
#define RGB(r,g,b) (0xFF000000u | ((uint32_t)(b)<<16) | ((uint32_t)(g)<<8) | (uint32_t)(r))
#else
#define RGB(r,g,b) (0xFF000000u | ((uint32_t)(r)<<16) | ((uint32_t)(g)<<8) | (uint32_t)(b))
#endif

#define DEFAULT_BG RGB(0x00,0x00,0x00)
#define DEFAULT_FG RGB(0x66,0xff,0x99)

/* 16-colour ANSI palette */
static uint32_t g_ansi[16];
static void init_palette(void){
    g_ansi[0]=RGB(0x1c,0x1f,0x26); g_ansi[1]=RGB(0xe0,0x50,0x50);
    g_ansi[2]=RGB(0x50,0xd0,0x60); g_ansi[3]=RGB(0xe0,0xc0,0x50);
    g_ansi[4]=RGB(0x50,0x90,0xe0); g_ansi[5]=RGB(0xc0,0x60,0xe0);
    g_ansi[6]=RGB(0x40,0xc8,0xd0); g_ansi[7]=RGB(0xd0,0xd4,0xdc);
    g_ansi[8]=RGB(0x55,0x5a,0x66); g_ansi[9]=RGB(0xff,0x70,0x70);
    g_ansi[10]=RGB(0x80,0xf0,0x90);g_ansi[11]=RGB(0xff,0xe0,0x70);
    g_ansi[12]=RGB(0x80,0xb0,0xff);g_ansi[13]=RGB(0xe0,0x90,0xff);
    g_ansi[14]=RGB(0x70,0xf0,0xf8);g_ansi[15]=RGB(0xff,0xff,0xff);
}

/* ---- terminal state ---- */
static unsigned char g_ch[ROWS][COLS];
static uint32_t      g_fg[ROWS][COLS];
static uint32_t      g_bg[ROWS][COLS];
static int  g_cr = 0, g_cc = 0;           /* cursor row/col */
static uint32_t g_cur_fg, g_cur_bg;       /* current drawing attrs */
static int  g_master = -1;
static volatile int g_running = 1;
static evo_context_t *g_ctx = NULL;

/* modifier state (keyboard thread) */
static int g_shift = 0, g_ctrl = 0, g_ext = 0, g_caps = 0, g_num = 1;

static void cell_clear(int r,int c){ g_ch[r][c]=' '; g_fg[r][c]=g_cur_fg; g_bg[r][c]=g_cur_bg; }
static void grid_clear(void){ int r,c; for(r=0;r<ROWS;r++) for(c=0;c<COLS;c++) cell_clear(r,c); }

static void scroll_up(void){
    int r,c;
    for(r=0;r<ROWS-1;r++) for(c=0;c<COLS;c++){ g_ch[r][c]=g_ch[r+1][c]; g_fg[r][c]=g_fg[r+1][c]; g_bg[r][c]=g_bg[r+1][c]; }
    for(c=0;c<COLS;c++){ g_ch[ROWS-1][c]=' '; g_fg[ROWS-1][c]=g_cur_fg; g_bg[ROWS-1][c]=g_cur_bg; }
}
static void newline(void){ g_cc=0; if(++g_cr>=ROWS){ scroll_up(); g_cr=ROWS-1; } }

/* ---- VT100 parser ---- */
static enum { P_NORMAL, P_ESC, P_CSI } g_pstate = P_NORMAL;
static int g_par[8], g_npar;

static void sgr_apply(void){
    int i;
    if(g_npar==0){ g_cur_fg=DEFAULT_FG; g_cur_bg=DEFAULT_BG; return; }
    for(i=0;i<g_npar;i++){
        int p=g_par[i];
        if(p==0){ g_cur_fg=DEFAULT_FG; g_cur_bg=DEFAULT_BG; }
        else if(p==7){ uint32_t t=g_cur_fg; g_cur_fg=g_cur_bg; g_cur_bg=t; }
        else if(p>=30&&p<=37) g_cur_fg=g_ansi[p-30];
        else if(p>=90&&p<=97) g_cur_fg=g_ansi[8+p-90];
        else if(p>=40&&p<=47) g_cur_bg=g_ansi[p-40];
        else if(p>=100&&p<=107) g_cur_bg=g_ansi[8+p-100];
        else if(p==39) g_cur_fg=DEFAULT_FG;
        else if(p==49) g_cur_bg=DEFAULT_BG;
    }
}

static void csi_dispatch(char cmd){
    int n = (g_npar>0)?g_par[0]:0;
    switch(cmd){
    case 'A': g_cr -= (n?n:1); if(g_cr<0)g_cr=0; break;
    case 'B': g_cr += (n?n:1); if(g_cr>=ROWS)g_cr=ROWS-1; break;
    case 'C': g_cc += (n?n:1); if(g_cc>=COLS)g_cc=COLS-1; break;
    case 'D': g_cc -= (n?n:1); if(g_cc<0)g_cc=0; break;
    case 'H': case 'f': {
        int r=(g_npar>0&&g_par[0])?g_par[0]-1:0;
        int c=(g_npar>1&&g_par[1])?g_par[1]-1:0;
        g_cr=(r<0)?0:(r>=ROWS?ROWS-1:r); g_cc=(c<0)?0:(c>=COLS?COLS-1:c); break; }
    case 'J': { int r,c; /* 2=whole,0=to end,1=to start */
        if(n==2){ grid_clear(); g_cr=0; g_cc=0; }
        else if(n==0){ for(c=g_cc;c<COLS;c++)cell_clear(g_cr,c); for(r=g_cr+1;r<ROWS;r++)for(c=0;c<COLS;c++)cell_clear(r,c); }
        else { for(r=0;r<g_cr;r++)for(c=0;c<COLS;c++)cell_clear(r,c); for(c=0;c<=g_cc;c++)cell_clear(g_cr,c); }
        break; }
    case 'K': { int c;
        if(n==0) for(c=g_cc;c<COLS;c++)cell_clear(g_cr,c);
        else if(n==1) for(c=0;c<=g_cc;c++)cell_clear(g_cr,c);
        else for(c=0;c<COLS;c++)cell_clear(g_cr,c);
        break; }
    case 'm': sgr_apply(); break;
    default: break; /* ignore the rest */
    }
}

static void put_byte(unsigned char b){
    if(g_pstate==P_NORMAL){
        if(b==0x1b){ g_pstate=P_ESC; return; }
        if(b=='\n'){ newline(); return; }
        if(b=='\r'){ g_cc=0; return; }
        if(b=='\b'){ if(g_cc>0)g_cc--; return; }
        if(b=='\t'){ g_cc=(g_cc+8)&~7; if(g_cc>=COLS)g_cc=COLS-1; return; }
        if(b==0x07) return;                 /* bell */
        if(b<0x20) return;                  /* other control */
        g_ch[g_cr][g_cc]=b; g_fg[g_cr][g_cc]=g_cur_fg; g_bg[g_cr][g_cc]=g_cur_bg;
        if(++g_cc>=COLS) newline();
    } else if(g_pstate==P_ESC){
        if(b=='['){ g_pstate=P_CSI; g_npar=0; g_par[0]=0; }
        else g_pstate=P_NORMAL;              /* unsupported ESC x */
    } else { /* P_CSI */
        if(b>='0'&&b<='9'){ if(g_npar==0)g_npar=1; g_par[g_npar-1]=g_par[g_npar-1]*10+(b-'0'); }
        else if(b==';'){ if(g_npar<8){ g_par[g_npar]=0; g_npar++; } }
        else if(b=='?'){ /* private mode marker, ignore */ }
        else { csi_dispatch((char)b); g_pstate=P_NORMAL; }
    }
}

/* ---- rendering ---- */
static void draw_cell(uint32_t *fb,int r,int c){
    int px=c*CELL_W, py=r*CELL_H, y,x,sy,sx;
    const char *g = font8x8_basic[g_ch[r][c] & 0x7f];
    uint32_t fg=g_fg[r][c], bg=g_bg[r][c];
    for(y=0;y<8;y++){
        unsigned char row=(unsigned char)g[y];
        for(x=0;x<8;x++){
            uint32_t col=(row & (1u<<x))?fg:bg;
            for(sy=0;sy<SCALE;sy++){
                uint32_t *p=&fb[(py+y*SCALE+sy)*DISPLAY_W + px+x*SCALE];
                for(sx=0;sx<SCALE;sx++) p[sx]=col;
            }
        }
    }
}
static void render(uint32_t *fb){
    int r,c,y,x,i;
    for(i=0;i<DISPLAY_W*DISPLAY_H;i++) fb[i]=DEFAULT_BG;   /* clear whole screen */
    for(r=0;r<ROWS;r++) for(c=0;c<COLS;c++) draw_cell(fb,r,c);
    /* cursor: solid block, inverted */
    int px=g_cc*CELL_W, py=g_cr*CELL_H;
    for(y=0;y<CELL_H;y++) for(x=0;x<CELL_W;x++){
        uint32_t *p=&fb[(py+y)*DISPLAY_W+px+x]; *p = 0xFFFFFFFFu ^ (*p & 0x00FFFFFF);
    }
}

/* ---- pty + shell ---- */
static int open_pty(char *slave_out,int sz){
    int n; char mp[32], sp[32];
    const char *hex="0123456789abcdef";
    for(n=0;n<16;n++){
        snprintf(mp,sizeof(mp),"/dev/ptyp%c",hex[n]);
        int fd=open(mp,O_RDWR|O_NONBLOCK);
        if(fd>=0){ snprintf(sp,sizeof(sp),"/dev/ttyp%c",hex[n]); snprintf(slave_out,sz,"%s",sp); return fd; }
    }
    return -1;
}
/* QNX fork() fails in a multithreaded process (the ILM library spawns threads),
 * so use posix_spawn with file actions to wire the pty slave to the shell. */
static pid_t spawn_shell(int master,const char *slave){
    int s=open(slave,O_RDWR);
    if(s<0){ printf("[TERM] open slave %s failed: %s\n",slave,strerror(errno)); return -1; }
    struct winsize ws; ws.ws_row=ROWS; ws.ws_col=COLS; ws.ws_xpixel=DISPLAY_W; ws.ws_ypixel=DISPLAY_H;
    ioctl(s,TIOCSWINSZ,&ws);

    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa,s,0);
    posix_spawn_file_actions_adddup2(&fa,s,1);
    posix_spawn_file_actions_adddup2(&fa,s,2);
    posix_spawn_file_actions_addclose(&fa,s);
    posix_spawn_file_actions_addclose(&fa,master);

    posix_spawnattr_t at; posix_spawnattr_init(&at);
#ifdef POSIX_SPAWN_SETSID
    posix_spawnattr_setflags(&at,POSIX_SPAWN_SETSID);
#endif
    /* PS1 is re-expanded by ksh at every prompt, so $PWD tracks cd. History persists on the SD card. */
    char *envp[]={"TERM=vt100","HOME=/fs/sda1/evo","PS1=evo:$PWD# ","HISTFILE=/fs/sda1/evo/.evo_term_history","HISTSIZE=500",
                  "PATH=/bin:/usr/bin:/proc/boot:/sbin:/usr/sbin:/var/run/screen_tests:/fs/sda1/evo",(char*)NULL};
    char *av1[]={"sh","-i",(char*)NULL};
    char *av2[]={"ksh","-i",(char*)NULL};
    pid_t pid=-1;
    int rc=posix_spawn(&pid,"/bin/sh",&fa,&at,av1,envp);
    if(rc!=0) rc=posix_spawn(&pid,"/bin/ksh",&fa,&at,av2,envp);
    posix_spawn_file_actions_destroy(&fa); posix_spawnattr_destroy(&at);
    close(s);
    if(rc!=0){ printf("[TERM] posix_spawn failed: %s\n",strerror(rc)); return -1; }
    return pid;
}

/* ---- keyboard ---- */
#define KBD_DEV "/dev/usbkbd0"
static char g_norm[128], g_shft[128];
static void init_keymap(void){
    memset(g_norm,0,sizeof(g_norm)); memset(g_shft,0,sizeof(g_shft));
    struct { int sc; char n,s; } k[] = {
        {0x02,'1','!'},{0x03,'2','@'},{0x04,'3','#'},{0x05,'4','$'},{0x06,'5','%'},
        {0x07,'6','^'},{0x08,'7','&'},{0x09,'8','*'},{0x0a,'9','('},{0x0b,'0',')'},
        {0x0c,'-','_'},{0x0d,'=','+'},
        {0x10,'q','Q'},{0x11,'w','W'},{0x12,'e','E'},{0x13,'r','R'},{0x14,'t','T'},
        {0x15,'y','Y'},{0x16,'u','U'},{0x17,'i','I'},{0x18,'o','O'},{0x19,'p','P'},
        {0x1a,'[','{'},{0x1b,']','}'},
        {0x1e,'a','A'},{0x1f,'s','S'},{0x20,'d','D'},{0x21,'f','F'},{0x22,'g','G'},
        {0x23,'h','H'},{0x24,'j','J'},{0x25,'k','K'},{0x26,'l','L'},{0x27,';',':'},
        {0x28,'\'','"'},{0x29,'`','~'},{0x2b,'\\','|'},
        {0x2c,'z','Z'},{0x2d,'x','X'},{0x2e,'c','C'},{0x2f,'v','V'},{0x30,'b','B'},
        {0x31,'n','N'},{0x32,'m','M'},{0x33,',','<'},{0x34,'.','>'},{0x35,'/','?'},
        {0x39,' ',' '},
        {0,0,0}
    };
    int i; for(i=0;k[i].sc;i++){ g_norm[k[i].sc]=k[i].n; g_shft[k[i].sc]=k[i].s; }
}
static void kbd_send(const char *s,int n){ if(g_master>=0) write(g_master,s,n); }

static void *kbd_thread(void *arg){
    (void)arg;
    unsigned char buf[32];
    int fd=-1;
    while(g_running){
        if(fd<0){ fd=open(KBD_DEV,O_RDONLY); if(fd<0){ sleep(1); continue; } }
        int n=read(fd,buf,sizeof(buf));
        if(n<=0){ if(n<0 && errno==EINTR) continue; close(fd); fd=-1; sleep(1); continue; }
        int i;
        for(i=0;i<n;i++){
            unsigned char sc=buf[i];
            if(sc==0xe0){ g_ext=1; continue; }
            int brk = sc & 0x80; unsigned char code = sc & 0x7f;
            if(g_ext){
                g_ext=0;
                if(!brk){
                    if(code==0x48) kbd_send("\x1b[A",3);
                    else if(code==0x50) kbd_send("\x1b[B",3);
                    else if(code==0x4b) kbd_send("\x1b[D",3);
                    else if(code==0x4d) kbd_send("\x1b[C",3);
                }
                continue;
            }
            if(code==0x2a||code==0x36){ g_shift = !brk; continue; }
            if(code==0x1d){ g_ctrl = !brk; continue; }
            if(code==0x3a){ if(!brk) g_caps = !g_caps; continue; }   /* Caps Lock toggles on press */
            if(code==0x45){ if(!brk) g_num = !g_num; continue; }     /* Num Lock toggles on press */
            if(brk) continue;                      /* ignore key-up for others */
            {   /* numeric keypad (set-1 codes 0x47-0x53): digits when Num Lock is on, else navigation */
                static const char kp_num[] = "789-456+1230.";
                static const char *kp_nav[] = {"\x1b[H","\x1b[A","\x1b[5~",0,"\x1b[D",0,"\x1b[C",0,"\x1b[F","\x1b[B","\x1b[6~","\x1b[2~","\x7f"};
                if(code>=0x47 && code<=0x53){
                    int k = code-0x47;
                    if(g_num || kp_num[k]=='-' || kp_num[k]=='+'){ kbd_send(&kp_num[k],1); }
                    else if(kp_nav[k]) kbd_send(kp_nav[k], (int)strlen(kp_nav[k]));
                    continue;
                }
                if(code==0x37){ kbd_send("*",1); continue; }   /* keypad * */
            }
            if(code==0x1c){ kbd_send("\r",1); continue; }     /* Enter */
            if(code==0x0e){ kbd_send("\x7f",1); continue; }   /* Backspace -> DEL */
            if(code==0x0f){ kbd_send("\t",1); continue; }     /* Tab */
            if(code==0x01){ kbd_send("\x1b",1); continue; }   /* Esc */
            char ch = g_shift ? g_shft[code] : g_norm[code];
            if(g_caps && ch){                        /* Caps Lock: letters only, inverted by Shift */
                if(ch>='a'&&ch<='z') ch = (char)(ch-'a'+'A');
                else if(ch>='A'&&ch<='Z') ch = (char)(ch-'A'+'a');
            }
            if(ch){
                if(g_ctrl){ char cc = ch & 0x1f; kbd_send(&cc,1); }  /* Ctrl-x */
                else kbd_send(&ch,1);
            }
        }
    }
    if(fd>=0) close(fd);
    return NULL;
}

static void on_sig(int s){ (void)s; g_running=0; }

int main(int argc,char**argv){
    (void)argc;(void)argv;
    signal(SIGINT,on_sig); signal(SIGTERM,on_sig); signal(SIGCHLD,SIG_IGN); signal(SIGPIPE,SIG_IGN);
    init_palette(); init_keymap();
    g_cur_fg=DEFAULT_FG; g_cur_bg=DEFAULT_BG;
    grid_clear();

    printf("[TERM] evo_terminal starting: %dx%d grid, %dx%d display\n",COLS,ROWS,DISPLAY_W,DISPLAY_H);
    fflush(stdout);

    evo_graphics_init();
    g_ctx = evo_context_create(DISPLAY_W,DISPLAY_H,0,0);
    if(!g_ctx){ printf("[TERM] evo_context_create failed\n"); return 1; }
    if(evo_context_show(g_ctx)!=0){ printf("[TERM] evo_context_show failed\n"); evo_context_destroy(g_ctx); return 1; }
    /* Hide the car HMI so the terminal is fullscreen (same as DOOM). */
    { evo_display_t *disp = evo_context_get_display(g_ctx);
      if(disp && evo_display_enable_presentation_mode(disp)==0) printf("[TERM] presentation mode on (HMI hidden)\n");
      else printf("[TERM] warning: presentation mode not enabled\n"); }
    printf("[TERM] display up\n"); fflush(stdout);

    char slave[32];
    g_master = open_pty(slave,sizeof(slave));
    if(g_master<0){ printf("[TERM] no pty available\n"); evo_context_destroy(g_ctx); return 1; }
    printf("[TERM] pty master open, slave=%s\n",slave); fflush(stdout);
    pid_t shell = spawn_shell(g_master,slave);
    if(shell<0){ printf("[TERM] fork failed\n"); evo_context_destroy(g_ctx); return 1; }
    printf("[TERM] shell pid=%d\n",(int)shell); fflush(stdout);

    pthread_t kt; pthread_create(&kt,NULL,kbd_thread,NULL);

    /* iDrive knob: hold the grab (HMI ignores the knob while we're up) and let the
     * BACK button quit us - so the terminal can be exited in the car with no keyboard.
     * Sets g_running=0 on BACK press (or a MENU hold); the main loop below then tears
     * down and restores the OEM HMI exactly as a keyboard exit would. */
    if (evo_ai_start(&g_running) == 0) printf("[TERM] iDrive BACK = exit (knob thread up)\n");
    else printf("[TERM] warning: knob thread failed; keyboard-only exit\n");
    fflush(stdout);

    /* banner */
    const char *banner="EVO Terminal  -  iDrive BACK = exit  -  type away\r\n";
    const char *p; for(p=banner;*p;p++) put_byte((unsigned char)*p);

    /* -demo: feed a few commands to the shell so rendering can be verified
     * without a keyboard. */
    int demo = 0, demo_fired = 0, tick = 0;
    if(argc>1 && strcmp(argv[1],"-demo")==0) demo = 1;

    while(g_running){
        if(demo && !demo_fired && ++tick>20){
            const char *cmds="echo EVO terminal online; uname -a; echo ---; ls -1 /\r";
            write(g_master,cmds,strlen(cmds)); demo_fired=1;
        }
        fd_set rf; FD_ZERO(&rf); FD_SET(g_master,&rf);
        struct timeval tv={0,33000};          /* ~30 Hz */
        int r=select(g_master+1,&rf,NULL,NULL,&tv);
        if(r>0 && FD_ISSET(g_master,&rf)){
            unsigned char buf[4096];
            int n=read(g_master,buf,sizeof(buf));
            if(n>0){ int i; for(i=0;i<n;i++) put_byte(buf[i]); }
            else if(n==0){ break; }             /* shell exited */
            else if(errno!=EAGAIN && errno!=EINTR) break;
        }
        uint32_t *fb=evo_context_get_framebuffer(g_ctx);
        if(fb){ render(fb); evo_context_present(g_ctx); }
    }

    g_running=0;
    printf("[TERM] shutting down\n"); fflush(stdout);
    if(g_master>=0) close(g_master);
    if(shell>0){ kill(shell,SIGTERM); }
    evo_context_destroy(g_ctx);
    return 0;
}
