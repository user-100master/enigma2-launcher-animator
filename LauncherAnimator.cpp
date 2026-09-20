#include <lib/gui/ewidget.h>
#include <lib/base/etimer.h>
#include <lib/base/ebase.h>
#include <time.h>
#include <vector>

// Structure to track separate animation parameters for individual tile widgets
struct TileAnimationData {
    eWidget* targetWidget;
    float startX;
    float targetX;
    float currentX;
    struct timespec startTime;
    float durationSeconds;
    int targetY;
};

class LauncherAnimator : public iObject {
    DECLARE_REF(LauncherAnimator);
private:
    eTimer* m_animation_timer;
    std::vector<TileAnimationData> m_active_animations;

    void timerTick() {
        struct timespec currentTime;
        clock_gettime(CLOCK_MONOTONIC, &currentTime);

        // Vector iterator to track and update all active animations simultaneously
        for (auto it = m_active_animations.begin(); it != m_active_animations.end(); ) {
            if (!it->targetWidget) {
                it = m_active_animations.erase(it);
                continue;
            }

            // High-precision fractional elapsed time calculation via float math
            float elapsed = (currentTime.tv_sec - it->startTime.tv_sec) + 
                            (currentTime.tv_nsec - it->startTime.tv_nsec) / 1000000000.0f;

            if (elapsed >= it->durationSeconds) {
                // Hard-lock final layout bounds once destination target is achieved
                try {
                    it->targetWidget->move(ePoint((int)it->targetX, it->targetY));
                } catch (...) {}
                it = m_active_animations.erase(it); // Remove completed animation from queue
            } else {
                float progress = elapsed / it->durationSeconds;
                
                // PREMIUM CUBIC EASE-OUT SMOOTH CURVE MATH (Calculated natively)
                float eased = 1.0f - (1.0f - progress) * (1.0f - progress) * (1.0f - progress);
                
                it->currentX = it->startX + (it->targetX - it->startX) * eased;

                try {
                    // Push direct sub-coordinate updates to the Mali framebuffer canvas
                    it->targetWidget->move(ePoint((int)it->currentX, it->targetY));
                } catch (...) {
                    it = m_active_animations.erase(it);
                    continue;
                }
                ++it;
            }
        }

        // Keep driving the internal C++ eTimer loop if animations are still running
        if (!m_active_animations.empty()) {
            m_animation_timer->start(16, true); // Stays locked at 16ms (60 FPS target refresh matching TV VSync)
        } else {
            m_animation_timer->stop();
        }
    }

public:
    LauncherAnimator() {
        // Instantiate Enigma2 C++ core eTimer hooked directly into the main loop application thread
        m_animation_timer = new eTimer(eApp);
        CONNECT(m_animation_timer->timeout, LauncherAnimator::timerTick);
    }

    ~LauncherAnimator() {
        if (m_animation_timer) {
            m_animation_timer->stop();
            delete m_animation_timer;
        }
        m_active_animations.clear();
    }

    void startSlide(unsigned long widgetPointer, int fromX, int toX, int durationMs) {
        eWidget* widget = (eWidget*)widgetPointer;
        if (!widget) return;

        // Remove any existing active animations for this specific widget pointer to avoid conflicts
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
        anim.targetY = widget->position().y(); // Freeze current Y alignment to ensure it slides strictly on X axis
        clock_gettime(CLOCK_MONOTONIC, &anim.startTime);

        m_active_animations.push_back(anim);

        // Wake up the hardware timer loop if it isn't running
        if (!m_animation_timer->isActive()) {
            m_animation_timer->start(16, true);
        }
    }
};

DEFINE_REF(LauncherAnimator);

// Exported Python C-Linkage Interface Hooks
extern "C" {
    LauncherAnimator* NewAnimator() {
        LauncherAnimator* anim = new LauncherAnimator();
        anim->AddRef();
        return anim;
    }
    
    void DeleteAnimator(LauncherAnimator* anim) {
        if (anim) anim->Release();
    }
    
    void TriggerSlide(LauncherAnimator* anim, unsigned long ptr, int fx, int tx, int dur) {
        if (anim) anim->startSlide(ptr, fx, tx, dur);
    }
}
