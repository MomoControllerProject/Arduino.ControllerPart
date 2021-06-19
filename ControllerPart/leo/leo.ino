/* Arduino IIDX Controller Code for Leonardo
 * 1 Encoders + 11 Buttons + 11 HID controlable LED
 * with switchable analog/digital turntable output
 * release page
 * http://knuckleslee.blogspot.com/2018/06/RhythmCodes.html
 * 
 * Arduino Joystick Library
 * https://github.com/MHeironimus/ArduinoJoystickLibrary/
 * mon's Arduino-HID-Lighting
 * https://github.com/mon/Arduino-HID-Lighting
 */
/* pin assignments
 *   TT Sensor to pin 0 and White to pin 1
 * current pin layout
 *  SinglePins {2,4,6,8,10,12,18,20,22,14,16} = LED 1 to 11
 *    connect pin to resistor and then + termnial of LED
 *    connect ground to - terminal of LED
 *  ButtonPins {3,5,7,9,11,13,19,21,23,15,17} = Button input 1 to 11
 *    connect button pin to ground to trigger button press
 *  Light mode detection by read first button while connecting usb 
 *   hold    = false = reactive lighting 
 *   release = true  = HID lighting
 *  TT mode detection by read second button while connecting usb 
 *   hold    = false = digital turntable mode
 *   release = true  =  analog turntable mode
 */
#include <Joystick.h>
#include "serialPrintf.h"
#include "stdlib.h"
#include <EEPROM.h>
#include <ArduinoJson.h>
#include "math.h"
#include <Keyboard.h>

Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_GAMEPAD, 14, 0,
                   true, true, false, false, false, false, false, false, false, false, false);

boolean hidMode = true, state[1] = {false}, set[2] = {false};
int encTT = 0, TTold = 0;
unsigned long ReportRate;
unsigned long TTmillis;

void dispatchJsonMessage(const DynamicJsonDocument &);

#define ReportDelay 1000
#define GEAR 60     //number of gear teeth or ppr of encoder
#define TTdz 0      //digital tt deadzone (pulse)
#define TTdelay 100 //digital tt button release delay (millisecond)
byte EncPins[] = {0, 1};
byte SinglePins[] = {2, 3, 4, 5, 6, 7, 8, 9, 10};
byte ButtonPins[] = {11, 12, 13, 18, 19, 20, 21, 22, 23};

const int ButtonCount = sizeof(ButtonPins) / sizeof(ButtonPins[0]);
const int SingleCount = sizeof(SinglePins) / sizeof(SinglePins[0]);
const int EncPinCount = sizeof(EncPins) / sizeof(EncPins[0]);

#define BUTTON_MODE_JOYSTICK 0
#define BUTTON_MODE_KEYBOARD 1

#define SCRATCH_MODE_EMPTY 0
#define SCRATCH_MODE_ANALOG 1
#define SCRATCH_MODE_DIGIT 2

#define BUTTON_1 2
#define BUTTON_2 3
#define BUTTON_3 4
#define BUTTON_4 5
#define BUTTON_5 6
#define BUTTON_6 7
#define BUTTON_7 8
#define BUTTON_Select 1
#define BUTTON_Start 0
#define BUTTON_VEFX 9
#define BUTTON_EFFECT 10

//for example : isPress(BUTTON_1)
#define isPress(key) !(digitalRead(ButtonPins[key]))

#pragma region Config

typedef struct
{
    int ButtonMode = BUTTON_MODE_JOYSTICK;
    int ScratchMode = SCRATCH_MODE_ANALOG;
    int ButtonsKeycode[ButtonCount] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i'};
    int ScratchKeycode[2] = {'j', 'k'};
    int VerifyCode = 2857;
} Config;

Config config;

void loadConfig()
{
    Serial.println("begin load config...");
    for (size_t i = 0; i < sizeof(Config); i++)
        ((char *)&config)[i] = EEPROM.read(i);
    if (config.VerifyCode != 2857)
    {
        Config c;
        config = c;
        Serial.println("config load failed, reset.");
    }
    else
    {
        Serial.println("config loaded.");
    }
}

void saveConfig()
{
    Serial.println("begin save config...");
    for (size_t i = 0; i < sizeof(Config); i++)
        EEPROM.write(i, ((char *)&config)[i]);
    Serial.println("config saved.");
}

#pragma endregion

