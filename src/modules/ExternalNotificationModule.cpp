/**
 * @file ExternalNotificationModule.cpp
 * @brief Implementation of the ExternalNotificationModule class.
 *
 * This file contains the implementation of the ExternalNotificationModule class, which is responsible for handling external
 * notifications such as vibration, buzzer, and LED lights. The class provides methods to turn on and off the external
 * notification outputs and to play ringtones using PWM buzzer. It also includes default configurations and a runOnce() method to
 * handle the module's behavior.
 *
 * Documentation:
 * https://meshtastic.org/docs/configuration/module/external-notification
 *
 * @author Jm Casler & Meshtastic Team
 * @date [Insert Date]
 */
#include "ExternalNotificationModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "buzz/buzz.h"
#include "configuration.h"
#include "main.h"
#include "mesh/generated/meshtastic/rtttl.pb.h"
#include <Arduino.h>

#ifdef HAS_NCP5623
#include <graphics/RAKled.h>
#endif

#ifdef HAS_LP5562
#include <graphics/NomadStarLED.h>
#endif

#ifdef HAS_NEOPIXEL
#include <graphics/NeoPixel.h>
#endif

#ifdef UNPHONE
#include "unPhone.h"
extern unPhone unphone;
#endif

#if defined(HAS_RGB_LED)
uint8_t red = 0;
uint8_t green = 0;
uint8_t blue = 0;
uint8_t white = 0;
uint8_t colorState = 1;
uint8_t brightnessIndex = 0;
uint8_t brightnessValues[] = {0, 10, 20, 30, 50, 90, 160, 170}; // blue gets multiplied by 1.5
bool ascending = true;
#endif

#ifndef PIN_BUZZER
#define PIN_BUZZER false
#endif

/*
    Documentation:
        https://meshtastic.org/docs/configuration/module/external-notification
*/

// Default configurations
#ifdef EXT_NOTIFY_OUT
#define EXT_NOTIFICATION_MODULE_OUTPUT EXT_NOTIFY_OUT
#else
#define EXT_NOTIFICATION_MODULE_OUTPUT 0
#endif
#define EXT_NOTIFICATION_MODULE_OUTPUT_MS 1000

#define EXT_NOTIFICATION_DEFAULT_THREAD_MS 25

#define ASCII_BELL 0x07

meshtastic_RTTTLConfig rtttlConfig;

ExternalNotificationModule *externalNotificationModule;

bool externalCurrentState[3] = {};

uint32_t externalTurnedOn[3] = {};

static const char *rtttlConfigFile = "/prefs/ringtone.proto";

