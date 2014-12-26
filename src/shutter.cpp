/*
 *  shutter.cpp
 *  Timelapse+
 *
 *  Created by Elijah Parker
 *  Copyright 2012 Timelapse+
 *	Licensed under GPLv3
 *
 */

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/version.h>
#include <avr/eeprom.h>
#include <string.h>

#include "tldefs.h"
#include "shutter.h"
#include "clock.h"
#include "hardware.h"
#include "IR.h"
#include "bluetooth.h"
#include "debug.h"
#include "math.h"
#include "settings.h"
#include "PTP_Driver.h"
#include "PTP.h"
#include "timelapseplus.h"
#include "light.h"
#include "5110LCD.h"
#include "button.h"
#include "Menu.h"

#define RUN_DELAY 0
#define RUN_BULB 1
#define RUN_PHOTO 2
#define RUN_GAP 3
#define RUN_NEXT 4
#define RUN_END 5
#define RUN_ERROR 6

extern IR ir;
extern PTP camera;
extern settings_t conf;
extern shutter timer;
extern BT bt;
extern Clock clock;
extern Light light;
extern MENU menu;
extern LCD lcd;
extern Button button;

volatile unsigned char state;
const uint16_t settings_warn_time = 0;
const uint16_t settings_mirror_up_time = 3;
volatile char cable_connected; // 1 = cable connected, 0 = disconnected

char shutter_state, ir_shutter_state; // used only for momentary toggle mode //
uint32_t BulbMax; // calculated during bulb ramp mode
program stored[MAX_STORED+1]EEMEM;
uint8_t BulbMaxEv;

uint8_t lastShutterError = 0;
uint8_t usbPrimary = 0;

const char STR_BULBMODE_ALERT[]PROGMEM = "Please make sure the camera is in bulb mode before continuing";
const char STR_BULBSUPPORT_ALERT[]PROGMEM = "Bulb mode not supported via USB. Use an adaptor cable with the USB";
const char STR_MEMORYSPACE_ALERT[]PROGMEM = "Please confirm there is enough space on the memory card in the camera";
const char STR_APERTURE_ALERT[]PROGMEM = "Note that using an aperture other than the maximum without lens-twist can cause flicker";
const char STR_DEVMODE_ALERT[]PROGMEM = "DEV Mode can interfere with the light sensor. Please disable before continuing";
const char STR_ZEROLENGTH_ALERT[]PROGMEM = "Length greater than zero required in bulb ramp mode. Set the length before running";
const char STR_NOCAMERA_ALERT[]PROGMEM = "Camera not connected. Connect a camera or press ok to use the IR interface";

keyframeGroup_t kfg;

/******************************************************************
 *
 *   shutter::shutter
 *
 *
 ******************************************************************/

shutter::shutter()
{
    if(eeprom_read_byte((const uint8_t*)&stored[0].Name[0]) == 255) 
        setDefault();
    
    restoreCurrent(); // Default Program //
    ENABLE_MIRROR;
    ENABLE_SHUTTER;
    CHECK_CABLE;
    shutter_state = 0;
    ir_shutter_state = 0;
}

/******************************************************************
 *
 *   shutter::setDefault
 *
 *
 ******************************************************************/

void shutter::setDefault()
{
    current.Name[0] = 'D';
    current.Name[1] = 'E';
    current.Name[2] = 'F';
    current.Name[3] = 'A';
    current.Name[4] = 'U';
    current.Name[5] = 'L';
    current.Name[6] = 'T';
    current.Name[7] = '\0';
    current.Delay = 5;
    current.Photos = 10;
    current.Gap = 20;
    current.GapMin = BRAMP_INTERVAL_MIN;
    current.IntervalMode = INTERVAL_MODE_FIXED;
    current.Exps = 3;
    current.Mode = MODE_TIMELAPSE;
    current.Exp = 46;
    current.ArbExp = 100;
    current.Bracket = 6;
    current.Keyframes = 1;
    current.Duration = 60;  //J.R.
    current.BulbStart = 53;
    current.Bulb[0] = 36;
    current.Key[0] = 3600;
    current.brampMethod = BRAMP_METHOD_AUTO;
    current.nightMode = BRAMP_TARGET_AUTO;
    current.nightISO = 31;
    current.nightShutter = 31;
    current.nightAperture = 9;
    save(0);

    uint8_t i;
    for(i = 1; i < MAX_STORED; i++)
    {
        eeprom_write_byte((uint8_t*)&stored[i].Name[0],  255);
    }

    saveCurrent();
}

/******************************************************************
 *
 *   shutter::off
 *
 *
 ******************************************************************/

void shutter::off(void)
{
    shutter_off();
}
void shutter_off(void)
{
    if(conf.devMode) 
        hardware_flashlight(0);
    
    SHUTTER_CLOSE;
    MIRROR_DOWN; 
    clock.in(20, &check_cable);
    ir_shutter_state = 0;
    shutter_state = 0;
}
void shutter_off_quick(void)
{
    SHUTTER_CLOSE;
    MIRROR_DOWN;
    ir_shutter_state = 0;
    shutter_state = 0;
}

/******************************************************************
 *
 *   shutter::half
 *
 *
 ******************************************************************/

void shutter::half(void)
{
    shutter_half();
}
void shutter_half(void)
{
    shutter_off(); // first we completely release the shutter button since some cameras need this to release the bulb

    if(conf.camera.halfPress == HALF_PRESS_ENABLED) clock.in(30, &shutter_half_delayed);
}
void shutter_half_delayed(void)
{
    if(conf.devMode) 
        hardware_flashlight(0);
    
    SHUTTER_CLOSE;
    MIRROR_UP;
}

/******************************************************************
 *
 *   shutter::full
 *
 *
 ******************************************************************/

void shutter::full(void)
{
    shutter_full();
}
void shutter_full(void)
{
    if(conf.devMode) 
        hardware_flashlight(1);

    MIRROR_UP;
    SHUTTER_OPEN;
}

/******************************************************************
 *
 *   shutter::bulbStart
 *
 *
 ******************************************************************/

void shutter::bulbStart(void)
{
    shutter_bulbStart();
}
void shutter_bulbStart(void)
{
    lastShutterError = 0;
    if((cable_connected == 0 || usbPrimary || !(conf.camera.interface & INTERFACE_CABLE)) && ir_shutter_state != 1)
    {
        if(camera.supports.bulb && (conf.camera.interface & INTERFACE_USB))
        {
            lastShutterError = camera.bulbMode();
            if(lastShutterError) return;
            lastShutterError = camera.bulbStart();
        }
        else if(conf.camera.interface & INTERFACE_IR && !usbPrimary)
        {
            ir_shutter_state = 1;
            ir.bulbStart();
        }
    }
    else
    {
        //if(camera.supports.capture) camera.busy = true;
    }

    if(conf.camera.interface & INTERFACE_CABLE && !usbPrimary)
    {
        if(conf.camera.bulbMode == 0)
        {
            shutter_full();
        } 
        else if(conf.camera.bulbMode == 1 && shutter_state != 1)
        {
            shutter_full();
            shutter_state = 1;
            if(camera.ready)
                clock.in(SHUTTER_PRESS_TIME, &shutter_off);
            else
                clock.in(SHUTTER_PRESS_TIME, &shutter_half);
        }
    }
}
//Error: 1001:2001
/******************************************************************
 *
 *   shutter::bulbEnd
 *
 *
 ******************************************************************/

