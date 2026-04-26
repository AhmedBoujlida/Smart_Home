# Smart_Home
# 🏠 Maison Intelligente — Smart Home Automation (mbed OS 5/6)

An embedded C++ smart home system built on the **mbed** platform. It learns from user behavior over multiple simulated days and automatically controls a LED, fan, and buzzer based on recognized patterns.

---

## 📋 Table of Contents

- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Pin Configuration](#pin-configuration)
- [System Architecture](#system-architecture)
- [Operating Modes](#operating-modes)
- [How Pattern Learning Works](#how-pattern-learning-works)
- [Bugs Fixed](#bugs-fixed)
- [Getting Started](#getting-started)
- [Project Structure](#project-structure)

---

## ✨ Features

- **3 operating modes**: Learning, Automatic, Manual
- **Pattern recognition**: Records and replays daily routines
- **Sensor fusion**: Combines presence, light, and temperature data
- **Similarity scoring**: Matches current context to learned patterns
- **Simulated time**: 10-second ticks simulate one full minute
- **Interrupt-driven mode switching**: Instant response via button ISR + EventQueue
- **Button debouncing**: 200 ms hardware debounce on all buttons
- **mbed OS 5/6 compatible**: Uses `BufferedSerial`, `ThisThread::sleep_for()`, `EventQueue`

---

## 🔧 Hardware Requirements

| Component | Description |
|---|---|
| mbed-compatible board | e.g., NUCLEO-F401RE or similar |
| PIR / presence sensor | Digital |
| Light sensor (LDR) | Analog |
| Temperature sensor | Analog (0–50 °C mapped from 0–3.3 V) |
| User button | Digital input (momentary push) |
| Mode button | Digital input with interrupt |
| LED | Digital output |
| Fan / relay module | Digital output |
| Buzzer | PWM output |
| Indicator LED | Digital output |
| Serial terminal | USB (9600 baud) |

---

## 📌 Pin Configuration

| Pin | Role | Type |
|---|---|---|
| `D2` | Presence sensor | `DigitalIn` |
| `A0` | Light sensor | `AnalogIn` |
| `A1` | Temperature sensor | `AnalogIn` |
| `D3` | User button (manual toggle / record) | `DigitalIn` |
| `D4` | Mode button (cycle modes) | `InterruptIn` |
| `D5` | LED | `DigitalOut` |
| `D6` | Fan | `DigitalOut` |
| `D7` | Buzzer | `PwmOut` |
| `D8` | Indicator LED (ON in AUTO mode) | `DigitalOut` |
| `USBTX / USBRX` | Serial monitor | `BufferedSerial` |

---

## 🏗️ System Architecture

```
main loop (100 ms tick)
│
├── [ISR] modeButton → debounce check → event_queue.call(changeMode)
│                                              │
│                                       event_thread (RTOS)
│                                              │
│                                        changeMode() [safe context]
│
├── hourTimer >= 10 s → updateSimulatedTime()
│   └── Every 5 simulated days → analyzePatterns() → MODE_AUTOMATIC
│
└── switch(currentMode)
    ├── MODE_LEARNING   → learningControl()
    ├── MODE_AUTOMATIC  → automaticControl()
    └── MODE_MANUAL     → manualControl()
```

### Data Flow

```
Sensors ──► readSensors()
              │
              ▼
        RoutineRecord  (meaningful LED/fan state captured)
              │
     ┌────────┴────────┐
     ▼                 ▼
saveToMemory()    calculateSimilarity()  ← const refs
     │                 │
     ▼                 ▼
routineHistory   LearnedPattern match
     │                 │
     └────────┬────────┘
              ▼
     executeAnticipatedAction()
```

---

## 🔄 Operating Modes

### 🟡 MODE_LEARNING (Default)
- Applies simple default rules (LED on if dark + presence, fan on if hot) so recorded states are meaningful
- Auto-records every **30 seconds**
- User can manually record a snapshot by pressing **D3** (200 ms debounced)
- After **5 simulated days**, auto-switches to `MODE_AUTOMATIC` with 3 beeps

### 🟢 MODE_AUTOMATIC
- Finds the **best-matching** learned pattern via similarity score (0.0–1.0)
- Score ≥ 0.5 → executes anticipated action (LED / fan state)
- Score < 0.5 → falls back to simple light/temperature rules
- **10-second cooldown** between actions to prevent rapid toggling
- **Indicator LED (`D8`)** is ON in this mode

### 🔵 MODE_MANUAL
- **D3** toggles the LED (debounced)
- Fan is controlled automatically by temperature (threshold: 25 °C)

### Mode Cycling (D4 button)
```
LEARNING → AUTOMATIC → MANUAL → LEARNING → ...
```
Switching back to LEARNING resets the learning cycle counter and the auto-record timer.

---

## 🧠 How Pattern Learning Works

### Similarity Score

| Factor | Weight | Condition |
|---|---|---|
| Time proximity | 40% | Within 60 minutes |
| Presence match | 30% | Same presence state |
| Light level | 20% | Within ±20% |
| Temperature | 10% | Within ±3 °C |

A score ≥ **0.5** triggers pattern execution.

### Pattern Consolidation

Records with the **same hour, ±10 min window, same presence and LED state** are merged and their sensor values averaged rather than stored as duplicates.

### Pattern Extraction

Only records seen **≥ 2 times** become learned patterns (minimum confidence filter).

---

## 🔧 Bugs Fixed

| # | Severity | Location | Issue | Fix Applied |
|---|---|---|---|---|
| 1 | 🔴 Critical | `learningControl()` | `led`/`fan` forced to `0` **after** recording → all learned `ledState`/`fanState` were always `0` | Moved default state logic **before** recording |
| 2 | 🔴 Critical | `beep()` | Division by zero when `frequency = 0` | Added `if (frequency <= 0.0f) return;` guard |
| 3 | 🔴 Critical | `analyzePatterns()` | `size_t` passed to `%d` format specifier → undefined behavior | Cast to `(int)` |
| 4 | 🟠 Deprecated | Global | `Serial` is deprecated in mbed OS 5+ | Replaced with `BufferedSerial` + `lcdPrint()` helper |
| 5 | 🟠 Deprecated | Throughout | `wait()` is deprecated in mbed OS 5+ | Replaced with `ThisThread::sleep_for()` |
| 6 | 🟠 Warning | `modeButtonISR` | Calling heavy functions directly from ISR context | ISR now posts to `EventQueue`; `changeMode()` runs in RTOS thread |
| 7 | 🟡 Logic | D3 / D4 buttons | No debouncing → multiple triggers per press | 200 ms Timer-based debounce on both buttons |
| 8 | 🟡 Logic | `learningControl()` | `static int learningCounter` could not be reset from `changeMode()` | Promoted to global; reset in `changeMode()` |
| 9 | 🟡 Style | `calculateSimilarity()` | Parameters were non-const references though never modified | Changed to `const RoutineRecord &` and `const LearnedPattern &` |
| 10 | 🟡 Style | `manualControl()` | `led = !led` relies on implicit `int` conversion | Changed to `led = !led.read()` for clarity |

---

## 🚀 Getting Started

### 1. Clone & Import

```bash
git clone 
# Import into Mbed Studio or the online compiler
```

### 2. Connect Hardware

Wire components according to the [Pin Configuration](#pin-configuration) table above.

### 3. Flash & Run

Compile and flash to your mbed board, then open a serial terminal at **9600 baud**:

```
Initialisation...
Maison Intelligente Ready
Mode: APPRENTISSAGE
```

### 4. Learning Phase

- The system auto-records every 30 seconds using sensible light/temperature defaults
- Press **D3** to manually save a snapshot at any key moment
- After 5 simulated days, it auto-transitions to `MODE_AUTOMATIC` with 3 beeps

### 5. Automatic Mode

- Watches for pattern matches and applies anticipated LED/fan states
- Monitor serial output for match scores and executed actions
- Indicator LED (`D8`) glows steady in this mode

---

## 📁 Project Structure

```
project/
├── main.cpp       
└── README.md    
```

### Key Data Structures

```cpp
RoutineRecord    // Raw sensor snapshot with timestamp and device states
LearnedPattern   // Consolidated pattern with confidence score
SystemMode       // Enum: MODE_LEARNING | MODE_AUTOMATIC | MODE_MANUAL
```
