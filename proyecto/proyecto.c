// proyecto.c — Animación de pelotas con trayectorias parabólicas (Win32/GDI, secuencial)
// Dibuja con doble buffer, muestra FPS y gestiona partículas/sombras.

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

/* Utilidades */
static COLORREF Darken(COLORREF c,int pct){int r=GetRValue(c),g=GetGValue(c),b=GetBValue(c);r=r*(100-pct)/100;g=g*(100-pct)/100;b=b*(100-pct)/100;return RGB(r,g,b);}
static float GroundY(){return (float)(height-floorH);}
static int clampi(int v,int a,int b){return v<a?a:(v>b?b:v);}
static float clampf(float v,float a,float b){return v<a?a:(v>b?b:v);}

/* Libera pinceles y memoria de bolas */
static void FreeBalls(){
    if(!balls) return;
    for(int i=0;i<N;i++){ if(balls[i].brush) DeleteObject(balls[i].brush); if(balls[i].shadow) DeleteObject(balls[i].shadow); }
    free(balls); balls=NULL;
    if(gPortalBrush){ DeleteObject(gPortalBrush); gPortalBrush=NULL; }
}

/* Crea pinceles de color y sombra para una bola */
static void MakeBrushes(Ball* b){
    if(b->brush) DeleteObject(b->brush);
    if(b->shadow) DeleteObject(b->shadow);
    b->brush=CreateSolidBrush(b->color);
    b->shadow=CreateSolidBrush(Darken(b->color,75));
}

/* Próximo intervalo de reaparición */
static double NextInterval(){ return 0.12 + (rand()%10)*0.012; }

/* Inicializa el pool de partículas */
static void ParticlesClear(){ for(int i=0;i<MAX_PARTICLES;i++) gParticles[i].alive=FALSE; }

/* Genera chispas (efecto simple) */
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

/* Actualiza partículas (gravedad, amortiguamiento, rebote suelo) */
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

/* Dibuja partículas */
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

/* Inicializa una bola nueva (posición, velocidades, color, pinceles) */
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

/* Desactiva y programa reaparición */
static void ScheduleBall(Ball* b){
    if(gPortalBrush){
        float cy=b->y+b->r;
        BOOL onFloor=fabsf(cy-GroundY())<2.0f;
        if(onFloor) SpawnSparks(b->x+b->r,GroundY(),18,gPortalBrush,b->vx);
    }
    b->active=FALSE;
    b->spawnAt=gTime+NextInterval();
}

/* Reserva arreglo de bolas y configura spawn escalonado */
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

/* Crea backbuffer compatible con la ventana */
static void InitBackBuffer(HDC wndDC,int w,int h){
    if(backDC){ SelectObject(backDC,backOld); DeleteObject(backBMP); DeleteDC(backDC); backDC=NULL; backBMP=NULL; backOld=NULL; }
    backDC=CreateCompatibleDC(wndDC);
    backBMP=CreateCompatibleBitmap(wndDC,w,h);
    backOld=(HBITMAP)SelectObject(backDC,backBMP);
}

/* Fondo con gradiente y piso */
static void DrawBackground(){
    TRIVERTEX v[4];
    v[0].x=0; v[0].y=0; v[0].Red=0x0015; v[0].Green=0x0015; v[0].Blue=0x0024; v[0].Alpha=0;
    v[1].x=width; v[1].y=0; v[1].Red=0x0024; v[1].Green=0x0024; v[1].Blue=0x0040; v[1].Alpha=0;
    v[2].x=0; v[2].y=height; v[2].Red=0x0010; v[2].Green=0x0010; v[2].Blue=0x0020; v[2].Alpha=0;
    v[3].x=width; v[3].y=height; v[3].Red=0x0030; v[3].Green=0x0030; v[3].Blue=0x004A; v[3].Alpha=0;
    GRADIENT_TRIANGLE g[2]={{0,1,2},{1,3,2}};
    GradientFill(backDC,v,4,g,2,GRADIENT_FILL_TRIANGLE);
    RECT floor={0,height-floorH,width,height};
    HBRUSH ground=CreateSolidBrush(RGB(18,18,22));
    FillRect(backDC,&floor,ground);
    DeleteObject(ground);
}

