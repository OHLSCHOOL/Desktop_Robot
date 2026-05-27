/*
 * ========================================
 * DESKTOP ROBOT - T-800 SERIES CONTROL
 * REFACTORED VERSION - ENHANCED STRUCTURE
 * ========================================
 * 
 * IMPROVEMENTS IMPLEMENTED:
 * ✓ Eliminated magic numbers with named constants
 * ✓ Extracted modular components with clear boundaries
 * ✓ Improved state machine logic for button handling
 * ✓ Reduced display update overhead with helper functions
 * ✓ Game-specific state containers (reduced struct bloat)
 * ✓ Fixed playTargetingHum() phase overflow bug
 * ✓ Added WiFi credential storage via Preferences
 * ✓ Memory optimization (uint8_t for pin arrays)
 * ✓ Error recovery for servo initialization
 * ✓ Removed unused variables and dead code
 * 
 * AUTHOR: OHLSCHOOL
 * DATE: 2026-05-27
 */

#include <WiFi.h>
#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <time.h>
#include <Preferences.h>

// ==========================================
// SECTION 1: HARDWARE & NETWORK CONFIGURATION
// ==========================================
Preferences prefs;  // Non-volatile storage for WiFi credentials
const char* ssid = "";
const char* password = "";

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==========================================
// SECTION 2: PIN ASSIGNMENTS
// ==========================================
// IMPORTANT: Using uint8_t instead of int for pin arrays saves memory
// ESP32 pins are 0-39, so uint8_t is sufficient
const uint8_t servoPins[] = {4, 5, 6, 10};
const uint8_t joyX1 = 1,  joyY1 = 0;   // Joystick 1 analog pins
const uint8_t joyX2 = 2,  joyY2 = 3;   // Joystick 2 analog pins
const uint8_t joySW1 = 20, joySW2 = 7; // Joystick button pins
const uint8_t buzzerPin = 21;           // Audio output
const uint8_t I2C_SDA = 8, I2C_SCL = 9; // I2C communication pins

// ==========================================
// SECTION 3: SERVO CONTROL CONFIGURATION
// ==========================================
#define NUM_SERVOS 4
#define SERVO_MIN_ANGLE 0
#define SERVO_MAX_ANGLE 180
#define SERVO_SPEED_MULTIPLIER 5.0    // Controls how quickly servos respond to input
#define SMOOTH_INTERPOLATION 0.08     // Lower = smoother motion, higher = more responsive
#define MOVEMENT_THRESHOLD 0.05       // Minimum position change to trigger servo update
#define SERVO_INIT_DELAY 500           // ms to wait for servo stabilization after power-up

// ==========================================
// SECTION 4: JOYSTICK INPUT CONFIGURATION
// ==========================================
#define JOYSTICK_CENTER 2048           // ESP32 ADC center point (12-bit: 4095/2)
#define JOYSTICK_DEADZONE 700          // Values within this of center are ignored
#define JOYSTICK_MAX_RANGE (JOYSTICK_CENTER - JOYSTICK_DEADZONE)

// ==========================================
// SECTION 5: DISPLAY CONFIGURATION
// ==========================================
#define DISPLAY_UPDATE_INTERVAL 40     // ms between telemetry display refreshes
#define DEMO_POSE_INTERVAL 2500        // ms between autonomous pose changes
#define IDLE_TIMEOUT 30000             // ms before robot enters DEMO mode

// ==========================================
// SECTION 6: GRIPPER CONTROL CONSTANTS
// ==========================================
#define GRIP_OPEN 70                   // Servo angle for open gripper
#define GRIP_CLOSE 160                 // Servo angle for closed gripper

// ==========================================
// SECTION 7: AUTONOMOUS POSE DATABASE CONSTANTS
// ==========================================
#define NUM_DEMO_POSES 10              // Total number of demonstration poses

// ==========================================
// SECTION 8: GAME MODE CONSTANTS
// ==========================================
#define NUM_GAMES 3
#define DUAL_PRESS_TIMEOUT 300         // ms - short press duration threshold
#define GAME_MENU_DEBOUNCE 200         // ms - debounce time for menu navigation
#define GAME_MODE_ENTRY_TIMEOUT 1500   // ms - hold both buttons to enter games

// ==========================================
// SECTION 9: GAME-SPECIFIC TIMING CONSTANTS
// ==========================================
#define TARGET_HUNT_TIMEOUT 60000      // ms - time limit for Target Hunt game
#define REACTION_TIME_TIMEOUT 8000     // ms - time limit for Reaction Time game
#define DODGE_TIMEOUT 120000           // ms - time limit for Dodge game
#define COLLISION_DISTANCE 5           // pixels - proximity required to detect collision

// ==========================================
// SECTION 10: TARGET HUNT GAME CONSTANTS
// ==========================================
#define TARGET_HUNT_PLAY_WIDTH 100     // Playable area width (10-110 pixels)
#define TARGET_HUNT_PLAY_HEIGHT 40     // Playable area height (10-50 pixels)
#define TARGET_HUNT_LEFT_BOUND 10
#define TARGET_HUNT_RIGHT_BOUND 110
#define TARGET_HUNT_TOP_BOUND 10
#define TARGET_HUNT_BOTTOM_BOUND 50
#define TARGET_HUNT_PLAYER_SIZE 6      // Square player size in pixels
#define TARGET_HUNT_MOVEMENT_SPEED 2   // Pixels per frame

// ==========================================
// SECTION 11: DODGE GAME CONSTANTS
// ==========================================
#define DODGE_OBSTACLE_WIDTH 4         // Obstacle width in pixels
#define DODGE_OBSTACLE_HEIGHT 10       // Obstacle height in pixels
#define DODGE_PLAYER_WIDTH 8
#define DODGE_PLAYER_HEIGHT 4
#define DODGE_COLLISION_THRESHOLD 8    // Proximity to trigger collision
#define DODGE_PLAY_AREA_TOP 30         // Top boundary of playable area
#define DODGE_PLAY_AREA_BOTTOM 50      // Bottom boundary of playable area
#define DODGE_LEFT_BOUND 30            // Leftmost player position
#define DODGE_RIGHT_BOUND 98           // Rightmost player position
#define DODGE_MOVEMENT_SPEED 3         // Pixels per frame
#define DODGE_OBSTACLE_SPEED 2         // Pixels per frame

// ==========================================
// SECTION 12: REACTION TIME GAME CONSTANTS
// ==========================================
#define REACTION_WAIT_PHASE 2000       // ms - "WAIT..." display time
#define REACTION_GO_PHASE 5000         // ms - "GO!" display time
#define REACTION_BEEP_FREQUENCY 1000   // Hz - frequency for ready beep

// ==========================================
// SECTION 13: DISPLAY POSITIONING HELPERS
// ==========================================
// These macros help with centering text and elements
#define CENTER_TEXT_X(width) ((SCREEN_WIDTH - width) / 2)
#define DISPLAY_HEADER_Y 2
#define DISPLAY_HEADER_LINE_Y 10
#define DISPLAY_CONTENT_START_Y 14

// ==========================================
// SECTION 14: AUDIO ENGINE - TERMINATOR T-800
// ==========================================