void shutter::bulbEnd(void)
{
    shutter_bulbEnd();
}
void shutter_bulbEnd(void)
{
    DEBUG(PSTR("Bulb End: "));
    if(camera.bulb_open || ir_shutter_state == 1)
    {
        if(camera.bulb_open && (conf.camera.interface & INTERFACE_USB))
        {
            DEBUG(PSTR("USB "));
            camera.bulbEnd();
        }
        else if(conf.camera.interface & INTERFACE_IR && !usbPrimary)
        {
            DEBUG(PSTR("IR "));
            ir.bulbEnd();
        }
        ir_shutter_state = 0;
    } 
    if(conf.camera.interface & INTERFACE_CABLE && !usbPrimary)
    {
        if(conf.camera.bulbMode == 0)
        {
            DEBUG(PSTR("CABLE "));
            shutter_off();
        }
        else if(conf.camera.bulbMode == 1 && shutter_state == 1)
        {
            shutter_full();
            shutter_state = 0;
            clock.in(SHUTTER_PRESS_TIME, &shutter_off);
        }
    }
    DEBUG_NL();
}

/******************************************************************
 *
 *   shutter::capture
 *
 *
 ******************************************************************/

void shutter::capture(void)
{
    shutter_capture();
}
void shutter_capture(void)
{
    lastShutterError = 0;
    if(conf.camera.interface & (INTERFACE_CABLE | INTERFACE_USB))
    {
        shutter_full();
        clock.in(SHUTTER_PRESS_TIME, &shutter_off);
        ir_shutter_state = 0;
        shutter_state = 0;
        if(cable_connected == 0)
        {
            if(camera.supports.capture)
            {
                    lastShutterError = camera.capture();
            }
            else
            {
                if(conf.camera.interface & INTERFACE_IR) ir.shutterNow();
            }
        }
        else
        {
            if(camera.supports.capture) camera.busy = true;
        }
    }
    else if(conf.camera.interface == INTERFACE_IR)
    {
        ir.shutterNow();
    }
}

/******************************************************************
 *
 *   shutter::cableIsConnected
 *
 *
 ******************************************************************/

char shutter::cableIsConnected(void)
{
    if(MIRROR_IS_DOWN)
    {
        CHECK_CABLE;
    }
    return cable_connected;
}

/******************************************************************
 *
 *   shutter::save
 *
 *
 ******************************************************************/

void shutter::save(char id)
{
    eeprom_write_block((const void*)&current, &stored[(uint8_t)id], sizeof(program));
    currentId = id;
}

void shutter::saveCurrent()
{
    eeprom_write_block((const void*)&current, &stored[MAX_STORED], sizeof(program));
}
void shutter::restoreCurrent()
{
    eeprom_read_block((void*)&current, &stored[MAX_STORED], sizeof(program));
}

/******************************************************************
 *
 *   shutter::load
 *
 *
 ******************************************************************/

void shutter::load(char id)
{
    eeprom_read_block((void*)&current, &stored[(uint8_t)id], sizeof(program));
    currentId = id;
}

/******************************************************************
 *
 *   shutter::nextId
 *
 *
 ******************************************************************/

int8_t shutter::nextId(void)
{
    int8_t id = -1, i;
    for(i = 1; i < MAX_STORED; i++)
    {
        if(eeprom_read_byte((uint8_t*)&stored[i].Name[0]) == 255)
        {
            id = i;
            break;
        }
    }
    return id;
}

/******************************************************************
 *
 *   shutter::begin
 *
 *
 ******************************************************************/

void shutter::begin()
{
    saveCurrent();
    running = 1;
}

/******************************************************************
 *
 *   shutter::begin
 *
 *
 ******************************************************************/

void shutter::pause(uint8_t p)
{
    if(p)
    {
        if(paused) return;
        pausing = 1;
    }
    else
    {
        if(pausing > 1) return;
        pausing = 1;
        paused = 1;
    }
}

/******************************************************************
 *
 *   shutter::run
 *
 *
 ******************************************************************/

