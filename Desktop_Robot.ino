#include <WiFi.h>
#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <time.h>

/* 
 * [SECTION 1: HARDWARE & NETWORK CONFIGURATION]
 */
const char* ssid = "";
const char* password = "";
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* 
 * [SECTION 2: PIN ASSIGNMENTS]
 */
const int servoPins[] = {4, 5, 6, 10};
const int joyX1 = 1, joyY1 = 0; 
const int joyX2 = 2, joyY2 = 3; 
const int joySW1 = 20, joySW2 = 7;
const int buzzerPin = 21;
#define I2C_SDA 8
#define I2C_SCL 9

/* 
 * [SECTION 3: CONFIGURATION CONSTANTS]
 */
#define JOYSTICK_CENTER 2048
#define JOYSTICK_DEADZONE 700
#define JOYSTICK_MAX_RANGE (JOYSTICK_CENTER - JOYSTICK_DEADZONE)
#define SERVO_SPEED_MULTIPLIER 5.0
#define SMOOTH_INTERPOLATION 0.08
#define SERVO_MIN_ANGLE 0
#define SERVO_MAX_ANGLE 180
#define DISPLAY_UPDATE_INTERVAL 40
#define DEMO_POSE_INTERVAL 2500
#define IDLE_TIMEOUT 30000
#define SERVO_INIT_DELAY 500
#define NUM_SERVOS 4
#define NUM_DEMO_POSES 10
#define GRIP_CLOSE 160
#define GRIP_OPEN 70
#define MOVEMENT_THRESHOLD 0.05

/* 
 * [SECTION 3.5: GAME MODE CONSTANTS]
 */
#define NUM_GAMES 3
#define DUAL_PRESS_TIMEOUT 300
#define GAME_MENU_DEBOUNCE 200
#define GAME_MODE_ENTRY_TIMEOUT 3000
#define TARGET_HUNT_TIMEOUT 60000
#define REACTION_TIME_TIMEOUT 8000
#define DODGE_TIMEOUT 120000
#define COLLISION_DISTANCE 5

/* 
 * [SECTION 4: TERMINATOR T-800 AUDIO ENGINE]
 */
void playTerminatorStartup() {
  // T-800 Boot Sequence with original composition
  
  // Deep bass pulses (power-up)
  for (int i = 0; i < 3; i++) {
    tone(buzzerPin, 60, 200);
    delay(250);
  }
  
  // Rising tension sweep
  for (int f = 200; f <= 1200; f += 40) {
    tone(buzzerPin, f, 30);
    delay(20);
  }
  
  // Main T-800 Theme riff
  int theme[] = {659, 659, 659, 622, 659, 784, 523, 392, 523, 392, 330, 523, 330, 330};
  int durations[] = {150, 150, 150, 100, 150, 300, 200, 200, 200, 200, 200, 200, 150, 300};
  
  for (int i = 0; i < 14; i++) {
    tone(buzzerPin, theme[i], durations[i]);
    delay(durations[i] + 50);
  }
  
  // Power-up chords
  tone(buzzerPin, 523, 150); delay(100);
  tone(buzzerPin, 783, 150); delay(100);
  tone(buzzerPin, 1046, 300);
  
  noTone(buzzerPin);
  delay(200);
}

void playSystemsOnline() {
  // Quick beep sequence
  for (int i = 0; i < 3; i++) {
    tone(buzzerPin, 1000, 50);
    delay(80);
  }
  noTone(buzzerPin);
}

void playTargetingHum(float speed) {
  if (speed > 0.1) {
    static int phase = 0;
    int baseFreq = 500 + (int)(speed * 300);
    int variation = sin(phase * 0.1) * 100;
    tone(buzzerPin, baseFreq + variation, 25);
    phase++;
  }
}

void playServoLock() {
  // Ascending mechanical lock
  for (int i = 0; i < 4; i++) {
    tone(buzzerPin, 800 + i * 200, 50);
    delay(60);
  }
  noTone(buzzerPin);
}

void playServoRelease() {
  // Descending mechanical release
  for (int i = 3; i >= 0; i--) {
    tone(buzzerPin, 800 + i * 200, 50);
    delay(60);
  }
  noTone(buzzerPin);
}