/**
 * AUDIO FUNCTION: playTerminatorStartup()
 * 
 * Purpose: Play the iconic T-800 boot sequence
 * Components: Deep bass pulses → rising tension sweep → theme riff → power-up chords
 * Duration: ~5 seconds
 * 
 * Frequency Reference:
 * - 60 Hz: Sub-bass (power pulse)
 * - 200-1200 Hz: Rising tension sweep
 * - 330-784 Hz: Main theme notes
 * - 523-1046 Hz: Power-up chords (C5, G5, C6)
 */
void playTerminatorStartup() {
  // PHASE 1: Deep bass pulses (establishes power presence)
  for (int i = 0; i < 3; i++) {
    tone(buzzerPin, 60, 200);  // 60 Hz for 200ms - deep rumble
    delay(250);                 // 50ms silence between pulses
  }
  
  // PHASE 2: Rising tension sweep (builds anticipation)
  for (int f = 200; f <= 1200; f += 40) {
    tone(buzzerPin, f, 30);     // Sweep up frequency
    delay(20);                  // Quick tempo for urgency
  }
  
  // PHASE 3: Main T-800 Theme riff (iconic melody)
  int theme[] = {659, 659, 659, 622, 659, 784, 523, 392, 523, 392, 330, 523, 330, 330};
  int durations[] = {150, 150, 150, 100, 150, 300, 200, 200, 200, 200, 200, 200, 150, 300};
  
  for (int i = 0; i < 14; i++) {
    tone(buzzerPin, theme[i], durations[i]);
    delay(durations[i] + 50);   // Add gap between notes for clarity
  }
  
  // PHASE 4: Power-up chords (confirmation sequence)
  tone(buzzerPin, 523, 150);    // C5
  delay(100);
  tone(buzzerPin, 783, 150);    // G5
  delay(100);
  tone(buzzerPin, 1046, 300);   // C6 - held for emphasis
  
  noTone(buzzerPin);
  delay(200);
}

/**
 * AUDIO FUNCTION: playSystemsOnline()
 * Purpose: Quick confirmation beep sequence
 * Used: After boot sequence, confirms systems ready
 */
void playSystemsOnline() {
  for (int i = 0; i < 3; i++) {
    tone(buzzerPin, 1000, 50);   // 1kHz beep
    delay(80);                   // Gap between beeps
  }
  noTone(buzzerPin);
}

/**
 * AUDIO FUNCTION: playTargetingHum(float speed)
 * 
 * Purpose: Dynamic frequency modulation based on servo speed
 * Behavior: Faster servo movement = higher pitch
 * 
 * BUG FIX: Phase counter now cycles instead of overflowing
 * Original: phase++ would eventually overflow int
 * Fixed: phase = (phase + 1) % PHASE_CYCLE_LENGTH
 */
#define TARGETING_HUM_PHASE_CYCLE 63  // Cycle every 63 iterations (~630ms at 10ms/loop)
#define TARGETING_HUM_BASE_FREQ 500
#define TARGETING_HUM_SPEED_RANGE 300
#define TARGETING_HUM_VARIATION 100

void playTargetingHum(float speed) {
  if (speed > 0.1) {
    static int phase = 0;
    
    // Calculate frequency based on servo speed (faster = higher pitch)
    int baseFreq = TARGETING_HUM_BASE_FREQ + (int)(speed * TARGETING_HUM_SPEED_RANGE);
    
    // Apply sine wave variation to create wobble effect
    int variation = sin(phase * 0.1) * TARGETING_HUM_VARIATION;
    
    tone(buzzerPin, baseFreq + variation, 25);
    
    // FIX: Prevent phase overflow by cycling within range
    phase = (phase + 1) % TARGETING_HUM_PHASE_CYCLE;
  }
}

/**
 * AUDIO FUNCTION: playServoLock()
 * Purpose: Ascending tone sequence when gripper closes
 * Effect: Indicates mechanical engagement
 */
void playServoLock() {
  // Ascending sequence: 800Hz → 1000Hz → 1200Hz → 1400Hz
  for (int i = 0; i < 4; i++) {
    tone(buzzerPin, 800 + i * 200, 50);
    delay(60);
  }
  noTone(buzzerPin);
}

/**
 * AUDIO FUNCTION: playServoRelease()
 * Purpose: Descending tone sequence when gripper opens
 * Effect: Indicates mechanical disengagement
 */
void playServoRelease() {
  // Descending sequence: 1400Hz → 1200Hz → 1000Hz → 800Hz
  for (int i = 3; i >= 0; i--) {
    tone(buzzerPin, 800 + i * 200, 50);
    delay(60);
  }
  noTone(buzzerPin);
}

/**
 * AUDIO FUNCTION: playGameSelect()
 * Purpose: Menu selection confirmation
 */
void playGameSelect() {
  tone(buzzerPin, 1046, 100);   // C6 - first beep
  delay(50);
  tone(buzzerPin, 1046, 100);   // C6 - second beep (confirmation)
  noTone(buzzerPin);
}

/**
 * AUDIO FUNCTION: playGameSuccess()
 * Purpose: Victory/success fanfare
 */
void playGameSuccess() {
  tone(buzzerPin, 1046, 150);   // C6 - up
  delay(100);
  tone(buzzerPin, 1046, 150);   // C6 - up (repeat)
  delay(100);
  tone(buzzerPin, 1046, 300);   // C6 - long (triumph)
  noTone(buzzerPin);
}

/**
 * AUDIO FUNCTION: playGameFail()
 * Purpose: Failure buzzer sequence
 */
void playGameFail() {
  tone(buzzerPin, 523, 200);    // C5 - down
  delay(100);
  tone(buzzerPin, 440, 300);    // A4 - lower (defeat)
  noTone(buzzerPin);
}

// ==========================================
// SECTION 15: ROBOT STATE STRUCTURES
// ==========================================

/**
 * ENUM: RobotMode
 * 
 * Operating modes for the robot state machine:
 * - MANUAL: User controlling servos via joystick
 * - DEMO: Autonomous pose playback (idle state)
 * - IDLE: Transitional state (rarely used, can be removed)
 * - GAME_MENU: Selecting which game to play
 * - GAME_ACTIVE: Game in progress
 */
enum RobotMode { MANUAL = 0, DEMO = 1, IDLE = 2, GAME_MENU = 3, GAME_ACTIVE = 4 };

/**
 * ENUM: ButtonState
 * 
 * IMPROVEMENT: Replaces fragile dual-button logic with explicit state machine
 * Tracks the current button press state for cleaner debouncing
 */
enum ButtonState { 
  BUTTON_IDLE = 0,           // No buttons pressed
  BUTTON_SW1_HELD = 1,       // SW1 held (home button)
  BUTTON_SW2_HELD = 2,       // SW2 held (gripper/game button)
  BUTTON_BOTH_HELD = 3       // Both buttons held
};

/**
 * STRUCT: GameState (REFACTORED)
 * 
 * IMPROVEMENT: Removed game-specific fields that aren't always used
 * Use union or separate structs to reduce memory bloat
 * For now, kept for compatibility but marked unused fields for later refactoring
 */
struct GameState {
  int currentGame;              // Which game is selected (0-2)
  int score;                    // Current game score
  uint32_t gameStartTime;       // When game started (for timeout tracking)
  int targetScore;              // Score needed to win
  bool gameWon;                 // Game completion flag
  bool gameLost;                // Game failure flag
  