char shutter::task()
{
    char cancel = 0;
    static uint8_t enter, exps, run_state = RUN_DELAY, old_state = 255;
    static uint16_t photos;
    static uint32_t last_photo_end_ms;
    static uint8_t usingUSB;
    static int8_t aperturePausedStart;

    if(MIRROR_IS_DOWN)
    {
        CHECK_CABLE;
    }
    
    if(!running)
    {
        if(enter) 
            cancel = 1;
        else 
            return 0;
    }

    if((enter == 0 || status.preChecked > 0) && cancel == 0) // Initialize variables and setup I/O pins
    {
        enter = 1;
        paused = 0;
        pausing = 0;
        if(current.Mode & RAMP)
        {
            uint32_t tmp = (uint32_t)current.Duration * 10 * 60;  //J.R.
            tmp /= (uint32_t) current.Gap;
            current.Photos = (uint16_t) tmp;
        }
        status.interval = current.Gap;

        usingUSB = camera.ready;

        if(conf.camera.interface == INTERFACE_AUTO && usingUSB && camera.supports.bulb) usbPrimary = 1; else usbPrimary = 0;

        ////////////////////////////// pre check ////////////////////////////////////
        CHECK_ALERT(STR_NOCAMERA_ALERT, conf.camera.interface != INTERFACE_IR && !camera.supports.capture && !cableIsConnected());
        CHECK_ALERT(STR_APERTURE_ALERT, camera.ready && camera.supports.aperture && camera.aperture() != camera.apertureWideOpen() && (current.Mode & TIMELAPSE) && !((current.Mode & RAMP) && (conf.brampMode & BRAMP_MODE_APERTURE)));
        CHECK_ALERT(STR_BULBSUPPORT_ALERT, (current.Mode & RAMP) && camera.ready && !camera.supports.bulb && !cable_connected);
        CHECK_ALERT(STR_BULBMODE_ALERT, (current.Mode & RAMP) && !(camera.isInBulbMode() || conf.extendedRamp));
        CHECK_ALERT(STR_MEMORYSPACE_ALERT, (current.Photos > 0) && camera.ready && camera.photosRemaining && camera.photosRemaining < current.Photos && (current.Mode & TIMELAPSE));
        CHECK_ALERT(STR_DEVMODE_ALERT, (current.Mode & RAMP) && (current.brampMethod == BRAMP_METHOD_AUTO) && conf.devMode);
        CHECK_ALERT(STR_ZEROLENGTH_ALERT, (current.Mode & RAMP) && (current.Duration == 0));

        if(!status.preChecked && menu.waitingAlert())
        {
            menu.blink();
        }

        if(status.preChecked == 0) status.preChecked = 1;
        if(menu.waitingAlert()) return CONTINUE; //////////////////////////////////////////////

        if(status.preChecked == 1)
        {
            
            menu.message(TEXT("Loading"));
            menu.task();

            if(current.Mode & RAMP)
            {
                memset(&pastErrors, 0, sizeof(pastErrors));
                calcBulbMax();
                status.rampMax = calcRampMax();
                status.rampMin = calcRampMin();
                rampRate = 0;
                status.rampStops = 0.0;
                light.integrationStart(conf.lightIntegrationMinutes);
                lightReading = status.lightStart = light.readIntegratedEv();

                if(current.nightMode == BRAMP_TARGET_AUTO)
                {
                    status.nightTarget = 0;
                    status.rampTarget = (float)status.nightTarget;
                }
                else if(current.nightMode == BRAMP_TARGET_CUSTOM)
                {
                    status.nightTarget = calcRampTarget(current.nightShutter, current.nightISO, current.nightAperture);
                    status.rampTarget = (float)status.nightTarget;
                }
                else
                {
                    status.nightTarget = ((int8_t)current.nightMode) - BRAMP_TARGET_OFFSET;
                    status.rampTarget = status.lightStart - (float)status.nightTarget;
                }
                
                DEBUG(STR(" -----> starting ramp stops: "));
                DEBUG(status.rampStops);
                DEBUG(STR(" -----> starting light reading: "));
                DEBUG(status.lightStart);
                DEBUG_NL(); 
                //if(light.underThreshold && current.nightMode != BRAMP_TARGET_AUTO)
                //{
                //    DEBUG(STR(" -----> starting at night target\r\n"));
                //    status.lightStart = status.nightTarget;
                //}
                status.preChecked = 2;
            }
            
            if(!current.Mode & RAMP || current.nightMode == BRAMP_TARGET_AUTO || current.brampMethod != BRAMP_METHOD_AUTO)
            {
                status.preChecked = 3;
            }

            if(camera.supports.aperture) aperture = camera.aperture();
            if(camera.supports.iso) iso = camera.iso();

        }

        if(status.preChecked < 3) return CONTINUE;

        status.preChecked = 0;

        clock.tare();
        clock.reset();

        run_state = RUN_DELAY;
        photos = 0;
        exps = 0;

        current.infinitePhotos = current.Photos == 0 ? 1 : 0;
        status.infinitePhotos = current.infinitePhotos;
        status.photosRemaining = current.Photos;
        status.photosTaken = 0;
        status.mode = (uint8_t) current.Mode;
        last_photo_end_ms = 0;
        last_photo_ms = 0;
        evShift = 0;

        ENABLE_MIRROR;
        ENABLE_SHUTTER;
        SHUTTER_CLOSE;
        MIRROR_DOWN;
    }


    if(pausing > 0 && paused == 1) // delays the restart to prevent camera shake
    {
        if(pausing > 8)
        {
            paused = 0;
            pausing = 0;
            evShift += apertureEvShift;
            status.rampMax -= apertureEvShift;
            status.rampMin -= apertureEvShift;
            apertureEvShift = 0;
            aperturePausedStart = 0;
            apertureReady = 0;
        }
        else
        {
            apertureReady = 0;
            pausing++;
        }
    }

    if(paused)
    {
        if(aperturePausedStart == -1 && camera.supports.aperture && camera.aperture() > 0 && camera.aperture() != 255)
        {
            aperturePausedStart = camera.aperture();
        }
        else if(camera.supports.aperture && aperturePausedStart > 0)
        {
            int8_t rampCurrent = (int8_t) status.rampStops;
            if(status.rampStops > (float) rampCurrent) rampCurrent++;
            else if(status.rampStops < (float) rampCurrent) rampCurrent--;

            int8_t avMax = aperturePausedStart + (status.rampMax - rampCurrent); // max stops darker
            int8_t avMin = aperturePausedStart + (status.rampMin - rampCurrent); // max stops brighter

            if(avMax > conf.apertureMax) avMax = conf.apertureMax;
            if(avMin < conf.apertureMin) avMin = conf.apertureMin;

            DEBUG(PSTR("rampMin: "));
            DEBUG(status.rampMin);
            DEBUG_NL();
            DEBUG(PSTR("rampMax: "));
            DEBUG(status.rampMax);
            DEBUG_NL();
            DEBUG(PSTR("rampCurrent: "));
            DEBUG(rampCurrent);
            DEBUG_NL();
            DEBUG(PSTR("avMin: "));
            DEBUG(avMin);
            DEBUG_NL();
            DEBUG(PSTR("avMax: "));
            DEBUG(avMax);
            DEBUG_NL();

            //if(camera.aperture() > avMax)
            //{
            //    camera.setAperture(avMax);
            //}
            //else if(camera.aperture() < avMin)
            //{
            //    camera.setAperture(avMin);
            //}

            if(camera.aperture() != avMin) camera.setAperture(avMin);

            apertureEvShift = camera.aperture() - aperturePausedStart;
            apertureReady = 1;
        }
        else
        {
            apertureReady = 0;

            // limit any manual changes to the limits
            int8_t rampCurrent = (int8_t) status.rampStops;
            if(apertureEvShift > status.rampMax - rampCurrent) apertureEvShift = status.rampMax - rampCurrent; // max stops darker
            if(apertureEvShift < status.rampMin - rampCurrent) apertureEvShift = status.rampMin - rampCurrent; // max stops brighter

        }
        return CONTINUE;
    }
    if(pausing && run_state != RUN_BULB)
    {
        apertureReady = 0;
        pausing = 0;
        paused = 1;
        apertureEvShift = 0;
        if(!camera.supports.aperture && (current.Mode & RAMP)) aperturePausedStart = -1; else aperturePausedStart = 0;
    }

    /////////////////////////////////////////

    if(cancel)
    {
        run_state = RUN_END;
    }

    /////// RUNNING PROCEDURE ///////
    if(run_state == RUN_DELAY)
    {
        if(old_state != run_state)
        {
            if(conf.debugEnabled)
            {
                DEBUG(PSTR("State: RUN_DELAY"));
                DEBUG_NL();
            }
            strcpy((char *) status.textStatus, TEXT("Delay"));
            old_state = run_state;
        }


        if(((unsigned long)clock.event_ms / 1000) > current.Delay)
        {
            clock.tare();
            clock.reset();
            last_photo_ms = 0;
            run_state = RUN_PHOTO;
        } 
        else
        {
            status.nextPhoto = (unsigned int) (current.Delay - (unsigned long)clock.event_ms / 1000);
            if(((unsigned long)clock.event_ms / 1000) + settings_mirror_up_time >= current.Delay)
            {
                // Mirror Up //
                shutter_half(); // This is to wake up the camera (even if USB is connected)
                _delay_ms(100);
                if(usbPrimary) shutter_off();
            }

            if((settings_warn_time > 0) && (((unsigned long)clock.event_ms / 1000) + settings_warn_time >= current.Delay))
            {
                // Flash Light //
                _delay_ms(50);
            }
        }
    }

    if(run_state == RUN_PHOTO && (exps + photos == 0 || (uint8_t)((clock.Ms() - last_photo_end_ms) / 10) >= conf.camera.cameraFPS))
    {
        last_photo_end_ms = 0;
        if(old_state != run_state)
        {
            if(conf.debugEnabled)
            {
                DEBUG(PSTR("State: RUN_PHOTO"));
                DEBUG_NL();
            }
            strcpy((char *) status.textStatus, TEXT("Photo"));
            old_state = run_state;
        }
        if(current.Exp > 0 || (current.Mode & RAMP) || conf.arbitraryBulb)
        {
            clock.tare();
            run_state = RUN_BULB;
        } 
        else
        {
            exps++;
            capture();
            
            if(status.interval <= settings_mirror_up_time && !camera.ready) 
                shutter_half(); // Mirror Up //

            run_state = RUN_NEXT;
        }
    }
    
    if(run_state == RUN_BULB)
    {
        static uint32_t bulb_length, exp;
        static uint8_t m = SHUTTER_MODE_BULB;

        if(old_state != run_state)
        {
            old_state = run_state;

            if(conf.debugEnabled)
            {
                DEBUG(PSTR("State: RUN_BULB"));
                DEBUG_NL();
            }
            strcpy((char *) status.textStatus, TEXT("Bulb"));

            m = camera.shutterType(current.Exp);
            if(m & SHUTTER_MODE_BULB || conf.arbitraryBulb)
            {
                if(conf.arbitraryBulb)
                {
                    exp = current.ArbExp * 100;
                    m = SHUTTER_MODE_BULB;
                }
                else
                {
                    exp = camera.bulbTime((int8_t)current.Exp);
                }
            }

            if(current.Mode & RAMP)
            {
                float key1 = 1, key2 = 1, key3 = 1, key4 = 1;
                char found = 0;
                uint8_t i;
                m = SHUTTER_MODE_BULB;
                shutter_off();


                if(current.brampMethod == BRAMP_METHOD_KEYFRAME) //////////////////////////////// KEYFRAME RAMP /////////////////////////////////////
                {
                    // Bulb ramp algorithm goes here
                    for(i = 0; i < current.Keyframes; i++)
                    {
                        if(clock.Seconds() <= current.Key[i])
                        {
                            found = 1;
                            if(i == 0)
                            {
                                key2 = key1 = (float)(current.BulbStart);
                            }
                            else if(i == 1)
                            {
                                key1 = (float)(current.BulbStart);
                                key2 = (float)((int8_t)current.BulbStart - *((int8_t*)&current.Bulb[i - 1]));
                            }
                            else
                            {
                                key1 = (float)((int8_t)current.BulbStart - *((int8_t*)&current.Bulb[i - 2]));
                                key2 = (float)((int8_t)current.BulbStart - *((int8_t*)&current.Bulb[i - 1]));
                            }
                            key3 = (float)((int8_t)current.BulbStart - *((int8_t*)&current.Bulb[i]));
                            key4 = (float)((int8_t)current.BulbStart - *((int8_t*)&current.Bulb[i < (current.Keyframes - 1) ? i + 1 : i]));
                            break;
                        }
                    }
                    
                    if(found)
                    {
                        uint32_t var1 = clock.Seconds();
                        uint32_t var2 = (i > 0 ? current.Key[i - 1] : 0);
                        uint32_t var3 = current.Key[i];

                        float t = (float)(var1 - var2) / (float)(var3 - var2);
                        float curveEv = curve(key1, key2, key3, key4, t);
                        status.rampStops = (float)current.BulbStart - curveEv;
                        exp = camera.bulbTime(curveEv - (float)evShift);

                        if(conf.debugEnabled)
                        {
                            DEBUG(PSTR("   Keyframe: "));
                            DEBUG(i);
                            DEBUG_NL();
                            DEBUG(PSTR("    Percent: "));
                            DEBUG(t);
                            DEBUG_NL();
                            DEBUG(PSTR("    CurveEv: "));
                            DEBUG(curveEv);
                            DEBUG_NL();
                            DEBUG(PSTR("CorrectedEv: "));
                            DEBUG(curveEv - (float)evShift);
                            DEBUG_NL();
                            DEBUG(PSTR("   Exp (ms): "));
                            DEBUG(exp);
                            DEBUG_NL();
                            DEBUG(PSTR("    evShift: "));
                            DEBUG(evShift);
                            DEBUG_NL();
                        }
                    }
                    else
                    {
                        status.rampStops = (float)((int8_t)(current.BulbStart - (current.BulbStart - *((int8_t*)&current.Bulb[current.Keyframes - 1]))));
                        exp = camera.bulbTime((int8_t)(current.BulbStart - *((int8_t*)&current.Bulb[current.Keyframes - 1]) - (float)evShift));
                    }
                }

                else if(current.brampMethod == BRAMP_METHOD_GUIDED || current.brampMethod == BRAMP_METHOD_AUTO) //////////////////////////////// GUIDED / AUTO RAMP /////////////////////////////////////
                {

//#################### AUTO BRAMP ####################
                    if(current.brampMethod == BRAMP_METHOD_AUTO)
                    {
                        if(light.underThreshold && current.nightMode != BRAMP_TARGET_AUTO)
                        {
                            if(current.nightMode == BRAMP_TARGET_CUSTOM)
                            {
                                status.rampTarget = (float)status.nightTarget;
                            }
                            else
                            {
                                //if(light.slope <= 1 && status.lightStart == status.nightTarget && lightReading - light.integrated >= 3) // respond quickly during night-to-day once we see light
                                //{
                                //    DEBUG(STR(" -----> ramping toward sunrise\r\n"));
                                //    status.rampTarget = -BRAMP_RATE_MAX; //status.lightStart - (float)(NIGHT_THRESHOLD - status.nightTarget);
                                //}
                                //else
                                //{
                                    DEBUG(STR(" -----> holding night exposure\r\n"));
                                    status.rampTarget = status.lightStart - (float)status.nightTarget; // hold at night exposure
                                //}
                            }
                        }
                        else
                        {
                            DEBUG(STR(" -----> using light sensor target\r\n"));
                            status.rampTarget = (status.lightStart - lightReading);
                        }

                        if(status.rampTarget > status.rampMax) status.rampTarget = status.rampMax;
                        if(status.rampTarget < status.rampMin) status.rampTarget = status.rampMin;
                        float delta = status.rampTarget - status.rampStops;

                        float pastError = 0.0;
                        for(uint8_t i = 0; i < PAST_ERROR_COUNT; i++)
                        {
                            pastError += pastErrors[i];
                            if(i < PAST_ERROR_COUNT - 1) pastErrors[i] = pastErrors[i + 1];
                        }
                        pastError /= PAST_ERROR_COUNT;
                        pastErrors[PAST_ERROR_COUNT - 1] = delta;

                        if(delta != 0)
                        {
                            delta *= P_FACTOR;
                            if(!light.underThreshold) delta += pastError * I_FACTOR;

                            if(light.lockedSlope > 0.0 && current.nightMode != BRAMP_TARGET_AUTO)
                            {
                                if(light.lockedSlope < delta) delta = light.lockedSlope; // hold the last valid slope reading from the light sensor
                            }
                            else
                            {
                                delta += light.slope * D_FACTOR;
                            }

                        }

                        rampRate = (int8_t) delta;
                    }
//####################################################

                    status.rampStops += ((float)rampRate / (3600.0 / 3)) * ((float)status.interval / 10.0);

                    if(status.rampStops >= status.rampMax)
                    {
                        rampRate = 0;
                        status.rampStops = status.rampMax;
                    }
                    else if(status.rampStops <= status.rampMin)
                    {
                        rampRate = 0;
                        status.rampStops = status.rampMin;
                    }
                    exp = camera.bulbTime(current.BulbStart - status.rampStops - (float)evShift);
                }

                bulb_length = exp;

                if(current.IntervalMode == INTERVAL_MODE_AUTO) // Auto Interval
                {
                    float intPercent = (status.rampStops + ((float)camera.bulbMin() - (float)current.BulbStart)) / (float) ((int8_t)camera.bulbMin() - (int8_t)BulbMaxEv);

                    if(intPercent > 1.0) intPercent = 1.0;
                    if(intPercent < 0.0) intPercent = 0.0;

                    status.interval = current.GapMin + (uint16_t)(intPercent * (float)(current.Gap - current.GapMin));

                    if(conf.debugEnabled)
                    {
                        DEBUG(PSTR("rampStops: "));
                        DEBUG(status.rampStops);
                        DEBUG_NL();
                    }
                }
                else // Fixed Interval
                {
                    status.interval = current.Gap;
                }


                calculateExposure(&bulb_length, &aperture, &iso, &evShift);

                // bulb_length, aperture, iso, evShift, seconds, interval, light.lockedSlope, lightReading

                LOGGER(bulb_length); //0
                LOGGER(',');
                LOGGER(aperture); //1
                LOGGER(',');
                LOGGER(iso); //2
                LOGGER(',');
                LOGGER(evShift); //3
                LOGGER(',');
                LOGGER(lightReading); //4
                LOGGER(',');
                LOGGER(light.lockedSlope); //5
                LOGGER(',');
                LOGGER(light.slope); //6
                LOGGER(',');
                LOGGER(clock.Seconds()); //7
                LOGGER(',');
                LOGGER(status.interval); //8
                LOGGER(',');
                LOGGER(status.nightTarget); //9
                LOGGER(',');
                LOGGER(status.rampStops); //10
                LOGGER_NL();


                shutter_off_quick(); // Can't change parameters when half-pressed
                if((conf.brampMode & BRAMP_MODE_APERTURE) && camera.supports.aperture)
                {
                    // Change the Aperture //
                    if(camera.aperture() != aperture)
                    {
                        DEBUG(PSTR("Setting Aperture..."));
                        if(camera.setAperture(aperture) == PTP_RETURN_ERROR)
                        {
                            DEBUG(PSTR("ERROR!!!\r\n"));
                            run_state = RUN_ERROR;
                            return CONTINUE;
                        }
                        DEBUG_NL();
                    }
                }
                if((conf.brampMode & BRAMP_MODE_ISO) && camera.supports.iso)
                {
                    // Change the ISO //
                    if(camera.iso() != iso)
                    {
                        DEBUG(PSTR("Setting ISO..."));
                        if(camera.setISO(iso) == PTP_RETURN_ERROR)
                        {
                            DEBUG(PSTR("ERROR!!!\r\n"));
                            run_state = RUN_ERROR;
                            return CONTINUE;
                        }
                        DEBUG_NL();
                    }
                }
                
                if(conf.debugEnabled)
                {
                    DEBUG(PSTR("   Seconds: "));
                    DEBUG((uint16_t)clock.Seconds());
                    DEBUG_NL();
                    DEBUG(PSTR("   evShift: "));
                    DEBUG(evShift);
                    DEBUG_NL();
                    DEBUG(PSTR("BulbLength: "));
                    DEBUG((uint16_t)bulb_length);
                    if(found) DEBUG(PSTR(" (calculated)"));
                    DEBUG_NL();
                }
            }

            if(current.Mode & HDR)
            {
                uint8_t tv_offset = ((current.Exps - 1) / 2) * current.Bracket - exps * current.Bracket;
                if(current.Mode & RAMP)
                {
                    bulb_length = camera.shiftBulb(bulb_length, tv_offset);
                }
                else
                {
                    shutter_off_quick(); // Can't change parameters when half-pressed
                    m = camera.shutterType(current.Exp - tv_offset);
                    if(m == 0) m = SHUTTER_MODE_PTP;

                    if(m & SHUTTER_MODE_PTP)
                    {
                        DEBUG(PSTR("Shutter Mode PTP\r\n"));
                        camera.setShutter(current.Exp - tv_offset);
                        bulb_length = camera.bulbTime((int8_t)(current.Exp - tv_offset));
                    }
                    else
                    {
                        camera.bulbMode();
                        DEBUG(PSTR("Shutter Mode BULB\r\n"));
                        m = SHUTTER_MODE_BULB;
                        bulb_length = camera.bulbTime((int8_t)(current.Exp - tv_offset));
                    }
                }

                if(conf.debugEnabled)
                {
                    DEBUG_NL();
                    DEBUG(PSTR("Mode: "));
                    DEBUG(m);
                    DEBUG_NL();
                    DEBUG(PSTR("Tv: "));
                    DEBUG(current.Exp - tv_offset);
                    DEBUG_NL();
                    DEBUG(PSTR("Bulb: "));
                    DEBUG((uint16_t)bulb_length);
                    DEBUG_NL();
                }
            }
            
            if((current.Mode & (HDR | RAMP)) == 0)
            {
                if(conf.debugEnabled)
                {
                    DEBUG(PSTR("***Using exp: "));
                    DEBUG(exp);
                    DEBUG(PSTR(" ("));
                    DEBUG(current.Exp);
                    DEBUG(PSTR(")"));
                    DEBUG_NL();
                }
                bulb_length = exp;
                if(m & SHUTTER_MODE_PTP)
                {
                    shutter_off_quick(); // Can't change parameters when half-pressed
                    camera.manualMode();
                    camera.setShutter(current.Exp);
                }
            }

            status.bulbLength = bulb_length;

            if(current.Mode & RAMP && (!camera.isInBulbMode() && camera.ready))
            {
                DEBUG(PSTR("\r\n-->Using Extended Ramp\r\n"));
                DEBUG(PSTR("    ms: "));
                DEBUG(bulb_length);
                DEBUG_NL();
                DEBUG(PSTR("    ev: "));
                DEBUG(camera.bulbToShutterEv(bulb_length));
                DEBUG_NL();
                DEBUG_NL();
                camera.setShutter(camera.bulbToShutterEv(bulb_length));
                shutter_capture();
                _delay_ms(10);
                if(lastShutterError)
                {
                    clock.cancelBulb();
                    DEBUG(PSTR("USB Error - trying again\r\n"));
                    old_state = 0;
                    if(PTP_Error) run_state = RUN_ERROR;
                    return CONTINUE; // try it again
                }
            }
            else if(m & SHUTTER_MODE_BULB)
            {
                //DEBUG(PSTR("Running BULB\r\n"));
                camera.bulb_open = true;
                		
                clock.bulb(bulb_length);
                _delay_ms(10);
				
				
                if(lastShutterError)
                {
                    clock.cancelBulb();
                    DEBUG(PSTR("USB Error - trying again\r\n"));
                    old_state = 0;
                    if(PTP_Error) run_state = RUN_ERROR;
                    return CONTINUE; // try it again
                }
            }
            else
            {
                //DEBUG(PSTR("Running Capture\r\n"));
                shutter_capture();
            }
        }
        else if(!clock.bulbRunning && !camera.busy)
        {
            exps++;

            lightReading = light.readIntegratedEv();

            _delay_ms(50);

            if(status.interval <= settings_mirror_up_time && !camera.ready) 
                shutter_half(); // Mirror Up //

            run_state = RUN_NEXT;
        }
        else
        {
            //if(clock.bulbRunning) DEBUG(PSTR("Waiting on clock  "));
            //if(camera.busy) DEBUG(PSTR("Waiting on camera  "));
        }
    }
    
    if(run_state == RUN_NEXT)
    {
        last_photo_end_ms = clock.Ms();

        if((PTP_Connected && PTP_Error) || (usingUSB && !camera.ready))
        {
            run_state = RUN_ERROR;
            return CONTINUE;
        }

        if(old_state != run_state)
        {
            if(conf.debugEnabled)
            {
                DEBUG(PSTR("State: RUN_NEXT"));
                DEBUG_NL();
            }
            old_state = run_state;
        }

        if((exps >= current.Exps && (current.Mode & HDR)) || (current.Mode & HDR) == 0)
        {
            exps = 0;
            photos++;
            clock.tare();
            run_state = RUN_GAP;
        }
        else
        {
            clock.tare();
            run_state = RUN_PHOTO;
        }
    
        if(!current.infinitePhotos)
        {
            if(current.Mode & RAMP)
            {
                if(clock.Seconds() >= ((uint32_t)current.Duration * 60))  //J.R.
                {
                    run_state = RUN_END;
                }
            }
            else
            {
                if(photos >= current.Photos || (((current.Mode & TIMELAPSE) == 0) && photos >= 1))
                {
                    run_state = RUN_END;
                }
            }
        }

        if(camera.ready && current.Mode & RAMP && conf.extendedRamp && !camera.isInBulbMode() && conf.camera.modeSwitch == USB_CHANGE_MODE_ENABLED && timer.status.bulbLength > camera.bulbTime((int8_t)camera.bulbMin()))
        {
            camera.bulbMode();
        }
        else if(camera.ready && current.Mode & RAMP && conf.extendedRamp && camera.isInBulbMode() && conf.camera.modeSwitch == USB_CHANGE_MODE_ENABLED && timer.status.bulbLength < camera.bulbTime((int8_t)camera.bulbMin()))
        {
            camera.manualMode();
        }

        status.photosRemaining = current.Photos - photos;
        status.photosTaken = photos;
    }
    
    if(run_state == RUN_GAP)
    {
        if(old_state != run_state)
        {
            if(conf.auxPort == AUX_MODE_DOLLY) aux_pulse();
            if(conf.debugEnabled)
            {
                DEBUG(PSTR("State: RUN_GAP"));
                DEBUG_NL();
            }
            strcpy((char *) status.textStatus, TEXT("Waiting"));
            old_state = run_state;
        }

        if(camera.busy && (photos < current.Photos || ((current.Mode & RAMP) && (clock.Seconds() < ((uint32_t)current.Duration * 60)))))  //J.R.
        {
            run_state = RUN_GAP;
        }
        else
        {
            uint32_t cms = clock.Ms();

            if((cms - last_photo_ms) / 100 >= status.interval)
            {
                last_photo_ms = cms;
                clock.tare();
                run_state = RUN_PHOTO;
            } 
            else
            {
                status.nextPhoto = (unsigned int) ((status.interval - (cms - last_photo_ms) / 100) / 10);
                if((cms - last_photo_ms) / 100 + (uint32_t)settings_mirror_up_time * 10 >= status.interval)
                {
                    // Mirror Up //
                    if(!camera.ready) shutter_half(); // Don't do half-press if the camera is connected by USB
                }
            }
        }
    }
    
    if(run_state == RUN_ERROR)
    {
        static uint8_t errorRetryCount = 0;
        if(old_state != run_state || errorRetryCount > 40)
        {
            errorRetryCount = 0;
            shutter_off();
            camera.bulbEnd();

            if(conf.debugEnabled)
            {
                DEBUG(PSTR("State: RUN_ERROR"));
                DEBUG_NL();
            }
            menu.blink();
            strcpy((char *) status.textStatus, TEXT("Error"));
            old_state = run_state;

            //if(PTP_Connected && PTP_Error)
            //{
                DEBUG(PSTR("Resetting Camera"));
                DEBUG_NL();
                shutter_half();
                _delay_ms(100);
                shutter_off();
                camera.resetConnection();
            //}
        }

        //enter = 0;
        //running = 0;
        //light.stop();

        //hardware_flashlight((uint8_t) clock.Seconds() % 2);

        errorRetryCount++;

        if(camera.ready)
        {
            errorRetryCount = 0;
            run_state = RUN_NEXT;
        }
        return CONTINUE;
    }

    if(run_state == RUN_END)
    {
        if(old_state != run_state)
        {
            if(conf.debugEnabled)
            {
                DEBUG(PSTR("State: RUN_END"));
                DEBUG_NL();
            }
            strcpy((char *) status.textStatus, TEXT("Done"));
            old_state = run_state;
        }

        pausing = 0;
        paused = 0;
        enter = 0;
        running = 0;
        shutter_off();
        camera.bulbEnd();
        hardware_flashlight(0);
        light.stop();
        aux1_off();
        aux2_off();
        clock.awake();
        usbPrimary = 0;
        status.preChecked = 0;

        return DONE;
    }

    return CONTINUE;
}

