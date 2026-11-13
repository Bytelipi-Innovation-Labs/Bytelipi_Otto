// Otto Humanoid (6 servos) + animated OLED face + buzzer + Wi-Fi hotspot + web dashboard
// ESP32-C3 Super Mini + Waveshare 1.5inch OLED (128x128, SSD1327, SPI)
//
// - Every move button changes the face and plays its own tune/beat on the buzzer.
// - Arms rest down at the sides. The "Arms up" button raises both arms (press again to lower).
// - Moves are ported from the Otto Humanoid library (walk, turn, moonwalker, crusaito, ...).
//
// Wi-Fi "Devel_Sixteen" / password 14911491  ->  http://192.168.4.1
//
// ---------------- WIRING ----------------
// OLED : VCC-3V3, GND-GND, DIN-GPIO6, CLK-GPIO4, CS-GPIO7, DC-GPIO3, RST-GPIO10
// Servo signal wires (Otto Humanoid order: YL, YR, RL, RR, LA, RA):
//   Left leg  (YL) -> GPIO0     Right leg  (YR) -> GPIO1
//   Left foot (RL) -> GPIO2     Right foot (RR) -> GPIO5
//   Left arm  (LA) -> GPIO20    Right arm  (RA) -> GPIO21
// Buzzer module (3 pins): VCC -> 3V3, GND -> GND, I/O (signal) -> GPIO8
//
// POWER: servos need their own 5-6V supply (2A or more), NOT the ESP32 pins.
//   Join the servo supply GND to the ESP32 GND and put a 470-1000uF capacitor on the servo supply.
//
// FIRST: tap "Servos to 90" in the dashboard BEFORE fitting the servo horns, so every servo is
// centred, then assemble. Test walking with the robot lifted in your hand.

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <U8g2lib.h>
#include "esp_timer.h"
#include <animated_faces.h>

// ---------- settings you can change ----------
const char* AP_SSID = "Devel_Sixteen";
const char* AP_PASS = "14911491";            // 8+ characters (or "" for open network)
constexpr uint32_t AUTO_INTERVAL_MS = 4000;
constexpr bool     WAIT_FOR_CONNECTION = true;  // true = keep the Wi-Fi name/password/address on the screen until a phone connects
constexpr uint32_t INFO_SCREEN_MS      = 5000;  // used only when WAIT_FOR_CONNECTION is false
constexpr uint32_t CONNECTED_SCREEN_MS = 2000;  // "Connected!" screen time after a phone joins
constexpr uint32_t FAILSAFE_MS      = 1200; // hold-moves stop if the page stops sending commands

// Servo order: 0 YL left leg, 1 YR right leg, 2 RL left foot, 3 RR right foot, 4 LA left arm, 5 RA right arm
const int  SERVO_PIN[6] = { 0, 1, 2, 5, 20, 21 };
// Calibration in degrees (same idea as Otto's setTrims). Adjust so each servo is centred at rest.
const int  TRIM[6]      = { 0, 0, 0, 0, 0, 0 };
// Direction of each servo. Otto reverses none, but the arms on this robot are mounted the opposite
// way, so both arms are reversed (true). If one arm still moves the wrong way, flip only that one.
const bool INVERT[6]    = { false, false, false, false, true, true };
// Speed slider 1..5 multiplies every period (smaller = faster). Level 3 = original Otto timing.
const float SPEED_SCALE[5] = { 1.5f, 1.25f, 1.0f, 0.8f, 0.65f };

// Arm angles in degrees {left arm, right arm} in Otto's convention (before INVERT is applied).
// Arms are mirrored, so the values are mirrored too. With INVERT true for both arms (above) the real
// arms get 20/160 when "down" and 160/20 when "up". Flip INVERT for both arms to reverse everything.
const float ARM_DOWN[2] = { 160, 20 };
const float ARM_UP[2]   = {  20, 160 };
constexpr float ARM_SPEED = 150.0f;          // degrees per second when raising/lowering arms

// Buzzer
constexpr int  PIN_BUZZER        = 8;        // signal pin of the 3-pin buzzer module
constexpr bool BUZZER_ACTIVE     = false;    // false = passive buzzer (different pitches), true = active (beats only)
constexpr bool BUZZER_ACTIVE_LOW = false;    // set true only if the buzzer sounds while idle
constexpr float TUNE_TIME_SCALE  = 0.7f;     // all notes are this long compared with the tune tables (0.5 = shorter, 1.0 = longer)
constexpr uint32_t LOOP_GAP_MS   = 80;       // short pause between repeats while a button is held
// ---------------------------------------------

constexpr int PIN_SCK = 4, PIN_MOSI = 6;
constexpr int PIN_CS = 7, PIN_DC = 3, PIN_RST = 10;

U8G2_SSD1327_WS_128X128_F_4W_HW_SPI u8g2(U8G2_R0, PIN_CS, PIN_DC, PIN_RST);
WebServer server(80);

constexpr int Y_OFFSET = (128 - animated_faces::Height) / 2;
static uint8_t raster[animated_faces::FrameBytes];
animated_faces::Player player(0);

const char* currentName = nullptr;
bool autoCycle = false;
unsigned autoIndex = 0;
uint32_t autoChangedAt = 0;

// One move request (defined here, before any function, so the Arduino IDE's automatic
// function prototypes can see the type)
struct Act { uint8_t id; uint8_t h; uint16_t T; };

// ======================= BUZZER =======================
struct Note { uint16_t f; uint16_t ms; };      // f = Hz (0 = silence), ms = length
struct Tune { const Note* n; uint8_t len; };
#define TUNE(arr) { arr, (uint8_t)(sizeof(arr) / sizeof(arr[0])) }

