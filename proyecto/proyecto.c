#include <windows.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <math.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")
#endif

#define MAX_N 800
#define DEF_N 60
#define MIN_R 16
#define MAX_R 26
#define FRAME_MS 16
#define TIME_SCALE 0.40f
#define RIGHT_ZONE 0.80f
#define QUIET_VX 35.0f
#define QUIET_VY 40.0f
#define ENABLE_TRAILS 1
#define TRAIL_LEN 10
#define ENABLE_SPARKS 1
#define MAX_PARTICLES 1800

typedef struct {
    float x,y;
    float vx,vy;
    int r;
    COLORREF color;
    HBRUSH brush;
    HBRUSH shadow;
    BOOL active;
    double spawnAt;
    float angle;
    float angVel;
    float squash;
    float phase;
    float liftCoeff;
    float jitterT;
#if ENABLE_TRAILS
    float trailX[TRAIL_LEN], trailY[TRAIL_LEN];
    int trailCount;
#endif
} Ball;

typedef struct {
    BOOL alive;
    float x,y,vx,vy,life,maxLife;
    int size;
    HBRUSH brush;
} Particle;
static Ball *balls=NULL;
static int N=DEF_N;
static Particle gParticles[MAX_PARTICLES];
static HWND hwnd;
static RECT client;
static int width=960,height=560,floorH=48;
static BOOL running=TRUE;
static HDC backDC=NULL;
static HBITMAP backBMP=NULL, backOld=NULL;
static const float G=2000.0f;
static const float REST=0.80f;
static const float AIR=0.018f;
static const float GROUND_FRICTION=0.984f;
static const float WALL_DAMP=0.88f;
static double gTime=0.0;
static HBRUSH gPortalBrush=NULL;

static COLORREF Darken(COLORREF c,int pct){int r=GetRValue(c),g=GetGValue(c),b=GetBValue(c);r=r*(100-pct)/100;g=g*(100-pct)/100;b=b*(100-pct)/100;return RGB(r,g,b);}
static float GroundY(){return (float)(height-floorH);}
static int clampi(int v,int a,int b){return v<a?a:(v>b?b:v);}
static float clampf(float v,float a,float b){return v<a?a:(v>b?b:v);}

static void FreeBalls(){
    if(!balls) return;
    for(int i=0;i<N;i++){ if(balls[i].brush) DeleteObject(balls[i].brush); if(balls[i].shadow) DeleteObject(balls[i].shadow); }
    free(balls); balls=NULL;
    if(gPortalBrush){ DeleteObject(gPortalBrush); gPortalBrush=NULL; }
}

static void MakeBrushes(Ball* b){
    if(b->brush) DeleteObject(b->brush);
    if(b->shadow) DeleteObject(b->shadow);
    b->brush=CreateSolidBrush(b->color);
    b->shadow=CreateSolidBrush(Darken(b->color,75));
}

static double NextInterval(){ return 0.12 + (rand()%10)*0.012; }

static void ParticlesClear(){ for(int i=0;i<MAX_PARTICLES;i++) gParticles[i].alive=FALSE; }

static void SpawnSparks(float x,float y,int count,HBRUSH brush,float baseVx){
#if ENABLE_SPARKS
    if(count<=0) return;
    for(int k=0;k<count;k++){
        int idx=-1; for(int i=0;i<MAX_PARTICLES;i++){ if(!gParticles[i].alive){ idx=i; break; } }
        if(idx<0) break;
        Particle* p=&gParticles[idx];
        p->alive=TRUE; p->x=x; p->y=y;
        float a=((float)(rand()%360))*(3.14159265f/180.f);
        float sp=140.0f+(float)(rand()%160);
        float fwd=baseVx*0.25f;
        p->vx=cosf(a)*sp+fwd;
        p->vy=-fabsf(sinf(a))*sp*0.95f - 60.f;
        p->maxLife=0.28f+0.30f*((float)(rand()%100)/100.f);
        p->life=p->maxLife;
        p->size=2+rand()%3;
        p->brush=brush?brush:(HBRUSH)GetStockObject(WHITE_BRUSH);
    }
#else
    (void)x;(void)y;(void)count;(void)brush;(void)baseVx;
#endif
}