void shutter::calculateExposure(uint32_t *nextBulbLength, uint8_t *nextAperture, uint8_t *nextISO, int8_t *bulbChangeEv)
{
    if(camera.supports.iso || camera.supports.aperture || camera.supports.shutter)
    {
        uint8_t aperture = *nextAperture, iso = *nextISO;

        uint32_t exp = *nextBulbLength;

        int8_t tmpShift = 0;

        if((conf.brampMode & BRAMP_MODE_APERTURE) && camera.supports.aperture)
        {
            DEBUG(PSTR("Aperture Close: BEGIN\r\n\r\n"));
            // Check for too long bulb time and adjust Aperture //
            while(*nextBulbLength > BulbMax)
            {
                *nextAperture = camera.apertureDown(aperture);
                if(*nextAperture != aperture)
                {
                    *bulbChangeEv += *nextAperture - aperture;
                    tmpShift += *nextAperture - aperture;
                    aperture = *nextAperture;

                    DEBUG(PSTR("   Aperture UP:"));
                    DEBUG(*bulbChangeEv);
                    DEBUG(PSTR("   Aperture Val:"));
                    DEBUG(*nextAperture);
                }
                else
                {
                    DEBUG(PSTR("   Reached Aperture Max!!!\r\n"));
                    break;
                }
                *nextBulbLength = camera.shiftBulb(exp, tmpShift);
            }
            DEBUG(PSTR("Aperture Close: DONE\r\n\r\n"));
        }

        if((conf.brampMode & BRAMP_MODE_ISO) && camera.supports.iso)
        {
            DEBUG(PSTR("ISO Up: BEGIN\r\n\r\n"));
            // Check for too long bulb time and adjust ISO //
            while(*nextBulbLength > BulbMax)
            {
                *nextISO = camera.isoUp(iso);
                if(*nextISO != iso)
                {
                    *bulbChangeEv += *nextISO - iso;
                    tmpShift += *nextISO - iso;
                    iso = *nextISO;

                    DEBUG(PSTR("   ISO UP:"));
                    DEBUG(*bulbChangeEv);
                    DEBUG(PSTR("   ISO Val:"));
                    DEBUG(*nextISO);
                }
                else
                {
                    DEBUG(PSTR("   Reached ISO Max!!!\r\n"));
                    break;
                }
                *nextBulbLength = camera.shiftBulb(exp, tmpShift);
            }
            DEBUG(PSTR("ISO Up: DONE\r\n\r\n"));
        }


        if((conf.brampMode & BRAMP_MODE_ISO) && camera.supports.iso)
        {
            // Check for too short bulb time and adjust ISO //
            for(;;)
            {
                DEBUG(PSTR("ISO Down: BEGIN\r\n\r\n"));
                *nextISO = camera.isoDown(iso);
                if(*nextISO != iso && *nextISO < 127)
                {
                    uint32_t bulb_length_test = camera.shiftBulb(exp, tmpShift + (*nextISO - iso));
                    if(bulb_length_test < BulbMax)
                    {
                        *bulbChangeEv += *nextISO - iso;
                        tmpShift += *nextISO - iso;
                        iso = *nextISO;
                        DEBUG(PSTR("   ISO DOWN:"));
                        DEBUG(*bulbChangeEv);
                        DEBUG(PSTR("   ISO Val:"));
                        DEBUG(*nextISO);
                        *nextBulbLength = bulb_length_test;
                        continue;
                    }
                }
                *nextISO = iso;
                break;
            }
            DEBUG(PSTR("ISO Down: DONE\r\n\r\n"));
        }


        if((conf.brampMode & BRAMP_MODE_APERTURE) && camera.supports.aperture)
        {
            // Check for too short bulb time and adjust Aperture //
            for(;;)
            {
                DEBUG(PSTR("Aperture Open: BEGIN\r\n\r\n"));
                *nextAperture = camera.apertureUp(aperture);
                if(*nextAperture != aperture)
                {
                    if(*nextAperture >= 127) break;
                    uint32_t bulb_length_test = camera.shiftBulb(exp, tmpShift + (*nextAperture - aperture));
                    if(bulb_length_test < BulbMax)
                    {
                        *bulbChangeEv  += *nextAperture - aperture;
                        tmpShift += *nextAperture - aperture;
                        aperture = *nextAperture;
                        DEBUG(PSTR("Aperture DOWN:"));
                        DEBUG(*bulbChangeEv);
                        DEBUG(PSTR(" Aperture Val:"));
                        DEBUG(*nextAperture);
                        *nextBulbLength = camera.shiftBulb(exp, tmpShift);
                        continue;
                    }
                }
                *nextAperture = aperture;
                break;
            }
            DEBUG(PSTR("Aperture Open: DONE\r\n\r\n"));
        }
        
        if(conf.extendedRamp)
        {
            if(*nextBulbLength < camera.bulbTime((int8_t)MAX_EXTENDED_RAMP_SHUTTER))
            {
                DEBUG(PSTR("   Reached Max Shutter!!!\r\n"));
                *nextBulbLength = camera.bulbTime((int8_t)MAX_EXTENDED_RAMP_SHUTTER);
            }
        }
        else
        {
            if(*nextBulbLength < camera.bulbTime((int8_t)camera.bulbMin()))
            {
                DEBUG(PSTR("   Reached Bulb Min!!!\r\n"));
                *nextBulbLength = camera.bulbTime((int8_t)camera.bulbMin());
            }
        }
    }
}