// ---- Move tunes: every move button has its own sound (they repeat while the button is held) ----
const Note N_FWD[]       = {{440,50},{0,30},{523,50},{0,30},{659,50},{0,30},{784,90}};        // marching steps, rising
const Note N_BACK[]      = {{1000,100},{0,100},{1000,100}};                                   // truck reversing beeps
const Note N_LEFT[]      = {{1400,25},{1200,25},{1000,25},{800,25},{600,25},{450,40}};        // whoosh sliding down
const Note N_RIGHT[]     = {{450,25},{600,25},{800,25},{1000,25},{1200,25},{1400,40}};        // whoosh sliding up
const Note N_MOONL[]     = {{330,110},{392,110},{330,110},{294,110},{262,160}};               // smooth bass groove
const Note N_MOONR[]     = {{262,90},{0,30},{262,90},{330,90},{0,30},{392,140}};              // funky bass line
const Note N_SWING[]     = {{880,50},{0,70},{660,50},{0,70},{880,50},{0,70},{660,50}};        // tick-tock pendulum
const Note N_TIPTOE[]    = {{330,35},{0,90},{349,35},{0,90},{392,35},{0,90},{440,35}};        // sneaky quiet steps
const Note N_UPDOWN[]    = {{523,70},{659,70},{784,70},{1047,70},{784,70},{659,70},{523,70}}; // elevator up and down
const Note N_JITTER[]    = {{1500,30},{1800,30},{1500,30},{1800,30},{1500,30},{1800,30}};     // shivering trill
const Note N_CRUS[]      = {{440,80},{0,30},{554,80},{0,30},{659,80},{0,30},{554,80}};        // hopping dance
const Note N_CRUSB[]     = {{659,80},{0,30},{554,80},{0,30},{440,80},{0,30},{330,100}};       // hopping dance, back
const Note N_FLAP[]      = {{1175,35},{1397,35},{1175,35},{1397,35},{1175,35},{1397,35}};     // fast wing flutter
const Note N_FLAPB[]     = {{880,35},{1047,35},{880,35},{1047,35},{880,35},{1047,35}};        // slower, lower flutter
const Note N_ASCEND[]    = {{392,70},{440,70},{494,70},{523,70},{587,70},{659,70},{784,100}}; // rising scale
const Note N_WAVEL[]     = {{784,90},{988,90},{784,90}};                                      // "hello"
const Note N_WAVER[]     = {{988,80},{784,80},{659,120}};                                     // "bye-bye"
const Note N_ARMSUP[]    = {{523,60},{659,60},{784,60},{1047,120}};
const Note N_ARMSDOWN[]  = {{1047,60},{784,60},{659,60},{523,120}};
const Note N_STOP[]      = {{330,60},{262,100}};
const Note N_BOOT[]      = {{523,90},{659,90},{784,90},{1047,160}};
const Note N_DANCE[]     = {{523,110},{659,110},{784,110},{659,110},{523,110},{659,110},{784,110},{1047,220},{0,60},
                            {880,110},{784,110},{659,110},{587,110},{523,110},{0,60},{784,110},{1047,220},{0,200}};

// ---- Sound-effect buttons (hold to play, repeats while held) ----
const Note N_CRY[]       = {{587,80},{523,80},{440,80},{392,80},{330,80},{294,80},{262,110}};
const Note N_LAUGH[]     = {{523,55},{0,28},{659,55},{0,28},{523,55},{0,28},{659,55},{0,28},{784,55},{0,28},{523,55}};
const Note N_VICTORY[]   = {{262,80},{330,80},{392,80},{523,80},{659,80},{784,260}};
const Note N_ALERT[]     = {{900,80},{1400,80},{900,80},{1400,80},{900,80},{1400,80}};
const Note N_SUCCESS[]   = {{440,60},{554,60},{659,60},{880,100}};
const Note N_HELLO[]     = {{659,60},{880,60},{0,30},{659,60},{880,100}};
const Note N_WOW[]       = {{600,40},{800,40},{1000,40},{1300,40},{1700,120}};
const Note N_YAWN[]      = {{440,150},{392,150},{349,170},{294,220}};
const Note N_LOVE[]      = {{523,80},{659,80},{784,160},{0,40},{659,80},{784,80},{1047,200}};
const Note N_ANGRY[]     = {{150,60},{120,60},{150,60},{120,60},{100,120}};
const Note N_ROBOT[]     = {{1800,40},{2200,30},{1500,30},{2500,40},{1200,30},{2000,60}};
const Note N_LASER[]     = {{3000,15},{2500,15},{2000,15},{1600,15},{1200,15},{900,15},{0,60}};
const Note N_POWERUP[]   = {{262,50},{330,50},{392,50},{523,50},{659,50},{784,50},{1047,120}};
const Note N_POWERDOWN[] = {{1047,50},{784,50},{659,50},{523,50},{392,50},{330,50},{262,120}};
const Note N_DOORBELL[]  = {{659,200},{523,300}};

// ---- Expression tunes: one short sound for each expression button (played once when tapped) ----
const Note N_E_NEUTRAL[]     = {{523,40}};
const Note N_E_HAPPY[]       = {{659,50},{880,50},{1047,90}};
const Note N_E_FUNNY[]       = {{500,30},{700,30},{900,30},{1100,30},{900,30},{700,30}};  // slide whistle
const Note N_E_SAD[]         = {{392,120},{370,120},{330,120},{294,220}};
const Note N_E_EMBARRASSED[] = {{880,40},{0,40},{784,40},{0,40},{698,40},{0,80},{660,100}};
const Note N_E_SURPRISED[]   = {{700,35},{1000,35},{1500,100}};
const Note N_E_SHOCKED[]     = {{2000,25},{300,25},{2000,25},{300,25},{1000,80}};
const Note N_E_THINKING[]    = {{392,120},{0,50},{440,120},{0,50},{415,160}};
const Note N_E_WINKING[]     = {{1047,40},{0,60},{1568,70}};
const Note N_E_COOL[]        = {{262,90},{330,90},{392,90},{311,160}};
const Note N_E_RELAXED[]     = {{392,200},{523,260}};
const Note N_E_DELICIOUS[]   = {{659,60},{784,60},{659,60},{988,120}};
const Note N_E_KISSY[]       = {{900,25},{1400,25},{1900,60}};
const Note N_E_CONFIDENT[]   = {{392,60},{392,60},{523,160}};
const Note N_E_SILLY[]       = {{392,90},{0,30},{392,90},{311,180}};                        // clown horn
const Note N_E_CONFUSED[]    = {{523,70},{554,70},{587,70},{659,70},{784,150}};