/* Dibuja bola con sombra y brillo */
static void DrawBallWithEffects(Ball* b){
    int r=b->r;
    float gy=GroundY();
    float h=gy-(b->y+r); if(h<0) h=0;
    float sShadow=0.35f+0.65f*(1.0f-(h/(float)height));
    float squash=b->squash;
    int sw=(int)(r*1.45f*sShadow*squash);
    int sh=clampi((int)(r*0.44f*sShadow),3,r);
    SelectObject(backDC,b->shadow);
    int sx=(int)(b->x + r - sw/2);
    int sy=(int)(gy + (r - sh));
    Ellipse(backDC,sx,sy,sx+sw,sy+sh);

    float cx=b->x+r, cy=b->y+r;
    float drawW=2.0f*r*b->squash;
    float drawH=2.0f*r/b->squash;
    int left=(int)(cx - drawW*0.5f);
    int top=(int)(cy - drawH*0.5f);
    int right=(int)(left + drawW);
    int bot=(int)(top + drawH);
    HBRUSH oldBrush=(HBRUSH)SelectObject(backDC,b->brush);
    HPEN oldPen=(HPEN)SelectObject(backDC,GetStockObject(NULL_PEN));
    Ellipse(backDC,left,top,right,bot);

    static HBRUSH shine=NULL; if(!shine) shine=CreateSolidBrush(RGB(255,255,255));
    SelectObject(backDC,shine);
    float rad=(float)r*0.58f;
    float ox=cosf(b->angle)*rad*0.46f;
    float oy=sinf(b->angle)*rad*0.30f;
    int rx=(int)(r*0.38f), ry=(int)(r*0.24f);
    int hx1=(int)(cx - rx/2 + ox);
    int hy1=(int)(cy - ry/2 + oy);
    Ellipse(backDC,hx1,hy1,hx1+rx,hy1+ry);

    SelectObject(backDC,oldPen);
    SelectObject(backDC,oldBrush);
}

/* Dibuja estela atenuada */
static void DrawTrails(Ball* b){
#if ENABLE_TRAILS
    HBRUSH oldBrush=(HBRUSH)SelectObject(backDC,b->shadow);
    HPEN oldPen=(HPEN)SelectObject(backDC,GetStockObject(NULL_PEN));
    int r=b->r;
    int steps=b->trailCount;
    for(int k=1;k<steps && k<TRAIL_LEN;k++){
        float t=(float)k/(float)TRAIL_LEN;
        int rr=(int)(r*(0.42f*(1.0f-t)+0.12f)); if(rr<1) rr=1;
        int x=(int)b->trailX[k]-rr;
        int y=(int)b->trailY[k]-rr;
        Ellipse(backDC,x,y,x+2*rr,y+2*rr);
    }
    SelectObject(backDC,oldPen);
    SelectObject(backDC,oldBrush);
#else
    (void)b;
#endif
}

/* Dibuja todas las bolas activas */
static void DrawBalls(int* outActive){
    int active=0;
    for(int i=0;i<N;i++){
        Ball* b=&balls[i];
        if(!b->active) continue;
        active++;
        DrawTrails(b);
        DrawBallWithEffects(b);
    }
    if(outActive) *outActive=active;
}

/* HUD de FPS y conteo de bolas activas */
static void DrawHUD(double fps,int active){
    SetBkMode(backDC,TRANSPARENT);
    SetTextColor(backDC,RGB(240,240,240));
    char buf[128];
    sprintf(buf,"FPS: %.1f   Activas: %d/%d",fps,active,N);
    TextOutA(backDC,8,8,buf,lstrlenA(buf));
}

