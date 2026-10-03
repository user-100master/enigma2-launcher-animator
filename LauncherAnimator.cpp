#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <time.h>
#include <cmath>
#include <vector>
#include <cstring>
#include <cstdint>      

struct TileAnimationData {
    int id;
    float startX;
    float targetX;
    float currentX;
    float currentTime; // Frame-driven time track instead of system clock
    float durationSeconds;
    bool isFinished;
};

class LauncherAnimator {
private:
    std::vector<TileAnimationData> m_animations;
    int m_fb_fd;
    uint32_t* m_fb_mem;
    size_t m_fb_size;
    int m_screen_width;
    int m_screen_height;

public:
    LauncherAnimator() : m_fb_fd(-1), m_fb_mem((uint32_t*)MAP_FAILED), m_screen_width(1920), m_screen_height(1080) {
        m_fb_fd = open("/dev/fb0", O_RDWR);
        if (m_fb_fd >= 0) {
            struct fb_var_screeninfo vinfo;
            struct fb_fix_screeninfo finfo;
            if (ioctl(m_fb_fd, FBIOGET_VSCREENINFO, &vinfo) == 0 && ioctl(m_fb_fd, FBIOGET_FSCREENINFO, &finfo) == 0) {
                m_fb_size = finfo.smem_len;
                m_screen_width = vinfo.xres;
                m_screen_height = vinfo.yres;
                m_fb_mem = (uint32_t*)mmap(0, m_fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fb_fd, 0);
            }
        }
    }

    ~LauncherAnimator() {
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
        anim.currentTime = 0.0f; // Reset chronological frame index
        anim.durationSeconds = (float)durationMs / 1000.0f;
        anim.isFinished = false;

        m_animations.push_back(anim);
    }

    // Accept structural deltaTime increments directly out of the Python timer loop
    int updateCalculationsDirect(int tileId, int currentY, int width, int height, float deltaTime) {
        for (size_t i = 0; i < m_animations.size(); ++i) {
            if (m_animations[i].id == tileId) {
                if (m_animations[i].isFinished) {
                    int finalX = (int)m_animations[i].targetX;
                    m_animations.erase(m_animations.begin() + i);
                    return finalX;
                }

                int oldX = (int)m_animations[i].currentX;
                
                // Explicit frame progression step allocation
                m_animations[i].currentTime += deltaTime;

                if (m_animations[i].currentTime >= m_animations[i].durationSeconds) {
                    m_animations[i].isFinished = true;
                    m_animations[i].currentX = m_animations[i].targetX;
                } else {
                    float progress = m_animations[i].currentTime / m_animations[i].durationSeconds;
                    // PREMIUM CUBIC EASE-OUT MATH: f(t) = 1 - (1 - t)^3
                    float eased = 1.0f - std::pow(1.0f - progress, 3.0f);
                    m_animations[i].currentX = m_animations[i].startX + (m_animations[i].targetX - m_animations[i].startX) * eased;
                }

                int newX = (int)m_animations[i].currentX;

                // Sync pixel row buffer memory structures instantly
                if (m_fb_mem != MAP_FAILED && oldX != newX && width > 0 && height > 0) {
                    int safeWidth = (newX + width > m_screen_width) ? (m_screen_width - newX) : width;
                    if (oldX + safeWidth > m_screen_width) safeWidth = m_screen_width - oldX;
                    
                    if (safeWidth > 0 && currentY + height <= m_screen_height) {
                        for (int row = 0; row < height; ++row) {
                            int actualRow = currentY + row;
                            uint32_t* rowBase = m_fb_mem + (actualRow * m_screen_width);
                            std::memmove(rowBase + newX, rowBase + oldX, safeWidth * sizeof(uint32_t));
                        }
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
    
    int GetCurrentFrameXAdvanced(LauncherAnimator* anim, int tileId, int currentY, int width, int height, float deltaTime) {
        if (anim) return anim->updateCalculationsDirect(tileId, currentY, width, height, deltaTime);
        return -1;
    }
}