#define MAKE_TUNE(x) const Tune T_##x = TUNE(N_##x);
MAKE_TUNE(FWD) MAKE_TUNE(BACK) MAKE_TUNE(LEFT) MAKE_TUNE(RIGHT) MAKE_TUNE(MOONL) MAKE_TUNE(MOONR)
MAKE_TUNE(SWING) MAKE_TUNE(TIPTOE) MAKE_TUNE(UPDOWN) MAKE_TUNE(JITTER) MAKE_TUNE(CRUS) MAKE_TUNE(CRUSB)
MAKE_TUNE(FLAP) MAKE_TUNE(FLAPB) MAKE_TUNE(ASCEND) MAKE_TUNE(WAVEL) MAKE_TUNE(WAVER)
MAKE_TUNE(ARMSUP) MAKE_TUNE(ARMSDOWN) MAKE_TUNE(STOP) MAKE_TUNE(BOOT) MAKE_TUNE(DANCE)
MAKE_TUNE(CRY) MAKE_TUNE(LAUGH) MAKE_TUNE(VICTORY) MAKE_TUNE(ALERT) MAKE_TUNE(SUCCESS)
MAKE_TUNE(HELLO) MAKE_TUNE(WOW) MAKE_TUNE(YAWN) MAKE_TUNE(LOVE) MAKE_TUNE(ANGRY)
MAKE_TUNE(ROBOT) MAKE_TUNE(LASER) MAKE_TUNE(POWERUP) MAKE_TUNE(POWERDOWN) MAKE_TUNE(DOORBELL)
MAKE_TUNE(E_NEUTRAL) MAKE_TUNE(E_HAPPY) MAKE_TUNE(E_FUNNY) MAKE_TUNE(E_SAD) MAKE_TUNE(E_EMBARRASSED)
MAKE_TUNE(E_SURPRISED) MAKE_TUNE(E_SHOCKED) MAKE_TUNE(E_THINKING) MAKE_TUNE(E_WINKING) MAKE_TUNE(E_COOL)
MAKE_TUNE(E_RELAXED) MAKE_TUNE(E_DELICIOUS) MAKE_TUNE(E_KISSY) MAKE_TUNE(E_CONFIDENT) MAKE_TUNE(E_SILLY)
MAKE_TUNE(E_CONFUSED)

// Same order as the expressions in the face library:
// neutral happy laughing funny sad angry crying loving embarrassed surprised shocked
// thinking winking cool relaxed delicious kissy confident sleepy silly confused
const Tune* const EXPR_TUNES[] = {
  &T_E_NEUTRAL, &T_E_HAPPY, &T_LAUGH, &T_E_FUNNY, &T_E_SAD, &T_ANGRY, &T_CRY, &T_LOVE,
  &T_E_EMBARRASSED, &T_E_SURPRISED, &T_E_SHOCKED, &T_E_THINKING, &T_E_WINKING, &T_E_COOL,
  &T_E_RELAXED, &T_E_DELICIOUS, &T_E_KISSY, &T_E_CONFIDENT, &T_YAWN, &T_E_SILLY, &T_E_CONFUSED
};
constexpr unsigned EXPR_TUNE_COUNT = sizeof(EXPR_TUNES) / sizeof(EXPR_TUNES[0]);

bool soundOn = true;
const Note* tuneNotes = nullptr;
uint8_t tuneLen = 0, tuneIdx = 0;
uint32_t noteEndMs = 0;
bool toneRunning = false;
volatile bool toneLevel = false;
esp_timer_handle_t toneTimer = nullptr;
const Tune* curTune = nullptr;   // tune that is playing (or looping)
bool tuneLoop = false;           // true = repeat the tune until stopTune()
bool loopPending = false;
uint32_t loopRestartAt = 0;
bool dancing = false;

inline uint32_t scaledMs(uint16_t ms) {
  uint32_t v = (uint32_t)(ms * TUNE_TIME_SCALE);
  return v < 15 ? 15 : v;
}

inline void buzWrite(bool on) { digitalWrite(PIN_BUZZER, (on != BUZZER_ACTIVE_LOW) ? HIGH : LOW); }

static void toneCallback(void*) {   // toggles the pin to make a square wave
  toneLevel = !toneLevel;
  buzWrite(toneLevel);
}

void stopNote() {
  if (toneRunning) { esp_timer_stop(toneTimer); toneRunning = false; }
  buzWrite(false);
}

void startNote(uint16_t f) {
  stopNote();
  if (f == 0) return;
  if (BUZZER_ACTIVE) { buzWrite(true); return; }       // active buzzer: only on/off rhythm
  toneLevel = false;
  esp_timer_start_periodic(toneTimer, 500000UL / f);   // half period in microseconds
  toneRunning = true;
}

void buzzerInit() {
  pinMode(PIN_BUZZER, OUTPUT);
  buzWrite(false);
  esp_timer_create_args_t args = {};
  args.callback = toneCallback;
  args.arg = nullptr;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "buzzer";
  esp_timer_create(&args, &toneTimer);
}

void stopTune() {
  tuneNotes = nullptr;
  tuneLoop = false;
  loopPending = false;
  curTune = nullptr;
  stopNote();
}

void startTune(const Tune& t, bool loop) {
  if (!soundOn) return;
  curTune = &t;
  tuneLoop = loop;
  loopPending = false;
  tuneNotes = t.n;
  tuneLen = t.len;
  tuneIdx = 0;
  startNote(tuneNotes[0].f);
  noteEndMs = millis() + scaledMs(tuneNotes[0].ms);
}

void playTune(const Tune& t) { startTune(t, false); }   // plays once
void playLoop(const Tune& t) { startTune(t, true); }    // repeats until stopTune()

void updateBuzzer(uint32_t now) {
  if (loopPending && (int32_t)(now - loopRestartAt) >= 0) {
    loopPending = false;
    if (soundOn && curTune) startTune(*curTune, true);
    return;
  }
  if (!tuneNotes || (int32_t)(now - noteEndMs) < 0) return;
  tuneIdx++;
  if (tuneIdx >= tuneLen) {
    stopNote();
    tuneNotes = nullptr;
    if (tuneLoop && soundOn) { loopPending = true; loopRestartAt = now + LOOP_GAP_MS; }
    return;
  }
  startNote(tuneNotes[tuneIdx].f);
  noteEndMs = now + scaledMs(tuneNotes[tuneIdx].ms);
}

