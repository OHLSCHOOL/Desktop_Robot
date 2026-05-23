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

/* 
 * [SECTION 5: ROBOT STATE STRUCTURE]
 */
enum RobotMode { MANUAL = 0, DEMO = 1, IDLE = 2 };

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

void processGripperControl() {
  bool sw2 = digitalRead(joySW2) == LOW;
  static bool lastSw2 = HIGH;
  
  if (sw2 && lastSw2 == HIGH) {
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
  lastSw2 = sw2;
}

void processHomeButton() {
  bool sw1 = digitalRead(joySW1) == LOW;
  static bool lastSw1 = HIGH;
  
  if (sw1 && lastSw1 == HIGH) {
    returnToHome();
    playServoRelease();
    Serial.println("HOME POSITION - All servos centered");
  }
  lastSw1 = sw1;
}

/* 
 * [SECTION 9: OPERATING MODE MANAGEMENT]
 */
void updateOperatingMode() {
  uint32_t now = millis();
  
  if (now - robot.lastInputTime > IDLE_TIMEOUT) {
    robot.mode = DEMO;
  } else {
    robot.mode = MANUAL;
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
 * [SECTION 10: T-800 DISPLAY MANAGEMENT]
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
  display.println("T-800 SERIES 800");
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
  display.setCursor(18, 2);
  display.println("[T-800 COMBAT MODE]");
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
  display.setCursor(32, 2);
  display.println("[AUTONOMOUS MODE]");
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
        
        display.setCursor(12, 38);
        display.println("MISSION TIME:");
        strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
        display.setCursor(28, 48);
        display.println(buf);
        break;

      case 1: // Mission time display
        display.setCursor(20, 16);
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
 * [SECTION 11: INITIALIZATION]
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
 * [SECTION 12: MAIN CONTROL LOOP]
 */
void loop() {
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
  }
  
  if (robot.mode != DEMO) {
    noTone(buzzerPin);
  }

  delay(10);
}