int32_t ExternalNotificationModule::runOnce()
{
    if (!moduleConfig.external_notification.enabled) {
        return INT32_MAX; // we don't need this thread here...
    } else {

        bool isPlaying = rtttl::isPlaying();
#ifdef HAS_I2S
        isPlaying = rtttl::isPlaying() || audioThread->isPlaying();
#endif
        if ((nagCycleCutoff < millis()) && !isPlaying) {
            // let the song finish if we reach timeout
            nagCycleCutoff = UINT32_MAX;
            LOG_INFO("Turning off external notification: ");
            for (int i = 0; i < 3; i++) {
                setExternalState(i, false);
                externalTurnedOn[i] = 0;
                LOG_INFO("%d ", i);
            }
            LOG_INFO("");
#ifdef HAS_I2S
            // GPIO0 is used as mclk for I2S audio and set to OUTPUT by the sound library
            // T-Deck uses GPIO0 as trackball button, so restore the mode
#if defined(T_DECK) || (defined(BUTTON_PIN) && BUTTON_PIN == 0)
            pinMode(0, INPUT);
#endif
#endif
            isNagging = false;
            return INT32_MAX; // save cycles till we're needed again
        }

        // If the output is turned on, turn it back off after the given period of time.
        if (isNagging) {
            if (externalTurnedOn[0] + (moduleConfig.external_notification.output_ms ? moduleConfig.external_notification.output_ms
                                                                                    : EXT_NOTIFICATION_MODULE_OUTPUT_MS) <
                millis()) {
                setExternalState(0, !getExternal(0));
            }
            if (externalTurnedOn[1] + (moduleConfig.external_notification.output_ms ? moduleConfig.external_notification.output_ms
                                                                                    : EXT_NOTIFICATION_MODULE_OUTPUT_MS) <
                millis()) {
                setExternalState(1, !getExternal(1));
            }
            // Only toggle buzzer output if not using PWM mode (to avoid conflict with RTTTL)
            if (!moduleConfig.external_notification.use_pwm &&
                externalTurnedOn[2] + (moduleConfig.external_notification.output_ms ? moduleConfig.external_notification.output_ms
                                                                                    : EXT_NOTIFICATION_MODULE_OUTPUT_MS) <
                    millis()) {
                LOG_DEBUG("EXTERNAL 2 %d compared to %d", externalTurnedOn[2] + moduleConfig.external_notification.output_ms,
                          millis());
                setExternalState(2, !getExternal(2));
            }
#if defined(HAS_RGB_LED)
            red = (colorState & 4) ? brightnessValues[brightnessIndex] : 0;          // Red enabled on colorState = 4,5,6,7
            green = (colorState & 2) ? brightnessValues[brightnessIndex] : 0;        // Green enabled on colorState = 2,3,6,7
            blue = (colorState & 1) ? (brightnessValues[brightnessIndex] * 1.5) : 0; // Blue enabled on colorState = 1,3,5,7
            white = (colorState & 12) ? brightnessValues[brightnessIndex] : 0;
#ifdef HAS_NCP5623
            if (rgb_found.type == ScanI2C::NCP5623) {
                rgb.setColor(red, green, blue);
            }
#endif
#ifdef HAS_LP5562
            if (rgb_found.type == ScanI2C::LP5562) {
                rgbw.setColor(red, green, blue, white);
            }
#endif
#ifdef RGBLED_CA
            analogWrite(RGBLED_RED, 255 - red); // CA type needs reverse logic
            analogWrite(RGBLED_GREEN, 255 - green);
            analogWrite(RGBLED_BLUE, 255 - blue);
#elif defined(RGBLED_RED)
            analogWrite(RGBLED_RED, red);
            analogWrite(RGBLED_GREEN, green);
            analogWrite(RGBLED_BLUE, blue);
#endif
#ifdef HAS_NEOPIXEL
            pixels.fill(pixels.Color(red, green, blue), 0, NEOPIXEL_COUNT);
            pixels.show();
#endif
#ifdef UNPHONE
            unphone.rgb(red, green, blue);
#endif
            if (ascending) { // fade in
                brightnessIndex++;
                if (brightnessIndex == (sizeof(brightnessValues) - 1)) {
                    ascending = false;
                }
            } else {
                brightnessIndex--; // fade out
            }
            if (brightnessIndex == 0) {
                ascending = true;
                colorState++; // next color
                if (colorState > 7) {
                    colorState = 1;
                }
            }
#endif

#ifdef T_WATCH_S3
            drv.go();
#endif
        }

        // Play RTTTL over i2s audio interface if enabled as buzzer
#ifdef HAS_I2S
        if (moduleConfig.external_notification.use_i2s_as_buzzer && canBuzz()) {
            if (audioThread->isPlaying()) {
                // Continue playing
            } else if (isNagging && (nagCycleCutoff >= millis())) {
                audioThread->beginRttl(rtttlConfig.ringtone, strlen_P(rtttlConfig.ringtone));
            }
        }
#endif
        // now let the PWM buzzer play
        if (moduleConfig.external_notification.use_pwm && config.device.buzzer_gpio && canBuzz()) {
            if (rtttl::isPlaying()) {
                rtttl::play();
            } else if (isNagging && (nagCycleCutoff >= millis())) {
                // start the song again if we have time left
                rtttl::begin(config.device.buzzer_gpio, rtttlConfig.ringtone);
            }
        }

        return EXT_NOTIFICATION_DEFAULT_THREAD_MS;
    }
}