void shutter::switchToGuided()
{
    rampRate = 0;
    current.brampMethod = BRAMP_METHOD_GUIDED;
}

void shutter::switchToAuto()
{
    light.integrationStart(conf.lightIntegrationMinutes);
    lightReading = status.lightStart = light.readIntegratedEv();
    current.brampMethod = BRAMP_METHOD_AUTO;
    current.nightMode = BRAMP_TARGET_AUTO;
}

void check_cable()
{
    CHECK_CABLE;
}

void aux_pulse()
{
    aux1_on();
    aux2_on();
    if(conf.dollyPulse == 65535) conf.dollyPulse = 100;
    if(conf.dollyPulse2 == 65535) conf.dollyPulse2 = 100;
    clock.in(conf.dollyPulse, &aux1_off);
    clock.in(conf.dollyPulse2, &aux2_off);
}

void aux1_on()
{
    ENABLE_AUX_PORT1;
    AUX_OUT1_ON;
}

void aux1_off()
{
    ENABLE_AUX_PORT1;
}

void aux2_on()
{
    ENABLE_AUX_PORT2;
    AUX_OUT2_ON;
}

void aux2_off()
{
    ENABLE_AUX_PORT2;
}

uint8_t stopName(char name[8], uint8_t stop)
{
    name[0] = ' ';
    name[1] = ' ';
    name[2] = ' ';
    name[3] = ' ';
    name[4] = ' ';
    name[5] = ' ';
    name[6] = ' ';
    name[7] = '\0';

    int8_t ev = *((int8_t*)&stop);

    uint8_t sign = 0;
    if(ev < 0)
    {
        sign = 1;
        ev = 0 - ev;
    }
    uint8_t mod = ev % 3;
    ev /= 3;
    if(mod == 0)
    {
        if(sign) name[5] = '-'; else name[5] = '+';
        if(ev > 9)
        {
            name[4] = name[5];
            name[5] = '0' + ev / 10;
            ev %= 10;
            name[6] = '0' + ev;
        }
        else
        {
            name[6] = '0' + ev;
        }
    }
    else
    {
        if(sign) name[ev > 0 ? 1 : 3] = '-'; else name[ev > 0 ? 1 : 3] = '+';
        if(ev > 9)
        {
            name[0] = name[1];
            name[1] = '0' + ev / 10;
            ev %= 10;
            name[2] = '0' + ev;
        }
        else if(ev > 0)
        {
            name[2] = '0' + ev;
        }
        name[4] = '0' + mod;
        name[5] = '/';
        name[6] = '3';
    }
    return 1;
}