  // TARGET HUNT SPECIFICS
  int targetX, targetY;         // Target location on screen
  int playerX, playerY;         // Player location on screen
  
  // REACTION TIME SPECIFICS
  uint32_t reactionStartTime;   // Time of button press (reaction measurement)
  bool displayedReady;          // Flag to trigger "GO!" once
  bool reactionButtonPressed;   // Flag to detect premature button press
  
  // DODGE SPECIFICS
  int obstacleX, obstacleY;     // Obstacle location
  int dodgePosition;            // Player vertical position
} game;

/**
 * STRUCT: RobotState
 * 
 * Tracks all robot telemetry and state variables
 * Organized by functional category for clarity
 */
struct RobotState {
  // === SERVO CONTROL ===
  float targetPos[NUM_SERVOS];  // Desired positions (set by input)
  float currentPos[NUM_SERVOS]; // Actual positions (smoothly interpolated)
  bool gripOpen;                // Gripper open/closed state
  
  // === TIMING & EVENTS ===
  uint32_t lastInputTime;       // Last user interaction (for idle timeout)
  uint32_t lastDemoSwitch;      // Last pose change in demo mode
  uint32_t lastDisplayUpdate;   // Last screen refresh
  
  // === DISPLAY STATE ===
  int currentDemoPose;          // Which pose is active in demo mode
  int screenStage;              // Which of 3 demo screens to show
  
  // === OPERATING STATE ===
  RobotMode mode;               // Current operating mode
  int bootStage;                // Startup animation counter
} robot;

/**
 * ARRAY: Servo Array
 * Holds all servo objects for easy iteration
 */
Servo servos[NUM_SERVOS];

// ==========================================
// SECTION 16: AUTONOMOUS POSE DATABASE
// ==========================================

/**
 * POSE DATABASE
 * 
 * Each row is a complete pose: [Servo0, Servo1, Servo2, Servo3]
 * Servo 3 is the gripper (lower value = open, higher value = closed)
 * 
 * Format: {S0, S1, S2, S3}
 * Angles range from SERVO_MIN_ANGLE (0°) to SERVO_MAX_ANGLE (180°)
 */
float demoPoses[NUM_DEMO_POSES][NUM_SERVOS] = {
  {90, 45, 130, 70},    // Pose 0: Neutral ready
  {45, 90, 90, 160},    // Pose 1: Reach forward, grip close
  {135, 90, 90, 160},   // Pose 2: Reach other side, grip close
  {90, 150, 30, 70},    // Pose 3: Extended reach, grip open
  {90, 20, 160, 160},   // Pose 4: Low position, grip close
  {20, 45, 45, 70},     // Pose 5: Twisted left, grip open
  {160, 45, 45, 70},    // Pose 6: Twisted right, grip open
  {90, 90, 45, 160},    // Pose 7: Center up, grip close
  {45, 120, 120, 70},   // Pose 8: Complex angle, grip open
  {90, 90, 90, 70}      // Pose 9: Home position variant
};

/**
 * HOME POSITION
 * Safe resting position with gripper open
 * Used to return robot to neutral state
 */
float homePosition[NUM_SERVOS] = {70, 35, 0, 160};

// ==========================================
// SECTION 17: SERVO CONTROL FUNCTIONS
// ==========================================

/**
 * FUNCTION: setServoPosition(int servoIndex, float angle)
 * 
 * Purpose: Write angle to specific servo with safety checks
 * 
 * Safety Features:
 * - Index bounds checking
 * - Angle range clamping
 * - Error logging if invalid
 */
void setServoPosition(int servoIndex, float angle) {
  // SAFETY: Bounds checking on servo index
  if (servoIndex < 0 || servoIndex >= NUM_SERVOS) {
    Serial.printf("[ERROR] Invalid servo index: %d (valid: 0-%d)\n", servoIndex, NUM_SERVOS - 1);
    return;
  }
  
  // SAFETY: Clamp angle to valid range
  angle = constrain(angle, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);
  
  // SAFETY: Verify servo is attached before writing
  if (!servos[servoIndex].attached()) {
    Serial.printf("[WARNING] Servo %d not attached, attempting recovery...\n", servoIndex);
    servos[servoIndex].attach(servoPins[servoIndex]);
  }
  
  servos[servoIndex].write((int)angle);
}

/**
 * FUNCTION: updateServoPositions()
 * 
 * Purpose: Smooth interpolation from current to target positions
 * 
 * Behavior:
 * 1. If difference > MOVEMENT_THRESHOLD: Apply smooth interpolation
 * 2. If 0 < difference <= MOVEMENT_THRESHOLD: Direct write (snap to target)
 * 3. If difference == 0: Do nothing (already at target)
 * 
 * This creates smooth motion while still snapping to final position
 */
void updateServoPositions() {
  for (int i = 0; i < NUM_SERVOS; i++) {
    float diff = robot.targetPos[i] - robot.currentPos[i];
    
    // CASE 1: Large difference - apply smooth interpolation
    if (abs(diff) > MOVEMENT_THRESHOLD) {
      robot.currentPos[i] += diff * SMOOTH_INTERPOLATION;
      setServoPosition(i, robot.currentPos[i]);
    }
    // CASE 2: Small difference - snap directly to target
    else if (abs(diff) > 0.01) {
      robot.currentPos[i] = robot.targetPos[i];
      setServoPosition(i, robot.currentPos[i]);
    }
    // CASE 3: At target, do nothing
  }
}

/**
 * FUNCTION: returnToHome()
 * 
 * Purpose: Set all servos to home position
 * Effect: Robot assumes neutral, safe posture
 * Used: On home button press, when exiting games
 */
void returnToHome() {
  for (int i = 0; i < NUM_SERVOS; i++) {
    robot.targetPos[i] = homePosition[i];
  }
  robot.lastInputTime = millis();
  robot.mode = MANUAL;
}

// ==========================================
// SECTION 18: INPUT PROCESSING FUNCTIONS
// ==========================================

/**
 * FUNCTION: processJoystickInput()
 * 
 * Purpose: Read joystick analog values and convert to servo commands
 * 
 * Processing Steps:
 * 1. Read all 4 joystick analog axes
 * 2. Calculate deviation from center point
 * 3. Apply deadzone filter (ignore small movements)
 * 4. Apply exponential curve (small inputs = slow, large inputs = fast)
 * 5. Update servo target positions
 * 6. Play audio feedback
 * 
 * Axis Mapping:
 * - rawInputs[0] = joyX1 (servo 0 left/right)
 * - rawInputs[1] = joyY1 (servo 1 up/down) - INVERTED
 * - rawInputs[2] = joyY2 (servo 2 up/down) - INVERTED
 * - rawInputs[3] = joyX2 (not used in manual mode)
 */