// ======================= OTTO MOVES =======================
enum GaitId : uint8_t {
  G_STOP = 0, G_FORWARD, G_BACKWARD, G_LEFT, G_RIGHT, G_MOON_L, G_MOON_R,
  G_SWING, G_UPDOWN, G_JITTER, G_CRUSAITO, G_CRUSAITO_B, G_FLAP, G_FLAP_B,
  G_TIPTOE, G_ASCEND, G_WAVE_L, G_WAVE_R, G_COUNT
};

// name used by the web page, default height h (degrees), default period T (ms) from the Otto library,
// face shown while the move runs, and the tune played when the move starts
struct Def { const char* name; uint8_t h; uint16_t T; const char* face; const Tune* tune; };
const Def DEFS[G_COUNT] = {
  {"stop",      0, 1000, "neutral",   &T_STOP},
  {"forward",   0, 1000, "happy",     &T_FWD},
  {"backward",  0, 1000, "surprised", &T_BACK},
  {"left",      0, 2000, "thinking",  &T_LEFT},
  {"right",     0, 2000, "winking",   &T_RIGHT},
  {"moonl",    20,  900, "cool",      &T_MOONL},
  {"moonr",    20,  900, "confident", &T_MOONR},
  {"swing",    20, 1000, "relaxed",   &T_SWING},
  {"updown",   20, 1000, "funny",     &T_UPDOWN},
  {"jitter",   20,  500, "shocked",   &T_JITTER},
  {"crusaito", 20,  900, "silly",     &T_CRUS},
  {"crusaitob",20,  900, "confused",  &T_CRUSB},
  {"flap",     20, 1000, "loving",    &T_FLAP},
  {"flapb",    20, 1000, "embarrassed", &T_FLAPB},
  {"tiptoe",   20,  900, "kissy",     &T_TIPTOE},
  {"ascend",   13,  900, "delicious", &T_ASCEND},
  {"wavel",     0,  500, "happy",     &T_WAVEL},
  {"waver",     0,  500, "loving",    &T_WAVER},
};

inline bool sameAct(const Act& a, const Act& b) { return a.id == b.id && a.h == b.h && a.T == b.T; }

// Sound-effect buttons: name used by the web page, tune, and the face shown while it plays
struct Fx { const char* name; const Tune* tune; const char* face; };
const Fx FXS[] = {
  {"cry",      &T_CRY,      "crying"},
  {"laugh",    &T_LAUGH,    "laughing"},
  {"victory",  &T_VICTORY,  "cool"},
  {"alert",    &T_ALERT,    "shocked"},
  {"success",  &T_SUCCESS,  "happy"},
  {"hello",    &T_HELLO,    "happy"},
  {"wow",      &T_WOW,      "surprised"},
  {"yawn",     &T_YAWN,     "sleepy"},
  {"love",     &T_LOVE,     "loving"},
  {"angry",    &T_ANGRY,    "angry"},
  {"robot",    &T_ROBOT,    "winking"},
  {"laser",    &T_LASER,    "confident"},
  {"powerup",  &T_POWERUP,  "confident"},
  {"powerdown",&T_POWERDOWN,"sleepy"},
  {"doorbell", &T_DOORBELL, "happy"},
};
constexpr int FX_COUNT = sizeof(FXS) / sizeof(FXS[0]);
bool fxHold = false;
int  fxId = -1;
uint32_t lastFxMs = 0;

float GA[6], GO[6], GP[6];      // amplitude, offset, phase (degrees) of the active gait
bool armsOwn = false;           // true if the active move moves the arms itself (moonwalk, swing, wave...)

static void fill6(float* d, float a, float b, float c, float e, float f, float g) {
  d[0] = a; d[1] = b; d[2] = c; d[3] = e; d[4] = f; d[5] = g;
}