void playGameSelect() {
  tone(buzzerPin, 1046, 100);
  delay(50);
  tone(buzzerPin, 1046, 100);
  noTone(buzzerPin);
}

void playGameSuccess() {
  tone(buzzerPin, 1046, 150);
  delay(100);
  tone(buzzerPin, 1046, 150);
  delay(100);
  tone(buzzerPin, 1046, 300);
  noTone(buzzerPin);
}

void playGameFail() {
  tone(buzzerPin, 523, 200);
  delay(100);
  tone(buzzerPin, 440, 300);
  noTone(buzzerPin);
}

/* 
 * [SECTION 5: ROBOT STATE STRUCTURE]
 */
enum RobotMode { MANUAL = 0, DEMO = 1, IDLE = 2, GAME_MENU = 3, GAME_ACTIVE = 4 };

struct GameState {
  int currentGame;
  int score;
  uint32_t gameStartTime;
  uint32_t lastEventTime;
  int targetScore;
  bool gameWon;
  bool gameLost;
  // Target Hunt specifics
  int targetX, targetY;
  int playerX, playerY;
  // Reaction Time specifics
  uint32_t reactionStartTime;
  bool displayedReady;
  bool reactionButtonPressed;
  // Dodge specifics
  int obstacleX, obstacleY;
  int dodgePosition;
} game;

struct RobotState {
  float targetPos[NUM_SERVOS];
  float currentPos[NUM_SERVOS];
  bool gripOpen;
  uint32_t lastInputTime;
  uint32_t lastDemoSwitch;
  uint32_t lastDisplayUpdate;
  int currentDemoPose;
  int screenStage;
  RobotMode mode;
  int rawInputs[4];
  int bootStage;
  uint32_t sw1PressTime, sw2PressTime;
  bool sw1Pressed, sw2Pressed;
} robot;

Servo servos[NUM_SERVOS];

/* 
 * [SECTION 6: AUTONOMOUS POSE DATABASE]
 */
float demoPoses[NUM_DEMO_POSES][NUM_SERVOS] = {
  {90, 45, 130, 70},   {45, 90, 90, 160},  {135, 90, 90, 160},
  {90, 150, 30, 70},   {90, 20, 160, 160}, {20, 45, 45, 70},
  {160, 45, 45, 70},   {90, 90, 45, 160},  {45, 120, 120, 70},
  {90, 90, 90, 70}
};

/* HOME POSITION - Center all servos with gripper open */
float homePosition[NUM_SERVOS] = {70, 35, 0, 160};

/* 
 * [SECTION 7: SERVO CONTROL]
 */
void setServoPosition(int servoIndex, float angle) {
  if (servoIndex < 0 || servoIndex >= NUM_SERVOS) {
    Serial.printf("ERROR: Invalid servo index %d\n", servoIndex);
    return;
  }
  angle = constrain(angle, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);
  servos[servoIndex].write((int)angle);
}

void updateServoPositions() {
  for (int i = 0; i < NUM_SERVOS; i++) {
    float diff = robot.targetPos[i] - robot.currentPos[i];
    if (abs(diff) > MOVEMENT_THRESHOLD) {
      robot.currentPos[i] += diff * SMOOTH_INTERPOLATION;
      setServoPosition(i, robot.currentPos[i]);
    } else if (abs(diff) > 0.01) {
      // Direct write for final adjustments
      robot.currentPos[i] = robot.targetPos[i];
      setServoPosition(i, robot.currentPos[i]);
    }
  }
}

void returnToHome() {
  for (int i = 0; i < NUM_SERVOS; i++) {
    robot.targetPos[i] = homePosition[i];
  }
  robot.lastInputTime = millis();
  robot.mode = MANUAL;
}

/* 
 * [SECTION 8: INPUT PROCESSING]
 */