void calcBulbMax()
{
    BulbMax = (timer.current.Gap - BRAMP_GAP_PADDING) * 100;

    DEBUG(PSTR("BMAX1: "));
    DEBUG(BulbMax);
    DEBUG_NL();

    BulbMaxEv = 1;
    for(int8_t i = camera.bulbMax(); i < camera.bulbMin(); i++)
    {
        if(BulbMax >= camera.bulbTime(i))
        {
            if(i < 1) i = 1;
            BulbMaxEv = i;
            BulbMax = camera.bulbTime((int8_t)BulbMaxEv);
            break;
        }
    }


    DEBUG(PSTR("BMAX2: "));
    DEBUG(BulbMax);
    DEBUG_NL();
}

uint8_t stopUp(uint8_t stop)
{
    int8_t ev = *((int8_t*)&stop);

    int8_t evMax = calcRampMax();

    if(ev < 30*3) ev++; else ev = 30*3;

    if(ev <= evMax) return ev; else return evMax;
}

uint8_t stopDown(uint8_t stop)
{
    int8_t ev = *((int8_t*)&stop);

    int8_t evMin = calcRampMin();

    if(ev > -30*3) ev--; else ev = -30*3;

    if(ev >= evMin) return ev; else return evMin;
}

int8_t calcRampMax()
{
    calcBulbMax();

    int8_t bulbRange = 0;
    int8_t isoRange = 0;
    int8_t apertureRange = 0;

    if(conf.brampMode & BRAMP_MODE_BULB) bulbRange = (int8_t)timer.current.BulbStart - (int8_t)BulbMaxEv;
    if(conf.brampMode & BRAMP_MODE_ISO) isoRange = (int8_t)camera.iso() - (int8_t)camera.isoMax();
    if(conf.brampMode & BRAMP_MODE_APERTURE) apertureRange = (int8_t)camera.aperture() - (int8_t)camera.apertureMin();

    return isoRange + apertureRange + bulbRange;
}