// Parameters copied from Otto9Humanoid.cpp (walk/turn keep the arms down at the sides)
void loadGait(const Act& a) {
  const float h = a.h;
  fill6(GA, 0, 0, 0, 0, 0, 0);
  fill6(GO, 0, 0, 0, 0, 0, 0);
  fill6(GP, 0, 0, 0, 0, 0, 0);
  switch (a.id) {
    case G_FORWARD: case G_BACKWARD: {            // walk(): hips in phase, feet -90 deg (forward)
      float dir = (a.id == G_FORWARD) ? 1.0f : -1.0f;
      fill6(GA, 30, 30, 20, 20, 0, 0);
      fill6(GO, 0, 0, 4, -4, 0, 0);
      fill6(GP, 0, 0, -90 * dir, -90 * dir, 0, 0);
      break; }
    case G_LEFT:                                   // turn(): different hip amplitudes
      fill6(GA, 30, 10, 20, 20, 0, 0);
      fill6(GO, 0, 0, 4, -4, 0, 0);
      fill6(GP, 0, 0, -90, -90, 0, 0);
      break;
    case G_RIGHT:
      fill6(GA, 10, 30, 20, 20, 0, 0);
      fill6(GO, 0, 0, 4, -4, 0, 0);
      fill6(GP, 0, 0, -90, -90, 0, 0);
      break;
    case G_MOON_L: case G_MOON_R: {               // moonwalker()
      float dir = (a.id == G_MOON_L) ? 1.0f : -1.0f;
      float phi = -dir * 90.0f;
      fill6(GA, 0, 0, h, h, h, h);
      fill6(GO, 0, 0, h / 2 + 2, -h / 2 - 2, -h, h);
      fill6(GP, 0, 0, phi, -60 * dir + phi, phi, phi);
      break; }
    case G_SWING:                                  // swing()
      fill6(GA, 0, 0, h, h, h, h);
      fill6(GO, 0, 0, h / 2, -h / 2, h, -h);
      break;
    case G_TIPTOE:                                 // tiptoeSwing()
      fill6(GA, 0, 0, h, h, h, h);
      fill6(GO, 0, 0, h, -h, h, -h);
      break;
    case G_UPDOWN:                                 // updown()
      fill6(GA, 0, 0, h, h, h, h);
      fill6(GO, 0, 0, h, -h, h, -h);
      fill6(GP, 0, 0, -90, 90, -90, 90);
      break;
    case G_JITTER: {                               // jitter()
      float j = fminf(h, 25.0f);
      fill6(GA, j, j, 0, 0, 0, 0);
      fill6(GP, -90, 90, 0, 0, 0, 0);
      break; }
    case G_CRUSAITO: case G_CRUSAITO_B: {          // crusaito()
      float dir = (a.id == G_CRUSAITO) ? 1.0f : -1.0f;
      fill6(GA, 25, 25, h, h, 0, 0);
      fill6(GO, 0, 0, h / 2 + 4, -h / 2 - 4, 0, 0);
      fill6(GP, 90, 90, 0, -60 * dir, 0, 0);
      break; }
    case G_FLAP: case G_FLAP_B: {                  // flapping()
      float dir = (a.id == G_FLAP) ? 1.0f : -1.0f;
      fill6(GA, 12, 12, h, h, 0, 0);
      fill6(GO, 0, 0, h - 10, -h + 10, 0, 0);
      fill6(GP, 0, 180, -90 * dir, 90 * dir, 0, 0);
      break; }
    case G_ASCEND: {                               // ascendingTurn()
      float k = fminf(h, 13.0f);
      fill6(GA, k, k, k, k, 40, 40);
      fill6(GO, 0, 0, k + 4, -k + 4, 0, 0);
      fill6(GP, -90, 90, -90, 90, 0, 0);
      break; }
    case G_WAVE_L:                                 // armwave(): left-arm servo waves
      fill6(GA, 0, 0, 0, 0, 30, 0);
      fill6(GO, 0, 0, 0, 0, -30, -40);
      break;
    case G_WAVE_R:                                 // armwave(): right-arm servo waves
      fill6(GA, 0, 0, 0, 0, 0, 30);
      fill6(GO, 0, 0, 0, 0, 40, 60);
      break;
    default: break;
  }
  armsOwn = (GA[4] != 0 || GA[5] != 0 || GO[4] != 0 || GO[5] != 0);
}

// ---- Dance: sequence of Otto moves (from the Otto "happy dance" example), repeats until STOP ----
struct Step { uint8_t id; uint8_t h; uint16_t T; float cycles; };
const Step DANCE[] = {
  {G_JITTER,    20,  750, 2},
  {G_CRUSAITO,  30,  800, 1}, {G_CRUSAITO_B, 30, 800, 1}, {G_CRUSAITO, 30, 800, 1},
  {G_BACKWARD,   0, 1500, 1}, {G_FORWARD,     0, 1000, 2},
  {G_MOON_L,    30,  600, 1}, {G_MOON_R,     30,  600, 1},
  {G_MOON_L,    30,  600, 1}, {G_MOON_R,     30,  600, 1},
  {G_BACKWARD,   0, 1500, 1}, {G_FORWARD,     0, 1000, 2},
  {G_MOON_L,    30,  600, 2}, {G_MOON_R,     30,  600, 2},
  {G_UPDOWN,    20,  800, 2}, {G_SWING,      20,  900, 2},
  {G_FLAP,      20,  900, 2}, {G_FLAP_B,     20,  900, 2},
  {G_WAVE_L,     0,  500, 3}, {G_WAVE_R,      0,  500, 3},
  {G_TIPTOE,    20,  900, 2},
};
constexpr int DANCE_STEPS = sizeof(DANCE) / sizeof(DANCE[0]);

// ---- Engine state ----
Act want = {G_STOP, 0, 1000}, cur = {G_STOP, 0, 1000};
bool rawCenter = false;          // hold every servo at exactly 90 degrees (for assembly)
bool holdMode = false;           // true while a hold-to-move button is being pressed
bool armsRaised = false;         // "Arms up" toggle
float armPos[2] = { ARM_DOWN[0], ARM_DOWN[1] };
int  danceIdx = 0;
float danceCycles = 0;
uint32_t lastCmdMs = 0;
float gain = 0, phase = 0;
int speedLevel = 3;

void writeServo(int i, float angle) {
  angle = constrain(angle, 0.0f, 180.0f);
  float us = 500.0f + angle / 180.0f * 1900.0f;                   // 500..2400 us
  ledcWrite(SERVO_PIN[i], (uint32_t)(us * 16384.0f / 20000.0f));  // 14-bit duty at 50 Hz
}

// angle in "Otto degrees" (90 = centre) -> trim and reverse applied
inline void writeOtto(int i, float a) {
  writeServo(i, (INVERT[i] ? 180.0f - a : a) + TRIM[i]);
}

void servosInit() {
  for (int i = 0; i < 6; ++i) ledcAttach(SERVO_PIN[i], 50, 14);
  for (int i = 0; i < 4; ++i) writeOtto(i, 90);
  writeOtto(4, ARM_DOWN[0]);       // arms start down at the sides
  writeOtto(5, ARM_DOWN[1]);
}

void danceLoadStep() {
  const Step& s = DANCE[danceIdx];
  want = { s.id, s.h, s.T };
  danceCycles = 0;
}

