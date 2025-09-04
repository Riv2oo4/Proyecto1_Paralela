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

