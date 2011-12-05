#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/stat.h>

#define MIN 100
#define MAX 928
#define XQC 5
#define YQC 6

#define XQ(q) (((MAX-MIN)/XQC)*q)
#define YQ(q) (((MAX-MIN)/YQC)*q)

#define BYTESCMD 16
#define BYTESKEY 5

#define SCREENX(touchX) (touchX)
#define SCREENY(touchY) (YQC-touchY-1)
#define WIDTHX(xres) (xres/XQC)
#define WIDTHY(yres) (yres/YQC)

#define INCFAC 20
#define DELAY 250000

#define COLOR_PRIMARY 0
#define COLOR_SHIFT 1
#define COLOR_META 2
#define COLOR_NUM 3
#define COLOR_SNUM 4
#define COLOR_GER 5
#define COLOR_FK 6
#define COLOR_HOLD 7
#define COLOR_TOGGLE 8

struct TS {
    int fd;
};

struct TTY {
    int fd;
};

struct Rect {
    int x;
    int y;
    int xw;
    int yw;
    char* ptr;
    char* color;
    int active;
};

struct FB {
    int fd;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    int size;
    int bpp;
    char* ptr;
    struct Rect rect;
};

struct KB {
    int shift;
    int num;
    int meta;
    int hold;
    int ger;
    int fk;
    int ctrl;
    int alt;
    int super;
};

struct Key {
    char key[BYTESKEY];
    int color;
};

struct Global {
    struct TS* ts;
    struct TTY* tty;
    struct FB* fb;
} global;

const char map_primary[6][5] = { 
    {'\b','m','k','l','p'}, {'v','n','j','i','o'},
    {'c','b','h','u','z'}, {'x','s','g','r','t'},
    {0,'y','f','e','w'}, {0,0,'d','a','q'} };
    
const char map_shift[6][5] = {
    {0,'M','K','L','P'}, {'V','N','J','I','O'},
    {'C','B','H','U','Z'}, {'X','S','G','R','T'},
    {0,'Y','F','E','W'}, {0,0,'D','A','Q'} };
    
const char map_num[6][5] = {
    {'_','-','+','/','*'}, {'.',':','\\','0','5'},
    {',',';','?','9','4'}, {'<','>','|','8','3'},
    {0,'#','\'','7','2'}, {'~',0,'@','6','1'} };
    
const char map_snum[6][5][2] = {
    {{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0},{'=',0},{'%',0}},
    {{0,0},{0,0},{0,0},{')',0},{'$',0}},
    {{0,0},{0,0},{0,0},{'(',0},{-62,-89},},
    {{0,0},{0,0},{0,0},{'/',0},{'"',0},},
    {{0,0},{0,0},{0,0},{'&',0},{'!',0},} };
    
const char map_meta[6][5][4] = {
    {{'\n',0,0,0},{27,'[','4','~'},{27,'[','C',0},{27,'[','5','~'},{27,'[','6','~'}},
    {{27,'[','3','~'},{27,'[','2','~'},{27,'[','B',0},{27,'[','A',0},{0,0,0,0}},
    {{' ',0,0,0},{27,'[','1','~'},{27,'[','D',0},{0,0,0,0},{0,0,0,0}},
    {{' ',0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}},
    {{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}},
    {{0,0,0,0},{0,0,0,0},{0,0,0,0},{'\t',0,0,0},{27,0,0,0}} };
    
const char map_ger[6][5][3] = {
    {{0,0,0},{0,0,0},{'`',0,0},{0,0,0},{0,0,0}},
    {{0,0,0},{0,0,0},{-62,-76,0},{0,0,0},{0,0,0}},
    {{0,0,0},{0,0,0},{-62,-80,0},{0,0,0},{-61,-97,0}},
    {{0,0,0},{0,0,0},{'^',0,0},{-61,-100,0},{-61,-68,0}},
    {{0,0,0},{0,0,0},{-62,-75,0},{-61,-106,0},{-61,-74,0}},
    {{0,0,0},{0,0,0},{-30,-126,-84},{-61,-124,0},{-61,-92,0}} };
    
const char map_fk[6][5][4] = {
    {{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}},
    {{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}},
    {{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}},
    {{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}},
    {{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}},
    {{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}} };

