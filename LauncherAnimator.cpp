#include <time.h>
#include <cmath>
#include <vector>
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

public:
    LauncherAnimator() {}
    ~LauncherAnimator() {}

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
        anim.currentTime = 0.0f; 
        anim.durationSeconds = (float)durationMs / 1000.0f;
        anim.isFinished = false;

        m_animations.push_back(anim);
    }

    int updateCalculationsDirect(int tileId, float deltaTime) {
        for (size_t i = 0; i < m_animations.size(); ++i) {
            if (m_animations[i].id == tileId) {
                if (m_animations[i].isFinished) {
                    m_animations.erase(m_animations.begin() + i);
                    return -2; // Loop complete status boundary code
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

                return static_cast<int>(std::round(m_animations[i].currentX));
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
    
    int GetCurrentFrameXAdvanced(LauncherAnimator* anim, int tileId, float deltaTime) {
        if (anim) return anim->updateCalculationsDirect(tileId, deltaTime);
        return -1;
    }
}
