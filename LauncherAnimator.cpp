// launcher_tde.cpp
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <dlfcn.h>
#include <iostream>
#include <vector>
#include <time.h>
#include <cmath>

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
        // 1. Map openATV Master Framebuffer Device
        m_fb_fd = open("/dev/fb0", O_RDWR);
        if (m_fb_fd >= 0) {
            struct fb_var_screeninfo vinfo;
            struct fb_fix_screeninfo finfo;
            if (ioctl(m_fb_fd, FBIOGET_VSCREENINFO, &vinfo) == 0 && ioctl(m_fb_fd, FBIOGET_FSCREENINFO, &finfo) == 0) {
                m_fb_size = finfo.smem_len;
                m_fb_phys_addr = finfo.smem_start; // Underlying hi_mmz absolute memory pointer
                m_fb_mem = (uint32_t*)mmap(0, m_fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fb_fd, 0);
            }
        }

        // 2. Load the explicitly identified HiSilicon Media Signal Processor library
        const char* lib_paths[] = {
            "libhi_msp.so", 
            "/usr/lib/libhi_msp.so",
            "libhi_common.so"
        };

        for (const char* path : lib_paths) {
            m_tde_handle = dlopen(path, RTLD_LAZY | RTLD_GLOBAL);
            if (m_tde_handle) break;
        }

        if (m_tde_handle) {
            // Re-bind symbols directly out of the MSP binary mapping layer
            TDE_Open = (HI_TDE_Open_t)dlsym(m_tde_handle, "HI_TDE2_Open");
            TDE_Close = (HI_TDE_Close_t)dlsym(m_tde_handle, "HI_TDE2_Close");
            TDE_BeginJob = (HI_TDE_BeginJob_t)dlsym(m_tde_handle, "HI_TDE2_BeginJob");
            TDE_Bitblit = (HI_TDE_Bitblit_t)dlsym(m_tde_handle, "HI_TDE2_Bitblit");
            TDE_EndJob = (HI_TDE_EndJob_t)dlsym(m_tde_handle, "HI_TDE2_EndJob");

            if (TDE_Open && TDE_BeginJob && TDE_Bitblit && TDE_EndJob) {
                if (TDE_Open() == 0) {
                    m_use_hardware = true; // Hardware Acceleration is officially active!
                }
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

    int updateCalculations(int tileId, int currentY, int width, int height) {
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

                // Execute TDE Hardware Overlap Translation
                if (m_use_hardware && m_fb_mem != MAP_FAILED && oldX != newX) {
                    TDE_SURFACE_S surf;
                    surf.Phaddr = m_fb_phys_addr;
                    surf.Format = 0; // ARGB8888 32-bit output standard
                    surf.Width = 1920;
                    surf.Height = 1080;
                    surf.Stride = 1920 * 4;

                    TDE_RECT_S srcRect = { oldX, currentY, (uint32_t)width, (uint32_t)height };
                    TDE_RECT_S dstRect = { newX, currentY, (uint32_t)width, (uint32_t)height };
                    TDE_OPT_S opts = { 255, 0, {0, 0, 0, 0} };

                    HI_HANDLE job = TDE_BeginJob();
                    if (job) {
                        // TDE schedules this block directly onto the HiSilicon 2D hardware coprocessor
                        TDE_Bitblit(job, &surf, &srcRect, &surf, &dstRect, &opts);
                        TDE_EndJob(job, true, 30); // Block wait flag for synchronization
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
        if (anim) return anim->updateCalculations(tileId, currentY, width, height);
        return -1;
    }
}