const char colors[9][4] = {
    {0x00,0xFF,0x00,0x00},   // PRIMARY
    {0x00,0x88,0x00,0x00},   // SHIFT
    {0x00,0x88,0xFF,0x00},   // META
    {0xFF,0x00,0x00,0x00},   // NUM
    {0x88,0x00,0x00,0x00},   // SNUM
    {0xFF,0xFF,0x00,0x00},   // GER
    {0xFF,0x88,0x00,0x00},   // FK
    {0x00,0x00,0xFF,0x00},   // HOLD
    {0x00,0xFF,0xFF,0x00} }; // TOGGLE

char* color32to16 (int src, char* dst) {
    dst[0] = (colors[src][0] & ~7)>>0 | colors[src][2]>>5;
    dst[1] = (colors[src][2] & ~7)<<3 | (colors[src][1] & ~7)>>2;
    return dst;
}

int getXQ (int x) {
    int i;
    for(i=1;i<XQC;i++) {
        if(x<XQ(i)) return i-1;
    }
    return XQC-1;
}

int getYQ (int y) {
    int i;
    for(i=1;i<YQC;i++) {
        if(y<YQ(i)) return i-1;
    }
    return YQC-1;
}

struct Key parse (int xq, int yq, int mode, struct KB* kb) {
    struct Key key;
    memset(key.key,0,BYTESKEY);
    int reset = 1, metareset = 1;
    int pos = yq*XQC+xq;
    
    if(kb->meta) {
        if(kb->ger) {
            assert(printf("German\n"));
            if(mode) {
                switch(pos) {
                    case 21: assert(printf("Hold %s\n",(!kb->hold?"on":"off")));
                             kb->hold = !kb->hold; break;
                    default: key.key[0] = map_ger[yq][xq][0];
                             key.key[1] = map_ger[yq][xq][1];
                             key.key[2] = map_ger[yq][xq][2];
                }
            }
            switch(pos) {
                case 21: key.color = COLOR_HOLD; break;
                default: key.color = COLOR_GER;
            }
        } else if(kb->fk) {
            assert(printf("Functionkeys\n"));
            if(mode) {
                key.key[0] = map_fk[yq][xq][0];
                key.key[1] = map_fk[yq][xq][1];
                key.key[2] = map_fk[yq][xq][2];
                key.key[3] = map_fk[yq][xq][3];
            }
            key.color = COLOR_FK;
        } else {
            assert(printf("Meta\n"));
            if(mode) {
                switch(pos) {
                    case 20: assert(printf("Alt %s\n",(!kb->alt?"on":"off")));
                             kb->alt = !kb->alt; metareset = 0; break;
                    case 21: assert(printf("to German\n"));
                             kb->ger = 1; kb->hold = 0; reset = 0; break;
                    case 22: assert(printf("to Functionkeys\n"));
                             kb->fk = 1; kb->hold = 0; reset = 0; break;
                    case 25: assert(printf("Hold %s\n",(!kb->hold?"on":"off")));
                             kb->hold = !kb->hold; break;
                    case 26: assert(printf("Ctrl %s\n",(!kb->ctrl?"on":"off")));
                             kb->ctrl = !kb->ctrl; metareset = 0; break;
                    case 27: assert(printf("Super %s\n",(!kb->super?"on":"off")));
                             kb->super = !kb->super; metareset = 0; break;
                    default: key.key[0] = map_meta[yq][xq][0];
                             key.key[1] = map_meta[yq][xq][1];
                             key.key[2] = map_meta[yq][xq][2];
                             key.key[3] = map_meta[yq][xq][3];
                }
            }
            switch(pos) {
                case 20: key.color = COLOR_TOGGLE; break;
                case 21: key.color = COLOR_GER; break;
                case 22: key.color = COLOR_FK; break;
                case 25: key.color = COLOR_HOLD; break;
                case 26: key.color = COLOR_TOGGLE; break;
                case 27: key.color = COLOR_TOGGLE; break;
                default: key.color = COLOR_META;
            }
        }
    } else if(kb->num) {
        if(kb->shift) {
            assert(printf("Numbers+Shift\n"));
            if(mode) {
                switch(pos) {
                    case 26: assert(printf("Hold %s\n",(!kb->hold?"on":"off")));
                             kb->hold = !kb->hold; break;
                    default: key.key[0] = map_snum[yq][xq][0];
                             key.key[1] = map_snum[yq][xq][1];
                }
            }
            switch(pos) {
                case 26: key.color = COLOR_HOLD; break;
                default: key.color = COLOR_SNUM;
            }
        } else {
            assert(printf("Numbers\n"));
            if(mode) {
                switch(pos) {
                    case 20: assert(printf("Hold %s\n",(!kb->hold?"on":"off")));
                             kb->hold = !kb->hold; break;
                    case 26: assert(printf("to Numbers+Shift\n"));
                             kb->shift = 1; kb->hold = 0; reset = 0; break;
                    default: key.key[0] = map_num[yq][xq];
                }
            }
            switch(yq*XQC+xq) {
                case 20: key.color = COLOR_HOLD; break;
                case 26: key.color = COLOR_SNUM; break;
                default: key.color = COLOR_NUM;
            }
        }
    } else if(kb->shift) {
        assert(printf("Shift\n"));
        if(mode) {
            switch(pos) {
                case 26: assert(printf("Hold %s\n",(!kb->hold?"on":"off")));
                         kb->hold = !kb->hold; break;
                default: key.key[0] = map_shift[yq][xq];
                         if(kb->ctrl) key.key[0] -= 64;
            }
        }
        switch(pos) {
            case 26: key.color = COLOR_HOLD; break;
            default: key.color = COLOR_SHIFT;
        }
    } else {
        assert(printf("Primary\n"));
        if(mode) {
            switch(pos) {
                case 20: assert(printf("to Numbers\n"));
                         kb->num = 1; reset = 0; break;
                case 25: assert(printf("to Meta\n"));
                         kb->meta = 1; reset = 0; break;
                case 26: assert(printf("to Shift\n"));
                         kb->shift = 1; reset = 0; break;
                default: key.key[0] = map_primary[yq][xq];
                         if(kb->ctrl) key.key[0] -= 96;
            }
        }
        switch(pos) {
            case 20: key.color = COLOR_NUM; break;
            case 25: key.color = COLOR_META; break;
            case 26: key.color = COLOR_SHIFT; break;
            default: key.color = COLOR_PRIMARY;
        }
    }
    
