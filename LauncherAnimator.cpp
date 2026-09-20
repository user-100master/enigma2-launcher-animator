#include <time.h>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>

// Minimal structural definitions for Enigma2 coordinate spaces
class ePoint {
public:
    int m_x, m_y;
    ePoint(int x, int y) : m_x(x), m_y(y) {}
};

class eSize {
public:
    int m_width, m_height;
    eSize(int w, int h) : m_width(w), m_height(h) {}
};

class eWidget {
public:
    virtual ~eWidget() {}
    virtual void move(const ePoint &p) = 0;
    virtual void resize(const eSize &s) = 0;
    virtual ePoint position() = 0;
    virtual eSize size() = 0;
};

struct TileAnimationData {
    eWidget* targetWidget;
    float startX;
    float targetX;
    float currentX;
    struct timespec startTime;
    float durationSeconds;
    int targetY;
};

class LauncherAnimator {
private:
    std::vector<TileAnimationData> m_active_animations;
    std::mutex m_mutex;
    std::thread m_worker_thread;
    bool m_loop_running;

    void animationThreadLoop() {
        while (m_loop_running) {
            auto loop_start = std::chrono::steady_clock::now();
            
            struct timespec currentTime;
            clock_gettime(CLOCK_MONOTONIC, &currentTime);

            m_mutex.lock();
            if (m_active_animations.empty()) {
                m_loop_running = false;
                m_mutex.unlock();
                break;
            }

            for (auto it = m_active_animations.begin(); it != m_active_animations.end(); ) {
                if (!it->targetWidget) {
                    it = m_active_animations.erase(it);
                    continue;
                }

                float elapsed = (currentTime.tv_sec - it->startTime.tv_sec) + 
                                (currentTime.tv_nsec - it->startTime.tv_nsec) / 1000000000.0f;

                if (elapsed >= it->durationSeconds) {
                    try {
                        it->targetWidget->move(ePoint((int)it->targetX, it->targetY));
                    } catch (...) {}
                    it = m_active_animations.erase(it);
                } else {
                    float progress = elapsed / it->durationSeconds;
                    // PREMIUM CUBIC EASE-OUT SMOOTH CURVE MATH
                    float eased = 1.0f - (1.0f - progress) * (1.0f - progress) * (1.0f - progress);
                    
                    it->currentX = it->startX + (it->targetX - it->startX) * eased;

                    try {
                        it->targetWidget->move(ePoint((int)it->currentX, it->targetY));
                    } catch (...) {
                        it = m_active_animations.erase(it);
                        continue;
                    }
                    ++it;
                }
            }
            m_mutex.unlock();

            // Enforce a strict 60 FPS cadence cycle (16.6ms frame step spacing)
            auto loop_end = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(loop_end - loop_start).count();
            if (elapsed_ms < 16) {
                // Sleep remaining milliseconds to keep execution completely fluid
                std::this_thread::sleep_for(std::chrono::milliseconds(16 - elapsed_ms));
            }
        }
    }

public:
    LauncherAnimator() : m_loop_running(false) {}

    ~LauncherAnimator() {
        m_loop_running = false;
        if (m_worker_thread.joinable()) {
            m_worker_thread.join();
        }
        m_active_animations.clear();
    }

    void startSlide(unsigned long widgetPointer, int fromX, int toX, int durationMs) {
        eWidget* widget = (eWidget*)widgetPointer;
        if (!widget) return;

        m_mutex.lock();
        for (auto it = m_active_animations.begin(); it != m_active_animations.end(); ) {
            if (it->targetWidget == widget) {
                it = m_active_animations.erase(it);
            } else {
                ++it;
            }
        }

        TileAnimationData anim;
        anim.targetWidget = widget;
        anim.startX = (float)fromX;
        anim.targetX = (float)toX;
        anim.currentX = anim.startX;
        anim.durationSeconds = (float)durationMs / 1000.0f;
        try {
            anim.targetY = widget->position().m_y;
        } catch (...) {
            anim.targetY = 0;
        }
        clock_gettime(CLOCK_MONOTONIC, &anim.startTime);

        m_active_animations.push_back(anim);

        if (!m_loop_running) {
            m_loop_running = true;
            if (m_worker_thread.joinable()) {
                m_worker_thread.join();
            }
            // Fire off a native hardware background thread 
            m_worker_thread = std::thread(&LauncherAnimator::animationThreadLoop, this);
        }
        m_mutex.unlock();
    }
};

extern "C" {
    LauncherAnimator* NewAnimator() { 
        return new LauncherAnimator(); 
    }
    
    void DeleteAnimator(LauncherAnimator* anim) {
        if (anim) delete anim;
    }
    
    void TriggerSlide(LauncherAnimator* anim, unsigned long ptr, int fx, int tx, int dur) {
        if (anim) anim->startSlide(ptr, fx, tx, dur);
    }
}