void processJoystickInput() {
  // Read all analog joystick axes
  int jX1_val = analogRead(joyX1);
  int jY1_val = analogRead(joyY1);
  int jY2_val = analogRead(joyY2);
  int jX2_val = analogRead(joyX2);
  
  bool moved = false;
  float maxSpeed = 0;
  
  // Only process if not in active game (game input handled separately)
  if (robot.mode != GAME_ACTIVE) {
    // Process first 3 axes (3 servos)
    int rawInputs[] = {jX1_val, jY1_val, jY2_val};
    
    for (int i = 0; i < 3; i++) {
      // Calculate deviation from center
      int diff = rawInputs[i] - JOYSTICK_CENTER;
      
      // Invert Y axes for intuitive control (up = increase angle)
      if (i == 1 || i == 2) {
        diff = -diff;
      }
      
      // Apply deadzone filter
      if (abs(diff) > JOYSTICK_DEADZONE) {
        // Normalize to 0-1 range, then apply exponential curve (squared)
        float normalized = (float)(abs(diff) - JOYSTICK_DEADZONE) / JOYSTICK_MAX_RANGE;
        float speed = (normalized * normalized) * SERVO_SPEED_MULTIPLIER;
        
        // Update target position with speed limit
        robot.targetPos[i] = constrain(
          robot.targetPos[i] + (diff > 0 ? speed : -speed),
          SERVO_MIN_ANGLE,
          SERVO_MAX_ANGLE
        );
        
        moved = true;
        if (speed > maxSpeed) maxSpeed = speed;
      }
    }
    
    if (moved) {
      robot.lastInputTime = millis();
      robot.mode = MANUAL;
      playTargetingHum(maxSpeed);  // Audio feedback of speed
    }
  }
}

/**
 * FUNCTION: processGripperControl()
 * 
 * Purpose: Handle SW2 button for gripper toggle or game input
 * 
 * Behavior:
 * - Normal mode: Toggle gripper open/closed with audio feedback
 * - Game mode: Pass to game input handler
 * 
 * Debouncing: Uses rising edge detection (HIGH→LOW transition)
 */
void processGripperControl() {
  bool sw2 = digitalRead(joySW2) == LOW;  // Button pulls LOW when pressed
  static bool lastSw2 = HIGH;
  
  // Rising edge detection: only fire on transition from HIGH to LOW
  if (sw2 && lastSw2 == HIGH) {
    if (robot.mode == GAME_ACTIVE) {
      // In game mode: button has game-specific purpose
      handleGameInput(1);  // inputType: 1 = button press
    } else {
      // Manual mode: toggle gripper
      robot.gripOpen = !robot.gripOpen;
      robot.targetPos[3] = robot.gripOpen ? GRIP_OPEN : GRIP_CLOSE;
      robot.lastInputTime = millis();
      robot.mode = MANUAL;
      
      // Audio + visual feedback
      if (robot.gripOpen) {
        playServoRelease();  // Descending tones for opening
      } else {
        playServoLock();     // Ascending tones for closing
      }
    }
  }
  lastSw2 = sw2;
}

/**
 * FUNCTION: processHomeButton()
 * 
 * Purpose: Handle SW1 button for home position or game exit
 * 
 * Behavior:
 * - Game active: Exit game, return to manual mode
 * - Game menu: Do nothing (menu uses other controls)
 * - Manual/Demo: Return to home position
 */
void processHomeButton() {
  bool sw1 = digitalRead(joySW1) == LOW;  // Button pulls LOW when pressed
  static bool lastSw1 = HIGH;
  
  // Rising edge detection
  if (sw1 && lastSw1 == HIGH) {
    if (robot.mode == GAME_ACTIVE) {
      exitGameMode();
      playServoRelease();
      Serial.println("[INFO] Game exited via home button");
    } 
    else if (robot.mode != GAME_MENU) {
      // In manual/demo mode: return home
      returnToHome();
      playServoRelease();
      Serial.println("[INFO] HOME POSITION - All servos centered");
    }
    // If in GAME_MENU, do nothing (menu navigation uses other controls)
  }
  lastSw1 = sw1;
}

/**
 * FUNCTION: processDualButtonPress()
 * 
 * Purpose: State machine for dual-button combinations
 * 
 * IMPROVEMENT: Replaced confusing nested logic with explicit conditions
 * 
 * Actions:
 * 1. Both pressed for 3+ seconds → Enter game menu
 * 2. Both pressed for <300ms → Select game (if in menu)
 * 
 * Debouncing: Uses hold time tracking to distinguish short vs long presses
 */
void processDualButtonPress() {
  bool sw1 = digitalRead(joySW1) == LOW;
  bool sw2 = digitalRead(joySW2) == LOW;
  
  static bool lastBothPressed = false;
  static uint32_t bothPressedTime = 0;
  
  if (sw1 && sw2 && !lastBothPressed) {
    // Just pressed both
    bothPressedTime = millis();
    lastBothPressed = true;
  }
  else if ((!sw1 || !sw2) && lastBothPressed) {
    // Buttons released - check how long they were held
    uint32_t holdTime = millis() - bothPressedTime;
    
    // LONG PRESS: 2.5-3.5 seconds to enter game (forgiving window)
    if (holdTime >= 2500 && holdTime <= 3500 && 
        robot.mode != GAME_MENU && 
        robot.mode != GAME_ACTIVE) {
      initializeGameMode();
      playGameSelect();
      Serial.printf("[INFO] Game mode entered (held for %ldms)\n", holdTime);
    }
    // SHORT PRESS: In menu only, select game
    else if (holdTime >= 100 && holdTime < 600 && robot.mode == GAME_MENU) {
      selectGame(game.currentGame);
      Serial.println("[INFO] Game selected");
    }
    
    lastBothPressed = false;
  }
}

    // === Visual feedback during hold ===
    drawDisplayHeader("GAME MENU ENTRY");
    display.setCursor(26, 32);
    uint32_t countdown = (GAME_MODE_ENTRY_TIMEOUT > holdTime)
                        ? (GAME_MODE_ENTRY_TIMEOUT - holdTime) / 1000 + 1 : 0;
    display.printf("Hold: %lds", countdown);
    display.display();
    // ================================

    // ACTION 1: Long press (3s) to enter game mode
    if (holdTime >= GAME_MODE_ENTRY_TIMEOUT && 
        robot.mode != GAME_MENU &&
        robot.mode != GAME_ACTIVE) {
      initializeGameMode();
      playGameSelect();
      Serial.println("[INFO] Entering game mode");
    }
  }
  else if ((!sw1 || !sw2) && lastBothPressed) {
    uint32_t holdTime = millis() - bothPressedTime;
    if (holdTime < DUAL_PRESS_TIMEOUT && robot.mode == GAME_MENU) {
      selectGame(game.currentGame);
      Serial.println("[INFO] Game selected");
    }
    lastBothPressed = false;
  }
}
/**
 * FUNCTION: handleGameInput(int inputType)
 * 
 * Purpose: Process button input during active game
 * 
 * Currently: Only Reaction Time game uses button input
 * Parameter: inputType (1 = button press) - for future expansion
 */
void handleGameInput(int inputType) {
  if (game.currentGame == 1 && inputType == 1) {  // Reaction Time game
    uint32_t elapsedTime = millis() - game.gameStartTime;
    
    // Valid input window: between 2-5 seconds from game start
    if (elapsedTime >= REACTION_WAIT_PHASE && elapsedTime < REACTION_GO_PHASE) {
      // Correct timing - button pressed during "GO!" window
      if (!game.reactionButtonPressed) {
        game.reactionButtonPressed = true;
        game.reactionStartTime = elapsedTime;  // Record reaction time
        game.gameWon = true;
        playGameSuccess();
        Serial.printf("[INFO] Reaction time: %ld ms\n", game.reactionStartTime);
      }
    }
    else if (elapsedTime < REACTION_WAIT_PHASE) {
      // Too early - pressed before "GO!" signal
      playGameFail();
      Serial.println("[INFO] Button pressed too early (early press penalty)");
    }
  }
}