/**
 * Based on buzzer mode, return true if we can buzz.
 */
bool ExternalNotificationModule::canBuzz()
{
    if (config.device.buzzer_mode != meshtastic_Config_DeviceConfig_BuzzerMode_DISABLED &&
        config.device.buzzer_mode != meshtastic_Config_DeviceConfig_BuzzerMode_SYSTEM_ONLY) {
        return true;
    }
    return false;
}

bool ExternalNotificationModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return MeshService::isTextPayload(p);
}

/**
 * Sets the external notification for the specified index.
 *
 * @param index The index of the external notification to change state.
 * @param on Whether we are turning things on (true) or off (false).
 */
void ExternalNotificationModule::setExternalState(uint8_t index, bool on)
{
    externalCurrentState[index] = on;
    externalTurnedOn[index] = millis();

    switch (index) {
    case 1:
#ifdef UNPHONE
        unphone.vibe(on); // the unPhone's vibration motor is on a i2c GPIO expander
#endif
        if (moduleConfig.external_notification.output_vibra)
            digitalWrite(moduleConfig.external_notification.output_vibra, on);
        break;
    case 2:
        // Only control buzzer pin digitally if not using PWM mode
        if (moduleConfig.external_notification.output_buzzer && !moduleConfig.external_notification.use_pwm)
            digitalWrite(moduleConfig.external_notification.output_buzzer, on);
        break;
    default:
        if (output > 0)
            digitalWrite(output, (moduleConfig.external_notification.active ? on : !on));
        break;
    }

#if defined(HAS_RGB_LED)
    if (!on) {
        red = 0;
        green = 0;
        blue = 0;
        white = 0;
    }
#endif

#ifdef HAS_NCP5623
    if (rgb_found.type == ScanI2C::NCP5623) {
        rgb.setColor(red, green, blue);
    }
#endif
#ifdef HAS_LP5562
    if (rgb_found.type == ScanI2C::LP5562) {
        rgbw.setColor(red, green, blue, white);
    }
#endif
#ifdef RGBLED_CA
    analogWrite(RGBLED_RED, 255 - red); // CA type needs reverse logic
    analogWrite(RGBLED_GREEN, 255 - green);
    analogWrite(RGBLED_BLUE, 255 - blue);
#elif defined(RGBLED_RED)
    analogWrite(RGBLED_RED, red);
    analogWrite(RGBLED_GREEN, green);
    analogWrite(RGBLED_BLUE, blue);
#endif
#ifdef HAS_NEOPIXEL
    pixels.fill(pixels.Color(red, green, blue), 0, NEOPIXEL_COUNT);
    pixels.show();
#endif
#ifdef UNPHONE
    unphone.rgb(red, green, blue);
#endif
#ifdef T_WATCH_S3
    if (on) {
        drv.go();
    } else {
        drv.stop();
    }
#endif
}

bool ExternalNotificationModule::getExternal(uint8_t index)
{
    return externalCurrentState[index];
}

// Allow other firmware components to determine whether a notification is ongoing
bool ExternalNotificationModule::nagging()
{
    return isNagging;
}

void ExternalNotificationModule::stopNow()
{
    rtttl::stop();
#ifdef HAS_I2S
    if (audioThread->isPlaying())
        audioThread->stop();
#endif
    nagCycleCutoff = 1; // small value
    isNagging = false;
    // Turn off all outputs
    for (int i = 0; i < 3; i++) {
        setExternalState(i, false);
        externalTurnedOn[i] = 0;
    }
    setIntervalFromNow(0);
#ifdef T_WATCH_S3
    drv.stop();
#endif
}