void processJoystickInput() {
  robot.rawInputs[0] = analogRead(joyX1);
  robot.rawInputs[1] = analogRead(joyY1);
  robot.rawInputs[2] = analogRead(joyY2);
  robot.rawInputs[3] = analogRead(joyX2);
  
  bool moved = false;
  float maxSpeed = 0;

  if (robot.mode != GAME_ACTIVE) {
    for (int i = 0; i < 3; i++) {
      int diff = robot.rawInputs[i] - JOYSTICK_CENTER;
      // Reverse axis 2 and axis 3 (invert the difference)
      if (i == 2 || i == 1) {
        diff = -diff;
      }
      if (abs(diff) > JOYSTICK_DEADZONE) {
        float normalized = (float)(abs(diff) - JOYSTICK_DEADZONE) / JOYSTICK_MAX_RANGE;
        float speed = (normalized * normalized) * SERVO_SPEED_MULTIPLIER;
        robot.targetPos[i] = constrain(robot.targetPos[i] + (diff > 0 ? speed : -speed), 
                                       SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);
        moved = true;
        if (speed > maxSpeed) maxSpeed = speed;
      }
    }

    if (moved) {
      robot.lastInputTime = millis();
      robot.mode = MANUAL;
      playTargetingHum(maxSpeed);
    }
  }
}

void processGripperControl() {
  bool sw2 = digitalRead(joySW2) == LOW;
  static bool lastSw2 = HIGH;
  
  if (sw2 && lastSw2 == HIGH) {
    if (robot.mode == GAME_ACTIVE) {
      // Game-specific button handling
      handleGameInput(1);
    } else {
      robot.gripOpen = !robot.gripOpen;
      robot.targetPos[3] = robot.gripOpen ? GRIP_OPEN : GRIP_CLOSE;
      robot.lastInputTime = millis();
      robot.mode = MANUAL;
      
      if (robot.gripOpen) {
        playServoRelease();
      } else {
        playServoLock();
      }
    }
  }
  lastSw2 = sw2;
}

void processHomeButton() {
  bool sw1 = digitalRead(joySW1) == LOW;
  static bool lastSw1 = HIGH;
  
  if (sw1 && lastSw1 == HIGH) {
    if (robot.mode == GAME_ACTIVE) {
      // Exit game
      exitGameMode();
      playServoRelease();
    } else if (robot.mode == GAME_MENU) {
      // Do nothing in menu
    } else {
      returnToHome();
      playServoRelease();
      Serial.println("HOME POSITION - All servos centered");
    }
  }
  lastSw1 = sw1;
}

void processDualButtonPress() {
  bool sw1 = digitalRead(joySW1) == LOW;
  bool sw2 = digitalRead(joySW2) == LOW;
  static bool lastBothPressed = false;
  static uint32_t bothPressedTime = 0;
  static bool entryDetected = false;
  
  if (sw1 && sw2 && !lastBothPressed) {
    bothPressedTime = millis();
    lastBothPressed = true;
    entryDetected = false;
  } else if (sw1 && sw2 && lastBothPressed && !entryDetected) {
    // Both buttons held down
    uint32_t holdTime = millis() - bothPressedTime;
    
    // Check for 3 second hold to enter game mode
    if (holdTime >= GAME_MODE_ENTRY_TIMEOUT && robot.mode != GAME_MENU && robot.mode != GAME_ACTIVE) {
      initializeGameMode();
      entryDetected = true;
      playGameSelect();
    }
    // Check for short press to select game (when in menu)
    else if (holdTime < DUAL_PRESS_TIMEOUT && robot.mode == GAME_MENU && entryDetected) {
      selectGame(game.currentGame);
      entryDetected = true;
    }
  } else if (!sw1 || !sw2) {
    if (lastBothPressed && (millis() - bothPressedTime) < DUAL_PRESS_TIMEOUT && !entryDetected) {
      // Short press in game menu
      if (robot.mode == GAME_MENU) {
        selectGame(game.currentGame);
      }
    }
    lastBothPressed = false;
    entryDetected = false;
  }
}

void handleGameInput(int inputType) {
  // inputType: 1 = button press
  if (game.currentGame == 1 && inputType == 1) { // Reaction Time game
    uint32_t elapsedTime = millis() - game.gameStartTime;
    
    // Only register if in GO phase (after 2000ms, before 5000ms)
    if (elapsedTime >= 2000 && elapsedTime < 5000) {
      if (!game.reactionButtonPressed) {
        game.reactionButtonPressed = true;
        game.reactionStartTime = millis() - game.gameStartTime;
        game.gameWon = true;
        playGameSuccess();
      }
    } else if (elapsedTime < 2000) {
      // Early press during wait phase - minor penalty
      playGameFail();
    }
  }
}