#pragma region Message Process

#define MESSAGE_BUFFER_SIZE 128
char messageInputBuffer[MESSAGE_BUFFER_SIZE + 1];
int messageInputBufferPos = 0;
int prevCh = 0;
int stack = 0;
boolean quoteMarking = false;
boolean messageBroken = false;

void resetMessageInputStatus()
{
    messageInputBufferPos = 0;
    messageBroken = false;
    quoteMarking = false;
    stack = 0;
    prevCh = -1;
}

void processMessageInput()
{
    const char *jsonStr = messageInputBuffer;
    DynamicJsonDocument doc(MESSAGE_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error.code() == DeserializationError::Code::Ok)
    {
        Serial.print("recv json : ");
        Serial.println(jsonStr);

        dispatchJsonMessage(doc);
    }
    else
    {
        Serial.print("parse message content into json failed :");
        Serial.println(jsonStr);
    }
}

void appendMessageChar(int ch)
{
    if (messageInputBufferPos >= MESSAGE_BUFFER_SIZE)
    {
        messageBroken = true;
    }
    else
    {
        messageInputBuffer[messageInputBufferPos++] = ch;
    }
}

#pragma endregion

#pragma region Scratch Process
//Interrupts encoder status changing...
void doEncoder0()
{
    int encpin0 = analogRead(EncPins[0]);
    Serial.println(encpin0);

    if (state[0] == false && digitalRead(EncPins[0]) == LOW)
    {
        set[0] = digitalRead(EncPins[1]);
        state[0] = true;
    }
    if (state[0] == true && digitalRead(EncPins[0]) == HIGH)
    {
        set[1] = !digitalRead(EncPins[1]);
        if (set[0] == true && set[1] == true)
        {
            encTT++;
        }
        if (set[0] == false && set[1] == false)
        {
            encTT--;
        }
        state[0] = false;
    }
}
void (*scratchLoop)();
void scratchEmptyLoop() {}
void scratchAnalogLoop()
{
    if (encTT < -GEAR / 2 || encTT > GEAR / 2 - 1)
        encTT = constrain(encTT * -1, -GEAR / 2, GEAR / 2 - 1);
    Joystick.setXAxis(encTT);

    digitalWrite(SinglePins[1], HIGH);
}
void scratchDigitalLoop()
{
#define setButton(joystickId, isPress)                          \
    if (config.ButtonMode == BUTTON_MODE_JOYSTICK)              \
        Joystick.setButton(joystickId, isPress);                \
    else if (isPress)                                           \
        Keyboard.press(config.ScratchKeycode[joystickId - 11]); \
    else                                                        \
        Keyboard.release(config.ScratchKeycode[joystickId - 11]);

    if (encTT != TTold)
    {
        if (encTT < -TTdz)
        {
            setButton(11, 1);
            setButton(12, 0);
            TTmillis = millis();
            encTT = 0;
        }
        else if (encTT > TTdz)
        {
            setButton(11, 0);
            setButton(12, 1);
            TTmillis = millis();
            encTT = 0;
        }
    }
    if ((millis() - TTmillis) > TTdelay)
    {
        setButton(11, 0);
        setButton(12, 0);
        TTmillis = millis();
        encTT = 0;
    }
    TTold = encTT;
#undef setButton
}
#pragma endregion

#pragma region Buttons Process
void (*buttonsLoop)();
void buttonsEmptyLoop() {}
void buttonsJoystickLoop()
{
    for (int i = 0; i < ButtonCount; i++)
    {
        uint8_t d = isPress(i);
        Joystick.setButton(i, d);
        if (d)
        {
            Serial.print("button ");
            Serial.print(i);
            Serial.println(" is pressed.");
        }
    }
}
void buttonsKeyboardLoop()
{
    for (int i = 0; i < ButtonCount; i++)
    {
        if (isPress(i))
        {
            Keyboard.press(config.ButtonsKeycode[i]);
        }
        else
        {
            Keyboard.release(config.ButtonsKeycode[i]);
        }
    }
}
#pragma endregion