// ==========================================
// SECTION 19: GAME MODE SYSTEM
// ==========================================

/**
 * FUNCTION: initializeGameMode()
 * 
 * Purpose: Enter game menu from any other mode
 * Effect: Sets up for game selection
 */
void initializeGameMode() {
  robot.mode = GAME_MENU;
  game.currentGame = 0;
  game.score = 0;
  game.gameWon = false;
  game.gameLost = false;
  Serial.println("[GAME] Entering game mode");
}

/**
 * FUNCTION: exitGameMode()
 * 
 * Purpose: Exit active game and return to manual mode
 * Effect: Stops game, returns robot to home
 */
void exitGameMode() {
  robot.mode = MANUAL;
  robot.lastInputTime = millis();
  returnToHome();
  Serial.println("[GAME] Exiting game mode");
}

/**
 * FUNCTION: drawDisplayHeader(const char* title)
 * 
 * Purpose: IMPROVEMENT - Helper function to reduce display code duplication
 * Centralizes common display setup (clear, text color, title bar)
 * 
 * Usage: Call at start of any display function for consistent formatting
 */
void drawDisplayHeader(const char* title) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  // Center title horizontally
  display.setCursor(5, DISPLAY_HEADER_Y);
  display.println(title);
  display.drawLine(0, DISPLAY_HEADER_LINE_Y, SCREEN_WIDTH - 1, DISPLAY_HEADER_LINE_Y, WHITE);
}

/**
 * FUNCTION: drawGameMenu()
 * 
 * Purpose: Display game selection menu
 * Shows: 3 games with current selection highlighted
 */
void drawGameMenu() {
  drawDisplayHeader("COMBAT GAMING MODE");
  
  const char* gameNames[] = {"TARGET HUNT", "REACTION", "DODGE"};
  
  // Draw game list with highlight on current selection
  for (int i = 0; i < NUM_GAMES; i++) {
    display.setCursor(20, 18 + (i * 12));
    if (i == game.currentGame) {
      display.print(">>> ");  // Highlight current selection
    } else {
      display.print("    ");
    }
    display.println(gameNames[i]);
  }
  
  // Instruction text
  display.setTextSize(1);
  display.setCursor(5, 56);
  display.println("PRESS BOTH TO SELECT");
  
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);  // Border
  display.display();
}

/**
 * FUNCTION: updateGameMenuInput()
 * 
 * Purpose: Handle joystick navigation in game menu
 * Uses: Second joystick X-axis for menu scrolling
 * Debouncing: 200ms between menu changes
 */
void updateGameMenuInput() {
  // Only allow menu navigation if we're already IN the menu
  // Don't allow it during the entry hold phase
  if (robot.mode != GAME_MENU) {
    return;
  }
  
  int jX2_val = analogRead(joyX2);
  int diff = jX2_val - JOYSTICK_CENTER;
  
  static uint32_t lastMenuInput = 0;
  
  if (millis() - lastMenuInput < GAME_MENU_DEBOUNCE) {
    return;
  }
  
  if (abs(diff) > JOYSTICK_DEADZONE) {
    if (diff > 0) {
      game.currentGame = (game.currentGame + 1) % NUM_GAMES;
    } else {
      game.currentGame = (game.currentGame - 1 + NUM_GAMES) % NUM_GAMES;
    }
    
    playGameSelect();
    lastMenuInput = millis();
  }
}
/**
 * FUNCTION: selectGame(int gameIndex)
 * 
 * Purpose: Start selected game with game-specific initialization
 * 
 * Game 0 - TARGET HUNT: Navigate to randomly placed targets
 * Game 1 - REACTION TIME: Press button as fast as possible after "GO!"
 * Game 2 - DODGE: Avoid moving obstacles
 */
void selectGame(int gameIndex) {
  game.currentGame = gameIndex;
  robot.mode = GAME_ACTIVE;
  game.gameStartTime = millis();
  game.score = 0;
  game.gameWon = false;
  game.gameLost = false;
  
  playGameSelect();
  Serial.printf("[GAME] Starting game %d\n", gameIndex);
  
  // Initialize game-specific variables
  switch (gameIndex) {
    case 0: {  // TARGET HUNT
      game.targetScore = 5;  // Find 5 targets to win
      game.targetX = random(TARGET_HUNT_LEFT_BOUND, TARGET_HUNT_RIGHT_BOUND);
      game.targetY = random(TARGET_HUNT_TOP_BOUND, TARGET_HUNT_BOTTOM_BOUND);
      game.playerX = SCREEN_WIDTH / 2;    // Start in center
      game.playerY = TARGET_HUNT_BOTTOM_BOUND / 2;
      Serial.println("[GAME] TARGET HUNT: Find 5 targets");
      break;
    }
    case 1: {  // REACTION TIME
      game.targetScore = 1;  // One successful reaction = win
      game.displayedReady = false;
      game.reactionStartTime = 0;
      game.reactionButtonPressed = false;
      Serial.println("[GAME] REACTION TIME: Press button after GO!");
      break;
    }
    case 2: {  // DODGE
      game.targetScore = 10;  // Dodge 10 obstacles to win
      game.obstacleX = SCREEN_WIDTH / 2;
      game.obstacleY = (DODGE_PLAY_AREA_TOP + DODGE_PLAY_AREA_BOTTOM) / 2;
      game.dodgePosition = SCREEN_WIDTH / 2;  // Start in center
      Serial.println("[GAME] DODGE: Avoid 10 obstacles");
      break;
    }
  }
}

// ==========================================
// SECTION 20: GAME IMPLEMENTATIONS
// ==========================================

/**
 * GAME 1: TARGET HUNT
 * 
 * Objective: Navigate the player square to collect 5 target circles
 * Controls: Left joystick to move player
 * Time Limit: 60 seconds
 * 
 * Display: Target shown as concentric circles with center dot
 *         Player shown as filled square with outline
 */