/* 
 * [SECTION 9: GAME MODE SYSTEM]
 */

void initializeGameMode() {
  robot.mode = GAME_MENU;
  game.currentGame = 0;
  game.score = 0;
  game.gameWon = false;
  game.gameLost = false;
  Serial.println("ENTERING GAME MODE");
  playGameSelect();
}

void exitGameMode() {
  robot.mode = MANUAL;
  robot.lastInputTime = millis();
  returnToHome();
  Serial.println("EXITING GAME MODE");
}

void drawGameMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  display.setCursor(10, 2);
  display.println("COMBAT GAMING MODE");
  display.drawLine(0, 10, 127, 10, WHITE);
  
  const char* gameNames[] = {"TARGET HUNT", "REACTION", "DODGE"};
  
  for (int i = 0; i < NUM_GAMES; i++) {
    display.setCursor(20, 18 + (i * 12));
    if (i == game.currentGame) {
      display.print(">>> ");
    } else {
      display.print("    ");
    }
    display.println(gameNames[i]);
  }
  
  display.setTextSize(1);
  display.setCursor(5, 56);
  display.println("PRESS BOTH TO SELECT");
  
  drawScanlineFrame();
  display.display();
}

void updateGameMenuInput() {
  int jX2_val = analogRead(joyX2);
  int diff = jX2_val - JOYSTICK_CENTER;
  
  static uint32_t lastMenuInput = 0;
  if (millis() - lastMenuInput < GAME_MENU_DEBOUNCE) return; // Debounce
  
  if (abs(diff) > JOYSTICK_DEADZONE) {
    if (diff > 0) {
      game.currentGame = (game.currentGame + 1) % NUM_GAMES;
      playGameSelect();
    } else {
      game.currentGame = (game.currentGame - 1 + NUM_GAMES) % NUM_GAMES;
      playGameSelect();
    }
    lastMenuInput = millis();
  }
}

void selectGame(int gameIndex) {
  game.currentGame = gameIndex;
  robot.mode = GAME_ACTIVE;
  game.gameStartTime = millis();
  game.score = 0;
  game.gameWon = false;
  game.gameLost = false;
  playGameSelect();
  
  // Initialize specific game
  switch (gameIndex) {
    case 0: // Target Hunt
      game.targetScore = 5;
      game.targetX = random(10, 110);
      game.targetY = random(10, 50);
      game.playerX = 64;
      game.playerY = 32;
      break;
    case 1: // Reaction Time
      game.targetScore = 1;
      game.displayedReady = false;
      game.reactionStartTime = 0;
      game.reactionButtonPressed = false;
      break;
    case 2: // Dodge
      game.targetScore = 10;
      game.obstacleX = 64;
      game.obstacleY = 40;
      game.dodgePosition = 64;
      break;
  }
}

/* 
 * [SECTION 10: GAME IMPLEMENTATIONS]
 */

// GAME 1: TARGET HUNT - Navigate joystick to move player towards randomly placed targets
void updateTargetHunt() {
  // Check timeout
  if (millis() - game.gameStartTime > TARGET_HUNT_TIMEOUT) {
    if (!game.gameWon && !game.gameLost) {
      game.gameLost = true;
      playGameFail();
    }
  }
  
  int jX1_val = analogRead(joyX1);
  int jY1_val = analogRead(joyY1);
  
  int diffX = jX1_val - JOYSTICK_CENTER;
  int diffY = jY1_val - JOYSTICK_CENTER;
  
  if (abs(diffX) > JOYSTICK_DEADZONE) {
    game.playerX += (diffX > 0 ? 2 : -2);
    game.playerX = constrain(game.playerX, 10, 110);
  }
  
  if (abs(diffY) > JOYSTICK_DEADZONE) {
    game.playerY += (diffY < 0 ? 2 : -2);
    game.playerY = constrain(game.playerY, 10, 50);
  }
  
  // Check collision with target - tightened distance
  if (abs(game.playerX - game.targetX) < COLLISION_DISTANCE && abs(game.playerY - game.targetY) < COLLISION_DISTANCE) {
    game.score++;
    playGameSuccess();
    if (game.score >= game.targetScore) {
      game.gameWon = true;
    }
    game.targetX = random(10, 110);
    game.targetY = random(10, 50);
  }
  
  // Draw game
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  display.setCursor(20, 2);
  display.printf("TARGET HUNT");
  display.drawLine(0, 10, 127, 10, WHITE);
  
  // Draw target (larger, more visible)
  display.drawCircle(game.targetX, game.targetY, 4, WHITE);
  display.drawCircle(game.targetX, game.targetY, 5, WHITE);
  display.drawPixel(game.targetX, game.targetY, WHITE);
  
  // Draw player (more visible)
  display.fillRect(game.playerX - 3, game.playerY - 3, 6, 6, WHITE);
  display.drawRect(game.playerX - 3, game.playerY - 3, 6, 6, BLACK); // Outline
  
  display.setCursor(3, 54);
  display.printf("SCORE: %d/%d", game.score, game.targetScore);
  
  display.display();
}