    if(mode) {
        if(reset && !kb->hold) {
            assert(printf("clear stats\n"));
            kb->meta = 0; kb->num = 0; kb->shift = 0;
            kb->ger = 0; kb->fk = 0;
        }
        if(metareset) {
            kb->ctrl = 0; kb->alt = 0; kb->super = 0;
        }
    }
    
    assert(printf("char: %s\n",key.key));
    
    return key;
}

int insert (char* bytes, struct TTY* tty) {
    int x;
    for(x=0;x<strlen(bytes);x++) {
        if(ioctl(tty->fd, TIOCSTI, &(bytes[x]))<0) {
            perror("Failed to insert byte into input queue");
            return 1;
        }
    }
    return 0;
}

void calcRect (int xq, int yq, int xw, int yw, int xinc, int yinc, int color, struct FB* fb) {
    if(!xq) {
        assert(printf("x down, "));
        fb->rect.x = xw*SCREENX(xq);
        fb->rect.xw = xw+xinc;
    } else if(xq==XQC-1) {
        assert(printf("x up, "));
        fb->rect.x = xw*SCREENX(xq)-xinc;
        fb->rect.xw = xw+xinc;
    } else {
        assert(printf("x middle, "));
        fb->rect.x = xw*SCREENX(xq)-xinc;
        fb->rect.xw = xw+2*xinc;
    }
    if(!yq) {
        assert(printf("y right\n"));
        fb->rect.y = yw*SCREENY(yq)-yinc;
        fb->rect.yw = yw+yinc;
    } else if(yq==YQC-1) {
        assert(printf("y left\n"));
        fb->rect.y = yw*SCREENY(yq);
        fb->rect.yw = yw+yinc;
    } else {
        assert(printf("y middle\n"));
        fb->rect.y = yw*SCREENY(yq)-yinc;
        fb->rect.yw = yw+2*yinc;
    }
    
    assert(printf("x: %i y: %i xw: %i yw: %i\n",fb->rect.x,fb->rect.y,fb->rect.xw,fb->rect.yw));
    
    if(fb->bpp==2) color32to16(color,fb->rect.color);
    else fb->rect.color = (char*)colors[color];
}

