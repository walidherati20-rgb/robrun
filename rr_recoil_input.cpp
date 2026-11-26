/* External CB-45 M3 mouse-input recoil test based on the supplied G HUB
 * preset. This does not modify game memory; in-game effect is unverified. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "rr_mem.h"

namespace {
struct Offset { float x, y; };
constexpr Offset pattern[] = {
    {-.000329f,-.0167f},{.000359f,-.0125f},{.00105f,-.0122f},
    {.00124f,-.0119f},{.00151f,-.0116f},{.00154f,-.0117f},
    {.00101f,-.0115f},{.000627f,-.0112f},{.000206f,-.011f},
    {-.000484f,-.011f},{-.000676f,-.0109f},{-.000939f,-.0106f},
    {-.000637f,-.0105f},{-.000176f,-.0102f},{.0000915f,-.0103f},
    {.000704f,-.0102f},{.00121f,-.00986f},{.00121f,-.00977f},
    {.00132f,-.00986f},{.00121f,-.0096f},{.000627f,-.00944f},
    {.000283f,-.00926f},{-.000329f,-.00901f},{-.000484f,-.00918f},
    {-.000518f,-.00901f},{-.000924f,-.0089f},{-.000992f,-.00875f},
    {-.00117f,-.00851f},{-.00128f,-.00819f},{-.00128f,-.00783f},
    {-.00119f,-.00745f},{-.00101f,-.00703f},{-.000765f,-.00662f},
    {-.000494f,-.0062f},{-.000249f,-.00582f},{-.0000745f,-.00547f},
    {.00000205f,-.00517f},{-.0000317f,-.00493f},{-.000165f,-.00477f},
    {-.000367f,-.00469f}
};
static_assert(sizeof(pattern)/sizeof(pattern[0]) == 40);
SRWLOCK lock = SRWLOCK_INIT;
HANDLE thread;
DWORD beat;
int requested, pause_reason;
volatile LONG stopped, status;

bool armed() {
    /* The menu checkbox is the arm switch. GetKeyState(VK_SCROLL) in a
     * background thread can report stale toggle state while the game owns
     * foreground input. Require only the physical ADS+fire chord here. */
    return (GetAsyncKeyState(VK_LBUTTON)&0x8000) &&
           (GetAsyncKeyState(VK_RBUTTON)&0x8000);
}
struct PlaybackReport {
    int* shots;
    int* sent_x;
    int* sent_y;
    int* calls;
    DWORD start;
    ~PlaybackReport() {
        char path[MAX_PATH];
        DWORD length=GetModuleFileNameA(nullptr,path,MAX_PATH);
        if (!length || length>=MAX_PATH) return;
        char* slash=strrchr(path,'\\');
        if (!slash) return;
        slash[1]=0;
        if (strcat_s(path,sizeof path,"recoil-input.log")) return;
        FILE* f=fopen(path,"a+");
        if (!f) return;
        fseek(f,0,SEEK_END);
        if (ftell(f)<1024*1024)
            fprintf(f,"ms=%lu shots=%d calls=%d mouse_total=(%d,%d) duration_ms=%lu\n",
                    (unsigned long)start,*shots,*calls,*sent_x,*sent_y,
                    (unsigned long)(GetTickCount()-start));
        fclose(f);
    }
};
bool allowed() {
    int on, pause; DWORD last;
    AcquireSRWLockShared(&lock);
    on=requested; pause=pause_reason; last=beat;
    ReleaseSRWLockShared(&lock);
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return !InterlockedCompareExchange(&stopped,0,0) && on && !pause &&
           GetTickCount()-last<=100 && g_rr_pid && pid==g_rr_pid;
}
int rounded(double n) { return n>=0 ? int(floor(n+.5)) : int(ceil(n-.5)); }
float smooth(float t) { return t*t*(3.f-2.f*t); }

