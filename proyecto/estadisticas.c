
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef _OPENMP
#include <omp.h>
#endif

/* Parámetros del modelo físico (alineados con la app gráfica) */
#define MAX_N            100000
#define DEF_N            60
#define MIN_R            16
#define MAX_R            26
#define TIME_SCALE       0.40f
#define RIGHT_ZONE       0.80f
#define QUIET_VX         35.0f
#define QUIET_VY         40.0f

static const float G               = 2000.0f;
static const float REST            = 0.80f;
static const float AIR             = 0.018f;
static const float GROUND_FRICTION = 0.984f;
static const float WALL_DAMP       = 0.88f;

/* RNG por bola (xorshift32) */
static inline uint32_t xrshift32(uint32_t *s){
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x; return x;
}
static inline float frand01(uint32_t *s){ return (xrshift32(s) >> 8) * (1.0f / 16777216.0f); }
static inline int irand_range(uint32_t *s, int a, int b){
    float u = frand01(s); int span = (b - a + 1);
    int v = a + (int)(u * span); if (v > b) v = b; return v;
}
static inline float frand_range(uint32_t *s, float a, float b){ return a + (b - a) * frand01(s); }

/* Estado de cada bola y del mundo */
typedef struct {
    float x, y, vx, vy;
    int   r, active;
    double spawnAt;
    float angle, angVel, squash;
    float phase, liftCoeff, jitterT;
    uint32_t rng;
} Ball;

typedef struct {
    Ball* balls;
    int   N, width, height, floorH;
    double gTime;
} World;

static inline float GroundY(World* w){ return (float)(w->height - w->floorH); }
static inline double NextIntervalRNG(uint32_t *rng){ return 0.12 + 0.12 * frand01(rng); }

/* Inicializa una bola activa con valores aleatorios reproducibles */
static void ActivateBall(World* w, Ball* b){
    b->rng ^= xrshift32(&b->rng);
    int r = irand_range(&b->rng, MIN_R, MAX_R); b->r = r;
    b->x = -2.0f*r - frand_range(&b->rng, 0.0f, 60.0f);
    float startY = GroundY(w) - (w->height*0.48f + frand_range(&b->rng, 0.0f, (float)(w->height/7)));
    if(startY < 0) startY = 0; b->y = startY - r;
    b->vx = frand_range(&b->rng, 350.0f, 560.0f);
    b->vy = -frand_range(&b->rng, 640.0f, 1000.0f);
    b->active = 1; b->angle = frand_range(&b->rng, 0.0f, 6.2831853f);
    b->angVel = 0.0f; b->squash = 1.0f;
    b->phase = frand_range(&b->rng, 0.0f, 6.2831853f);
    b->liftCoeff = frand_range(&b->rng, 0.00055f, 0.00090f);
    b->jitterT = frand01(&b->rng);
}

/* Reserva e inicializa el mundo (semilla controlada) */
static void InitWorld(World* w, int N, int width, int height, int floorH, uint32_t seed){
    w->N = N; w->width = width; w->height = height; w->floorH = floorH; w->gTime = 0.0;
    w->balls = (Ball*)calloc(N, sizeof(Ball));
    uint32_t base = seed ? seed : (uint32_t)time(NULL);
    double t = 0.0;
    for(int i=0;i<N;i++){
        w->balls[i].rng = base ^ (0x9E3779B9u * (uint32_t)(i+1));
        w->balls[i].active = 0;
        w->balls[i].spawnAt = t;
        w->balls[i].squash  = 1.0f;
        t += 0.09 + 0.008 * (double)(i%10);
    }
}
static void FreeWorld(World* w){ free(w->balls); w->balls = NULL; }