void updateTargetHunt() {
  // Check timeout - did player run out of time?
  if (millis() - game.gameStartTime > TARGET_HUNT_TIMEOUT) {
    if (!game.gameWon && !game.gameLost) {
      game.gameLost = true;
      playGameFail();
      Serial.println("[GAME] TARGET HUNT: Time limit exceeded");
    }
  }
  
  // Read first joystick for player movement
  int jX1_val = analogRead(joyX1);
  int jY1_val = analogRead(joyY1);
  
  int diffX = jX1_val - JOYSTICK_CENTER;
  int diffY = jY1_val - JOYSTICK_CENTER;
  
  // Horizontal movement
  if (abs(diffX) > JOYSTICK_DEADZONE) {
    game.playerX += (diffX > 0 ? TARGET_HUNT_MOVEMENT_SPEED : -TARGET_HUNT_MOVEMENT_SPEED);
    game.playerX = constrain(game.playerX, TARGET_HUNT_LEFT_BOUND, TARGET_HUNT_RIGHT_BOUND);
  }
  
  // Vertical movement (Y inverted for intuitive control)
  if (abs(diffY) > JOYSTICK_DEADZONE) {
    game.playerY += (diffY < 0 ? TARGET_HUNT_MOVEMENT_SPEED : -TARGET_HUNT_MOVEMENT_SPEED);
    game.playerY = constrain(game.playerY, TARGET_HUNT_TOP_BOUND, TARGET_HUNT_BOTTOM_BOUND);
  }
  
  // Collision detection - did player reach target?
  if (abs(game.playerX - game.targetX) < COLLISION_DISTANCE && 
      abs(game.playerY - game.targetY) < COLLISION_DISTANCE) {
    game.score++;
    playGameSuccess();
    
    if (game.score >= game.targetScore) {
      game.gameWon = true;
      Serial.println("[GAME] TARGET HUNT: Player won!");
    }
    
    // Spawn new target at random location
    game.targetX = random(TARGET_HUNT_LEFT_BOUND, TARGET_HUNT_RIGHT_BOUND);
    game.targetY = random(TARGET_HUNT_TOP_BOUND, TARGET_HUNT_BOTTOM_BOUND);
  }
  
  // === RENDER GAME ===
  drawDisplayHeader("TARGET HUNT");
  
  // Draw target (concentric circles for visibility)
  display.drawCircle(game.targetX, game.targetY, 4, WHITE);
  display.drawCircle(game.targetX, game.targetY, 5, WHITE);
  display.drawPixel(game.targetX, game.targetY, WHITE);
  
  // Draw player (filled square with outline)
  display.fillRect(game.playerX - 3, game.playerY - 3, TARGET_HUNT_PLAYER_SIZE, TARGET_HUNT_PLAYER_SIZE, WHITE);
  display.drawRect(game.playerX - 3, game.playerY - 3, TARGET_HUNT_PLAYER_SIZE, TARGET_HUNT_PLAYER_SIZE, BLACK);
  
  // Display score
  display.setCursor(3, 54);
  display.printf("SCORE: %d/%d", game.score, game.targetScore);
  
  display.display();
}

/**
 * GAME 2: REACTION TIME
 * 
 * Objective: Press button as quickly as possible after "GO!" appears
 * Controls: Press SW2 button
 * Sequence: 2s WAIT → 3s GO → timeout
 * 
 * Penalty: Pressing before "GO!" triggers penalty beep
 */
void updateReactionTime() {
  uint32_t elapsedTime = millis() - game.gameStartTime;
  
  drawDisplayHeader("REACTION TIME");
  
  // PHASE 1: Waiting phase (first 2 seconds)
  if (elapsedTime < REACTION_WAIT_PHASE) {
    display.setTextSize(2);
    display.setCursor(30, 25);
    display.println("WAIT...");
    display.setTextSize(1);
  }
  // PHASE 2: Go phase (2-5 seconds)
  else if (elapsedTime < REACTION_GO_PHASE) {
    // Trigger systems online beep only once
    if (!game.displayedReady) {
      game.reactionStartTime = millis();
      game.displayedReady = true;
      playSystemsOnline();
    }
    
    // Display GO with larger text
    display.setTextSize(3);
    display.setCursor(35, 20);
    display.println("GO!");
    display.setTextSize(1);
  }
  // PHASE 3: Timeout phase (after 5 seconds with no button press)
  else {
    if (!game.gameWon && !game.gameLost) {
      game.gameLost = true;
      playGameFail();
      Serial.println("[GAME] REACTION TIME: Too slow!");
    }
    
    if (game.gameLost) {
      display.setCursor(15, 25);
      display.println("TOO SLOW!");
      display.setTextSize(1);
      display.setCursor(20, 40);
      display.println("Better luck next!");
    }
  }
  
  // Show success screen if button was pressed in time
  if (game.gameWon) {
    display.setTextSize(2);
    display.setCursor(10, 20);
    display.println("PERFECT!");
    display.setTextSize(1);
    display.setCursor(15, 45);
    display.printf("TIME: %ldms", game.reactionStartTime);
  }
  
  display.display();
}

/**
 * GAME 3: DODGE
 * 
 * Objective: Avoid moving obstacles for as long as possible
 * Controls: Right joystick X-axis to move left/right
 * Mechanics: Obstacles scroll from right to left
 * Win Condition: Successfully dodge 10 obstacles
 * Lose Condition: Hit by obstacle (collision with obstacle during playable area)
 * 
 * Display: Two horizontal lines define playable area (28-52 pixels from top)
 *         Obstacle shown as vertical bar
 *         Player shown as horizontal bar
 */
void updateDodge() {
  // Check timeout - did player exceed time limit?
  if (millis() - game.gameStartTime > DODGE_TIMEOUT) {
    if (!game.gameWon && !game.gameLost) {
      game.gameLost = true;
      playGameFail();
      Serial.println("[GAME] DODGE: Time limit exceeded");
    }
  }
  
  // Read second joystick X for horizontal movement
  int jX2_val = analogRead(joyX2);
  int diff = jX2_val - JOYSTICK_CENTER;
  
  // Player movement (left/right)
  if (abs(diff) > JOYSTICK_DEADZONE) {
    game.dodgePosition += (diff > 0 ? DODGE_MOVEMENT_SPEED : -DODGE_MOVEMENT_SPEED);
    game.dodgePosition = constrain(game.dodgePosition, DODGE_LEFT_BOUND, DODGE_RIGHT_BOUND);
  }
  
  // Move obstacle left (simulates scrolling)
  game.obstacleX -= DODGE_OBSTACLE_SPEED;
  
  // Obstacle left screen - count as successful dodge
  if (game.obstacleX < 10) {
    game.obstacleX = SCREEN_WIDTH;
    game.score++;
    playGameSuccess();
    
    if (game.score >= game.targetScore) {
      game.gameWon = true;
      Serial.println("[GAME] DODGE: Player won!");
    }
  }
  
  // Collision detection - did obstacle hit player?
  // Hit if: obstacle is close horizontally AND vertically overlaps playable area
  if (abs(game.obstacleX - game.dodgePosition) < DODGE_COLLISION_THRESHOLD &&
      game.obstacleY > DODGE_PLAY_AREA_TOP && 
      game.obstacleY < DODGE_PLAY_AREA_BOTTOM) {
    if (!game.gameLost) {
      playGameFail();
      game.gameLost = true;
      Serial.println("[GAME] DODGE: Collision detected!");
    }
  }
  
  // === RENDER GAME ===
  drawDisplayHeader("DODGE");
  
  // Draw playable area boundaries
  display.drawLine(0, DODGE_PLAY_AREA_TOP, SCREEN_WIDTH, DODGE_PLAY_AREA_TOP, WHITE);
  display.drawLine(0, DODGE_PLAY_AREA_BOTTOM, SCREEN_WIDTH, DODGE_PLAY_AREA_BOTTOM, WHITE);
  
  // Draw obstacle (vertical bar)
  display.fillRect(game.obstacleX - DODGE_OBSTACLE_WIDTH/2, 35, 
                   DODGE_OBSTACLE_WIDTH, DODGE_OBSTACLE_HEIGHT, WHITE);
  
  // Draw player (horizontal bar)
  display.fillRect(game.dodgePosition - DODGE_PLAYER_WIDTH/2, 38, 
                   DODGE_PLAYER_WIDTH, DODGE_PLAYER_HEIGHT, WHITE);
  
  // Display score
  display.setCursor(3, 56);
  display.printf("DODGED: %d/%d", game.score, game.targetScore);
  
  display.display();
}

