#include <WiFi.h>
#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <time.h>

/* 
 * [SECTION 1: HARDWARE & NETWORK CONFIGURATION]
 */
const char* ssid = "";    // wifi name inside ""
const char* password = ""; //wifi password inside ""
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* 
 * [SECTION 2: PIN ASSIGNMENTS]
 */
const int servoPins[] = {4, 5, 6, 10};
const int joyX1 = 1, joyY1 = 0; 
const int joyX2 = 2, joyY2 = 3; 
const int joySW1 = 21, joySW2 = 7;
const int buzzerPin = 20;
#define I2C_SDA 8
#define I2C_SCL 9

/* 
 * [SECTION 3: ROBOT KINEMATICS & SMOOTHING]
 */
Servo servos[4];
float pos[] = {90, 90, 90, 160};      
float currentPos[] = {90, 90, 90, 160}; 
bool gripOpen = true;
const int GRIP_CLOSE = 160;
const int GRIP_OPEN = 70;
const float SMOOTH_SPEED = 0.03; 

/* 
 * [SECTION 4: AUTONOMOUS POSE DATABASE]
 */
float demoPoses[10][4] = {
  {90, 45, 130, 70},  {45, 90, 90, 160}, {135, 90, 90, 160},
  {90, 150, 30, 70},  {90, 20, 160, 160}, {20, 45, 45, 70},
  {160, 45, 45, 70},  {90, 90, 45, 160}, {45, 120, 120, 70},
  {90, 90, 90, 70}
};

/* 
 * [SECTION 5: SYSTEM TIMERS]
 */
unsigned long lastInputTime = 0;
const unsigned long TIMEOUT = 30000;      
const unsigned long DEMO_INTERVAL = 30000; 
unsigned long lastDemoSwitch = 0;
unsigned long lastScreenSwitch = 0;
int currentDemoPose = 0;
int screenStage = 0; 

/* 
 * [SECTION 6: SCI-FI AUDIO ENGINE]
 */
void playSciFiStartup() {
  int startupNotes[] = {300, 600, 450, 900, 750, 1500, 2000};
  for (int n : startupNotes) { tone(buzzerPin, n, 100); delay(120); }
  for (int f = 2000; f < 3000; f += 50) { tone(buzzerPin, f, 10); delay(10); }
  noTone(buzzerPin);
}

void playSciFiHum(float speed) {
  if (speed > 0.1) {
    static int phase = 0;
    int baseFreq = 700 + (int)(speed * 180);
    int osc = sin(phase * 0.4) * 80; 
    tone(buzzerPin, baseFreq + osc, 20);
    phase++;
  }
}

void playChirp(bool open) {
  for(int i=0; i<3; i++) {
    int f = open ? (1000 + i*500) : (2500 - i*500);
    tone(buzzerPin, f, 40); delay(50);
  }
  noTone(buzzerPin);
}

/* 
 * [SECTION 7: TELEMETRY DISPLAY]
 */
void updateTelemetryDisplay(int inputs[]) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(10, 2);
  display.println(">> MANUAL OVERRIDE");
  for(int i=0; i<3; i++) {
    display.setCursor(5, 17 + (i*11));
    display.printf("AX %d: %3.0f | IN:%4d", i+1, pos[i], inputs[i]);
  }
  display.setCursor(5, 52);
  display.printf("GRIPPER: %s", gripOpen ? "READY" : "LOCKED");
  display.display();
}

/* 
 * [SECTION 8: AUTONOMOUS LOGIC & PIXEL ROTATION]
 */