/* Actualiza física; puede paralelizar la parte por-bola con OpenMP */
static void UpdatePhysics(World* w, double dt, int use_omp){
    dt *= TIME_SCALE;
    float gy = GroundY(w);
    Ball* balls = w->balls; int N = w->N;

    for(int i=0;i<N;i++)
        if(!balls[i].active && w->gTime >= balls[i].spawnAt) ActivateBall(w, &balls[i]);

    #ifdef _OPENMP
    #pragma omp parallel for if(use_omp) schedule(static)
    #endif
    for(int i=0;i<N;i++){
        Ball* b = &balls[i];
        if(!b->active) continue;

        int r = b->r;
        float prevVy = b->vy;

        float wind = 70.0f*sinf((float)(1.10*w->gTime + b->phase)) + 35.0f*sinf((float)(0.63*w->gTime + i*0.19f));
        b->vx += wind*(float)dt;

        float lift = b->liftCoeff * b->angVel * b->vx;
        b->vy += (G + lift)*(float)dt;
        b->vx *= (1.0f - AIR*(float)dt);

        b->x += b->vx*(float)dt;
        b->y += b->vy*(float)dt;

        float cy = b->y + r;
        if (cy + r > gy){
            float impact = fabsf(prevVy);
            b->y  = gy - r;
            b->vy = -b->vy * REST;
            b->vx *= GROUND_FRICTION;
            if (fabsf(b->vy) < 60.f) b->vy = 0.f;

            float squashAmt = fminf(fmaxf(1.0f + impact/850.0f, 1.0f), 1.95f);
            b->squash = squashAmt;
            b->angVel += (b->vx/(float)(r))*0.35f;

            if (fabsf(b->vx)>420.f && fabsf(b->vy)<30.f && (frand01(&b->rng) < (1.0f/4.0f)))
                b->vy -= frand_range(&b->rng, 420.f, 600.f);
        }

        if (b->y < 0) { b->y = 0; b->vy = -b->vy*WALL_DAMP; }
        if (b->x < -2*r) { b->x = -2*r; b->vx = fabsf(b->vx)*0.95f; }
        if (b->x + 2*r > w->width) {
            b->x  = w->width - 2*r;
            b->vx = -fabsf(b->vx)*0.75f;
            b->angVel *= 0.85f;
        }

        b->squash += (1.0f - b->squash)*(float)(9.0*dt);
        if (fabsf(b->squash - 1.0f) < 0.01f) b->squash = 1.0f;
        b->angVel *= (1.0f - 0.26f*(float)dt);
        b->angle  += b->angVel*(float)dt;

        b->jitterT += (float)dt;
        if (b->jitterT > 0.08f){
            b->jitterT = 0.f;
            b->vx += frand_range(&b->rng, -60.f, 60.f);
            if (frand01(&b->rng) < (1.0f/6.0f))
                b->angVel += frand_range(&b->rng, -0.9f, 0.9f);
        }

        int onFloor   = fabsf((b->y+r) - gy) < 1.0f;
        int nearRight = (b->x + 2*r) > (RIGHT_ZONE * w->width);
        int quiet     = (fabsf(b->vx) < QUIET_VX && fabsf(b->vy) < QUIET_VY);
        if (onFloor && nearRight && quiet) {
            b->active = 0; b->spawnAt = w->gTime + NextIntervalRNG(&b->rng);
        } else if (b->x - 2*r > w->width + 20) {
            b->active = 0; b->spawnAt = w->gTime + NextIntervalRNG(&b->rng);
        }
    }
}

/* Argumentos de línea de comandos */
static void parse_args(int argc, char** argv, int* outN, int* outFrames, uint32_t* outSeed,
                       int* outW, int* outH, int* outReps){
    int Nval = 400, F = 100000, R = 3, W=960, H=560; uint32_t seed = 12345;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i], "-n") && i+1<argc) { Nval = atoi(argv[++i]); }
        else if(!strcmp(argv[i], "-frames") && i+1<argc) { F = atoi(argv[++i]); }
        else if(!strcmp(argv[i], "-seed") && i+1<argc) { seed = (uint32_t)strtoul(argv[++i], NULL, 10); }
        else if(!strcmp(argv[i], "-width") && i+1<argc) { W = atoi(argv[++i]); }
        else if(!strcmp(argv[i], "-height") && i+1<argc) { H = atoi(argv[++i]); }
        else if(!strcmp(argv[i], "-reps") && i+1<argc) { R = atoi(argv[++i]); }
    }
    if (Nval < 1) Nval = 1; if (Nval > MAX_N) Nval = MAX_N;
    if (F < 1) F = 1; if (R < 1) R = 1;
    *outN = Nval; *outFrames = F; *outSeed = seed; *outW=W; *outH=H; *outReps=R;
}

/* Ejecuta una medición: ms por frame con warmup previo */
static double medir_una(World* w, int frames, int use_omp){
    const double dt = 1.0/60.0;
    LARGE_INTEGER qpf; QueryPerformanceFrequency(&qpf);
    LARGE_INTEGER t0, t1;

    for(int i=0;i<100;i++){ w->gTime += dt; UpdatePhysics(w, dt, use_omp); }

    QueryPerformanceCounter(&t0);
    for(int i=0;i<frames;i++){ w->gTime += dt; UpdatePhysics(w, dt, use_omp); }
    QueryPerformanceCounter(&t1);

    double ms_total = 1000.0 * (double)(t1.QuadPart - t0.QuadPart) / (double)qpf.QuadPart;
    return ms_total / (double)frames;
}

/* Punto de entrada: promedia repeticiones y calcula speedup */
int main(int argc, char** argv){
    int N, frames, reps, W, H; uint32_t seed;
    parse_args(argc, argv, &N, &frames, &seed, &W, &H, &reps);

    double acc_sec = 0.0;
    for(int r=0;r<reps;r++){
        World w = {0}; InitWorld(&w, N, W, H, 48, seed + r);
        double mspf = medir_una(&w, frames, 0);
        acc_sec += mspf;
        FreeWorld(&w);
    }
    double ms_sec = acc_sec / (double)reps;
    double fps_sec = 1000.0 / ms_sec;

    double acc_omp = 0.0;
    for(int r=0;r<reps;r++){
        World w = {0}; InitWorld(&w, N, W, H, 48, seed + r);
        double mspf = medir_una(&w, frames, 1);
        acc_omp += mspf;
        FreeWorld(&w);
    }
    double ms_omp = acc_omp / (double)reps;
    double fps_omp = 1000.0 / ms_omp;

    double speedup = (ms_omp > 0.0) ? (ms_sec / ms_omp) : 0.0;

    printf("SEC: ms_per_frame=%.6f  fps=%.2f\n", ms_sec, fps_sec);
    printf("OMP: ms_per_frame=%.6f  fps=%.2f\n", ms_omp, fps_omp);
    printf("SPEEDUP (seq/omp) = %.2fx\n", speedup);

    return 0;
}