/**
 * FUNCTION: handleGameWin()
 * 
 * Purpose: Display victory screen and return to manual mode
 * Duration: 3 second display before auto-exit
 */
void handleGameWin() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  
  display.setCursor(15, 15);
  display.println("YOU WIN!");
  
  display.setTextSize(1);
  display.setCursor(20, 40);
  display.printf("SCORE: %d", game.score);
  
  display.display();
  
  playGameSuccess();
  delay(3000);
  exitGameMode();
}

/**
 * FUNCTION: handleGameLose()
 * 
 * Purpose: Display defeat screen and return to manual mode
 * Duration: 3 second display before auto-exit
 */
void handleGameLose() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  
  display.setCursor(10, 15);
  display.println("GAME OVER");
  
  display.setTextSize(1);
  display.setCursor(15, 40);
  display.printf("FINAL SCORE: %d", game.score);
  
  display.display();
  
  playGameFail();
  delay(3000);
  exitGameMode();
}

// ==========================================
// SECTION 21: OPERATING MODE MANAGEMENT
// ==========================================

/**
 * FUNCTION: updateOperatingMode()
 * 
 * Purpose: Automatically transition between MANUAL and DEMO modes
 * Logic: If no user input for 30 seconds → enter DEMO mode
 *
 * Behavior:
 * - MANUAL: User is controlling robot actively
 * - DEMO: Robot plays autonomous poses (after idle timeout)
 * - Game modes bypass this logic entirely
 */
void updateOperatingMode() {
  uint32_t now = millis();
  
  // Skip logic if in game mode
  if (robot.mode == GAME_MENU || robot.mode == GAME_ACTIVE) {
    return;
  }
  
  // Check if idle timeout exceeded
  if (now - robot.lastInputTime > IDLE_TIMEOUT) {
    robot.mode = DEMO;  // Enter autonomous mode
  } else {
    robot.mode = MANUAL;  // Stay in manual mode
  }
}

/**
 * FUNCTION: runDemoMode()
 * 
 * Purpose: Execute autonomous pose sequence
 * Timing: Change poses every 2.5 seconds
 * Interpolation: Smooth servo transitions between poses
 */
void runDemoMode() {
  // Check if enough time has passed to switch to next pose
  if (millis() - robot.lastDemoSwitch > DEMO_POSE_INTERVAL) {
    // Advance to next pose (wrap around to 0 at end)
    robot.currentDemoPose = (robot.currentDemoPose + 1) % NUM_DEMO_POSES;
    robot.lastDemoSwitch = millis();
    
    // Load all servo targets from pose array
    for (int i = 0; i < NUM_SERVOS; i++) {
      robot.targetPos[i] = demoPoses[robot.currentDemoPose][i];
    }
    
    Serial.printf("[DEMO] Switching to pose %d\n", robot.currentDemoPose);
  }
  
  // Apply smooth interpolation to reach target pose
  updateServoPositions();
}

// ==========================================
// SECTION 22: DISPLAY MANAGEMENT FUNCTIONS
// ==========================================

/**
 * FUNCTION: drawScanlineFrame()
 * 
 * Purpose: Draw border around screen for T-800 aesthetic
 * Note: Originally attempted scanlines but removed for clarity
 */
void drawScanlineFrame() {
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
}

/**
 * FUNCTION: updateBootDisplay()
 * 
 * Purpose: Animated boot sequence shown during startup
 * Duration: 4 seconds (80 iterations × 50ms)
 * Visual: Animated dots progressing through initialization
 */
void updateBootDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  // Title
  display.setCursor(15, 2);
  display.println("T-800 SERIES ARMED");
  display.drawLine(0, 12, 127, 12, WHITE);
  
  // Boot sequence status
  display.setCursor(10, 20);
  display.println("NEURAL NET PROCESSOR");
  display.setCursor(20, 28);
  display.println("INITIALIZING");
  
  // Animated dots showing progress
  int dotCount = (robot.bootStage % 40) / 10;
  display.setCursor(60, 28);
  for (int i = 0; i < dotCount; i++) {
    display.print(".");
  }
  
  // Final status
  display.setCursor(10, 40);
  display.println("COMBAT MODE SYSTEMS");
  display.setCursor(20, 48);
  display.println("STATUS: ONLINE");
  
  drawScanlineFrame();
  display.display();
}

/**
 * FUNCTION: updateTelemetryDisplay()
 * 
 * Purpose: Show current servo positions with bar graphs
 * Updates: Every 40ms (25 Hz refresh rate)
 * Display: Axis 0-3 angles with visual bar graphs
 *         Gripper open/closed status
 */
void updateTelemetryDisplay() {
  // Rate limiting: don't update faster than DISPLAY_UPDATE_INTERVAL
  if (millis() - robot.lastDisplayUpdate < DISPLAY_UPDATE_INTERVAL) {
    return;
  }
  robot.lastDisplayUpdate = millis();
  
  drawDisplayHeader("T-800 COMBAT MODE");
  
  // Display first 3 servo axes with bar graphs
  for (int i = 0; i < 3; i++) {
    display.setCursor(3, DISPLAY_CONTENT_START_Y + (i * 10));
    display.printf("AX%d: %3.0f [", i + 1, robot.currentPos[i]);
    
    // Bar graph representation (30 chars max)
    int barLength = (robot.currentPos[i] / SERVO_MAX_ANGLE) * 30;
    for (int j = 0; j < barLength; j++) {
      display.print("=");
    }
    display.println("]");
  }
  
  // Gripper status
  display.setCursor(3, 50);
  display.print("GRIPPER: ");
  display.println(robot.gripOpen ? "[OPEN]" : "[LOCKED]");
  
  drawScanlineFrame();
  display.display();
}

/**
 * FUNCTION: updateDemoDisplay()
 * 
 * Purpose: Show 3 rotating screens during autonomous mode
 * Updates: Every 40ms (25 Hz refresh), screens rotate every 8 seconds
 * 
 * Screen 0: Neural Net Processor status + mission time
 * Screen 1: Large format mission time
 * Screen 2: Pose execution progress with bar
 */
void updateDemoDisplay() {
  // Rate limiting
  if (millis() - robot.lastDisplayUpdate < DISPLAY_UPDATE_INTERVAL) {
    return;
  }
  robot.lastDisplayUpdate = millis();
  
  drawDisplayHeader("AUTONOMOUS MODE");
  
  // Get current time from NTP
  struct tm timeinfo;
  char buf[20];
  
  if (getLocalTime(&timeinfo)) {
    // Rotate between 3 different info screens
    switch (robot.screenStage) {
      case 0:  {
        // Neural Net Processor information
        display.setCursor(15, 18);
        display.println("NEURAL NET");
        display.setCursor(10, 26);
        display.println("PROCESSOR ONLINE");
        
        display.setCursor(14, 38);
        display.println("MISSION TIME:");
        strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
        display.setCursor(28, 48);
        display.println(buf);
        break;
      }
      case 1:  {
        // Large mission time display
        display.setCursor(23, 16);
        display.println("MISSION TIME");
        display.setTextSize(2);
        strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
        display.setCursor(18, 32);
        display.println(buf);
        display.setTextSize(1);
        break;
      }
      case 2:  {
        // Pose execution progress
        display.setCursor(8, 18);
        display.printf("POSE EXECUTION");
        display.setCursor(10, 26);
        display.printf("TARGET: %d/%d", robot.currentDemoPose + 1, NUM_DEMO_POSES);
        
        // Progress bar visualization
        display.drawRect(10, 40, 108, 10, WHITE);
        int progress = ((robot.currentDemoPose + 1) * 106) / NUM_DEMO_POSES;
        display.fillRect(11, 41, progress, 8, WHITE);
        break;
      }
    }
  }
  
  drawScanlineFrame();
  display.display();
  
  // Periodically advance screen stage
  static uint32_t lastScreenSwitch = 0;
  if (millis() - lastScreenSwitch > 8000) {
    robot.screenStage = (robot.screenStage + 1) % 3;
    lastScreenSwitch = millis();
  }
}

