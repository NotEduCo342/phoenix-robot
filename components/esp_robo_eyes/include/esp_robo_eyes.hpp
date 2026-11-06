#ifndef ESP_ROBO_EYES_HPP
#define ESP_ROBO_EYES_HPP

#include "st7735.h"
#include <stdint.h>
#include <stdbool.h>

// Mood type constants matching Arduino RoboEyes
enum RoboMood : uint8_t {
    MOOD_DEFAULT = 0,
    MOOD_TIRED   = 1,
    MOOD_ANGRY   = 2,
    MOOD_HAPPY   = 3
};

#ifndef DEFAULT
#define DEFAULT 0
#endif
#ifndef TIRED
#define TIRED 1
#endif
#ifndef ANGRY
#define ANGRY 2
#endif
#ifndef HAPPY
#define HAPPY 3
#endif

#ifndef ON
#define ON 1
#endif
#ifndef OFF
#define OFF 0
#endif

// Predefined positions matching Arduino RoboEyes
#ifndef N
#define N 1   // North, top center
#define NE 2  // North-east, top right
#define E 3   // East, middle right
#define SE 4  // South-east, bottom right
#define S 5   // South, bottom center
#define SW 6  // South-west, bottom left
#define W 7   // West, middle left
#define NW 8  // North-west, top left
#endif

class EspRoboEyes {
public:
    EspRoboEyes();
    ~EspRoboEyes() = default;

    // Startup RoboEyes with ST7735 display, default 160x128 landscape at 50 FPS
    void begin(st7735_t *disp, int width = 160, int height = 128, uint8_t frameRate = 50);

    // Call regularly in loop or task; internally limits redraws to frameInterval
    void update();

    // Force an immediate redraw frame
    void drawEyes();

    // Setters
    void setFramerate(uint8_t fps);
    void setDisplayColors(uint16_t background, uint16_t main);
    void setAutoMoodColors(bool enable);
    void setWidth(uint8_t leftEye, uint8_t rightEye);
    void setHeight(uint8_t leftEye, uint8_t rightEye);
    void setBorderradius(uint8_t leftEye, uint8_t rightEye);
    void setSpacebetween(int space);
    void setMood(uint8_t mood);
    void setPosition(uint8_t position);

    void setAutoblinker(bool active, int interval, int variation);
    void setAutoblinker(bool active);

    void setIdleMode(bool active, int interval, int variation);
    void setIdleMode(bool active);

    void setCuriosity(bool curiousBit);
    void setCyclops(bool cyclopsBit);

    void setHFlicker(bool flickerBit, uint8_t amplitude);
    void setHFlicker(bool flickerBit);

    void setVFlicker(bool flickerBit, uint8_t amplitude);
    void setVFlicker(bool flickerBit);

    void setSweat(bool sweatBit);

    // Basic animation commands
    void close();
    void open();
    void blink();

    void close(bool left, bool right);
    void open(bool left, bool right);
    void blink(bool left, bool right);

    // Macro one-shot animations
    void anim_confused();
    void anim_laugh();

    // Getters
    int getScreenConstraint_X() const;
    int getScreenConstraint_Y() const;

private:
    st7735_t *display;

    uint16_t BGCOLOR;
    uint16_t MAINCOLOR;
    bool autoMoodColors;

    int screenWidth;
    int screenHeight;
    int frameInterval;
    uint64_t fpsTimer;

    // Mood flags
    bool tired;
    bool angry;
    bool happy;
    bool curious;
    bool cyclops;
    bool eyeL_open;
    bool eyeR_open;

    // Geometry - Left Eye
    int eyeLwidthDefault;
    int eyeLheightDefault;
    int eyeLwidthCurrent;
    int eyeLheightCurrent;
    int eyeLwidthNext;
    int eyeLheightNext;
    int eyeLheightOffset;
    uint8_t eyeLborderRadiusDefault;
    uint8_t eyeLborderRadiusCurrent;
    uint8_t eyeLborderRadiusNext;

    // Geometry - Right Eye
    int eyeRwidthDefault;
    int eyeRheightDefault;
    int eyeRwidthCurrent;
    int eyeRheightCurrent;
    int eyeRwidthNext;
    int eyeRheightNext;
    int eyeRheightOffset;
    uint8_t eyeRborderRadiusDefault;
    uint8_t eyeRborderRadiusCurrent;
    uint8_t eyeRborderRadiusNext;

    // Coordinates
    int eyeLxDefault;
    int eyeLyDefault;
    int eyeLx;
    int eyeLy;
    int eyeLxNext;
    int eyeLyNext;

    int eyeRxDefault;
    int eyeRyDefault;
    int eyeRx;
    int eyeRy;
    int eyeRxNext;
    int eyeRyNext;

    // Eyelids
    uint8_t eyelidsHeightMax;
    uint8_t eyelidsTiredHeight;
    uint8_t eyelidsTiredHeightNext;
    uint8_t eyelidsAngryHeight;
    uint8_t eyelidsAngryHeightNext;

    uint8_t eyelidsHappyBottomOffsetMax;
    uint8_t eyelidsHappyBottomOffset;
    uint8_t eyelidsHappyBottomOffsetNext;

    int spaceBetweenDefault;
    int spaceBetweenCurrent;
    int spaceBetweenNext;

    // Animations
    bool hFlicker;
    bool hFlickerAlternate;
    uint8_t hFlickerAmplitude;

    bool vFlicker;
    bool vFlickerAlternate;
    uint8_t vFlickerAmplitude;

    bool autoblinker;
    int blinkInterval;
    int blinkIntervalVariation;
    uint64_t blinktimer;

    bool idle;
    int idleInterval;
    int idleIntervalVariation;
    uint64_t idleAnimationTimer;

    bool confused;
    uint64_t confusedAnimationTimer;
    int confusedAnimationDuration;
    bool confusedToggle;

    bool laugh;
    uint64_t laughAnimationTimer;
    int laughAnimationDuration;
    bool laughToggle;

    bool sweat;
    uint8_t sweatBorderradius;

    int sweat1XPosInitial;
    int sweat1XPos;
    float sweat1YPos;
    int sweat1YPosMax;
    float sweat1Height;
    float sweat1Width;

    int sweat2XPosInitial;
    int sweat2XPos;
    float sweat2YPos;
    int sweat2YPosMax;
    float sweat2Height;
    float sweat2Width;

    int sweat3XPosInitial;
    int sweat3XPos;
    float sweat3YPos;
    int sweat3YPosMax;
    float sweat3Height;
    float sweat3Width;

    static uint64_t get_millis();
    static uint32_t get_random(uint32_t max);
    static uint32_t get_random_range(uint32_t min, uint32_t max);
};

#endif // ESP_ROBO_EYES_HPP