void waitDone()
{
    Serial.println("wait for key release.....");
    while (digitalRead(ButtonPins[0]) == LOW | digitalRead(ButtonPins[1]) == LOW)
    {
        if ((millis() % 1000) < 500)
        {
            for (int i = 0; i < SingleCount; i++)
            {
                digitalWrite(SinglePins[i], HIGH);
            }
        }
        else if ((millis() % 1000) > 500)
        {
            for (int i = 0; i < SingleCount; i++)
            {
                digitalWrite(SinglePins[i], LOW);
            }
        }
    }

    Serial.println("waitDone() fin.");
}

void bootLight()
{
    for (int i = 0; i < ButtonCount; i++)
    {
        digitalWrite(SinglePins[i], HIGH);
        delay(80);
        digitalWrite(SinglePins[i], LOW);
    }
    for (int i = ButtonCount - 2; i >= 0; i--)
    {
        digitalWrite(SinglePins[i], HIGH);
        delay(80);
        digitalWrite(SinglePins[i], LOW);
    }
    for (int i = 0; i < ButtonCount; i++)
    {
        digitalWrite(SinglePins[i], HIGH);
    }
    delay(500);
    for (int i = 0; i < ButtonCount; i++)
    {
        digitalWrite(SinglePins[i], LOW);
    }
}

void setupConfig()
{
    Serial.println("setupConfig() start.");
    loadConfig();
    Serial.println("setupConfig() end.");
}

void setupButtons()
{
    if (config.ButtonMode == BUTTON_MODE_JOYSTICK)
    {
        buttonsLoop = buttonsJoystickLoop;
        Serial.println("setupButtons() buttonsLoop = buttonsJoystickLoop.");
    }
    else if (config.ButtonMode == BUTTON_MODE_KEYBOARD)
    {
        buttonsLoop = buttonsKeyboardLoop;
        Serial.println("setupButtons() buttonsLoop = buttonsKeyboardLoop.");
    }
    else
    {
        buttonsLoop = buttonsEmptyLoop;
        Serial.println("setupButtons() buttonsLoop = buttonsEmptyLoop.");
    }
}

void setupJoystick()
{
    Serial.println("setupJoystick() start.");
    Joystick.begin(false);
    Joystick.setXAxisRange(-GEAR / 2, GEAR / 2 - 1);
    Joystick.setYAxisRange(-GEAR / 2, GEAR / 2 - 1);
    Serial.println("setupJoystick() end.");
}

void setupPins()
{
    Serial.println("setupPins() start.");
    // setup I/O for pins
    for (int i = 0; i < ButtonCount; i++)
    {
        pinMode(ButtonPins[i], INPUT_PULLUP);
    }
    for (int i = 0; i < SingleCount; i++)
    {
        pinMode(SinglePins[i], OUTPUT);
    }
    for (int i = 0; i < EncPinCount; i++)
    {
        pinMode(EncPins[i], INPUT_PULLUP);
    }
    Serial.println("Setup IO for pins and encoder");
    Serial.println("setupPins() end.");
}

void setupScratch()
{
    Serial.println("setupScratch() start.");
    //setup interrupts
    attachInterrupt(digitalPinToInterrupt(EncPins[0]), doEncoder0, CHANGE);

    // light and turntable mode detection by buttons pressed.
    if ((digitalRead(ButtonPins[1]) == LOW) && (digitalRead(ButtonPins[0]) == LOW))
    {
        //both press and mean disable TT.
        scratchLoop = scratchEmptyLoop;
        Serial.println("scratchLoop = scratchEmptyLoop");
    }
    else
    {
        if (digitalRead(ButtonPins[1]))
        {
            scratchLoop = scratchAnalogLoop;
            Serial.println("scratchLoop = scratchAnalogLoop");
        }
        else
        {
            scratchLoop = scratchDigitalLoop;
            Serial.println("scratchLoop = scratchDigitalLoop");
        }
    }
    Serial.println("setupScratch() end.");
}

void ledLoop()
{
    if (!hidMode)
        return;

    for (int i = 0; i < ButtonCount; i++)
    {
        digitalWrite(SinglePins[i], isPress(i));
    }
}