ExternalNotificationModule::ExternalNotificationModule()
    : SinglePortModule("ExternalNotificationModule", meshtastic_PortNum_TEXT_MESSAGE_APP),
      concurrency::OSThread("ExternalNotification")
{
    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */

    // moduleConfig.external_notification.alert_message = true;
    // moduleConfig.external_notification.alert_message_buzzer = true;
    // moduleConfig.external_notification.alert_message_vibra = true;
    // moduleConfig.external_notification.use_i2s_as_buzzer = true;

    // moduleConfig.external_notification.active = true;
    // moduleConfig.external_notification.alert_bell = 1;
    // moduleConfig.external_notification.output_ms = 1000;
    // moduleConfig.external_notification.output = 4; // RAK4631 IO4
    // moduleConfig.external_notification.output_buzzer = 10; // RAK4631 IO6
    // moduleConfig.external_notification.output_vibra = 28; // RAK4631 IO7
    // moduleConfig.external_notification.nag_timeout = 300;

    // T-Watch / T-Deck i2s audio as buzzer:
    // moduleConfig.external_notification.enabled = true;
    // moduleConfig.external_notification.nag_timeout = 300;
    // moduleConfig.external_notification.output_ms = 1000;
    // moduleConfig.external_notification.use_i2s_as_buzzer = true;
    // moduleConfig.external_notification.alert_message_buzzer = true;

    if (moduleConfig.external_notification.enabled) {
        if (inputBroker) // put our callback in the inputObserver list
            inputObserver.observe(inputBroker);

        if (nodeDB->loadProto(rtttlConfigFile, meshtastic_RTTTLConfig_size, sizeof(meshtastic_RTTTLConfig),
                              &meshtastic_RTTTLConfig_msg, &rtttlConfig) != LoadFileResult::LOAD_SUCCESS) {
            memset(rtttlConfig.ringtone, 0, sizeof(rtttlConfig.ringtone));
            // The default ringtone is always loaded from userPrefs.jsonc
            strncpy(rtttlConfig.ringtone, USERPREFS_RINGTONE_RTTTL, sizeof(rtttlConfig.ringtone));
        }

        LOG_INFO("Init External Notification Module");

        output = moduleConfig.external_notification.output ? moduleConfig.external_notification.output
                                                           : EXT_NOTIFICATION_MODULE_OUTPUT;

        // Set the direction of a pin
        if (output > 0) {
            LOG_INFO("Use Pin %i in digital mode", output);
            pinMode(output, OUTPUT);
        }
        setExternalState(0, false);
        externalTurnedOn[0] = 0;
        if (moduleConfig.external_notification.output_vibra) {
            LOG_INFO("Use Pin %i for vibra motor", moduleConfig.external_notification.output_vibra);
            pinMode(moduleConfig.external_notification.output_vibra, OUTPUT);
            setExternalState(1, false);
            externalTurnedOn[1] = 0;
        }
        if (moduleConfig.external_notification.output_buzzer && canBuzz()) {
            if (!moduleConfig.external_notification.use_pwm) {
                LOG_INFO("Use Pin %i for buzzer", moduleConfig.external_notification.output_buzzer);
                pinMode(moduleConfig.external_notification.output_buzzer, OUTPUT);
                setExternalState(2, false);
                externalTurnedOn[2] = 0;
            } else {
                config.device.buzzer_gpio = config.device.buzzer_gpio ? config.device.buzzer_gpio : PIN_BUZZER;
                // in PWM Mode we force the buzzer pin if it is set
                LOG_INFO("Use Pin %i in PWM mode", config.device.buzzer_gpio);
            }
        }
#ifdef HAS_NCP5623
        if (rgb_found.type == ScanI2C::NCP5623) {
            rgb.begin();
            rgb.setCurrent(10);
        }
#endif
#ifdef HAS_LP5562
        if (rgb_found.type == ScanI2C::LP5562) {
            rgbw.begin();
            rgbw.setCurrent(20);
        }
#endif
#ifdef RGBLED_RED
        pinMode(RGBLED_RED, OUTPUT); // set up the RGB led pins
        pinMode(RGBLED_GREEN, OUTPUT);
        pinMode(RGBLED_BLUE, OUTPUT);
#endif
#ifdef RGBLED_CA
        analogWrite(RGBLED_RED, 255);   // with a common anode type, logic is reversed
        analogWrite(RGBLED_GREEN, 255); // so we want to initialise with lights off
        analogWrite(RGBLED_BLUE, 255);
#endif
#ifdef HAS_NEOPIXEL
        pixels.begin(); // Initialise the pixel(s)
        pixels.clear(); // Set all pixel colors to 'off'
        pixels.setBrightness(moduleConfig.ambient_lighting.current);
#endif
    } else {
        LOG_INFO("External Notification Module Disabled");
        disable();
    }
}