// GAME 2: REACTION TIME - Click button as fast as possible after "GO!" appears
void updateReactionTime() {
  uint32_t elapsedTime = millis() - game.gameStartTime;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  display.setCursor(15, 2);
  display.println("REACTION TIME");
  display.drawLine(0, 10, 127, 10, WHITE);
  
  if (elapsedTime < 2000) {
    // Waiting phase
    display.setTextSize(2);
    display.setCursor(30, 25);
    display.println("WAIT...");
    display.setTextSize(1);
  } else if (elapsedTime < 5000) {
    if (!game.displayedReady) {
      game.reactionStartTime = millis();
      game.displayedReady = true;
      playSystemsOnline();
    }
    // Go phase
    display.setTextSize(3);
    display.setCursor(35, 20);
    display.println("GO!");
    display.setTextSize(1);
  } else {
    // Game over - too slow
    if (!game.gameWon && !game.gameLost) {
      game.gameLost = true;
      display.setCursor(15, 25);
      display.println("TOO SLOW!");
      display.setTextSize(1);
      display.setCursor(20, 40);
      display.println("Better luck next time!");
      playGameFail();
    } else if (game.gameLost) {
      display.setCursor(15, 25);
      display.println("TOO SLOW!");
      display.setTextSize(1);
      display.setCursor(20, 40);
      display.println("Better luck next time!");
    }
  }
  
  if (game.gameWon) {
    uint32_t reactionTime = game.reactionStartTime;
    display.setTextSize(2);
    display.setCursor(10, 20);
    display.println("PERFECT!");
    display.setTextSize(1);
    display.setCursor(15, 45);
    display.printf("TIME: %ldms", reactionTime);
  }
  
  display.display();
}

// GAME 3: DODGE - Avoid obstacles moving left/right, control with joystick
void updateDodge() {
  // Check timeout
  if (millis() - game.gameStartTime > DODGE_TIMEOUT) {
    if (!game.gameWon && !game.gameLost) {
      game.gameLost = true;
      playGameFail();
    }
  }
  
  int jX2_val = analogRead(joyX2);
  int diff = jX2_val - JOYSTICK_CENTER;
  
  if (abs(diff) > JOYSTICK_DEADZONE) {
    game.dodgePosition += (diff > 0 ? 3 : -3);
    game.dodgePosition = constrain(game.dodgePosition, 30, 98);
  }
  
  // Move obstacle
  game.obstacleX -= 2;
  if (game.obstacleX < 10) {
    game.obstacleX = 118;
    game.score++;
    playGameSuccess();
    if (game.score >= game.targetScore) {
      game.gameWon = true;
    }
  }
  
  // Check collision - tightened detection
  if (abs(game.obstacleX - game.dodgePosition) < 8 &&
      game.obstacleY > 30 && game.obstacleY < 50) {
    if (!game.gameLost) {
      playGameFail();
      game.gameLost = true;
    }
  }
  
  // Draw game
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  display.setCursor(30, 2);
  display.println("DODGE");
  display.drawLine(0, 10, 127, 10, WHITE);
  
  // Draw game area
  display.drawLine(0, 28, 128, 28, WHITE);
  display.drawLine(0, 52, 128, 52, WHITE);
  
  // Draw obstacle
  display.fillRect(game.obstacleX - 2, 35, 4, 10, WHITE);
  
  // Draw player
  display.fillRect(game.dodgePosition - 4, 38, 8, 4, WHITE);
  
  display.setCursor(3, 56);
  display.printf("DODGED: %d/%d", game.score, game.targetScore);
  
  display.display();
}

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