void runDemoMode() {
  if (millis() - lastDemoSwitch > DEMO_INTERVAL) {
    currentDemoPose = (currentDemoPose + 1) % 10;
    lastDemoSwitch = millis();
  }

  for (int i = 0; i < 4; i++) {
    float target = demoPoses[currentDemoPose][i];
    currentPos[i] += (target - currentPos[i]) * SMOOTH_SPEED;
    servos[i].write((int)currentPos[i]);
  }

  if (millis() - lastScreenSwitch > 10000) {
    screenStage = (screenStage + 1) % 3;
    lastScreenSwitch = millis();
  }

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(34, 3);
  display.print("AUTOMATION");
  display.drawLine(0, 12, 127, 12, WHITE);

  struct tm timeinfo;
  char buf[20];
  if (getLocalTime(&timeinfo)) {
    switch (screenStage) {
      case 0: // DATE LAYOUT - Size 2, Two Lines
        display.setTextSize(2);
        // Line 1: Month and Day (e.g., "May 07")
        strftime(buf, sizeof(buf), "%b %d", &timeinfo);
        // Size 2 chars are 12px wide. "May 07" is 6 chars = 72px. (128-72)/2 = 28
        display.setCursor(28, 22); 
        display.print(buf);
        
        // Line 2: Year (e.g., "2026")
        strftime(buf, sizeof(buf), "%Y", &timeinfo);
        // "2026" is 4 chars = 48px. (128-48)/2 = 40
        display.setCursor(40, 42); 
        display.print(buf);
        break;

      case 1: // TIME LAYOUT
        strftime(buf, sizeof(buf), "%I:%M %p", &timeinfo);
        display.setTextSize(2);
        // "02:05 PM" is 8 chars = 96px. (128-96)/2 = 16
        display.setCursor(16, 32); 
        display.print(buf);
        break;

      case 2: // STATUS LAYOUT
        display.setTextSize(1);
        display.setCursor(10, 25);
        display.printf("EXECUTION POSE:%d/10", currentDemoPose + 1);
        display.drawRect(15, 50, 98, 8, WHITE);
        display.fillRect(17, 52, (currentDemoPose + 1) * 9, 4, WHITE);
        break;
    }
  }
  display.display();
}

/* 
 * [SECTION 9: INITIALIZATION]
 */
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  
  pinMode(buzzerPin, OUTPUT);
  pinMode(joySW1, INPUT_PULLUP);
  pinMode(joySW2, INPUT_PULLUP);

  for(int i=0; i<4; i++) {
    servos[i].attach(servoPins[i]);
    servos[i].write((int)pos[i]);
  }

  playSciFiStartup();
  WiFi.begin(ssid, password);
  configTzTime("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org");
}

/* 
 * [SECTION 10: MAIN CONTROL LOOP]
 */
void loop() {
  int rawInputs[4] = {analogRead(joyX1), analogRead(joyY1), analogRead(joyY2), analogRead(joyX2)};
  bool sw2 = digitalRead(joySW2) == LOW;
  bool moved = false;
  float maxSpeed = 0;

  for(int i=0; i<3; i++) {
    int diff = rawInputs[i] - 2048;
    if(abs(diff) > 700) {
      float normalized = (float)(abs(diff) - 700) / (2048 - 700);
      float speed = (normalized * normalized) * 5.0;
      pos[i] = constrain(pos[i] + (diff > 0 ? speed : -speed), 0, 180);
      moved = true;
      if(speed > maxSpeed) maxSpeed = speed;
    }
  }

  static bool lastSw2 = HIGH;
  if(sw2 && lastSw2 == HIGH) {
    gripOpen = !gripOpen;
    pos[3] = gripOpen ? GRIP_OPEN : GRIP_CLOSE;
    moved = true;
    playChirp(gripOpen);
  }
  lastSw2 = sw2;

  if(moved) {
    lastInputTime = millis();
    for(int i=0; i<4; i++) {
      servos[i].write((int)pos[i]);
      currentPos[i] = pos[i];
    }
    playSciFiHum(maxSpeed);
    updateTelemetryDisplay(rawInputs);
  } else {
    noTone(buzzerPin);
    if(millis() - lastInputTime > TIMEOUT) {
      runDemoMode();
    } else {
      updateTelemetryDisplay(rawInputs);
    }
  }
  delay(15);
}