ProcessMessage ExternalNotificationModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (moduleConfig.external_notification.enabled && !isMuted) {
#ifdef T_WATCH_S3
        drv.setWaveform(0, 75);
        drv.setWaveform(1, 56);
        drv.setWaveform(2, 0);
        drv.go();
#endif
        if (!isFromUs(&mp)) {
            // Check if the message contains a bell character. Don't do this loop for every pin, just once.
            auto &p = mp.decoded;
            bool containsBell = false;
            for (int i = 0; i < p.payload.size; i++) {
                if (p.payload.bytes[i] == ASCII_BELL) {
                    containsBell = true;
                }
            }

            if (moduleConfig.external_notification.alert_bell) {
                if (containsBell) {
                    LOG_INFO("externalNotificationModule - Notification Bell");
                    isNagging = true;
                    setExternalState(0, true);
                    if (moduleConfig.external_notification.nag_timeout) {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.nag_timeout * 1000;
                    } else {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.output_ms;
                    }
                }
            }

            if (moduleConfig.external_notification.alert_bell_vibra) {
                if (containsBell) {
                    LOG_INFO("externalNotificationModule - Notification Bell (Vibra)");
                    isNagging = true;
                    setExternalState(1, true);
                    if (moduleConfig.external_notification.nag_timeout) {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.nag_timeout * 1000;
                    } else {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.output_ms;
                    }
                }
            }

            if (moduleConfig.external_notification.alert_bell_buzzer && canBuzz()) {
                if (containsBell) {
                    LOG_INFO("externalNotificationModule - Notification Bell (Buzzer)");
                    isNagging = true;
                    if (!moduleConfig.external_notification.use_pwm && !moduleConfig.external_notification.use_i2s_as_buzzer) {
                        setExternalState(2, true);
                    } else {
#ifdef HAS_I2S
                        if (moduleConfig.external_notification.use_i2s_as_buzzer) {
                            audioThread->beginRttl(rtttlConfig.ringtone, strlen_P(rtttlConfig.ringtone));
                        } else
#endif
                            if (moduleConfig.external_notification.use_pwm) {
                            rtttl::begin(config.device.buzzer_gpio, rtttlConfig.ringtone);
                        }
                    }
                    if (moduleConfig.external_notification.nag_timeout) {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.nag_timeout * 1000;
                    } else {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.output_ms;
                    }
                }
            }

            if (moduleConfig.external_notification.alert_message) {
                LOG_INFO("externalNotificationModule - Notification Module");
                isNagging = true;
                setExternalState(0, true);
                if (moduleConfig.external_notification.nag_timeout) {
                    nagCycleCutoff = millis() + moduleConfig.external_notification.nag_timeout * 1000;
                } else {
                    nagCycleCutoff = millis() + moduleConfig.external_notification.output_ms;
                }
            }

            if (moduleConfig.external_notification.alert_message_vibra) {
                LOG_INFO("externalNotificationModule - Notification Module (Vibra)");
                isNagging = true;
                setExternalState(1, true);
                if (moduleConfig.external_notification.nag_timeout) {
                    nagCycleCutoff = millis() + moduleConfig.external_notification.nag_timeout * 1000;
                } else {
                    nagCycleCutoff = millis() + moduleConfig.external_notification.output_ms;
                }
            }

            if (moduleConfig.external_notification.alert_message_buzzer) {
                LOG_INFO("externalNotificationModule - Notification Module (Buzzer)");
                isNagging = true;
                if (!moduleConfig.external_notification.use_pwm && !moduleConfig.external_notification.use_i2s_as_buzzer) {
                    setExternalState(2, true);
                } else {
#ifdef HAS_I2S
                    if (moduleConfig.external_notification.use_i2s_as_buzzer) {
                        audioThread->beginRttl(rtttlConfig.ringtone, strlen_P(rtttlConfig.ringtone));
                    } else
#endif
                        if (moduleConfig.external_notification.use_pwm) {
                        rtttl::begin(config.device.buzzer_gpio, rtttlConfig.ringtone);
                    }
                }
                if (moduleConfig.external_notification.nag_timeout) {
                    nagCycleCutoff = millis() + moduleConfig.external_notification.nag_timeout * 1000;
                } else {
                    nagCycleCutoff = millis() + moduleConfig.external_notification.output_ms;
                }
            }
            setIntervalFromNow(0); // run once so we know if we should do something
        }
    } else {
        LOG_INFO("External Notification Module Disabled or muted");
    }

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

