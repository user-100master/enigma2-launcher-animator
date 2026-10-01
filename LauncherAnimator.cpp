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
    int updateCalculations(int tileId) {
        struct timespec currentTime;
        clock_gettime(CLOCK_MONOTONIC, &currentTime);

        for (size_t i = 0; i < m_animations.size(); ++i) {
            if (m_animations[i].id == tileId) {
                // If it was marked finished on a previous frame, return final location
                // and erase it now so the next query returns -1
                if (m_animations[i].isFinished) {
                    int finalX = (int)m_animations[i].targetX;
                    m_animations.erase(m_animations.begin() + i);
                    return finalX;
                }

                float elapsed = (currentTime.tv_sec - m_animations[i].startTime.tv_sec) + 
                                (currentTime.tv_nsec - m_animations[i].startTime.tv_nsec) / 1000000000.0f;

                if (elapsed >= m_animations[i].durationSeconds) {
                    m_animations[i].isFinished = true;
                    return (int)m_animations[i].targetX; // Return final target position frame
                }

                float progress = elapsed / m_animations[i].durationSeconds;
                
                // PREMIUM CUBIC EASE-OUT MATH
                float eased = 1.0f - (1.0f - progress) * (1.0f - progress) * (1.0f - progress);
                m_animations[i].currentX = m_animations[i].startX + (m_animations[i].targetX - m_animations[i].startX) * eased;
                
                return (int)m_animations[i].currentX;
            }
        }
        return -1; // Animation naturally not found or explicitly removed out of queue
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