void printRect (struct FB* fb) {
    int i,j,k,pos;
    char* cfbp;
    int offset = fb->rect.x*fb->bpp + fb->rect.y*fb->finfo.line_length;
    
    for(i=0;i<fb->rect.yw;i++) {
        for(j=0;j<fb->rect.xw;j++) {
            pos = i*fb->rect.xw*fb->bpp+j*fb->bpp;
            cfbp = fb->ptr+offset+j*fb->bpp;
            for(k=0;k<fb->bpp;k++) {
                fb->rect.ptr[pos+k] = cfbp[k];
                cfbp[k] = fb->rect.color[k];
            }
        }
        offset += fb->finfo.line_length;
    }
    fb->rect.active = 1;
}

void restoreRect (struct FB* fb) {
    if(fb->rect.active) {
        int i,j,k,pos;
        char* cfbp;
        int offset = fb->rect.x*fb->bpp + fb->rect.y*fb->finfo.line_length;
        
        for(i=0;i<fb->rect.yw;i++) {
            for(j=0;j<fb->rect.xw;j++) {
                pos = i*fb->rect.xw*fb->bpp+j*fb->bpp;
                cfbp = fb->ptr+offset+j*fb->bpp;
                for(k=0;k<fb->bpp;k++) {
                    cfbp[k] = fb->rect.ptr[pos+k];
                }
            }
            offset += fb->finfo.line_length;
        }
        fb->rect.active = 0;
    }
}

void clean (int ret) {
    free(global.fb->rect.ptr);
    if(global.fb->bpp==2) free(global.fb->rect.color);
    munmap(global.fb->ptr,global.fb->size);
    close(global.ts->fd);
    close(global.tty->fd);
    close(global.fb->fd);
    exit(ret);
}

void sigclean () {
    clean(0);
}

