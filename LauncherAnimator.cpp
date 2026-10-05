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
    float currentTime;
    float durationSeconds;
    bool isFinished;
};

class LauncherAnimator {
private:
    std::vector<TileAnimationData> m_animations;
    int m_fb_fd;
    uint32_t* m_fb_mem;
    uint32_t* m_backup_buffer; // Background cache array configuration
    size_t m_fb_size;
    int m_screen_width;
    int m_screen_height;

public:
    LauncherAnimator() : m_fb_fd(-1), m_fb_mem((uint32_t*)MAP_FAILED), m_backup_buffer(nullptr), m_screen_width(1920), m_screen_height(1080) {
        m_fb_fd = open("/dev/fb0", O_RDWR);
        if (m_fb_fd >= 0) {
            struct fb_var_screeninfo vinfo;
            struct fb_fix_screeninfo finfo;
            if (ioctl(m_fb_fd, FBIOGET_VSCREENINFO, &vinfo) == 0 && ioctl(m_fb_fd, FBIOGET_FSCREENINFO, &finfo) == 0) {
                m_fb_size = finfo.smem_len;
                m_screen_width = vinfo.xres;
                m_screen_height = vinfo.yres;
                m_fb_mem = (uint32_t*)mmap(0, m_fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fb_fd, 0);
                
                if (m_fb_mem != MAP_FAILED) {
                    // Cache the baseline wallpaper state out of memory pages on startup
                    m_backup_buffer = new uint32_t[m_fb_size / sizeof(uint32_t)];
                    std::memcpy(m_backup_buffer, m_fb_mem, m_fb_size);
                }
            }
        }
    }

    ~LauncherAnimator() {
        if (m_backup_buffer) delete[] m_backup_buffer;
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
        
        // Refresh dynamic canvas cache layer baseline from live state
        if (m_fb_mem != MAP_FAILED && m_backup_buffer) {
            std::memcpy(m_backup_buffer, m_fb_mem, m_fb_size);
        }

        TileAnimationData anim;
        anim.id = tileId;
        anim.startX = (float)fromX;
        anim.targetX = (float)toX;
        anim.currentX = anim.startX;
        anim.currentTime = 0.0f; 
        anim.durationSeconds = (float)durationMs / 1000.0f;
        anim.isFinished = false;

        m_animations.push_back(anim);
    }

    int updateCalculationsDirect(int tileId, int currentY, int width, int height, float deltaTime) {
        for (size_t i = 0; i < m_animations.size(); ++i) {
            if (m_animations[i].id == tileId) {
                int oldX = (int)m_animations[i].currentX;
                
                if (m_animations[i].isFinished) {
                    m_animations.erase(m_animations.begin() + i);
                    return -2; // Signal clear complete status boundary
                }

                m_animations[i].currentTime += deltaTime;

                if (m_animations[i].currentTime >= m_animations[i].durationSeconds) {
                    m_animations[i].isFinished = true;
                    m_animations[i].currentX = m_animations[i].targetX;
                } else {
                    float progress = m_animations[i].currentTime / m_animations[i].durationSeconds;
                    float eased = 1.0f - std::pow(1.0f - progress, 3.0f);
                    m_animations[i].currentX = m_animations[i].startX + (m_animations[i].targetX - m_animations[i].startX) * eased;
                }

                int newX = (int)m_animations[i].currentX;

                if (m_fb_mem != MAP_FAILED && m_backup_buffer && oldX != newX && width > 0 && height > 0) {
                    for (int row = 0; row < height; ++row) {
                        int actualRow = currentY + row;
                        if (actualRow >= m_screen_height) break;

                        uint32_t* rowBaseLive = m_fb_mem + (actualRow * m_screen_width);
                        uint32_t* rowBaseBackup = m_backup_buffer + (actualRow * m_screen_width);
                        
                        // 1. Wipe away the ghosting trail using clean background pixels
                        std::memcpy(rowBaseLive + oldX, rowBaseBackup + oldX, width * sizeof(uint32_t));
                        
                        // 2. Draw the pixel row at its brand new position
                        std::memmove(rowBaseLive + newX, rowBaseBackup + oldX, width * sizeof(uint32_t));
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
