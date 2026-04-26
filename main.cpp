#include "mbed.h"
#include <vector>
#include <string>
#include <cmath>
#include <cstdarg>
#include <cstring>

using namespace std::chrono_literals;

// ─── Peripherals ──────────────────────────────────────────────────────────────
DigitalIn    presenceSensor(D2);
AnalogIn     lightSensor(A0);
AnalogIn     tempSensor(A1);
DigitalIn    userButton(D3);
InterruptIn  modeButton(D4);

DigitalOut   led(D5);
DigitalOut   fan(D6);
PwmOut       buzzer(D7);
DigitalOut   indicatorLed(D8);


static BufferedSerial serial_port(USBTX, USBRX, 9600);


void lcdPrint(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) {
        serial_port.write(buf, (size_t)len);
    }
}

// ─── Data Structures ──────────────────────────────────────────────────────────
struct RoutineRecord {
    int   hour;
    int   minute;
    bool  presence;
    float lightLevel;
    float temperature;
    bool  ledState;
    bool  fanState;
    int   count;
};

struct LearnedPattern {
    int   hour;
    int   minute;
    bool  presence;
    float avgLight;
    float avgTemp;
    bool  expectedLedState;
    bool  expectedFanState;
    int   confidence;
};

enum SystemMode {
    MODE_LEARNING,
    MODE_AUTOMATIC,
    MODE_MANUAL
};

// ─── Global State ─────────────────────────────────────────────────────────────
SystemMode currentMode        = MODE_LEARNING;
int        simulatedHour      = 8;
int        simulatedMinute    = 0;
int        learningCycle      = 0;
const int  MAX_LEARNING_CYCLES = 5;

vector<RoutineRecord>  routineHistory;
vector<LearnedPattern> learnedPatterns;


int learningCounter = 0;

// ─── Timers ───────────────────────────────────────────────────────────────────
Timer systemTimer;
Timer hourTimer;
Timer actionCooldown;


Timer userButtonDebounce;
Timer modeButtonDebounce;
const int DEBOUNCE_MS = 200;


EventQueue event_queue(32 * EVENTS_EVENT_SIZE);
Thread     event_thread;

// ─── Forward Declarations ─────────────────────────────────────────────────────
void  updateSimulatedTime();
void  readSensors(bool &presence, float &light, float &temp);
void  learningControl();
void  automaticControl();
void  manualControl();
void  saveToMemory(RoutineRecord record);
void  analyzePatterns();
float calculateSimilarity(const RoutineRecord &current,   // FIX #9
                          const LearnedPattern &pattern);
void  executeAnticipatedAction(LearnedPattern &pattern);
void  changeMode();
void  beep(int count, float frequency);
void  displayLCD(const string &mode, const string &action);

// ─── ISR ──────────────────────────────────────────────────────────────────────