void updateServos(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 20) return;
  float dt = (now - last) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  last = now;

  if (rawCenter) {
    for (int i = 0; i < 6; ++i) writeServo(i, 90);
    return;
  }

  // Ease out the old move, then switch to the new one (avoids sudden jumps)
  float target;
  if (!sameAct(want, cur)) {
    target = 0;
    if (gain < 0.03f) { cur = want; loadGait(cur); phase = 0; }
  } else {
    target = (cur.id == G_STOP) ? 0.0f : 1.0f;
  }
  float step = 6.0f * dt;
  gain += constrain(target - gain, -step, step);

  if (cur.id != G_STOP) {
    float periodS = cur.T * SPEED_SCALE[speedLevel - 1] / 1000.0f;
    float dphase = 2.0f * PI * dt / periodS;
    phase += dphase;
    if (phase > 2.0f * PI) phase -= 2.0f * PI;
    if (dancing && sameAct(want, cur)) {
      danceCycles += dphase / (2.0f * PI);
      if (danceCycles >= DANCE[danceIdx].cycles) {
        danceIdx = (danceIdx + 1) % DANCE_STEPS;
        danceLoadStep();
      }
    }
  }

  // Legs and feet
  for (int i = 0; i < 4; ++i) {
    float d = gain * (GO[i] + GA[i] * sinf(phase + GP[i] * PI / 180.0f));
    writeOtto(i, 90 + d);
  }

  // Arms: rest pose (down, or up when raised); moves that use the arms blend in on top
  for (int k = 0; k < 2; ++k) {
    int i = 4 + k;
    float goal = armsRaised ? ARM_UP[k] : ARM_DOWN[k];
    armPos[k] += constrain(goal - armPos[k], -ARM_SPEED * dt, ARM_SPEED * dt);
    float a = armPos[k];
    if (armsOwn) {
      float moveAngle = 90 + GO[i] + GA[i] * sinf(phase + GP[i] * PI / 180.0f);
      a = armPos[k] + gain * (moveAngle - armPos[k]);
    }
    writeOtto(i, a);
  }
}

// ======================= FACE =======================
void selectExpression(const char* name) {
  player.Select(name, millis(), true);
  currentName = name;
  Serial.println(name);
}

const char* findExpression(const String& text) {
  for (unsigned i = 0; i < animated_faces::ExpressionCount; ++i)
    if (text == animated_faces::ExpressionName(i)) return animated_faces::ExpressionName(i);
  return nullptr;
}

void showFace(const char* name) {
  const char* n = findExpression(String(name));
  if (!n) return;
  autoCycle = false;
  selectExpression(n);
}