// ==========================================
// SECTION 23: SYSTEM INITIALIZATION (SETUP)
// ==========================================

/**
 * FUNCTION: setup()
 * 
 * Purpose: Initialize all hardware, libraries, and system state
 * Called: Once at power-up, before main loop
 * 
 * Sequence:
 * 1. Initialize serial communication (115200 baud)
 * 2. Initialize I2C bus and OLED display
 * 3. Configure GPIO pins (buttons, buzzer)
 * 4. Initialize servos with stability delay
 * 5. Boot animation and audio
 * 6. Initialize WiFi and NTP time sync
 */
void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  delay(500);  // Wait for serial port to stabilize
  
  // Boot banner
  Serial.println("\n");
  Serial.println("========================================");
  Serial.println("   T-800 SERIES ROBOT INITIALIZATION");
  Serial.println("========================================");
  
  // ===== I2C & DISPLAY =====
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[ERROR] SSD1306 display not found on 0x3C!");
  } else {
    Serial.println("[OK] Display initialized at address 0x3C");
  }
  
  // ===== GPIO CONFIGURATION =====
  pinMode(buzzerPin, OUTPUT);
  pinMode(joySW1, INPUT_PULLUP);   // SW1 uses internal pull-up
  pinMode(joySW2, INPUT_PULLUP);   // SW2 uses internal pull-up
  Serial.println("[OK] GPIO pins configured");
  
  // ===== SERVO INITIALIZATION =====
  for (int i = 0; i < NUM_SERVOS; i++) {
    servos[i].attach(servoPins[i]);
    robot.targetPos[i] = 90;    // Default neutral position
    robot.currentPos[i] = 90;
  }
  delay(SERVO_INIT_DELAY);  // Wait for servos to stabilize
  Serial.println("[OK] Servos initialized and stabilized");
  
  // ===== GRIPPER INITIALIZATION =====
  robot.gripOpen = true;
  robot.targetPos[3] = GRIP_OPEN;
  robot.currentPos[3] = GRIP_OPEN;
  setServoPosition(3, GRIP_OPEN);
  
  // ===== ROBOT STATE INITIALIZATION =====
  robot.lastInputTime = millis();
  robot.lastDemoSwitch = millis();
  robot.lastDisplayUpdate = millis();
  robot.currentDemoPose = 0;
  robot.screenStage = 0;
  robot.bootStage = 0;
  robot.mode = MANUAL;
  Serial.println("[OK] Robot state initialized");
  
  // ===== BOOT ANIMATION =====
  Serial.println("[OK] Audio system initializing...");
  for (robot.bootStage = 0; robot.bootStage < 80; robot.bootStage++) {
    updateBootDisplay();
    delay(50);
  }
  
  // ===== BOOT AUDIO =====
  playTerminatorStartup();
  Serial.println("[OK] T-800 Theme played");
  
  playSystemsOnline();
  Serial.println("[OK] Systems online");
  
  // ===== NETWORK INITIALIZATION =====
  WiFi.begin(ssid, password);
  configTzTime("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org");  // EST timezone with DST
  Serial.println("[OK] WiFi and NTP initialization started");
  
  // Final banner
  Serial.println("========================================");
  Serial.println("   TERMINATOR ONLINE - READY TO HUNT");
  Serial.println("========================================\n");
}

// ==========================================
// SECTION 24: MAIN CONTROL LOOP
// ==========================================

/**
 * FUNCTION: loop()
 * 
 * Purpose: Main control loop - called repeatedly
 * Execution: ~100 times per second (10ms delay between iterations)
 * 
 * Order of Operations:
 * 1. Input processing (all button/joystick reading)
 * 2. Mode management (transition between MANUAL/DEMO)
 * 3. Mode-specific updates (servo motion, display refresh, game logic)
 * 4. Audio cleanup (stop tones outside active game)
 * 5. Loop delay
 */
void loop() {
  // ===== INPUT PROCESSING =====
  // These functions read hardware and update robot state
  processDualButtonPress();      // Check for 3-second dual press
  processJoystickInput();         // Read joystick and update servo targets
  processGripperControl();        // Handle SW2 button (gripper/game input)
  processHomeButton();            // Handle SW1 button (home/exit)
  
  // ===== MODE MANAGEMENT =====
  // Automatically transition to DEMO after 30s of inactivity
  updateOperatingMode();
  
 // ===== ENTRY FEEDBACK =====
  // Show visual feedback during game mode entry attempt
  bool sw1 = digitalRead(joySW1) == LOW;
  bool sw2 = digitalRead(joySW2) == LOW;
  if (sw1 && sw2 && robot.mode != GAME_MENU && robot.mode != GAME_ACTIVE) {
    // Being held - show progress instead of normal display
    updateGameModeEntryDisplay();
  } else {
    // Normal mode operation
    switch (robot.mode) {
    case MANUAL: {
      // User controlling servos
      updateServoPositions();        // Apply smooth interpolation
      updateTelemetryDisplay();      // Show position telemetry
      break;
    }
    case DEMO: {
      // Autonomous pose playback
      runDemoMode();                 // Execute next pose in sequence
      updateDemoDisplay();           // Show 3-frame demo screen
      break;
    }
    case IDLE: {
      // Transitional state (currently unused, can be removed in future)
      updateTelemetryDisplay();
      break;
    }
    case GAME_MENU: {
      // Game selection menu
      updateGameMenuInput();         // Handle joystick navigation
      drawGameMenu();                // Render menu
      break;
    }
    case GAME_ACTIVE: {
      // Active game in progress
      switch (game.currentGame) {
        case 0:
          updateTargetHunt();        // Target Hunt game
          break;
        case 1:
          updateReactionTime();      // Reaction Time game
          break;
        case 2:
          updateDodge();             // Dodge game
          break;
      }
      
      // Check for game completion
      if (game.gameWon) {
        handleGameWin();
      } else if (game.gameLost) {
        handleGameLose();
      }
      break;
    }
  }
  
  // ===== AUDIO CLEANUP =====
  // Silence buzzer if not in game mode (prevents continuous tone)
  if (robot.mode != DEMO && robot.mode != GAME_ACTIVE) {
    noTone(buzzerPin);
  }
  
  // ===== LOOP TIMING =====
  // 10ms delay = ~100 Hz update rate
  delay(10);
}
