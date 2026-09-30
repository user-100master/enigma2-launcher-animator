#include <time.h>
#include <vector>
#include <cmath>
#include <mutex>
#include <cstdint>

// ---------------------------------------------------------------------------
// Easing curves (selectable per animation)
// ---------------------------------------------------------------------------
enum EasingType {
    EASE_CUBIC_OUT = 0,   // 1 - (1-p)^3  (Android "decelerate", default)
    EASE_QUAD_OUT  = 1,   // 1 - (1-p)^2  (gentler)
    EASE_LINEAR    = 2,
};

static inline float apply_easing(float p, int type) {
    switch (type) {
        case EASE_QUAD_OUT: {
            float inv = 1.0f - p;
            return 1.0f - inv * inv;
        }
        case EASE_LINEAR:
            return p;
        case EASE_CUBIC_OUT:
        default: {
            float inv = 1.0f - p;
            return 1.0f - inv * inv * inv;
        }
    }
}

// ---------------------------------------------------------------------------
// Animation record
// ---------------------------------------------------------------------------
struct TileAnimationData {
    int    id;
    float  startX;
    float  targetX;
    float  currentX;
    struct timespec startTime;
    float  durationSeconds;
    int    easing;
};

// ---------------------------------------------------------------------------
// Animator
// ---------------------------------------------------------------------------
class LauncherAnimator {
private:
    std::vector<TileAnimationData> m_animations;
    std::mutex m_mutex;

    // Called with m_mutex held.
    void removeByIdLocked(int tileId) {
        for (size_t i = 0; i < m_animations.size(); ++i) {
            if (m_animations[i].id == tileId) {
                m_animations.erase(m_animations.begin() + i);
                return;
            }
        }
    }

public:
    LauncherAnimator() {}

    void startSlide(int tileId, int fromX, int toX, int durationMs, int easing) {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Remove any older animation with the same ID so we don't fight layouts.
        removeByIdLocked(tileId);

        TileAnimationData anim;
        anim.id              = tileId;
        anim.startX          = (float)fromX;
        anim.targetX         = (float)toX;
        anim.currentX        = anim.startX;
        anim.durationSeconds = (float)durationMs / 1000.0f;
        anim.easing          = easing;
        clock_gettime(CLOCK_MONOTONIC, &anim.startTime);

        m_animations.push_back(anim);
    }

    // Advance every animation by one frame.
    // Writes (id, x) pairs into caller-provided buffers.
    // Finished animations are erased as they are reported (with their
    // final target X so the caller can snap the widget precisely).
    // Returns the number of entries written.
    int stepAll(int* outIds, int* outXs, int maxCount) {
        std::lock_guard<std::mutex> lock(m_mutex);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        int written = 0;
        size_t i = 0;
        while (i < m_animations.size() && written < maxCount) {
            TileAnimationData& a = m_animations[i];

            double elapsed = (now.tv_sec  - a.startTime.tv_sec)
                           + (now.tv_nsec - a.startTime.tv_nsec) / 1e9;

            if (elapsed >= (double)a.durationSeconds) {
                // Report final position, then erase.
                outIds[written] = a.id;
                outXs[written]  = (int)std::lround(a.targetX);
                ++written;
                m_animations.erase(m_animations.begin() + i);
                continue;  // don't advance i — erase shifted elements down
            }

            float p     = (float)(elapsed / (double)a.durationSeconds);
            float eased = apply_easing(p, a.easing);
            a.currentX  = a.startX + (a.targetX - a.startX) * eased;

            outIds[written] = a.id;
            outXs[written]  = (int)std::lround(a.currentX);
            ++written;
            ++i;
        }
        return written;
    }

    void cancel(int tileId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        removeByIdLocked(tileId);
    }

    void cancelAll() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_animations.clear();
    }

    int activeCount() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return (int)m_animations.size();
    }
};

// ---------------------------------------------------------------------------
// C ABI
// ---------------------------------------------------------------------------
extern "C" {

    LauncherAnimator* NewAnimator(void) {
        return new LauncherAnimator();
    }

    void DeleteAnimator(LauncherAnimator* anim) {
        delete anim;
    }

    // easing: 0 = cubic-out (default), 1 = quad-out, 2 = linear
    void TriggerSlide(LauncherAnimator* anim,
                      int tileId, int fx, int tx, int dur, int easing) {
        if (anim) anim->startSlide(tileId, fx, tx, dur, easing);
    }

    // Returns number of (id, x) pairs written to the buffers.
    int StepAllAnimations(LauncherAnimator* anim,
                          int* outIds, int* outXs, int maxCount) {
        if (!anim) return 0;
        return anim->stepAll(outIds, outXs, maxCount);
    }

    void CancelAnimation(LauncherAnimator* anim, int tileId) {
        if (anim) anim->cancel(tileId);
    }

    void CancelAllAnimations(LauncherAnimator* anim) {
        if (anim) anim->cancelAll();
    }

    int GetActiveAnimationCount(LauncherAnimator* anim) {
        return anim ? anim->activeCount() : 0;
    }
}