void modeButtonISR() {
    if (modeButtonDebounce.read_ms() > DEBOUNCE_MS) {
        event_queue.call(changeMode);
        modeButtonDebounce.reset();
    }
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main() {
    // Start all timers
    systemTimer.start();
    hourTimer.start();
    actionCooldown.start();
    userButtonDebounce.start();
    modeButtonDebounce.start();

    // FIX #6: Launch EventQueue thread before attaching ISR
    event_thread.start(callback(&event_queue, &EventQueue::dispatch_forever));
    modeButton.fall(&modeButtonISR);

    lcdPrint("\nInitialisation...\n");
    lcdPrint("Maison Intelligente Ready\n");
    lcdPrint("Mode: APPRENTISSAGE\n");
    ThisThread::sleep_for(2000ms);  // FIX #5

    while (true) {
        // 10 real seconds = 1 simulated minute
        if (hourTimer.read() >= 10.0f) {
            updateSimulatedTime();
            hourTimer.reset();
            lcdPrint("Tick:%02d:%02d\n", simulatedHour, simulatedMinute);
        }

        switch (currentMode) {
            case MODE_LEARNING:
                learningControl();
                indicatorLed = 0;
                break;
            case MODE_AUTOMATIC:
                automaticControl();
                indicatorLed = 1;
                break;
            case MODE_MANUAL:
                manualControl();
                indicatorLed = 0;
                break;
        }

        ThisThread::sleep_for(100ms);  // FIX #5
    }
}

// ─── Simulated Time ───────────────────────────────────────────────────────────
void updateSimulatedTime() {
    simulatedMinute++;
    if (simulatedMinute >= 60) {
        simulatedMinute = 0;
        simulatedHour++;
        if (simulatedHour >= 24) {
            simulatedHour = 0;
            learningCycle++;
        }
    }
    // Auto-transition: learning done after MAX_LEARNING_CYCLES days
    if (learningCycle >= MAX_LEARNING_CYCLES && currentMode == MODE_LEARNING) {
        lcdPrint("\nApprentissage termine!\n");
        analyzePatterns();
        currentMode = MODE_AUTOMATIC;
        beep(3, 1000.0f);
    }
}

// ─── Sensors ──────────────────────────────────────────────────────────────────
void readSensors(bool &presence, float &light, float &temp) {
    presence = presenceSensor.read();
    light    = lightSensor.read() * 100.0f;
    temp     = tempSensor.read() * 50.0f;
}

// ─── Learning Mode ────────────────────────────────────────────────────────────
void learningControl() {
    bool  presence;
    float light, temp;
    readSensors(presence, light, temp);


    led = (presence && light < 30.0f) ? 1 : 0;
    fan = (temp > 28.0f)              ? 1 : 0;

    
    learningCounter++;   // FIX #8: now a global, resets properly in changeMode()
    if (learningCounter >= 300) {
        learningCounter = 0;
        RoutineRecord rec;
        rec.hour        = simulatedHour;
        rec.minute      = simulatedMinute;
        rec.presence    = presence;
        rec.lightLevel  = light;
        rec.temperature = temp;
        rec.ledState    = led;    // FIX #1: captures actual, meaningful state
        rec.fanState    = fan;
        rec.count       = 1;
        saveToMemory(rec);
        lcdPrint("Auto-Rec:%02d:%02d P:%d L:%.1f T:%.1f\n",
                 simulatedHour, simulatedMinute, presence, light, temp);
    }

    // Manual record on button press
  
    if (userButton == 1 && userButtonDebounce.read_ms() > DEBOUNCE_MS) {
        userButtonDebounce.reset();
        RoutineRecord rec;
        rec.hour        = simulatedHour;
        rec.minute      = simulatedMinute;
        rec.presence    = presence;
        rec.lightLevel  = light;
        rec.temperature = temp;
        rec.ledState    = led;    // FIX #1
        rec.fanState    = fan;
        rec.count       = 1;
        saveToMemory(rec);
        lcdPrint("Manual-Rec:%02d:%02d P:%d L:%.1f T:%.1f\n",
                 simulatedHour, simulatedMinute, presence, light, temp);
        beep(1, 800.0f);
        ThisThread::sleep_for(500ms);  // FIX #5
    }
}

// ─── Automatic Mode ───────────────────────────────────────────────────────────
void automaticControl() {
    bool  presence;
    float light, temp;
    readSensors(presence, light, temp);

    RoutineRecord currentRecord;
    currentRecord.hour        = simulatedHour;
    currentRecord.minute      = simulatedMinute;
    currentRecord.presence    = presence;
    currentRecord.lightLevel  = light;
    currentRecord.temperature = temp;
    currentRecord.ledState    = led;
    currentRecord.fanState    = fan;
    currentRecord.count       = 1;

    float bestScore      = 0.0f;
    int   bestMatchIndex = -1;

    for (size_t i = 0; i < learnedPatterns.size(); i++) {
        float score = calculateSimilarity(currentRecord, learnedPatterns[i]);
        if (score > bestScore) {
            bestScore      = score;
            bestMatchIndex = (int)i;
        }
    }

    if (bestMatchIndex != -1 && bestScore >= 0.5f) {
        if (actionCooldown.read() >= 10.0f) {
            executeAnticipatedAction(learnedPatterns[bestMatchIndex]);
            actionCooldown.reset();
            displayLCD("AUTO", "Action Executed");
        }
    } else {
        // Fallback: simple rule-based defaults
        led = (presence && light < 30.0f) ? 1 : 0;
        if      (temp > 28.0f) fan = 1;
        else if (temp < 22.0f) fan = 0;
    }
}

// ─── Manual Mode ──────────────────────────────────────────────────────────────
void manualControl() {
   
    if (userButton == 1 && userButtonDebounce.read_ms() > DEBOUNCE_MS) {
        userButtonDebounce.reset();
        led = !led.read();   // FIX #10: explicit read(), not implicit int cast
        lcdPrint("MANUAL LED: %d\n", (int)led.read());
        displayLCD("MANUAL", led.read() ? "LED ON" : "LED OFF");
        beep(1, 600.0f);
        ThisThread::sleep_for(500ms);  // FIX #5
    }

    float temp = tempSensor.read() * 50.0f;
    fan = (temp > 25.0f) ? 1 : 0;
}

// ─── Memory ───────────────────────────────────────────────────────────────────
void saveToMemory(RoutineRecord record) {
    for (size_t i = 0; i < routineHistory.size(); i++) {
        RoutineRecord &existing = routineHistory[i];
        if (abs(existing.hour   - record.hour)  == 0 &&
            abs(existing.minute - record.minute) <= 10 &&
            existing.presence == record.presence &&
            existing.ledState == record.ledState) {
            existing.count++;
            existing.lightLevel  = (existing.lightLevel  * (existing.count - 1)
                                    + record.lightLevel)  / existing.count;
            existing.temperature = (existing.temperature * (existing.count - 1)
                                    + record.temperature) / existing.count;
            return;
        }
    }
    routineHistory.push_back(record);
}

void analyzePatterns() {
    learnedPatterns.clear();
    lcdPrint("\nAnalysing Data...\n");

    for (size_t i = 0; i < routineHistory.size(); i++) {
        RoutineRecord &rec = routineHistory[i];
        if (rec.count >= 2) {
            LearnedPattern pat;
            pat.hour             = rec.hour;
            pat.minute           = rec.minute;
            pat.presence         = rec.presence;
            pat.avgLight         = rec.lightLevel;
            pat.avgTemp          = rec.temperature;
            pat.expectedLedState = rec.ledState;
            pat.expectedFanState = rec.fanState;
            pat.confidence       = rec.count;
            learnedPatterns.push_back(pat);
            lcdPrint("Pattern learned: %02d:%02d (confidence: %d)\n",
                     rec.hour, rec.minute, rec.count);
        }
    }

    
    lcdPrint("Total patterns learned: %d\n", (int)learnedPatterns.size());
}

// ─── Pattern Matching ─────────────────────────────────────────────────────────

float calculateSimilarity(const RoutineRecord &current, const LearnedPattern &pattern) {
    float score = 0.0f;

    int currentTotal = current.hour  * 60 + current.minute;
    int patternTotal = pattern.hour  * 60 + pattern.minute;
    int diff = abs(currentTotal - patternTotal);
    if (diff > 720) diff = 1440 - diff;

    if (diff <= 60) {
        score += ((60.0f - (float)diff) / 60.0f) * 0.4f;
    }

    if (current.presence == pattern.presence)                    score += 0.3f;
    if (fabsf(current.lightLevel  - pattern.avgLight) <= 20.0f) score += 0.2f;
    if (fabsf(current.temperature - pattern.avgTemp)  <=  3.0f) score += 0.1f;

    return score;
}

void executeAnticipatedAction(LearnedPattern &pattern) {
    beep(2, 1200.0f);
    led = pattern.expectedLedState;
    fan = pattern.expectedFanState;
    lcdPrint("Anticipated: LED=%d FAN=%d at %02d:%02d\n",
             (int)pattern.expectedLedState, (int)pattern.expectedFanState,
             pattern.hour, pattern.minute);
}

// ─── Mode Switch ──────────────────────────────────────────────────────────────

void changeMode() {
    switch (currentMode) {
        case MODE_LEARNING:
            analyzePatterns();
            currentMode = MODE_AUTOMATIC;
            lcdPrint("\n>>> Mode: AUTOMATIC\n");
            break;
        case MODE_AUTOMATIC:
            currentMode = MODE_MANUAL;
            lcdPrint("\n>>> Mode: MANUAL\n");
            break;
        case MODE_MANUAL:
            currentMode    = MODE_LEARNING;
            learningCycle  = 0;
            learningCounter = 0;  // FIX #8: global counter now resets correctly
            lcdPrint("\n>>> Mode: LEARNING\n");
            break;
    }
    beep(1, 600.0f);
    ThisThread::sleep_for(300ms);  // FIX #5
}

// ─── Utilities ────────────────────────────────────────────────────────────────
void displayLCD(const string &mode, const string &action) {
    lcdPrint("[%s] %s | %02d:%02d\n",
             mode.c_str(), action.c_str(), simulatedHour, simulatedMinute);
}


void beep(int count, float frequency) {
    if (frequency <= 0.0f) return;
    buzzer.period(1.0f / frequency);
    buzzer = 0.5f;
    ThisThread::sleep_for(std::chrono::milliseconds(count * 100));  // FIX #5
    buzzer = 0.0f;
}
