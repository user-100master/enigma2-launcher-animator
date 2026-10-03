#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>   
#include <linux/fb.h>
#include <dlfcn.h>      
#include <pthread.h>    
#include <time.h>
#include <cmath>
#include <vector>
#include <iostream>
#include <cstdint>      

typedef int32_t HI_HANDLE;

struct TDE_SURFACE_S {
    uint32_t Phaddr;
    uint32_t Format;
    uint32_t Width;
    uint32_t Height;
    uint32_t Stride;
};

struct TDE_RECT_S {
    int32_t s32Xpos;
    int32_t s32Ypos;
    uint32_t u32Width;
    uint32_t u32Height;
};

struct TDE_OPT_S {
    uint32_t global_alpha;
    uint32_t clip_enable;
    TDE_RECT_S clip_rect;
};

typedef int (*HI_TDE_Open_t)();
typedef void (*HI_TDE_Close_t)();
typedef HI_HANDLE (*HI_TDE_BeginJob_t)();
typedef int (*HI_TDE_Bitblit_t)(HI_HANDLE, const TDE_SURFACE_S*, const TDE_RECT_S*, const TDE_SURFACE_S*, const TDE_RECT_S*, const TDE_OPT_S*);
typedef int (*HI_TDE_EndJob_t)(HI_HANDLE, bool, uint32_t);

struct ThreadedAnimParams {
    void* animator_instance;
    int fromX;
    int toX;
    int currentY;
    int width;
    int height;
    int durationMs;
};

class LauncherAnimator {
private:
    void* m_tde_handle;
    int m_fb_fd;
    uint32_t* m_fb_mem;
    size_t m_fb_size;
    uint32_t m_fb_phys_addr;
    bool m_use_hardware;
    
public:
    volatile bool m_thread_running; // Thread safety flag
    HI_TDE_Open_t TDE_Open;
    HI_TDE_Close_t TDE_Close;
    HI_TDE_BeginJob_t TDE_BeginJob;
    HI_TDE_Bitblit_t TDE_Bitblit;
    HI_TDE_EndJob_t TDE_EndJob;

    LauncherAnimator() : m_tde_handle(nullptr), m_fb_fd(-1), m_fb_mem((uint32_t*)MAP_FAILED), m_use_hardware(false), m_thread_running(false) {
        m_fb_fd = open("/dev/fb0", O_RDWR);
        if (m_fb_fd >= 0) {
            struct fb_var_screeninfo vinfo;
            struct fb_fix_screeninfo finfo;
            if (ioctl(m_fb_fd, FBIOGET_VSCREENINFO, &vinfo) == 0 && ioctl(m_fb_fd, FBIOGET_FSCREENINFO, &finfo) == 0) {
                m_fb_size = finfo.smem_len;
                m_fb_phys_addr = finfo.smem_start; 
                m_fb_mem = (uint32_t*)mmap(0, m_fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fb_fd, 0);
            }
        }

        m_tde_handle = dlopen("libhi_msp.so", RTLD_LAZY | RTLD_GLOBAL);
        if (m_tde_handle) {
            TDE_Open = (HI_TDE_Open_t)dlsym(m_tde_handle, "HI_TDE2_Open");
            TDE_Close = (HI_TDE_Close_t)dlsym(m_tde_handle, "HI_TDE2_Close");
            TDE_BeginJob = (HI_TDE_BeginJob_t)dlsym(m_tde_handle, "HI_TDE2_BeginJob");
            TDE_Bitblit = (HI_TDE_Bitblit_t)dlsym(m_tde_handle, "HI_TDE2_Bitblit");
            TDE_EndJob = (HI_TDE_EndJob_t)dlsym(m_tde_handle, "HI_TDE2_EndJob");

            if (TDE_Open && TDE_BeginJob && TDE_Bitblit && TDE_EndJob && TDE_Open() == 0) {
                m_use_hardware = true;
            }
        }
    }