int8_t calcRampTarget(int8_t targetShutter, int8_t targetISO, int8_t targetAperture)
{
    calcBulbMax();

    int8_t bulbRange = 0;
    int8_t isoRange = 0;
    int8_t apertureRange = 0;

    if(conf.brampMode & BRAMP_MODE_BULB) bulbRange = (int8_t)timer.current.BulbStart - targetShutter;
    if(conf.brampMode & BRAMP_MODE_ISO) isoRange = (int8_t)camera.iso() - targetISO;
    if(conf.brampMode & BRAMP_MODE_APERTURE) apertureRange = (int8_t)camera.aperture() - targetAperture;

    return isoRange + apertureRange + bulbRange;
}

int8_t calcRampMin()
{
    int8_t bulbRange = 0;
    int8_t isoRange = 0;
    int8_t apertureRange = 0;

    if(conf.brampMode & BRAMP_MODE_BULB)
    {
        if(conf.extendedRamp && camera.ready)
        {
            uint8_t shutterMax = PTP::shutterMax();
            if(shutterMax > MAX_EXTENDED_RAMP_SHUTTER) shutterMax = MAX_EXTENDED_RAMP_SHUTTER;
            bulbRange = (int8_t)MAX_EXTENDED_RAMP_SHUTTER - (int8_t)timer.current.BulbStart;
        }
        else
        {
            bulbRange = (int8_t)camera.bulbMin() - (int8_t)timer.current.BulbStart;
        }        
    }
    if((conf.brampMode & BRAMP_MODE_ISO) && camera.supports.iso) isoRange = (int8_t)camera.isoMin() - (int8_t)camera.iso();
    if((conf.brampMode & BRAMP_MODE_APERTURE) && camera.supports.aperture) apertureRange = (int8_t)camera.apertureMax() - (int8_t)camera.aperture();

    return 0 - (isoRange + apertureRange + bulbRange);
}

