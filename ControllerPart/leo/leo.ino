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
#include <EEPROM.h>
#include <Keyboard.h>
Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_GAMEPAD, 14, 0,
                   true, true, false, false, false, false, false, false, false, false, false);

boolean hidMode = true, state[1] = {false}, set[2] = {false};
int encTT = 0, TTold = 0;
unsigned long ReportRate;
unsigned long TTmillis;
void doEncoder0();

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

#define BUTTON_1 1
#define BUTTON_2 2
#define BUTTON_3 3
#define BUTTON_4 4
#define BUTTON_5 5
#define BUTTON_6 6
#define BUTTON_7 7
#define BUTTON_Select 8
#define BUTTON_Start 9
#define BUTTON_VEFX 10
#define BUTTON_EFFECT 11

//for example : isPress(BUTTON_1)
#define isPress(key) !(digitalRead(ButtonPins[key]))

typedef struct
{
    int ButtonMode = BUTTON_MODE_JOYSTICK;
    int ScratchMode = SCRATCH_MODE_ANALOG;
    int ButtonsKeycode[ButtonCount] = {'a', 'b', 'c', 'd', 'e', 'f', 'g'};
    int VerifyCode = 2857;
} Config;

Config config;

#pragma region Scratch Process
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
    if (encTT != TTold)
    {
        if (encTT < -TTdz)
        {
            Joystick.setButton(11, 1);
            Joystick.setButton(12, 0);
            TTmillis = millis();
            encTT = 0;
        }
        else if (encTT > TTdz)
        {
            Joystick.setButton(11, 0);
            Joystick.setButton(12, 1);
            TTmillis = millis();
            encTT = 0;
        }
    }
    if ((millis() - TTmillis) > TTdelay)
    {
        Joystick.setButton(11, 0);
        Joystick.setButton(12, 0);
        TTmillis = millis();
        encTT = 0;
    }
    TTold = encTT;
}
#pragma endregion

#pragma region Buttons Process
void (*buttonsLoop)();
void buttonsJoystickLoop()
{
    for (int i = 0; i < ButtonCount; i++)
    {
        uint8_t d = isPress(i);
        Joystick.setButton(i, d);
    }
    Serial.println(isPress(BUTTON_Start));
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
            Serial.println("wait for key release.....");
            for (int i = 0; i < SingleCount; i++)
            {
                digitalWrite(SinglePins[i], LOW);
            }
        }
    }
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
    Serial.println("begin loaded config...");
    for (size_t i = 0; i < sizeof(Config); i++)
        ((char *)&config)[i] = EEPROM.read(i);
    if (config.VerifyCode != 2857)
    {
        Config c;
        config = c;
        Serial.println("config loaded failed, reset.");
    }
    else
    {
        Serial.println("config loaded.");
    }
    Serial.println("setupConfig() end.");
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

void setup()
{
    Serial.begin(9600);
    Serial.println("Begin searial print output.");

    setupConfig();
    setupJoystick();
    setupPins();
    setupScratch();

    waitDone();

    for (int i = 0; i < SingleCount; i++)
        digitalWrite(SinglePins[i], LOW);
    Serial.println("reset all pins status.");

    bootLight();

    Serial.println("Setup end.");
} //end setup

void buttonLoop()
{
    if (!hidMode)
        return;

    for (int i = 0; i < ButtonCount; i++)
    {
        digitalWrite(SinglePins[i], !(digitalRead(ButtonPins[i])));
    }
}

void loop()
{
    ReportRate = micros();

    buttonsLoop();
    scratchLoop();

    Joystick.sendState();
    delayMicroseconds(ReportDelay);
}

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