// ======================= WEB PAGE =======================
const char PAGE_HEAD[] PROGMEM = R"rawliteral(<!doctype html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'>
<title>Varad's Otto Robot</title><style>
body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:16px;text-align:center}
h1{font-size:20px;margin:6px 0}h2{font-size:16px;margin:18px 0 6px;color:#aaa}
button{padding:14px 6px;font-size:15px;border:0;border-radius:10px;background:#2a2a2a;color:#eee}
button.on{background:#2e7d32}button:active{background:#444}
.pad{display:grid;grid-template-columns:repeat(3,92px);grid-template-rows:repeat(3,70px);gap:8px;justify-content:center;margin:10px auto}
.pad button,.xg button,.mw button{touch-action:none;user-select:none;-webkit-user-select:none;-webkit-touch-callout:none}
.mv{background:#1565c0}.mv:active{background:#2e7d32}
.stop{background:#b71c1c}.dance{background:#7b1fa2;font-size:17px;padding:14px 22px}
.row{display:flex;gap:8px;justify-content:center;align-items:center;flex-wrap:wrap;margin:8px 0}
.row button{padding:14px 16px}
.xg{display:grid;grid-template-columns:repeat(auto-fill,minmax(105px,1fr));gap:8px;max-width:520px;margin:8px auto}
.xg .mv{background:#37474f}.xg .mv:active{background:#2e7d32}
.xg .fx{background:#6a1b9a}.xg .fx:active{background:#2e7d32}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(110px,1fr));gap:10px;max-width:640px;margin:10px auto}
.auto{background:#1565c0}.auto.on{background:#2e7d32}
#arms{background:#ef6c00}#arms.on{background:#2e7d32}
</style></head><body><h1>Otto robot</h1>
<div class='pad'>
<button class='mv' data-d='forward'  style='grid-column:2;grid-row:1'>&#9650;<br>Forward</button>
<button class='mv' data-d='left'     style='grid-column:1;grid-row:2'>&#9664;<br>Left</button>
<button class='stop' onclick="mv('stop')" style='grid-column:2;grid-row:2'>STOP</button>
<button class='mv' data-d='right'    style='grid-column:3;grid-row:2'>&#9654;<br>Right</button>
<button class='mv' data-d='backward' style='grid-column:2;grid-row:3'>&#9660;<br>Backward</button>
</div>
<div class='row mw'>
<button class='mv' data-d='moonl'>&#9664; Moonwalk</button>
<button class='dance' onclick="fetch('/dance')">&#127926; DANCE</button>
<button class='mv' data-d='moonr'>Moonwalk &#9654;</button>
</div>
<div class='row'>
<button id='arms' onclick='toggleArms()'>Arms: down</button>
<button id='snd' onclick='toggleSound()'>Sound: on</button>
</div>
<h2>More moves (hold)</h2>
<div class='xg'>
<button class='mv' data-d='swing'>Swing</button>
<button class='mv' data-d='tiptoe'>Tiptoe swing</button>
<button class='mv' data-d='updown'>Up-down</button>
<button class='mv' data-d='jitter'>Jitter</button>
<button class='mv' data-d='crusaito'>Crusaito</button>
<button class='mv' data-d='crusaitob'>Crusaito back</button>
<button class='mv' data-d='flap'>Flapping</button>
<button class='mv' data-d='flapb'>Flapping back</button>
<button class='mv' data-d='ascend'>Ascending turn</button>
<button class='mv' data-d='wavel'>Wave L</button>
<button class='mv' data-d='waver'>Wave R</button>
</div>
<h2>Sound effects (hold)</h2>
<div class='xg'>
<button class='fx' data-n='cry'>&#128557; Cry</button>
<button class='fx' data-n='laugh'>&#128514; Laugh</button>
<button class='fx' data-n='victory'>&#127942; Victory</button>
<button class='fx' data-n='alert'>&#128680; Alert</button>
<button class='fx' data-n='success'>&#10024; Success</button>
<button class='fx' data-n='hello'>&#128075; Hello</button>
<button class='fx' data-n='wow'>&#128558; Wow</button>
<button class='fx' data-n='yawn'>&#128564; Yawn</button>
<button class='fx' data-n='love'>&#10084; Love</button>
<button class='fx' data-n='angry'>&#128545; Angry</button>
<button class='fx' data-n='robot'>&#129302; Robot talk</button>
<button class='fx' data-n='laser'>&#128299; Laser</button>
<button class='fx' data-n='powerup'>&#9889; Power up</button>
<button class='fx' data-n='powerdown'>&#128267; Power down</button>
<button class='fx' data-n='doorbell'>&#128276; Doorbell</button>
</div>
<div class='row'>Speed <input id='spd' type='range' min='1' max='5' value='3' onchange="fetch('/speed?v='+this.value)">
<button onclick="fetch('/center')">Servos to 90</button></div>
<h2>Expressions</h2>
<button id='auto' class='auto' onclick='toggleAuto()'>Auto cycle: off</button>
<div class='grid'>)rawliteral";

const char PAGE_TAIL[] PROGMEM = R"rawliteral(</div><script>
function mv(d){fetch('/move?d='+d).catch(function(){})}
var held=null,hb=null;
function press(d){if(held)return;held=d;mv(d);hb=setInterval(function(){mv(d)},300)}
function release(){if(!held)return;clearInterval(hb);held=null;mv('stop&q=1')}
document.querySelectorAll('.mv').forEach(function(b){
 b.addEventListener('pointerdown',function(e){e.preventDefault();press(b.dataset.d)});
 ['pointerup','pointercancel','pointerleave'].forEach(function(ev){b.addEventListener(ev,release)});
 b.addEventListener('contextmenu',function(e){e.preventDefault()});
});
function fx(n){fetch('/fx?n='+n).catch(function(){})}
var heldFx=null,hbFx=null;
function pressFx(n){if(heldFx)return;heldFx=n;fx(n);hbFx=setInterval(function(){fx(n)},300)}
function releaseFx(){if(!heldFx)return;clearInterval(hbFx);heldFx=null;fx('stop')}
document.querySelectorAll('.fx').forEach(function(b){
 b.addEventListener('pointerdown',function(e){e.preventDefault();pressFx(b.dataset.n)});
 ['pointerup','pointercancel','pointerleave'].forEach(function(ev){b.addEventListener(ev,releaseFx)});
 b.addEventListener('contextmenu',function(e){e.preventDefault()});
});
function relAll(){release();releaseFx()}
document.addEventListener('visibilitychange',relAll);window.addEventListener('blur',relAll);
function mark(s){document.querySelectorAll('.grid button').forEach(function(b){b.classList.toggle('on',b.dataset.e===s.e)});
 var a=document.getElementById('auto');a.classList.toggle('on',s.auto);
 a.textContent='Auto cycle: '+(s.auto?'on':'off');window.auto=s.auto;
 var r=document.getElementById('arms');r.classList.toggle('on',s.arms);
 r.textContent='Arms: '+(s.arms?'up':'down');window.arms=s.arms;
 var n=document.getElementById('snd');n.classList.toggle('on',s.snd);
 n.textContent='Sound: '+(s.snd?'on':'off');window.snd=s.snd}
function setE(n){fetch('/set?e='+n).then(function(r){return r.json()}).then(mark)}
function toggleAuto(){fetch('/auto?on='+(window.auto?0:1)).then(function(r){return r.json()}).then(mark)}
function toggleArms(){fetch('/arms?up='+(window.arms?0:1)).then(function(r){return r.json()}).then(mark)}
function toggleSound(){fetch('/sound?on='+(window.snd?0:1)).then(function(r){return r.json()}).then(mark)}
function poll(){fetch('/state').then(function(r){return r.json()}).then(mark).catch(function(){})}
poll();setInterval(poll,2000);
</script></body></html>)rawliteral";

String buildPage() {
  String h;
  h.reserve(8000);
  h += PAGE_HEAD;
  for (unsigned i = 0; i < animated_faces::ExpressionCount; ++i) {
    const char* n = animated_faces::ExpressionName(i);
    h += "<button data-e='"; h += n; h += "' onclick=\"setE('"; h += n; h += "')\">";
    h += n; h += "</button>";
  }
  h += PAGE_TAIL;
  return h;
}

String stateJson() {
  String s = "{\"e\":\"";
  s += currentName;
  s += "\",\"auto\":";
  s += autoCycle ? "true" : "false";
  s += ",\"arms\":";
  s += armsRaised ? "true" : "false";
  s += ",\"snd\":";
  s += soundOn ? "true" : "false";
  s += "}";
  return s;
}

void handleRoot()  { server.send(200, "text/html", buildPage()); }
void handleState() { server.send(200, "application/json", stateJson()); }

void handleSet() {
  const char* name = findExpression(server.arg("e"));
  if (!name) { server.send(400, "application/json", "{\"error\":\"unknown expression\"}"); return; }
  autoCycle = false;
  selectExpression(name);
  if (!dancing && !holdMode && !fxHold) {               // each expression has its own short sound
    for (unsigned i = 0; i < animated_faces::ExpressionCount; ++i)
      if (name == animated_faces::ExpressionName(i)) {
        if (i < EXPR_TUNE_COUNT) playTune(*EXPR_TUNES[i]);
        break;
      }
  }
  server.send(200, "application/json", stateJson());
}

void handleAuto() {
  autoCycle = server.arg("on") == "1";
  autoChangedAt = millis();
  server.send(200, "application/json", stateJson());
}

void handleMove() {
  String d = server.arg("d");
  bool quiet = server.arg("q") == "1";        // sent when a button is released
  int id = -1;
  for (int i = 0; i < G_COUNT; ++i)
    if (d == DEFS[i].name) { id = i; break; }
  if (id < 0) { server.send(400, "text/plain", "bad move"); return; }

  lastCmdMs = millis();
  if (id == G_STOP) {
    bool wasActive = holdMode || dancing;
    dancing = false;
    holdMode = false;
    want = { G_STOP, 0, 1000 };
    if (wasActive) {
      stopTune();                              // sound lasts only as long as the button is held
      showFace("neutral");                     // back to the neutral face
    }
    if (!quiet) playTune(T_STOP);              // blip only when the STOP button is pressed
  } else {
    bool isNew = !(holdMode && want.id == id);
    dancing = false;
    rawCenter = false;
    holdMode = true;
    armsRaised = false;                        // arms go down at the sides when a move starts
    want = { (uint8_t)id, DEFS[id].h, DEFS[id].T };
    if (isNew) {
      showFace(DEFS[id].face);
      playLoop(*DEFS[id].tune);                // repeats while the button is held
    }
  }
  server.send(200, "text/plain", "ok");
}

void handleDance() {
  rawCenter = false;
  holdMode = false;
  dancing = true;
  armsRaised = false;
  danceIdx = 0;
  danceLoadStep();
  showFace("laughing");
  playLoop(T_DANCE);                           // music loops until STOP
  server.send(200, "text/plain", "ok");
}

void handleArms() {
  armsRaised = server.arg("up") == "1";
  if (armsRaised) { showFace("surprised"); playTune(T_ARMSUP); }
  else            { showFace("neutral");   playTune(T_ARMSDOWN); }
  server.send(200, "application/json", stateJson());
}

void handleSound() {
  soundOn = server.arg("on") == "1";
  if (!soundOn) stopTune();
  server.send(200, "application/json", stateJson());
}

void handleFx() {
  String n = server.arg("n");
  lastFxMs = millis();
  if (n == "stop") {
    if (fxHold) {
      fxHold = false;
      fxId = -1;
      stopTune();
      showFace("neutral");
    }
    server.send(200, "text/plain", "ok");
    return;
  }
  int id = -1;
  for (int i = 0; i < FX_COUNT; ++i)
    if (n == FXS[i].name) { id = i; break; }
  if (id < 0) { server.send(400, "text/plain", "bad effect"); return; }
  if (!(fxHold && fxId == id)) {               // new press (the page repeats the same request while held)
    fxHold = true;
    fxId = id;
    showFace(FXS[id].face);
    playLoop(*FXS[id].tune);
  }
  server.send(200, "text/plain", "ok");
}

void handleSpeed() {
  speedLevel = constrain(server.arg("v").toInt(), 1, 5);
  server.send(200, "text/plain", "ok");
}

void handleCenter() {
  dancing = false;
  holdMode = false;
  stopTune();
  want = { G_STOP, 0, 1000 };
  cur = want;
  gain = 0;
  rawCenter = true;
  server.send(200, "text/plain", "ok");
}

// ======================= SETUP / LOOP =======================
void showInfoScreen() {
  String url = "http://" + WiFi.softAPIP().toString();
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 12, "Connect to Wi-Fi:");
  u8g2.drawStr(0, 30, "Name:");
  u8g2.drawStr(0, 43, AP_SSID);
  u8g2.drawStr(0, 61, "Password:");
  u8g2.drawStr(0, 74, strlen(AP_PASS) ? AP_PASS : "(open network)");
  u8g2.drawStr(0, 92, "Then open:");
  u8g2.drawStr(0, 105, url.c_str());
  u8g2.sendBuffer();
}

void showConnectedScreen() {
  String url = "http://" + WiFi.softAPIP().toString();
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_10x20_tf);
  u8g2.drawStr(10, 40, "Connected!");
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 75, "Open in browser:");
  u8g2.drawStr(0, 92, url.c_str());
  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  SPI.begin(PIN_SCK, -1, PIN_MOSI, PIN_CS);   // no MISO: GPIO5 is used by a servo
  u8g2.begin();
  u8g2.setContrast(255);
  servosInit();
  buzzerInit();
  playTune(T_BOOT);

  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, AP_PASS, 1);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  Serial.println(ok ? "AP started" : "AP FAILED");
  Serial.print("Open http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.on("/set", handleSet);
  server.on("/auto", handleAuto);
  server.on("/move", handleMove);
  server.on("/dance", handleDance);
  server.on("/arms", handleArms);
  server.on("/sound", handleSound);
  server.on("/fx", handleFx);
  server.on("/speed", handleSpeed);
  server.on("/center", handleCenter);
  server.begin();

  showInfoScreen();   // Wi-Fi name, password and address

  if (WAIT_FOR_CONNECTION) {
    // Keep the info on the screen until a phone or laptop joins the hotspot
    uint32_t joinedSince = 0;
    while (true) {
      server.handleClient();
      updateBuzzer(millis());
      if (WiFi.softAPgetStationNum() > 0) {
        if (joinedSince == 0) joinedSince = millis();
        if (millis() - joinedSince > 500) break;      // connected for half a second = stable
      } else {
        joinedSince = 0;
      }
      delay(5);
    }
    showConnectedScreen();
    playTune(T_SUCCESS);
    uint32_t shown = millis();
    while (millis() - shown < CONNECTED_SCREEN_MS) {
      server.handleClient();
      updateBuzzer(millis());
      delay(5);
    }
  } else {
    uint32_t start = millis();
    while (millis() - start < INFO_SCREEN_MS) {
      server.handleClient();
      updateBuzzer(millis());
      delay(5);
    }
  }

  selectExpression(animated_faces::ExpressionName(0));   // neutral
  player.Invalidate();
}

void loop() {
  server.handleClient();
  uint32_t now = millis();

  // Safety: hold-moves stop if the phone stops sending commands (page closed, Wi-Fi lost)
  if (holdMode && now - lastCmdMs > FAILSAFE_MS) {
    holdMode = false;
    want = { G_STOP, 0, 1000 };
    stopTune();
    showFace("neutral");
  }
  if (fxHold && now - lastFxMs > FAILSAFE_MS) {   // sound effect button released but page went silent
    fxHold = false;
    fxId = -1;
    stopTune();
    showFace("neutral");
  }
  updateServos(now);
  updateBuzzer(now);

  if (autoCycle && now - autoChangedAt >= AUTO_INTERVAL_MS) {
    autoChangedAt = now;
    autoIndex = (autoIndex + 1) % animated_faces::ExpressionCount;
    selectExpression(animated_faces::ExpressionName(autoIndex));
  }

  if (player.Draw(now, raster, sizeof(raster))) {
    u8g2.clearBuffer();
    u8g2.setDrawColor(1);
    for (unsigned y = 0; y < animated_faces::Height; ++y)
      for (unsigned x = 0; x < animated_faces::Width; ++x)
        if (raster[y * 16 + x / 8] & (0x80 >> (x % 8)))
          u8g2.drawPixel(x, y + Y_OFFSET);
    u8g2.sendBuffer();
  }
  delay(2);
}