/* Presenta el backbuffer en la ventana */
static void Present(HDC wndDC){ BitBlt(wndDC,0,0,width,height,backDC,0,0,SRCCOPY); }

/* Física: activa bolas pendientes y actualiza dinámica con rebotes/rozamientos */
static void UpdatePhysics(double dt){
    dt*=TIME_SCALE;
    float gy=GroundY();

    for(int i=0;i<N;i++){ if(!balls[i].active && gTime>=balls[i].spawnAt) ActivateBall(&balls[i]); }

    for(int i=0;i<N;i++){
        Ball* b=&balls[i];
        if(!b->active) continue;

        int r=b->r;
        float prevVy=b->vy;

        float wind = 70.0f*sinf((float)(1.10*gTime + b->phase)) + 35.0f*sinf((float)(0.63*gTime + i*0.19));
        b->vx += wind*(float)dt;

        float speed=fabsf(b->vx)+fabsf(b->vy); (void)speed;

        float lift = b->liftCoeff * b->angVel * b->vx;
        b->vy += (G + lift)*(float)dt;
        b->vx *= (1.0f - AIR * (float)dt);

        b->x += b->vx*(float)dt;
        b->y += b->vy*(float)dt;

        float cy=b->y+r;
        if(cy + r > gy){
            float impact=fabsf(prevVy);
            b->y=gy-r;
            b->vy=-b->vy*REST;
            b->vx*=GROUND_FRICTION;
            if(fabsf(b->vy)<60.f) b->vy=0.f;

            float squashAmt=clampf(1.0f + impact/850.0f,1.0f,1.95f);
            b->squash=squashAmt;

            b->angVel += (b->vx/(float)(r))*0.35f;

            if(impact>300.f){
                int cnt=8+(int)(impact/220.f); if(cnt>28) cnt=28;
                SpawnSparks(b->x+r,gy,cnt,b->shadow,b->vx);
            }
            if(fabsf(b->vx)>420.f && fabsf(b->vy)<30.f && (rand()%4==0)){
                b->vy -= 420.f + (rand()%180);
            }
        }

        if(b->y<0){ b->y=0; b->vy=-b->vy*WALL_DAMP; }
        if(b->x< -2*r){ b->x= -2*r; b->vx = fabsf(b->vx)*0.95f; }
        if(b->x + 2*r > width){
            b->x = width - 2*r;
            b->vx = -fabsf(b->vx)*0.75f;
            b->angVel *= 0.85f;
            SpawnSparks(b->x+2*r,cy,b->vy>0?10:6,b->shadow,-b->vx);
        }

        b->squash += (1.0f - b->squash)*(float)(9.0*dt);
        if(fabsf(b->squash-1.0f)<0.01f) b->squash=1.0f;

        b->angVel *= (1.0f - 0.26f * (float)dt);
        b->angle  += b->angVel*(float)dt;

        b->jitterT += (float)dt;
        if(b->jitterT>0.08f){
            b->jitterT=0.f;
            b->vx += (float)((rand()%200)-100)*0.6f;
            if(rand()%6==0) b->angVel += ((float)((rand()%200)-100)/100.f)*0.9f;
        }

        BOOL onFloor=fabsf((b->y+r)-gy)<1.0f;
        BOOL nearRight=(b->x + 2*r)>(RIGHT_ZONE*width);
        BOOL quiet=fabsf(b->vx)<QUIET_VX && fabsf(b->vy)<QUIET_VY;
        if(onFloor && nearRight && quiet){ ScheduleBall(b); continue; }
        if(b->x - 2*r > width+20){ ScheduleBall(b); continue; }

#if ENABLE_TRAILS
        for(int k=TRAIL_LEN-1;k>0;k--){ b->trailX[k]=b->trailX[k-1]; b->trailY[k]=b->trailY[k-1]; }
        b->trailX[0]=b->x+r; b->trailY[0]=b->y+r; if(b->trailCount<TRAIL_LEN) b->trailCount++;
#endif
    }

    ParticlesUpdate(dt);
}

