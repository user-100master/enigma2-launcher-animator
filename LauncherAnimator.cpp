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

class eMainloop {
public:
    virtual ~eMainloop() {}
    void startTimer(int msecs) {}
    void stopTimer() {}
};

class LauncherAnimator : public eMainloop {
public:
    eWidget* targetWidget;
    int startX, targetX, currentStep, totalSteps;

    LauncherAnimator() : targetWidget(0), startX(0), targetX(0), currentStep(0), totalSteps(0) {}

    void startSlide(unsigned long widgetPointer, int fromX, int toX, int durationMs) {
        targetWidget = (eWidget*)widgetPointer;
        if (!targetWidget) return;

        startX = fromX;
        targetX = toX;
        currentStep = 0;
        totalSteps = durationMs / 16; 
        if (totalSteps < 1) totalSteps = 1;
        
        this->startTimer(16); 
    }

    void triggerTickStep() {
        if (!targetWidget) return;

        if (currentStep <= totalSteps) {
            float progress = (float)currentStep / totalSteps;
            float eased = 1.0f - (1.0f - progress) * (1.0f - progress);
            
            int currentX = startX + static_cast<int>((targetX - startX) * eased);
            
            try {
                targetWidget->move(ePoint(currentX, targetWidget->position().m_y));
            } catch (...) {
                this->stopTimer();
                return;
            }
            
            currentStep++;
            this->startTimer(16); 
        } else {
            this->stopTimer();
        }
    }
};

extern "C" {
    LauncherAnimator* NewAnimator() { return new LauncherAnimator(); }
    void TriggerSlide(LauncherAnimator* anim, unsigned long ptr, int fx, int tx, int dur) {
        if (anim) anim->startSlide(ptr, fx, tx, dur);
    }
    void StepTick(LauncherAnimator* anim) {
        if (anim) anim->triggerTickStep();
    }
}
