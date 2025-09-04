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