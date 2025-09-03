#include <windows.h>
#include <stdlib.h>
#include <time.h>

#define N 60        
#define R 24        
#define FRAME_MS 16 

typedef struct {
    int x, y;
    int dx, dy;
    COLORREF color;
} Ball;

static Ball balls[N];
static HWND hwnd;
static RECT client;
static HDC backDC = NULL;        
static HBITMAP backBMP = NULL;   
static HBITMAP backBMPOld = NULL;
static int width = 800, height = 600;
static BOOL running = TRUE;

static void InitBackBuffer(HDC wndDC, int w, int h) {
    if (backDC) {
        SelectObject(backDC, backBMPOld);
        DeleteObject(backBMP);
        DeleteDC(backDC);
        backDC = NULL;
        backBMP = NULL;
        backBMPOld = NULL;
    }
    backDC = CreateCompatibleDC(wndDC);
    backBMP = CreateCompatibleBitmap(wndDC, w, h);
    backBMPOld = (HBITMAP)SelectObject(backDC, backBMP);
}

static void InitBalls() {
    srand((unsigned)time(NULL));
    for (int i = 0; i < N; i++) {
        balls[i].x = rand() % (width - 2*R);
        balls[i].y = rand() % (height - 2*R);
        balls[i].dx = (rand() % 11) - 5;
        balls[i].dy = (rand() % 11) - 5;
        if (balls[i].dx == 0) balls[i].dx = 1;
        if (balls[i].dy == 0) balls[i].dy = 1;
        balls[i].color = RGB(rand() % 256, rand() % 256, rand() % 256);
    }
}

static void Update() {
    for (int i = 0; i < N; i++) {
        balls[i].x += balls[i].dx;
        balls[i].y += balls[i].dy;
        if (balls[i].x < 0) { balls[i].x = 0; balls[i].dx *= -1; }
        if (balls[i].y < 0) { balls[i].y = 0; balls[i].dy *= -1; }
        if (balls[i].x + 2*R > width)  { balls[i].x = width - 2*R;  balls[i].dx *= -1; }
        if (balls[i].y + 2*R > height) { balls[i].y = height - 2*R; balls[i].dy *= -1; }
    }
}