int main(int argc, char* argv[]) {
    
    if(argc != 4) {
        printf("Usage: %s screen tty framebuffer\n",argv[0]);
        return 0;
    }
    
#ifdef NDEBUG
    
    int tmp = fork();
    if(tmp<0) {
        perror("Failed to fork process");
        return 1;
    }
    if(tmp>0) {
        return 0;
    }
    
    setsid();
    
    close(0); close(1); close(2); close(3);
    tmp = open("/dev/null",O_RDWR); dup(tmp); dup(tmp);
    
    mkdir("/etc/tkb",0755);
    chdir("/etc/tkb");
    
    if((tmp = open("lock",O_RDWR|O_CREAT,755))<0) {
        perror("Failed to open lockfile");
        return 2;
    }
    if(lockf(tmp,F_TLOCK,0)<0) {
        perror("Deamon already running");
        return 3;
    }
    
#endif
    
    struct TS ts;
    global.ts = &ts;
    
    if((ts.fd = open(argv[1], O_RDONLY))<0) {
        char buf[100] = "Failed to open ";
        strncat(buf,argv[1],100-strlen(buf)-1);
        perror(buf);
        return 4;
    }
    
    struct TTY tty;
    global.tty = &tty;
    
    if((tty.fd = open(argv[2], O_RDONLY))<0) {
        char buf[100] = "Failed to open ";
        strncat(buf,argv[2],100-strlen(buf)-1);
        perror(buf);
        close(ts.fd);
        return 5;
    }
    
    struct FB fb;
    global.fb = &fb;
    
    if((fb.fd = open(argv[3], O_RDWR))<0) {
        char buf[100] = "Failed to open ";
        strncat(buf,argv[3],100-strlen(buf)-1);
        perror(buf);
        close(ts.fd);
        close(tty.fd);
        return 6;
    }
    
    if(ioctl(fb.fd, FBIOGET_VSCREENINFO, &(fb.vinfo))) {
        perror("Failed to get variable screeninfo");
        close(ts.fd);
        close(tty.fd);
        close(fb.fd);
        return 7;
    }
    
    if(ioctl(fb.fd, FBIOGET_FSCREENINFO, &(fb.finfo))) {
        perror("Failed to get fixed screeninfo");
        close(ts.fd);
        close(tty.fd);
        close(fb.fd);
        return 8;
    }
    
    fb.bpp = fb.vinfo.bits_per_pixel/8;
    fb.size = fb.vinfo.xres*fb.vinfo.yres*fb.bpp;
    
    assert(printf("framebuffer size %i x %i, ",fb.vinfo.xres,fb.vinfo.yres));
    assert(printf("bytes per pixel %i, ",fb.bpp));
    assert(printf("line length %i\n",fb.finfo.line_length));
    
    if((fb.ptr = (char*)mmap(0, fb.size, PROT_READ|PROT_WRITE, MAP_SHARED, fb.fd, 0))==MAP_FAILED) {
        perror("Failed to create new mapping for framebuffer");
        close(ts.fd);
        close(tty.fd);
        close(fb.fd);
        return 9;
    }
    
    fb.ptr  += fb.vinfo.xoffset*(fb.vinfo.bits_per_pixel/8)
            + fb.vinfo.yoffset*fb.finfo.line_length;
         
    struct pollfd pfds[1];
    pfds[0].fd = ts.fd;
    pfds[0].events = POLLIN;
    
    unsigned char buf[16];
    struct Key key;
    struct KB kb;
    kb.shift = 0; kb.meta = 0; kb.ger = 0; kb.fk = 0; kb.num = 0;
    kb.hold = 0; kb.ctrl = 0; kb.alt = 0; kb.super = 0;
    
    int xq = -1, yq = -1, dxq, dyq, pressed = 0;
    int xw = WIDTHX(fb.vinfo.xres);
    int yw = WIDTHY(fb.vinfo.yres);
    int xinc = xw*INCFAC/100;
    int yinc = yw*INCFAC/100;
    fb.rect.ptr = (char*)malloc((xw+2*xinc)*(yw+2*yinc)*fb.bpp);
    if(fb.bpp==2) fb.rect.color = (char*)malloc(fb.bpp);
    fb.rect.active = 0;
    
    struct itimerval timerval;
    timerval.it_interval.tv_sec = 0;
    timerval.it_interval.tv_usec = 0;
    timerval.it_value.tv_sec = 0;
    signal(SIGALRM,SIG_IGN);
    
    signal(SIGINT,sigclean);
    
    while(1) {
        
        poll(pfds,1,-1);
        
        if(pfds[0].revents & POLLIN) {
            
            read(ts.fd,buf,BYTESCMD);
            
            if(buf[ 8] == 0x01 && buf[ 9] == 0x00
            && buf[10] == 0x4A && buf[11] == 0x01) {
                assert(printf("Button "));
                switch(buf[12]) {
                    case 0x00:
                        if(pressed) {
                            assert(printf("released at %i,%i\n",xq,yq));
                            pressed = 0;
                            restoreRect(&fb);
                            key = parse(xq,yq,1,&kb);
                            if(insert(key.key,&tty)) clean(10);
                        } break;
                    case 0x01:
                        if(!pressed && xq>=0 && yq>=0) {
                            assert(printf("pressed at %i,%i\n",xq,yq));
                            pressed = 1;
                            key = parse(xq,yq,0,&kb);
                            calcRect(xq,yq,xw,yw,xinc,yinc,key.color,&fb);
                            printRect(&fb);
                        } break;
                    default: assert(printf("unrecognized at %i,%i\n",xq,yq));
                }
            } else if(buf[ 8] == 0x03 && buf[ 9] == 0x00
                   && buf[10] == 0x00 && buf[11] == 0x00) {
                       
                dyq = getYQ(buf[13]*256+buf[12]-MIN);
                
            } else if(buf[ 8] == 0x03 && buf[ 9] == 0x00
                   && buf[10] == 0x01 && buf[11] == 0x00) {
                       
                dxq = getXQ(buf[13]*256+buf[12]-MIN);
                
                if(getitimer(ITIMER_REAL,&timerval)<0) {
                    perror("Failed to read timer");
                    clean(11);
                }
                if(!timerval.it_value.tv_usec && (dxq!=xq || dyq!=yq)) {
                    xq = dxq;
                    yq = dyq;
                    if(pressed) {
                        restoreRect(&fb);
                        key = parse(xq,yq,0,&kb);
                        calcRect(xq,yq,xw,yw,xinc,yinc,key.color,&fb);
                        printRect(&fb);
                    }
                    timerval.it_value.tv_usec = DELAY;
                    if(setitimer(ITIMER_REAL,&timerval,0)<0) {
                        perror("Failed to set timer");
                        clean(12);
                    }
                }
            }
            
        } else if(pfds[0].revents & POLLHUP) {
            break;
        }
    }
    
    clean(0);

    return 0;
}