/**
 * @brief An admin message arrived to AdminModule. We are asked whether we want to handle that.
 *
 * @param mp The mesh packet arrived.
 * @param request The AdminMessage request extracted from the packet.
 * @param response The prepared response
 * @return AdminMessageHandleResult HANDLED if message was handled
 *   HANDLED_WITH_RESULT if a result is also prepared.
 */
AdminMessageHandleResult ExternalNotificationModule::handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                                 meshtastic_AdminMessage *request,
                                                                                 meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result;

    switch (request->which_payload_variant) {
    case meshtastic_AdminMessage_get_ringtone_request_tag:
        LOG_INFO("Client getting ringtone");
        this->handleGetRingtone(mp, response);
        result = AdminMessageHandleResult::HANDLED_WITH_RESPONSE;
        break;

    case meshtastic_AdminMessage_set_ringtone_message_tag:
        LOG_INFO("Client setting ringtone");
        this->handleSetRingtone(request->set_canned_message_module_messages);
        result = AdminMessageHandleResult::HANDLED;
        break;

    default:
        result = AdminMessageHandleResult::NOT_HANDLED;
    }

    return result;
}

void ExternalNotificationModule::handleGetRingtone(const meshtastic_MeshPacket &req, meshtastic_AdminMessage *response)
{
    LOG_INFO("*** handleGetRingtone");
    if (req.decoded.want_response) {
        response->which_payload_variant = meshtastic_AdminMessage_get_ringtone_response_tag;
        strncpy(response->get_ringtone_response, rtttlConfig.ringtone, sizeof(response->get_ringtone_response));
    } // Don't send anything if not instructed to. Better than asserting.
}

void ExternalNotificationModule::handleSetRingtone(const char *from_msg)
{
    int changed = 0;

    if (*from_msg) {
        changed |= strcmp(rtttlConfig.ringtone, from_msg);
        strncpy(rtttlConfig.ringtone, from_msg, sizeof(rtttlConfig.ringtone));
        LOG_INFO("*** from_msg.text:%s", from_msg);
    }

    if (changed) {
        nodeDB->saveProto(rtttlConfigFile, meshtastic_RTTTLConfig_size, &meshtastic_RTTTLConfig_msg, &rtttlConfig);
    }
}

int ExternalNotificationModule::handleInputEvent(const InputEvent *event)
{
    if (nagCycleCutoff != UINT32_MAX) {
        stopNow();
        return 1;
    }
    return 0;
}