static void ParticlesUpdate(double dt){
#if ENABLE_SPARKS
    float gy=GroundY();
    for(int i=0;i<MAX_PARTICLES;i++){
        Particle* p=&gParticles[i];
        if(!p->alive) continue;
        p->life-=(float)dt; if(p->life<=0.f){ p->alive=FALSE; continue; }
        p->vy+=G*0.35f*(float)dt;
        p->vx*=(1.0f-0.035f*(float)dt);
        p->x+=p->vx*(float)dt;
        p->y+=p->vy*(float)dt;
        if(p->y>gy){ p->y=gy; p->vy=-p->vy*0.30f; p->vx*=0.84f; if(fabsf(p->vy)<16.f) p->vy=0; }
    }
#else
    (void)dt;
#endif
}

static void ParticlesDraw(){
#if ENABLE_SPARKS
    HPEN oldPen=(HPEN)SelectObject(backDC,GetStockObject(NULL_PEN));
    HBRUSH oldBrush=NULL;
    for(int i=0;i<MAX_PARTICLES;i++){
        Particle* p=&gParticles[i];
        if(!p->alive) continue;
        int s=p->size;
        float t=p->life/(p->maxLife+1e-6f);
        s=(int)(s*clampf(0.5f+t,0.5f,1.0f)); if(s<=0) s=1;
        int x=(int)p->x - s/2, y=(int)p->y - s/2;
        oldBrush=(HBRUSH)SelectObject(backDC,p->brush);
        Ellipse(backDC,x,y,x+s,y+s);
    }
    if(oldBrush) SelectObject(backDC,oldBrush);
    SelectObject(backDC,oldPen);
#endif
}

static void ActivateBall(Ball* b){
    int r=MIN_R+rand()%(MAX_R-MIN_R+1);
    b->r=r;
    b->x=(float)(-2*r - (rand()%60));
    float startY=GroundY()-(float)(height*0.48f + rand()%(height/7));
    if(startY<0) startY=0;
    b->y=startY - r;
    b->vx=350.0f + (float)(rand()%210);
    b->vy=-(640.0f + (float)(rand()%360));
    b->color=RGB(rand()%200+40,rand()%200+40,rand()%200+40);
    MakeBrushes(b);
    b->active=TRUE;
    b->angle=(float)((rand()%360)*3.14159265/180.0);
    b->angVel=0.0f;
    b->squash=1.0f;
    b->phase=(float)((rand()%628)/100.0f);
    b->liftCoeff=0.00055f + 0.00035f*((float)(rand()%100)/100.f);
    b->jitterT=(float)(rand()%1000)/1000.f;
#if ENABLE_TRAILS
    b->trailCount=0; for(int j=0;j<TRAIL_LEN;j++){ b->trailX[j]=b->x+b->r; b->trailY[j]=b->y+b->r; }
#endif
}

static void ScheduleBall(Ball* b){
    if(gPortalBrush){
        float cy=b->y+b->r;
        BOOL onFloor=fabsf(cy-GroundY())<2.0f;
        if(onFloor) SpawnSparks(b->x+b->r,GroundY(),18,gPortalBrush,b->vx);
    }
    b->active=FALSE;
    b->spawnAt=gTime+NextInterval();
}

static void InitBalls(){
    FreeBalls();
    balls=(Ball*)calloc(N,sizeof(Ball));
    srand((unsigned)time(NULL));
    if(!gPortalBrush) gPortalBrush=CreateSolidBrush(RGB(120,160,255));
    double t=0.0;
    for(int i=0;i<N;i++){
        balls[i].active=FALSE;
        balls[i].spawnAt=t;
        balls[i].squash=1.0f;
        t+=0.09 + (rand()%10)*0.008;
    }
    ParticlesClear();
}