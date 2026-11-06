#include "esp_robo_eyes.hpp"
#include "esp_timer.h"
#include "esp_random.h"
#include <string.h>

uint64_t EspRoboEyes::get_millis() {
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

uint32_t EspRoboEyes::get_random(uint32_t max) {
    if (max == 0) return 0;
    return esp_random() % max;
}

uint32_t EspRoboEyes::get_random_range(uint32_t min, uint32_t max) {
    if (min >= max) return min;
    return min + (esp_random() % (max - min));
}

EspRoboEyes::EspRoboEyes() :
    display(nullptr),
    BGCOLOR(COLOR_BLACK),
    MAINCOLOR(COLOR_CYAN),
    autoMoodColors(true),
    screenWidth(160),
    screenHeight(128),
    frameInterval(20),
    fpsTimer(0),
    tired(false),
    angry(false),
    happy(false),
    curious(false),
    cyclops(false),
    eyeL_open(false),
    eyeR_open(false),
    eyeLwidthDefault(46),
    eyeLheightDefault(48),
    eyeLwidthCurrent(46),
    eyeLheightCurrent(1),
    eyeLwidthNext(46),
    eyeLheightNext(48),
    eyeLheightOffset(0),
    eyeLborderRadiusDefault(12),
    eyeLborderRadiusCurrent(12),
    eyeLborderRadiusNext(12),
    eyeRwidthDefault(46),
    eyeRheightDefault(48),
    eyeRwidthCurrent(46),
    eyeRheightCurrent(1),
    eyeRwidthNext(46),
    eyeRheightNext(48),
    eyeRheightOffset(0),
    eyeRborderRadiusDefault(12),
    eyeRborderRadiusCurrent(12),
    eyeRborderRadiusNext(12),
    eyelidsHeightMax(24),
    eyelidsTiredHeight(0),
    eyelidsTiredHeightNext(0),
    eyelidsAngryHeight(0),
    eyelidsAngryHeightNext(0),
    eyelidsHappyBottomOffsetMax(27),
    eyelidsHappyBottomOffset(0),
    eyelidsHappyBottomOffsetNext(0),
    spaceBetweenDefault(16),
    spaceBetweenCurrent(16),
    spaceBetweenNext(16),
    hFlicker(false),
    hFlickerAlternate(false),
    hFlickerAmplitude(3),
    vFlicker(false),
    vFlickerAlternate(false),
    vFlickerAmplitude(12),
    autoblinker(false),
    blinkInterval(1),
    blinkIntervalVariation(4),
    blinktimer(0),
    idle(false),
    idleInterval(1),
    idleIntervalVariation(3),
    idleAnimationTimer(0),
    confused(false),
    confusedAnimationTimer(0),
    confusedAnimationDuration(500),
    confusedToggle(true),
    laugh(false),
    laughAnimationTimer(0),
    laughAnimationDuration(500),
    laughToggle(true),
    sweat(false),
    sweatBorderradius(4),
    sweat1XPosInitial(4), sweat1XPos(4), sweat1YPos(4.0f), sweat1YPosMax(25), sweat1Height(3.0f), sweat1Width(2.0f),
    sweat2XPosInitial(4), sweat2XPos(4), sweat2YPos(4.0f), sweat2YPosMax(25), sweat2Height(3.0f), sweat2Width(2.0f),
    sweat3XPosInitial(4), sweat3XPos(4), sweat3YPos(4.0f), sweat3YPosMax(25), sweat3Height(3.0f), sweat3Width(2.0f)
{
    eyeLxDefault = (screenWidth - (eyeLwidthDefault + spaceBetweenDefault + eyeRwidthDefault)) / 2;
    eyeLyDefault = (screenHeight - eyeLheightDefault) / 2;
    eyeLx = eyeLxDefault;
    eyeLy = eyeLyDefault;
    eyeLxNext = eyeLx;
    eyeLyNext = eyeLy;

    eyeRxDefault = eyeLx + eyeLwidthCurrent + spaceBetweenDefault;
    eyeRyDefault = eyeLy;
    eyeRx = eyeRxDefault;
    eyeRy = eyeRyDefault;
    eyeRxNext = eyeRx;
    eyeRyNext = eyeRy;
}

void EspRoboEyes::begin(st7735_t *disp, int width, int height, uint8_t frameRate) {
    display = disp;
    screenWidth = width;
    screenHeight = height;
    if (display) {
        st7735_clear(display, BGCOLOR);
        st7735_display(display);
    }
    eyeLheightCurrent = 1;
    eyeRheightCurrent = 1;
    setFramerate(frameRate);
    open();
}

void EspRoboEyes::setFramerate(uint8_t fps) {
    if (fps == 0) fps = 50;
    frameInterval = 1000 / fps;
}

void EspRoboEyes::setDisplayColors(uint16_t background, uint16_t main) {
    BGCOLOR = background;
    MAINCOLOR = main;
    autoMoodColors = false; // Manual color override
}

void EspRoboEyes::setAutoMoodColors(bool enable) {
    autoMoodColors = enable;
}

void EspRoboEyes::setWidth(uint8_t leftEye, uint8_t rightEye) {
    eyeLwidthNext = leftEye;
    eyeRwidthNext = rightEye;
    eyeLwidthDefault = leftEye;
    eyeRwidthDefault = rightEye;
}

void EspRoboEyes::setHeight(uint8_t leftEye, uint8_t rightEye) {
    eyeLheightNext = leftEye;
    eyeRheightNext = rightEye;
    eyeLheightDefault = leftEye;
    eyeRheightDefault = rightEye;
}

void EspRoboEyes::setBorderradius(uint8_t leftEye, uint8_t rightEye) {
    eyeLborderRadiusNext = leftEye;
    eyeRborderRadiusNext = rightEye;
    eyeLborderRadiusDefault = leftEye;
    eyeRborderRadiusDefault = rightEye;
}

void EspRoboEyes::setSpacebetween(int space) {
    spaceBetweenNext = space;
    spaceBetweenDefault = space;
}

void EspRoboEyes::setMood(uint8_t mood) {
    switch (mood) {
    case TIRED:
        tired = true;
        angry = false;
        happy = false;
        if (autoMoodColors) MAINCOLOR = COLOR_SOFT_BLUE;
        break;
    case ANGRY:
        tired = false;
        angry = true;
        happy = false;
        if (autoMoodColors) MAINCOLOR = COLOR_RED;
        break;
    case HAPPY:
        tired = false;
        angry = false;
        happy = true;
        if (autoMoodColors) MAINCOLOR = COLOR_GREEN;
        break;
    default:
        tired = false;
        angry = false;
        happy = false;
        if (autoMoodColors) MAINCOLOR = COLOR_CYAN;
        break;
    }
}

int EspRoboEyes::getScreenConstraint_X() const {
    return screenWidth - eyeLwidthCurrent - spaceBetweenCurrent - eyeRwidthCurrent;
}

int EspRoboEyes::getScreenConstraint_Y() const {
    return screenHeight - eyeLheightDefault;
}

void EspRoboEyes::setPosition(uint8_t position) {
    switch (position) {
    case N:
        eyeLxNext = getScreenConstraint_X() / 2;
        eyeLyNext = 0;
        break;
    case NE:
        eyeLxNext = getScreenConstraint_X();
        eyeLyNext = 0;
        break;
    case E:
        eyeLxNext = getScreenConstraint_X();
        eyeLyNext = getScreenConstraint_Y() / 2;
        break;
    case SE:
        eyeLxNext = getScreenConstraint_X();
        eyeLyNext = getScreenConstraint_Y();
        break;
    case S:
        eyeLxNext = getScreenConstraint_X() / 2;
        eyeLyNext = getScreenConstraint_Y();
        break;
    case SW:
        eyeLxNext = 0;
        eyeLyNext = getScreenConstraint_Y();
        break;
    case W:
        eyeLxNext = 0;
        eyeLyNext = getScreenConstraint_Y() / 2;
        break;
    case NW:
        eyeLxNext = 0;
        eyeLyNext = 0;
        break;
    default:
        eyeLxNext = getScreenConstraint_X() / 2;
        eyeLyNext = getScreenConstraint_Y() / 2;
        break;
    }
}

void EspRoboEyes::setAutoblinker(bool active, int interval, int variation) {
    autoblinker = active;
    blinkInterval = interval;
    blinkIntervalVariation = variation;
}

void EspRoboEyes::setAutoblinker(bool active) {
    autoblinker = active;
}

void EspRoboEyes::setIdleMode(bool active, int interval, int variation) {
    idle = active;
    idleInterval = interval;
    idleIntervalVariation = variation;
}

void EspRoboEyes::setIdleMode(bool active) {
    idle = active;
}

void EspRoboEyes::setCuriosity(bool curiousBit) {
    curious = curiousBit;
}

void EspRoboEyes::setCyclops(bool cyclopsBit) {
    cyclops = cyclopsBit;
}

void EspRoboEyes::setHFlicker(bool flickerBit, uint8_t amplitude) {
    hFlicker = flickerBit;
    hFlickerAmplitude = amplitude;
}

void EspRoboEyes::setHFlicker(bool flickerBit) {
    hFlicker = flickerBit;
}

void EspRoboEyes::setVFlicker(bool flickerBit, uint8_t amplitude) {
    vFlicker = flickerBit;
    vFlickerAmplitude = amplitude;
}

void EspRoboEyes::setVFlicker(bool flickerBit) {
    vFlicker = flickerBit;
}

void EspRoboEyes::setSweat(bool sweatBit) {
    sweat = sweatBit;
}

void EspRoboEyes::close() {
    eyeLheightNext = 1;
    eyeRheightNext = 1;
    eyeL_open = false;
    eyeR_open = false;
}

void EspRoboEyes::open() {
    eyeL_open = true;
    eyeR_open = true;
}

void EspRoboEyes::blink() {
    close();
    open();
}

void EspRoboEyes::close(bool left, bool right) {
    if (left) {
        eyeLheightNext = 1;
        eyeL_open = false;
    }
    if (right) {
        eyeRheightNext = 1;
        eyeR_open = false;
    }
}

void EspRoboEyes::open(bool left, bool right) {
    if (left)  eyeL_open = true;
    if (right) eyeR_open = true;
}

void EspRoboEyes::blink(bool left, bool right) {
    close(left, right);
    open(left, right);
}

void EspRoboEyes::anim_confused() {
    confused = true;
}

void EspRoboEyes::anim_laugh() {
    laugh = true;
}

void EspRoboEyes::update() {
    uint64_t now = get_millis();
    if (now - fpsTimer >= (uint64_t)frameInterval) {
        drawEyes();
        fpsTimer = now;
    }
}

void EspRoboEyes::drawEyes() {
    if (!display) return;
    uint64_t now = get_millis();

    // 1. Curious gaze: outer eye height offset
    if (curious) {
        if (eyeLxNext <= 10) {
            eyeLheightOffset = 10;
        } else if (eyeLxNext >= (getScreenConstraint_X() - 10) && cyclops) {
            eyeLheightOffset = 10;
        } else {
            eyeLheightOffset = 0;
        }

        if (eyeRxNext >= screenWidth - eyeRwidthCurrent - 10) {
            eyeRheightOffset = 10;
        } else {
            eyeRheightOffset = 0;
        }
    } else {
        eyeLheightOffset = 0;
        eyeRheightOffset = 0;
    }

    // 2. Exponential smoothing for eye height
    eyeLheightCurrent = (eyeLheightCurrent + eyeLheightNext + eyeLheightOffset) / 2;
    eyeLy += ((eyeLheightDefault - eyeLheightCurrent) / 2);
    eyeLy -= eyeLheightOffset / 2;

    eyeRheightCurrent = (eyeRheightCurrent + eyeRheightNext + eyeRheightOffset) / 2;
    eyeRy += ((eyeRheightDefault - eyeRheightCurrent) / 2);
    eyeRy -= eyeRheightOffset / 2;

    // Open eyes again after closing
    if (eyeL_open && eyeLheightCurrent <= 1 + eyeLheightOffset) {
        eyeLheightNext = eyeLheightDefault;
    }
    if (eyeR_open && eyeRheightCurrent <= 1 + eyeRheightOffset) {
        eyeRheightNext = eyeRheightDefault;
    }

    // Eye width smoothing
    eyeLwidthCurrent = (eyeLwidthCurrent + eyeLwidthNext) / 2;
    eyeRwidthCurrent = (eyeRwidthCurrent + eyeRwidthNext) / 2;

    // Space between smoothing
    spaceBetweenCurrent = (spaceBetweenCurrent + spaceBetweenNext) / 2;

    // Coordinate smoothing
    eyeLx = (eyeLx + eyeLxNext) / 2;
    eyeLy = (eyeLy + eyeLyNext) / 2;

    eyeRxNext = eyeLxNext + eyeLwidthCurrent + spaceBetweenCurrent;
    eyeRyNext = eyeLyNext;
    eyeRx = (eyeRx + eyeRxNext) / 2;
    eyeRy = (eyeRy + eyeRyNext) / 2;

    // Border radius smoothing
    eyeLborderRadiusCurrent = (eyeLborderRadiusCurrent + eyeLborderRadiusNext) / 2;
    eyeRborderRadiusCurrent = (eyeRborderRadiusCurrent + eyeRborderRadiusNext) / 2;

    // 3. Macro animations
    if (autoblinker) {
        if (now >= blinktimer) {
            blink();
            blinktimer = now + ((uint64_t)blinkInterval * 1000ULL) + ((uint64_t)get_random(blinkIntervalVariation) * 1000ULL);
        }
    }

    if (laugh) {
        if (laughToggle) {
            setVFlicker(true, 6);
            laughAnimationTimer = now;
            laughToggle = false;
        } else if (now >= laughAnimationTimer + (uint64_t)laughAnimationDuration) {
            setVFlicker(false, 0);
            laughToggle = true;
            laugh = false;
        }
    }

    if (confused) {
        if (confusedToggle) {
            setHFlicker(true, 24);
            confusedAnimationTimer = now;
            confusedToggle = false;
        } else if (now >= confusedAnimationTimer + (uint64_t)confusedAnimationDuration) {
            setHFlicker(false, 0);
            confusedToggle = true;
            confused = false;
        }
    }

    if (idle) {
        if (now >= idleAnimationTimer) {
            int cx = getScreenConstraint_X();
            int cy = getScreenConstraint_Y();
            eyeLxNext = (cx > 0) ? get_random(cx) : 0;
            eyeLyNext = (cy > 0) ? get_random(cy) : 0;
            idleAnimationTimer = now + ((uint64_t)idleInterval * 1000ULL) + ((uint64_t)get_random(idleIntervalVariation) * 1000ULL);
        }
    }

    if (hFlicker) {
        if (hFlickerAlternate) {
            eyeLx += hFlickerAmplitude;
            eyeRx += hFlickerAmplitude;
        } else {
            eyeLx -= hFlickerAmplitude;
            eyeRx -= hFlickerAmplitude;
        }
        hFlickerAlternate = !hFlickerAlternate;
    }

    if (vFlicker) {
        if (vFlickerAlternate) {
            eyeLy += vFlickerAmplitude;
            eyeRy += vFlickerAmplitude;
        } else {
            eyeLy -= vFlickerAmplitude;
            eyeRy -= vFlickerAmplitude;
        }
        vFlickerAlternate = !vFlickerAlternate;
    }

    if (cyclops) {
        eyeRwidthCurrent = 0;
        eyeRheightCurrent = 0;
        spaceBetweenCurrent = 0;
    }

    // 4. Actual Rendering into 16-bit Framebuffer
    st7735_clear(display, BGCOLOR);

    // Draw base eye rounded rectangles
    st7735_fill_round_rect(display, eyeLx, eyeLy, eyeLwidthCurrent, eyeLheightCurrent, eyeLborderRadiusCurrent, MAINCOLOR);
    if (!cyclops) {
        st7735_fill_round_rect(display, eyeRx, eyeRy, eyeRwidthCurrent, eyeRheightCurrent, eyeRborderRadiusCurrent, MAINCOLOR);
    }

    // Mood targets
    if (tired) {
        eyelidsTiredHeightNext = eyeLheightCurrent / 2;
        eyelidsAngryHeightNext = 0;
    } else {
        eyelidsTiredHeightNext = 0;
    }

    if (angry) {
        eyelidsAngryHeightNext = eyeLheightCurrent / 2;
        eyelidsTiredHeightNext = 0;
    } else {
        eyelidsAngryHeightNext = 0;
    }

    if (happy) {
        eyelidsHappyBottomOffsetNext = eyeLheightCurrent / 2;
    } else {
        eyelidsHappyBottomOffsetNext = 0;
    }

    // Draw tired top eyelids (cut from background)
    eyelidsTiredHeight = (eyelidsTiredHeight + eyelidsTiredHeightNext) / 2;
    if (!cyclops) {
        st7735_fill_triangle(display, eyeLx, eyeLy - 1, eyeLx + eyeLwidthCurrent, eyeLy - 1, eyeLx, eyeLy + eyelidsTiredHeight - 1, BGCOLOR);
        st7735_fill_triangle(display, eyeRx, eyeRy - 1, eyeRx + eyeRwidthCurrent, eyeRy - 1, eyeRx + eyeRwidthCurrent, eyeRy + eyelidsTiredHeight - 1, BGCOLOR);
    } else {
        st7735_fill_triangle(display, eyeLx, eyeLy - 1, eyeLx + (eyeLwidthCurrent / 2), eyeLy - 1, eyeLx, eyeLy + eyelidsTiredHeight - 1, BGCOLOR);
        st7735_fill_triangle(display, eyeLx + (eyeLwidthCurrent / 2), eyeLy - 1, eyeLx + eyeLwidthCurrent, eyeLy - 1, eyeLx + eyeLwidthCurrent, eyeLy + eyelidsTiredHeight - 1, BGCOLOR);
    }

    // Draw angry top eyelids (cut from background)
    eyelidsAngryHeight = (eyelidsAngryHeight + eyelidsAngryHeightNext) / 2;
    if (!cyclops) {
        st7735_fill_triangle(display, eyeLx, eyeLy - 1, eyeLx + eyeLwidthCurrent, eyeLy - 1, eyeLx + eyeLwidthCurrent, eyeLy + eyelidsAngryHeight - 1, BGCOLOR);
        st7735_fill_triangle(display, eyeRx, eyeRy - 1, eyeRx + eyeRwidthCurrent, eyeRy - 1, eyeRx, eyeRy + eyelidsAngryHeight - 1, BGCOLOR);
    } else {
        st7735_fill_triangle(display, eyeLx, eyeLy - 1, eyeLx + (eyeLwidthCurrent / 2), eyeLy - 1, eyeLx + (eyeLwidthCurrent / 2), eyeLy + eyelidsAngryHeight - 1, BGCOLOR);
        st7735_fill_triangle(display, eyeLx + (eyeLwidthCurrent / 2), eyeLy - 1, eyeLx + eyeLwidthCurrent, eyeLy - 1, eyeLx + (eyeLwidthCurrent / 2), eyeLy + eyelidsAngryHeight - 1, BGCOLOR);
    }

    // Draw happy bottom eyelids (curved upward squint cut from background)
    eyelidsHappyBottomOffset = (eyelidsHappyBottomOffset + eyelidsHappyBottomOffsetNext) / 2;
    st7735_fill_round_rect(display, eyeLx - 1, (eyeLy + eyeLheightCurrent) - eyelidsHappyBottomOffset + 1, eyeLwidthCurrent + 2, eyeLheightDefault, eyeLborderRadiusCurrent, BGCOLOR);
    if (!cyclops) {
        st7735_fill_round_rect(display, eyeRx - 1, (eyeRy + eyeRheightCurrent) - eyelidsHappyBottomOffset + 1, eyeRwidthCurrent + 2, eyeRheightDefault, eyeRborderRadiusCurrent, BGCOLOR);
    }

    // Sweat drops
    if (sweat) {
        uint16_t sweatColor = COLOR_WHITE;
        if (sweat1YPos <= sweat1YPosMax) {
            sweat1YPos += 0.5f;
        } else {
            sweat1XPosInitial = get_random(40);
            sweat1YPos = 4.0f;
            sweat1YPosMax = get_random(15) + 15;
            sweat1Width = 2.0f;
            sweat1Height = 3.0f;
        }
        if (sweat1YPos <= sweat1YPosMax / 2) {
            sweat1Width += 0.5f;
            sweat1Height += 0.5f;
        } else {
            sweat1Width -= 0.1f;
            sweat1Height -= 0.5f;
        }
        sweat1XPos = sweat1XPosInitial - (int)(sweat1Width / 2.0f);
        st7735_fill_round_rect(display, sweat1XPos, (int)sweat1YPos, (int)sweat1Width, (int)sweat1Height, sweatBorderradius, sweatColor);

        if (sweat2YPos <= sweat2YPosMax) {
            sweat2YPos += 0.5f;
        } else {
            sweat2XPosInitial = get_random(screenWidth - 80) + 40;
            sweat2YPos = 4.0f;
            sweat2YPosMax = get_random(15) + 15;
            sweat2Width = 2.0f;
            sweat2Height = 3.0f;
        }
        if (sweat2YPos <= sweat2YPosMax / 2) {
            sweat2Width += 0.5f;
            sweat2Height += 0.5f;
        } else {
            sweat2Width -= 0.1f;
            sweat2Height -= 0.5f;
        }
        sweat2XPos = sweat2XPosInitial - (int)(sweat2Width / 2.0f);
        st7735_fill_round_rect(display, sweat2XPos, (int)sweat2YPos, (int)sweat2Width, (int)sweat2Height, sweatBorderradius, sweatColor);

        if (sweat3YPos <= sweat3YPosMax) {
            sweat3YPos += 0.5f;
        } else {
            sweat3XPosInitial = (screenWidth - 40) + get_random(40);
            sweat3YPos = 4.0f;
            sweat3YPosMax = get_random(15) + 15;
            sweat3Width = 2.0f;
            sweat3Height = 3.0f;
        }
        if (sweat3YPos <= sweat3YPosMax / 2) {
            sweat3Width += 0.5f;
            sweat3Height += 0.5f;
        } else {
            sweat3Width -= 0.1f;
            sweat3Height -= 0.5f;
        }
        sweat3XPos = sweat3XPosInitial - (int)(sweat3Width / 2.0f);
        st7735_fill_round_rect(display, sweat3XPos, (int)sweat3YPos, (int)sweat3Width, (int)sweat3Height, sweatBorderradius, sweatColor);
    }

    st7735_display(display);
}