/* 
 * [SECTION 11: OPERATING MODE MANAGEMENT]
 */
void updateOperatingMode() {
  uint32_t now = millis();
  
  if (robot.mode != GAME_MENU && robot.mode != GAME_ACTIVE) {
    if (now - robot.lastInputTime > IDLE_TIMEOUT) {
      robot.mode = DEMO;
    } else {
      robot.mode = MANUAL;
    }
  }
}

void runDemoMode() {
  if (millis() - robot.lastDemoSwitch > DEMO_POSE_INTERVAL) {
    robot.currentDemoPose = (robot.currentDemoPose + 1) % NUM_DEMO_POSES;
    robot.lastDemoSwitch = millis();
    
    for (int i = 0; i < NUM_SERVOS; i++) {
      robot.targetPos[i] = demoPoses[robot.currentDemoPose][i];
    }
  }
  
  updateServoPositions();
}

/* 
 * [SECTION 12: T-800 DISPLAY MANAGEMENT]
 */
void drawScanlineFrame() {
  // Draw border rectangle ONLY (no scanlines to avoid screen lines effect)
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
}

void updateBootDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  // Title
  display.setCursor(15, 2);
  display.println("T-800 SERIES ARMED");
  display.drawLine(0, 12, 127, 12, WHITE);
  
  // Boot sequence
  display.setCursor(10, 20);
  display.println("NEURAL NET PROCESSOR");
  display.setCursor(20, 28);
  display.println("INITIALIZING...");
  
  // Animated dots
  int dotCount = (robot.bootStage % 40) / 10;
  display.setCursor(60, 28);
  for (int i = 0; i < dotCount; i++) {
    display.print(".");
  }
  
  display.setCursor(10, 40);
  display.println("COMBAT MODE SYSTEMS");
  display.setCursor(20, 48);
  display.println("STATUS: ONLINE");
  
  drawScanlineFrame();
  display.display();
}

void updateTelemetryDisplay() {
  if (millis() - robot.lastDisplayUpdate < DISPLAY_UPDATE_INTERVAL) {
    return;
  }
  robot.lastDisplayUpdate = millis();
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  // Title bar
  display.setCursor(12, 2);
  display.println("T-800 COMBAT MODE");
  display.drawLine(0, 10, 127, 10, WHITE);
  
  // Axis readouts
  for (int i = 0; i < 3; i++) {
    display.setCursor(3, 14 + (i * 10));
    display.printf("AX%d: %3.0f  [", i + 1, robot.currentPos[i]);
    // Bar graph
    int barLength = (robot.currentPos[i] / 180.0) * 30;
    for (int j = 0; j < barLength; j++) display.print("=");
    display.println("]");
  }
  
  // Gripper status
  display.setCursor(3, 50);
  display.print("GRIPPER: ");
  display.println(robot.gripOpen ? "[OPEN]" : "[LOCKED]");
  
  drawScanlineFrame();
  display.display();
}

void updateDemoDisplay() {
  if (millis() - robot.lastDisplayUpdate < DISPLAY_UPDATE_INTERVAL) {
    return;
  }
  robot.lastDisplayUpdate = millis();
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  // Title bar
  display.setCursor(15, 2);
  display.println("AUTONOMOUS MODE");
  display.drawLine(0, 10, 127, 10, WHITE);
  
  struct tm timeinfo;
  char buf[20];
  
  if (getLocalTime(&timeinfo)) {
    switch (robot.screenStage) {
      case 0: // Neural Net Processor status
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

      case 1: // Mission time display
        display.setCursor(23, 16);
        display.println("MISSION TIME");
        display.setTextSize(2);
        strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
        display.setCursor(18, 32);
        display.println(buf);
        display.setTextSize(1);
        break;

      case 2: // Execution progress
        display.setCursor(8, 18);
        display.printf("POSE EXECUTION");
        display.setCursor(10, 26);
        display.printf("TARGET: %d/%d", robot.currentDemoPose + 1, NUM_DEMO_POSES);
        
        // Progress bar
        display.drawRect(10, 40, 108, 10, WHITE);
        int progress = ((robot.currentDemoPose + 1) * 106) / NUM_DEMO_POSES;
        display.fillRect(11, 41, progress, 8, WHITE);
        break;
    }
  }
  
  drawScanlineFrame();
  display.display();
  
  // Update screen stage periodically
  static uint32_t lastScreenSwitch = 0;
  if (millis() - lastScreenSwitch > 8000) {
    robot.screenStage = (robot.screenStage + 1) % 3;
    lastScreenSwitch = millis();
  }
}