    ~LauncherAnimator() {
        if (m_use_hardware && TDE_Close) TDE_Close();
        if (m_tde_handle) dlclose(m_tde_handle);
        if (m_fb_mem != MAP_FAILED) munmap(m_fb_mem, m_fb_size);
        if (m_fb_fd >= 0) close(m_fb_fd);
    }

    bool isHardwareActive() const { return m_use_hardware && m_fb_mem != MAP_FAILED; }
    uint32_t getPhysAddr() const { return m_fb_phys_addr; }

    void startAsyncHardwareSlide(int fromX, int toX, int currentY, int width, int height, int durationMs);
};

void* runAnimationThreadInternal(void* arg) {
    ThreadedAnimParams* p = reinterpret_cast<ThreadedAnimParams*>(arg);
    LauncherAnimator* self = reinterpret_cast<LauncherAnimator*>(p->animator_instance);

    struct timespec startTime, currentTime;
    clock_gettime(CLOCK_MONOTONIC, &startTime);
    float durationSec = static_cast<float>(p->durationMs) / 1000.0f;
    
    int oldX = p->fromX;
    bool finished = false;

    TDE_SURFACE_S surf = { self->getPhysAddr(), 0, 1920, 1080, 1920 * 4 };
    TDE_RECT_S zeroClip = { 0, 0, 0, 0 };
    TDE_OPT_S opts = { 255, 0, zeroClip };

    while (!finished) {
        clock_gettime(CLOCK_MONOTONIC, &currentTime);
        float elapsed = (currentTime.tv_sec - startTime.tv_sec) + 
                        (currentTime.tv_nsec - startTime.tv_nsec) / 1000000000.0f;

        if (elapsed >= durationSec) {
            elapsed = durationSec;
            finished = true;
        }

        float progress = elapsed / durationSec;
        float eased = 1.0f - (1.0f - progress) * (1.0f - progress) * (1.0f - progress);
        int newX = static_cast<int>(p->fromX + (p->toX - p->fromX) * eased);

        if (oldX != newX) {
            if (self->isHardwareActive()) {
                TDE_RECT_S srcRect = { oldX, p->currentY, static_cast<uint32_t>(p->width), static_cast<uint32_t>(p->height) };
                TDE_RECT_S dstRect = { newX, p->currentY, static_cast<uint32_t>(p->width), static_cast<uint32_t>(p->height) };

                HI_HANDLE job = self->TDE_BeginJob();
                if (job) {
                    self->TDE_Bitblit(job, &surf, &srcRect, &surf, &dstRect, &opts);
                    self->TDE_EndJob(job, true, 10);
                }
            }
            oldX = newX;
        }

        if (!finished) {
            struct timespec sleepTime;
            sleepTime.tv_sec = 0;
            sleepTime.tv_nsec = 16666666; 
            nanosleep(&sleepTime, nullptr);
        }
    }

    self->m_thread_running = false; // Toggle complete status cleanly
    delete p;
    return nullptr;
}

void LauncherAnimator::startAsyncHardwareSlide(int fromX, int toX, int currentY, int width, int height, int durationMs) {
    m_thread_running = true;
    ThreadedAnimParams* params = new ThreadedAnimParams();
    params->animator_instance = this;
    params->fromX = fromX;
    params->toX = toX;
    params->currentY = currentY;
    params->width = width;
    params->height = height;
    params->durationMs = durationMs;

    pthread_t threadId;
    pthread_create(&threadId, nullptr, runAnimationThreadInternal, params);
    pthread_detach(threadId); 
}

extern "C" {
    LauncherAnimator* NewAnimator() { return new LauncherAnimator(); }
    void DeleteAnimator(LauncherAnimator* anim) { if (anim) delete anim; }
    
    void DispatchPureHardwareSlide(LauncherAnimator* anim, int fx, int tx, int cy, int w, int h, int dur) {
        if (anim) anim->startAsyncHardwareSlide(fx, tx, cy, w, h, dur);
    }

    bool IsAnimationActive(LauncherAnimator* anim) {
        if (anim) return anim->m_thread_running;
        return false;
    }
}