uint8_t checkHDR(uint8_t exps, uint8_t mid, uint8_t bracket)
{
    uint8_t up = mid - (exps / 2) * bracket;
    uint8_t down = mid + (exps / 2) * bracket;

    DEBUG(PSTR("up: "));
    DEBUG(up);
    DEBUG_NL();
    DEBUG(PSTR("down: "));
    DEBUG(down);
    DEBUG_NL();
    DEBUG(PSTR("max: "));
    DEBUG(camera.shutterMax());
    DEBUG_NL();
    DEBUG(PSTR("min: "));
    DEBUG(camera.shutterMin());
    DEBUG_NL();

    if(up < camera.shutterMax() || down > camera.shutterMin()) return 1; else return 0;
}

uint8_t hdrTvUp(uint8_t ev)
{
    if(checkHDR(timer.current.Exps, ev, timer.current.Bracket))
    {
        uint8_t mid = (camera.shutterMin() - camera.shutterMax()) / 2 + camera.shutterMax();
        if(mid > ev)
        {
            for(uint8_t i = ev; i <= mid; i++)
            {
                if(!checkHDR(timer.current.Exps, i, timer.current.Bracket))
                {
                    ev = i;
                    break;
                }
            }
        }
        else
        {
            for(uint8_t i = ev; i >= mid; i--)
            {
                if(!checkHDR(timer.current.Exps, i, timer.current.Bracket))
                {
                    ev = i;
                    break;
                }
            }
        }
    }
    else
    {
        uint8_t tmp = camera.shutterUp(ev);
        if(!checkHDR(timer.current.Exps, tmp, timer.current.Bracket)) ev = tmp;
    }
    return ev;
}

uint8_t hdrTvDown(uint8_t ev)
{
    if(checkHDR(timer.current.Exps, ev, timer.current.Bracket))
    {
        uint8_t mid = (camera.shutterMin() - camera.shutterMax()) / 2 + camera.shutterMax();
        if(mid > ev)
        {
            for(uint8_t i = ev; i <= mid; i++)
            {
                if(!checkHDR(timer.current.Exps, i, timer.current.Bracket))
                {
                    ev = i;
                    break;
                }
            }
        }
        else
        {
            for(uint8_t i = ev; i >= mid; i--)
            {
                if(!checkHDR(timer.current.Exps, i, timer.current.Bracket))
                {
                    ev = i;
                    break;
                }
            }
        }
    }
    else
    {
        uint8_t tmp = camera.shutterDown(ev);
        if(!checkHDR(timer.current.Exps, tmp, timer.current.Bracket)) ev = tmp;
    }
    return ev;
}

uint8_t bracketUp(uint8_t ev)
{
    uint8_t max = 1;
    for(uint8_t i = 1; i < 255; i++)
    {
        if(checkHDR(timer.current.Exps, timer.current.Exp, i))
        {
            max = i - 1;
            break;
        }
    }
    if(ev < max) ev++; else ev = max;
    return ev;
}

uint8_t bracketDown(uint8_t ev)
{
    if(ev > 1) ev--; else ev = 1;
    return ev;
}

uint8_t hdrExpsUp(uint8_t hdr_exps)
{
    uint8_t max = 1;
    for(uint8_t i = 1; i < 255; i++)
    {
        if(checkHDR(i, timer.current.Exp, timer.current.Bracket))
        {
            max = i - 1;
            if(max < 3) max = 3;
            break;
        }
    }
    if(hdr_exps < max) hdr_exps++; else hdr_exps = max;
    return hdr_exps;
}

uint8_t hdrExpsDown(uint8_t hdr_exps)
{
    if(hdr_exps > 3) hdr_exps--; else hdr_exps = 3;
    return hdr_exps;
}

uint8_t hdrExpsName(char name[8], uint8_t hdr_exps)
{
    name[0] = ' ';
    name[1] = ' ';
    name[2] = ' ';
    name[3] = ' ';
    name[4] = ' ';
    name[5] = ' ';
    name[6] = ' ';
    name[7] = '\0';

    if(hdr_exps > 9)
    {
        name[5] = '0' + (hdr_exps / 10);
        hdr_exps %= 10;
    }
    name[6] = '0' + hdr_exps;

    return 1;
}

uint8_t tvUp(uint8_t ev)
{
    if(ev == 254) return ev;
    uint8_t tmp = PTP::bulbUp(ev);
    if(tmp > 33) tmp = 254;
    return tmp;
}

uint8_t tvDown(uint8_t ev)
{
    if(ev == 254) return 33;
    return PTP::bulbDown(ev);
}


uint8_t rampTvUp(uint8_t ev)
{
    uint8_t tmp = PTP::bulbUp(ev);
    if(tmp > camera.bulbMin()) tmp = camera.bulbMin();
    return tmp;
}

uint8_t rampTvUpExtended(uint8_t ev)
{
    uint8_t tmp = PTP::shutterUp(ev);
    if(tmp == 254) tmp = ev;
    if(tmp > MAX_EXTENDED_RAMP_SHUTTER) tmp = MAX_EXTENDED_RAMP_SHUTTER;
    return tmp;
}

uint8_t rampTvUpStatic(uint8_t ev)
{
    uint8_t tmp = PTP::bulbUp(ev);
    //if(tmp > camera.bulbMin()) tmp = camera.bulbMin();
    return tmp;
}

uint8_t rampTvDown(uint8_t ev)
{
    calcBulbMax();
    uint8_t tmp = PTP::bulbDown(ev);
    if(tmp < BulbMaxEv) tmp = BulbMaxEv;
    return tmp;
}

uint8_t rampTvDownExtended(uint8_t ev)
{
    calcBulbMax();
    uint8_t tmp = PTP::shutterDown(ev);
    if(tmp < BulbMaxEv) tmp = BulbMaxEv;
    return tmp;
}


