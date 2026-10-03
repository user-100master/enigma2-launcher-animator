#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>   
#include <linux/fb.h>
#include <dlfcn.h>      
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

struct TileAnimationData {
    int id;
    float startX;
    float targetX;
    float currentX;
    struct timespec startTime;
    float durationSeconds;
    bool isFinished;
};

typedef int (*HI_TDE_Open_t)();
typedef void (*HI_TDE_Close_t)();
typedef HI_HANDLE (*HI_TDE_BeginJob_t)();
typedef int (*HI_TDE_Bitblit_t)(HI_HANDLE, const TDE_SURFACE_S*, const TDE_RECT_S*, const TDE_SURFACE_S*, const TDE_RECT_S*, const TDE_OPT_S*);
typedef int (*HI_TDE_EndJob_t)(HI_HANDLE, bool, uint32_t);

class LauncherAnimator {
private:
    std::vector<TileAnimationData> m_animations;
    void* m_tde_handle;
    int m_fb_fd;
    uint32_t* m_fb_mem;
    size_t m_fb_size;
    uint32_t m_fb_phys_addr;
    bool m_use_hardware;

    HI_TDE_Open_t TDE_Open;
    HI_TDE_Close_t TDE_Close;
    HI_TDE_BeginJob_t TDE_BeginJob;
    HI_TDE_Bitblit_t TDE_Bitblit;
    HI_TDE_EndJob_t TDE_EndJob;

public:
    LauncherAnimator() : m_tde_handle(nullptr), m_fb_fd(-1), m_fb_mem((uint32_t*)MAP_FAILED), m_use_hardware(false) {
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

    void startSlide(int tileId, int fromX, int toX, int durationMs) {
        for (size_t i = 0; i < m_animations.size(); ++i) {
            if (m_animations[i].id == tileId) {
                m_animations.erase(m_animations.begin() + i);
                break;
            }
        }
        TileAnimationData anim;
        anim.id = tileId;
        anim.startX = (float)fromX;
        anim.targetX = (float)toX;
        anim.currentX = anim.startX;
        anim.durationSeconds = (float)durationMs / 1000.0f;
        anim.isFinished = false;
        clock_gettime(CLOCK_MONOTONIC, &anim.startTime);
        m_animations.push_back(anim);
    }

    int updateCalculationsAdvanced(int tileId, int currentY, int width, int height) {
        struct timespec currentTime;
        clock_gettime(CLOCK_MONOTONIC, &currentTime);

        for (size_t i = 0; i < m_animations.size(); ++i) {
            if (m_animations[i].id == tileId) {
                if (m_animations[i].isFinished) {
                    int finalX = (int)m_animations[i].targetX;
                    m_animations.erase(m_animations.begin() + i);
                    return finalX;
                }

                float elapsed = (currentTime.tv_sec - m_animations[i].startTime.tv_sec) + 
                                (currentTime.tv_nsec - m_animations[i].startTime.tv_nsec) / 1000000000.0f;

                int oldX = (int)m_animations[i].currentX;

                if (elapsed >= m_animations[i].durationSeconds) {
                    m_animations[i].isFinished = true;
                    m_animations[i].currentX = m_animations[i].targetX;
                } else {
                    float progress = elapsed / m_animations[i].durationSeconds;
                    float eased = 1.0f - (1.0f - progress) * (1.0f - progress) * (1.0f - progress);
                    m_animations[i].currentX = m_animations[i].startX + (m_animations[i].targetX - m_animations[i].startX) * eased;
                }

                int newX = (int)m_animations[i].currentX;

                // Safely apply hardware-accelerated processing inside the native thread framework
                if (m_use_hardware && m_fb_mem != MAP_FAILED && oldX != newX) {
                    TDE_SURFACE_S surf = { m_fb_phys_addr, 0, 1920, 1080, 1920 * 4 };
                    TDE_RECT_S zeroClip = { 0, 0, 0, 0 };
                    TDE_OPT_S opts = { 255, 0, zeroClip };

                    TDE_RECT_S srcRect = { oldX, currentY, static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
                    TDE_RECT_S dstRect = { newX, currentY, static_cast<uint32_t>(width), static_cast<uint32_t>(height) };

                    HI_HANDLE job = TDE_BeginJob();
                    if (job) {
                        TDE_Bitblit(job, &surf, &srcRect, &surf, &dstRect, &opts);
                        TDE_EndJob(job, true, 10); // Wait 10ms until hardware pipeline completes
                    }
                }

                return newX;
            }
        }
        return -1;
    }
};

extern "C" {
    LauncherAnimator* NewAnimator() { return new LauncherAnimator(); }
    void DeleteAnimator(LauncherAnimator* anim) { if (anim) delete anim; }
    
    void TriggerSlide(LauncherAnimator* anim, int tileId, int fx, int tx, int dur) {
        if (anim) anim->startSlide(tileId, fx, tx, dur);
    }
    
    int GetCurrentFrameXAdvanced(LauncherAnimator* anim, int tileId, int currentY, int width, int height) {
        if (anim) return anim->updateCalculationsAdvanced(tileId, currentY, width, height);
        return -1;
    }
}