/* 
 * [SECTION 13: INITIALIZATION]
 */
void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n");
  Serial.println("========================================");
  Serial.println("   T-800 SERIES ROBOT INITIALIZATION");
  Serial.println("========================================");
  
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[ERROR] SSD1306 display not found!");
  } else {
    Serial.println("[OK] Display initialized");
  }
  
  pinMode(buzzerPin, OUTPUT);
  pinMode(joySW1, INPUT_PULLUP);
  pinMode(joySW2, INPUT_PULLUP);
  Serial.println("[OK] GPIO pins configured");

  // Initialize servos
  for (int i = 0; i < NUM_SERVOS; i++) {
    servos[i].attach(servoPins[i]);
    robot.targetPos[i] = 90;
    robot.currentPos[i] = 90;
  }
  delay(SERVO_INIT_DELAY);
  Serial.println("[OK] Servos initialized and stabilized");

  // Set gripper initial state
  robot.gripOpen = true;
  robot.targetPos[3] = GRIP_OPEN;
  robot.currentPos[3] = GRIP_OPEN;
  setServoPosition(3, GRIP_OPEN);

  // Initialize robot state
  robot.lastInputTime = millis();
  robot.lastDemoSwitch = millis();
  robot.lastDisplayUpdate = millis();
  robot.currentDemoPose = 0;
  robot.screenStage = 0;
  robot.bootStage = 0;
  robot.mode = MANUAL;
  robot.sw1Pressed = false;
  robot.sw2Pressed = false;

  // Display boot sequence
  Serial.println("[OK] Audio system initializing...");
  
  for (robot.bootStage = 0; robot.bootStage < 80; robot.bootStage++) {
    updateBootDisplay();
    delay(50);
  }
  
  playTerminatorStartup();
  Serial.println("[OK] T-800 Theme played");
  
  playSystemsOnline();
  Serial.println("[OK] Systems online");
  
  // Start WiFi connection (non-blocking)
  WiFi.begin(ssid, password);
  configTzTime("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org");
  Serial.println("[OK] WiFi and NTP initialization started");
  
  Serial.println("========================================");
  Serial.println("   TERMINATOR ONLINE - READY TO HUNT");
  Serial.println("========================================\n");
}

/* 
 * [SECTION 14: MAIN CONTROL LOOP]
 */
void loop() {
  processDualButtonPress();
  processJoystickInput();
  processGripperControl();
  processHomeButton();
  updateOperatingMode();

  switch (robot.mode) {
    case MANUAL:
      updateServoPositions();
      updateTelemetryDisplay();
      break;
      
    case DEMO:
      runDemoMode();
      updateDemoDisplay();
      break;
      
    case IDLE:
      updateTelemetryDisplay();
      break;

    case GAME_MENU:
      updateGameMenuInput();
      drawGameMenu();
      break;

    case GAME_ACTIVE:
      switch (game.currentGame) {
        case 0: // Target Hunt
          updateTargetHunt();
          break;
        case 1: // Reaction Time
          updateReactionTime();
          break;
        case 2: // Dodge
          updateDodge();
          break;
      }
      
      if (game.gameWon) {
        handleGameWin();
      } else if (game.gameLost) {
        handleGameLose();
      }
      break;
  }
  
  if (robot.mode != DEMO && robot.mode != GAME_ACTIVE) {
    noTone(buzzerPin);
  }

  delay(10);
}