void messageLoop()
{
    while (Serial.available())
    {
        int ch = Serial.read();
        switch (ch)
        {
        case '"':
            appendMessageChar(ch);
            if (prevCh == '\\')
                break;
            quoteMarking = !quoteMarking;
            break;
        case '{':
            if (prevCh == '\\')
                break;
            appendMessageChar(ch);
            if (!quoteMarking)
                stack++;
            break;
        case '}':
            if (prevCh == '\\')
                break;
            appendMessageChar(ch);
            if (!quoteMarking)
            {
                stack = max(0, stack - 1);
                if (stack == 0 && messageInputBufferPos > 0)
                {
                    //build string.
                    messageInputBuffer[messageInputBufferPos] = 0;

                    if (!messageBroken)
                    {
                        //the message content is good to parsing.
                        processMessageInput();
                    }
                    else
                    {
                        Serial.print("ERROR MESSAGE CONTENT PART:");
                        Serial.println(messageInputBuffer);
                    }

                    //reset status
                    resetMessageInputStatus();
                }
            }
            break;
        default:
            if (stack > 0)
                appendMessageChar(ch);
            break;
        }
        prevCh = ch;
    }
}

void setup()
{
    Serial.begin(9600);
    if (isPress(BUTTON_Start))
        delay(1250); //delay for serial printing.
    Serial.println("Begin searial print output.");

    setupConfig();
    setupJoystick();
    setupButtons();
    setupPins();
    setupScratch();
    resetMessageInputStatus();

    waitDone();

    for (int i = 0; i < SingleCount; i++)
        digitalWrite(SinglePins[i], LOW);
    Serial.println("reset all pins status.");

    bootLight();

    Serial.println("Setup end.");
} //end setup

void loop()
{
    ReportRate = micros();

    messageLoop();
    buttonsLoop();
    scratchLoop();
    ledLoop();

    Joystick.sendState();
    delayMicroseconds(ReportDelay);
}

void onChangeButtonKeycode(const DynamicJsonDocument &json)
{
    int button = json["button"];
    int keycode = json["keycode"];
    int beforeKeycode = config.ButtonsKeycode[button];
    config.ButtonsKeycode[button] = keycode;
    saveConfig();

    Serial.print("onChangeKeyCode() change button ");
    Serial.print(button);
    Serial.print(" from keycode ");
    Serial.print(beforeKeycode);
    Serial.print(" to ");
    Serial.println(keycode);
}

void onChangeScratchKeycode(const DynamicJsonDocument &json)
{
    int button = json["button"];
    int keycode = json["keycode"];
    int beforeKeycode = config.ScratchKeycode[button];

    config.ScratchKeycode[button] = keycode;
    saveConfig();

    serialPrintf("onChangeScratchKeycode() changed scratch %d keycode from %d to %d\n", button, beforeKeycode, keycode);
}

void onEchoPrint(const DynamicJsonDocument &json)
{
    const char *content = json["content"];
    serialPrintf("onEchoPrint() %s\n", content);
}

void onConfig(const DynamicJsonDocument &json)
{
    const char *action = json["action"];
    if (!strcmp(action, "save"))
    {
        saveConfig();
    }
    else if (!strcmp(action, "load"))
    {
        loadConfig();
    }
    else if (!strcmp(action, "reset"))
    {
        config = Config();
        saveConfig();
    }
    else if (!strcmp(action, "print"))
    {
        StaticJsonDocument<256> va;
        JsonArray array = va.createNestedArray("ButtonsKeycode");
        for (size_t i = 0; i < ButtonCount; i++)
            array.add(config.ButtonsKeycode[i]);
        array = va.createNestedArray("ScratchKeycode");
        for (size_t i = 0; i < 2; i++)
            array.add(config.ScratchKeycode[i]);
        va["ButtonMode"] = config.ButtonMode;
        va["ScratchMode"] = config.ScratchMode;

        serializeJson(va, Serial);
        Serial.println();
    }
    else
    {
        serialPrintf("onConfig() unknoen action %s.\n", action);
        return;
    }

    serialPrintf("onConfig() action %s done.\n", action);
}

void dispatchJsonMessage(const DynamicJsonDocument &json)
{
    const char *name = json["name"];

#define RegisterEvent(eventName, eventCallback)      \
    if (!strcmp(eventName, name))                    \
    {                                                \
        eventCallback(json);                         \
        serialPrintf("event %s processed.\n", name); \
        return;                                      \
    }

    RegisterEvent("ChangeKeyCode", onChangeButtonKeycode);
    RegisterEvent("ChangeScratchCode", onChangeScratchKeycode);
    RegisterEvent("Config", onConfig);
    RegisterEvent("EchoPrint", onEchoPrint);

    serialPrintf("event not processed : %s\n", name);

#undef RegisterEvent
}