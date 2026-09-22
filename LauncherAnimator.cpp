#include <time.h>
#include <vector>

struct TileAnimationData {
    int id;
    float startX;
    float targetX;
    float currentX;
    struct timespec startTime;
    float durationSeconds;
    bool isFinished;
};

class LauncherAnimator {
private:
    std::vector<TileAnimationData> m_animations;

public:
    LauncherAnimator() {}

    void startSlide(int tileId, int fromX, int toX, int durationMs) {
        // Erase any older animation with the same ID to prevent layout fights
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

    // HIGH SPEED MATHEMATICAL TICK ENGINE
    // Calculates float positions for all running tiles instantly
    int updateCalculations(int tileId) {
        struct timespec currentTime;
        clock_gettime(CLOCK_MONOTONIC, &currentTime);

        for (auto& anim : m_animations) {
            if (anim.id == tileId) {
                if (anim.isFinished) return (int)anim.targetX;

                float elapsed = (currentTime.tv_sec - anim.startTime.tv_sec) + 
                                (currentTime.tv_nsec - anim.startTime.tv_nsec) / 1000000000.0f;

                if (elapsed >= anim.durationSeconds) {
                    anim.isFinished = true;
                    return (int)anim.targetX;
                }

                float progress = elapsed / anim.durationSeconds;
                
                // PREMIUM CUBIC EASE-OUT MATH (Android UI Style)
                float eased = 1.0f - (1.0f - progress) * (1.0f - progress) * (1.0f - progress);
                anim.currentX = anim.startX + (anim.targetX - anim.startX) * eased;
                
                return (int)anim.currentX;
            }
        }
        return -1; // Animation not found or completed
    }
};

extern "C" {
    LauncherAnimator* NewAnimator() { return new LauncherAnimator(); }
    void DeleteAnimator(LauncherAnimator* anim) { if (anim) delete anim; }
    
    void TriggerSlide(LauncherAnimator* anim, int tileId, int fx, int tx, int dur) {
        if (anim) anim->startSlide(tileId, fx, tx, dur);
    }
    
    int GetCurrentFrameX(LauncherAnimator* anim, int tileId) {
        if (anim) return anim->updateCalculations(tileId);
        return -1;
    }
}
