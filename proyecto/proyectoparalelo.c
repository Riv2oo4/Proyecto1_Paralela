#include <windows.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#ifdef _OPENMP
#include <omp.h>
#endif

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

/* Utilidades básicas */
static COLORREF Darken(COLORREF c,int pct){int r=GetRValue(c),g=GetGValue(c),b=GetBValue(c);r=r*(100-pct)/100;g=g*(100-pct)/100;b=b*(100-pct)/100;return RGB(r,g,b);}
static float GroundY(){return (float)(height-floorH);}
static int clampi(int v,int a,int b){return v<a?a:(v>b?b:v);}
static float clampf(float v,float a,float b){return v<a?a:(v>b?b:v);}

/* RNG seguro en paralelo (usa región crítica) */
static int rand_inclusive_safe(int lo, int hi){
    int v;
    #ifdef _OPENMP
    #pragma omp critical(rng)
    #endif
    { v = lo + (rand() % (hi - lo + 1)); }
    return v;
}

/* Intervalo de reaparición seguro en paralelo */
static double NextIntervalSafe(){
    double v;
    #ifdef _OPENMP
    #pragma omp critical(rng)
    #endif
    { v = 0.12 + (rand()%10)*0.012; }
    return v;
}

/* Gestión de memoria y pinceles */
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

/* Emisión de partículas; protege acceso al pool con región crítica */
static void SpawnSparks(float x,float y,int count,HBRUSH brush,float baseVx){
#if ENABLE_SPARKS
    if(count<=0) return;
    for(int k=0;k<count;k++){
        int idx=-1;
        #ifdef _OPENMP
        #pragma omp critical(sparks)
        #endif
        {
            for(int i=0;i<MAX_PARTICLES;i++){ if(!gParticles[i].alive){ idx=i; break; } }
            if(idx>=0){
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
        }
        if(idx<0) break;
    }
#else
    (void)x;(void)y;(void)count;(void)brush;(void)baseVx;
#endif
}