void playback() {
    constexpr int step=5, cooldown=110, shots=40;
    constexpr float attack=.05f, decay_delay=.2f, decay_speed=6.f;
    constexpr double units=-1.0/(.0017999999690800905*.45);
    float cx=0,cy=0,tx=0,ty=0,ax=0,ay=0;
    double want_x=0,want_y=0;
    int count=0,next=0,sent_x=0,sent_y=0,calls=0,attack_start=0,deadline=0;
    bool attacking=false;
    DWORD start=GetTickCount(),previous=start;
    PlaybackReport report{&count,&sent_x,&sent_y,&calls,start};
    InterlockedExchange(&status,10);
    while (allowed() && armed()) {
        DWORD wall=GetTickCount();
        if (wall<previous || wall-previous>250) break;
        previous=wall;
        while (static_cast<DWORD>(next)<=wall-start) {
            if (!allowed() || !armed()) return;
            float old_x=cx,old_y=cy;
            if (count<shots && next==count*cooldown) {
                if (attacking && next-attack_start>=50) { cx=tx; cy=ty; }
                auto o=pattern[count];
                deadline=next+int(decay_delay*1000);
                if (!count) { tx=cx; ty=cy; }
                ax=cx; ay=cy; attack_start=next;
                tx+=o.x; ty+=o.y; attacking=true; ++count;
            }
            if (!attacking && next>=deadline) { tx=ty=ax=ay=0; attack_start=0; }
            if (attacking) {
                float t=fminf(1.f,(next-attack_start)/(attack*1000.f));
                if (t>=1.f) { cx=tx; cy=ty; attacking=false; }
                else { t=smooth(t); cx=ax+(tx-ax)*t; cy=ay+(ty-ay)*t; }
            } else {
                float t=step/1000.f*decay_speed;
                cx+=(tx-cx)*t; cy+=(ty-cy)*t;
            }
            want_x+=(cx-old_x)*units; want_y+=(cy-old_y)*units;
            next+=step;
            if (count==shots && !attacking) break;
        }
        int dx=rounded(want_x)-sent_x,dy=rounded(want_y)-sent_y;
        while (dx || dy) {
            if (!allowed() || !armed()) return;
            int x=dx<-127 ? -127 : dx>127 ? 127 : dx;
            int y=dy<-127 ? -127 : dy>127 ? 127 : dy;
            INPUT input{};
            input.type=INPUT_MOUSE; input.mi.dwFlags=MOUSEEVENTF_MOVE;
            input.mi.dx=x; input.mi.dy=y;
            if (SendInput(1,&input,sizeof input)!=1) {
                InterlockedExchange(&status,11);
                return;
            }
            ++calls;
            sent_x+=x; sent_y+=y; dx-=x; dy-=y;
        }
        if (count==shots && !attacking) return;
        Sleep(1);
    }
}
DWORD WINAPI worker(void*) {
    bool consumed=false;
    while (!InterlockedCompareExchange(&stopped,0,0)) {
        if (!allowed()) {
            consumed=false;
            int on,pause; DWORD last;
            AcquireSRWLockShared(&lock);
            on=requested; pause=pause_reason; last=beat;
            ReleaseSRWLockShared(&lock);
            InterlockedExchange(&status,!on ? 0 : pause ? pause :
                GetTickCount()-last>100 ? 8 : 6);
        } else if (!armed()) {
            consumed=false;
            InterlockedExchange(&status,9);
        } else if (!consumed) {
            consumed=true;
            playback();
        }
        Sleep(1);
    }
    InterlockedExchange(&status,0);
    return 0;
}
}
extern "C" void rr_recoil_publish(int on,int pause) {
    AcquireSRWLockExclusive(&lock);
    requested=!!on; pause_reason=pause; beat=GetTickCount();
    ReleaseSRWLockExclusive(&lock);
}
extern "C" int rr_recoil_status() { return (int)InterlockedCompareExchange(&status,0,0); }
extern "C" int rr_recoil_start() {
    if (thread) return 1;
    InterlockedExchange(&stopped,0);
    thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);
    return thread!=nullptr;
}
extern "C" void rr_recoil_stop() {
    InterlockedExchange(&stopped,1);
    if (thread) {
        WaitForSingleObject(thread,INFINITE);
        CloseHandle(thread);
        thread=nullptr;
    }
}