/* Redimensiona y recrea backbuffer al cambiar tamaño */
static void ResizeRecreate(){
    GetClientRect(hwnd,&client);
    width=client.right-client.left; height=client.bottom-client.top;
    if(width<1) width=1; if(height<1) height=1;
    HDC wndDC=GetDC(hwnd); InitBackBuffer(wndDC,width,height); ReleaseDC(hwnd,wndDC);
}

/* Ventana Win32: repinta, resize y cierre */
static LRESULT CALLBACK WndProc(HWND h,UINT msg,WPARAM wParam,LPARAM lParam){
    switch(msg){
        case WM_SIZE: ResizeRecreate(); return 0;
        case WM_PAINT: { PAINTSTRUCT ps; HDC hdc=BeginPaint(h,&ps); Present(hdc); EndPaint(h,&ps); return 0; }
        case WM_DESTROY: running=FALSE; PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h,msg,wParam,lParam);
}

/* Lee N desde línea de comandos (opcional) */
static int ParseN(LPSTR lpCmdLine){
    if(!lpCmdLine||!*lpCmdLine) return DEF_N;
    char* endp=NULL; long v=strtol(lpCmdLine,&endp,10);
    if(endp==lpCmdLine||v<=0) return DEF_N;
    if(v>MAX_N) v=MAX_N;
    return (int)v;
}

/* Programa principal: bucle de mensajes + render + HUD */
int WINAPI WinMain(HINSTANCE hInst,HINSTANCE hPrev,LPSTR lpCmd,int nShow){
    (void)hPrev;
    const char* CLASS_NAME="SequentialEmitterWnd";
    WNDCLASSA wc={0};
    wc.lpfnWndProc=WndProc; wc.hInstance=hInst; wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName=CLASS_NAME;
    if(!RegisterClassA(&wc)) return 0;
    int initW=960, initH=560;
    hwnd=CreateWindowA(CLASS_NAME,"Left-to-Right Parabolic Bounces (FX++)",
                       WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,initW,initH,NULL,NULL,hInst,NULL);
    if(!hwnd) return 0;
    ShowWindow(hwnd,nShow); UpdateWindow(hwnd);
    ResizeRecreate();
    N=ParseN(lpCmd);
    InitBalls();

    LARGE_INTEGER qpf; QueryPerformanceFrequency(&qpf);
    LARGE_INTEGER last; QueryPerformanceCounter(&last);
    double fps_acc=0.0; int fps_frames=0; double fps=0.0;

    MSG msg; DWORD nextFrame=GetTickCount();
    while(running){
        while(PeekMessage(&msg,NULL,0,0,PM_REMOVE)){ if(msg.message==WM_QUIT) running=FALSE; TranslateMessage(&msg); DispatchMessage(&msg); }
        if(!running) break;

        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        double dt=(double)(now.QuadPart-last.QuadPart)/(double)qpf.QuadPart; last=now;

        gTime+=dt;
        UpdatePhysics(dt);
        DrawBackground();
        int active=0; DrawBalls(&active);
        ParticlesDraw();
        DrawHUD(fps,active);
        HDC wndDC=GetDC(hwnd); Present(wndDC); ReleaseDC(hwnd,wndDC);

        fps_acc+=dt; fps_frames++; if(fps_acc>=0.25){ fps=(double)fps_frames/fps_acc; fps_acc=0.0; fps_frames=0; }
        nextFrame+=FRAME_MS; DWORD t=GetTickCount(); if(nextFrame>t) Sleep(nextFrame-t); else nextFrame=t;
    }
    FreeBalls();
    if(backDC){ SelectObject(backDC,backOld); DeleteObject(backBMP); DeleteDC(backDC); }
    return 0;
}
