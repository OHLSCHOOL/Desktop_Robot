//======================================== 
//  T-800 DESKTOP ROBOT CONTROL SYSTEM ARCHITECTURE V2
//========================================

#include <WiFi.h>
#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <time.h>
#include <Preferences.h>
#include <pgmspace.h>

// =====================================================
// SECTION 1: SYSTEM CONSTANTS
// =====================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define NUM_SERVOS 4
#define NUM_GAMES 3

#define DISPLAY_UPDATE_INTERVAL 40
#define INPUT_UPDATE_INTERVAL 10
#define SERVO_UPDATE_INTERVAL 10

#define DEMO_POSE_INTERVAL 2500
#define IDLE_TIMEOUT 30000

#define SERVO_MIN_ANGLE 0
#define SERVO_MAX_ANGLE 180

#define SMOOTH_INTERPOLATION 0.08f
#define MOVEMENT_THRESHOLD 0.05f
#define SERVO_SPEED_MULTIPLIER 5.0f

#define JOYSTICK_CENTER 2048
#define JOYSTICK_DEADZONE 700
#define JOYSTICK_MAX_RANGE (JOYSTICK_CENTER - JOYSTICK_DEADZONE)

#define GAME_MODE_ENTRY_TIMEOUT 3000
#define GAME_MENU_DEBOUNCE 200

#define GRIP_OPEN 70
#define GRIP_CLOSE 160

#define TARGET_HUNT_TIMEOUT 60000
#define REACTION_WAIT_PHASE 2000
#define REACTION_GO_PHASE 5000
#define DODGE_TIMEOUT 120000

#define DUAL_PRESS_TIMEOUT 300
#define NUM_DEMO_POSES 10

// =====================================================
// PIN CONFIGURATION
// =====================================================

// Servo Pins
const uint8_t servoPins[] = {
    4,
    5,
    6,
    10
};

// Joystick Analog Pins
const uint8_t joyX1 = 1;
const uint8_t joyY1 = 0;

const uint8_t joyX2 = 2;
const uint8_t joyY2 = 3;

// Joystick Button Pins
const uint8_t joySW1 = 20;
const uint8_t joySW2 = 7;

// Audio
const uint8_t buzzerPin = 21;

// I2C OLED Pins
const uint8_t I2C_SDA = 8;
const uint8_t I2C_SCL = 9;

// =====================================================
// SECTION 3: ENUMS
// =====================================================

enum RobotMode {
    MANUAL,
    DEMO,
    GAME_MENU,
    GAME_ACTIVE
};

enum GameType : uint8_t {

    TARGET_HUNT = 0,
    REACTION_GAME = 1,
    DODGE_GAME = 2
};
// =====================================================
// SECTION 4: STRUCTURES
// =====================================================

struct InputState {

    bool bothHeld;

    uint32_t bothHeldStart;

    bool lastSW1;
    bool lastSW2;
};

struct RobotState {

    RobotMode mode;

    float targetPos[NUM_SERVOS];
    float currentPos[NUM_SERVOS];

    bool gripOpen;

    uint32_t lastInputTime;
    uint32_t lastDisplayUpdate;
    uint32_t lastServoUpdate;
    uint32_t lastDemoSwitch;

    uint8_t currentDemoPose;
    uint8_t screenStage;
};

struct ServoState {

    bool attached[NUM_SERVOS];
    bool faulted[NUM_SERVOS];
};

struct GameState {

    GameType currentGame;

    int score;
    int targetScore;

    bool gameWon;
    bool gameLost;

    uint32_t gameStartTime;

    // =================================
    // TARGET HUNT
    // =================================

    int targetX;
    int targetY;

    int playerX;
    int playerY;

    // =================================
    // REACTION GAME
    // =================================

    bool reactionReady;
    bool reactionPressed;

    uint32_t reactionSignalTime;
    uint32_t reactionTime;

    // =================================
    // DODGE
    // =================================

    int obstacleX;
};
// =====================================================
// SECTION 5: GLOBALS
// =====================================================

Preferences prefs;

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    -1
);

Servo servos[NUM_SERVOS];

RobotState robot;
ServoState servoState;
InputState input;
GameState game;

// =====================================================
// SECTION 6: DEMO POSES IN FLASH MEMORY
// =====================================================

const uint8_t demoPoses[][NUM_SERVOS] PROGMEM = {

    {90, 45, 130, 70},
    {45, 90, 90, 160},
    {135, 90, 90, 160},
    {90, 150, 30, 70},
    {90, 20, 160, 160},
    {20, 45, 45, 70},
    {160, 45, 45, 70},
    {90, 90, 45, 160},
    {45, 120, 120, 70},
    {90, 90, 90, 70}
};

const float homePosition[NUM_SERVOS] = {

    70,
    35,
    0,
    160
};
// =====================================================
// SECTION 7: AUDIO ENGINE
// =====================================================

namespace AudioEngine {

void stop() {
    noTone(buzzerPin);
}

void systemsOnline() {

    for (int i = 0; i < 3; i++) {

        tone(buzzerPin, 1000, 50);
        delay(80);
    }

    stop();
}

void gameSelect() {

    tone(buzzerPin, 1046, 80);
    delay(50);

    tone(buzzerPin, 1046, 80);
}

void gameSuccess() {

    tone(buzzerPin, 1046, 150);
    delay(100);

    tone(buzzerPin, 1318, 250);
}

void gameFail() {

    tone(buzzerPin, 440, 250);
    delay(150);

    tone(buzzerPin, 220, 350);
}

void servoLock() {

    for (int i = 0; i < 4; i++) {

        tone(buzzerPin, 800 + (i * 200), 50);
        delay(60);
    }

    stop();
}

void servoRelease() {

    for (int i = 3; i >= 0; i--) {

        tone(buzzerPin, 800 + (i * 200), 50);
        delay(60);
    }

    stop();
}

void targetingHum(float speed) {

    static uint8_t phase = 0;

    if (speed <= 0.1f) {
        return;
    }

    int baseFreq = 500 + (speed * 300);
    int variation = sin(phase * 0.1f) * 100;

    tone(buzzerPin, baseFreq + variation, 25);

    phase = (phase + 1) % 63;
}

} // namespace AudioEngine

// =====================================================
// SECTION 8: DISPLAY MANAGER
// =====================================================

namespace DisplayManager {

void drawFrame() {

    display.drawRect(
        0,
        0,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        WHITE
    );
}

void header(const char* title) {

    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(WHITE);

    display.setCursor(4, 2);
    display.println(title);

    display.drawLine(
        0,
        10,
        127,
        10,
        WHITE
    );
}

void telemetry() {

    header("T-800 COMBAT MODE");

    for (int i = 0; i < 3; i++) {

        display.setCursor(2, 16 + (i * 10));

        display.printf(
            "AX%d:%3.0f",
            i + 1,
            robot.currentPos[i]
        );
    }

    display.setCursor(2, 50);

    display.printf(
        "GRIP: %s",
        robot.gripOpen ? "OPEN" : "LOCKED"
    );

    drawFrame();

    display.display();
}

void gameEntryProgress() {

    header("GAME MODE ENTRY");

    uint32_t heldTime =
        millis() - input.bothHeldStart;

    uint32_t progress = map(
        constrain(
            heldTime,
            0,
            GAME_MODE_ENTRY_TIMEOUT
        ),
        0,
        GAME_MODE_ENTRY_TIMEOUT,
        0,
        100
    );

    display.setCursor(18, 24);
    display.println("HOLD TO ENTER");

    display.drawRect(14, 40, 100, 10, WHITE);

    display.fillRect(
        15,
        41,
        progress - 2,
        8,
        WHITE
    );

    display.display();
}

void gameMenu() {

    header("COMBAT GAMES");

    const char* names[NUM_GAMES] = {
        "TARGET HUNT",
        "REACTION",
        "DODGE"
    };

    for (int i = 0; i < NUM_GAMES; i++) {

        display.setCursor(18, 18 + (i * 12));

        if (i == game.currentGame) {
            display.print("> ");
        } else {
            display.print("  ");
        }

        display.println(names[i]);
    }

    drawFrame();

    display.display();
}

void reactionGameWaiting() {

    header("REACTION TIME");

    display.setTextSize(2);

    display.setCursor(28, 24);
    display.println("WAIT");

    display.setTextSize(1);

    drawFrame();

    display.display();
}

void reactionGameGo() {

    header("REACTION TIME");

    display.setTextSize(3);

    display.setCursor(32, 20);
    display.println("GO!");

    display.setTextSize(1);

    drawFrame();

    display.display();
}

void reactionGameWin() {

    header("REACTION TIME");

    display.setCursor(20, 22);
    display.println("PERFECT!");

    display.setCursor(18, 40);

    display.printf(
        "TIME: %ld ms",
        game.reactionTime
    );

    drawFrame();

    display.display();
}

void reactionGameLose() {

    header("REACTION TIME");

    display.setCursor(24, 24);
    display.println("TOO SLOW");

    drawFrame();

    display.display();
}

} // namespace DisplayManager

// =====================================================
// SECTION 9: SERVO CONTROLLER
// =====================================================

namespace ServoController {

void writeServo(uint8_t index, float angle) {

    if (index >= NUM_SERVOS) {
        return;
    }

    if (!servoState.attached[index]) {
        return;
    }

    if (servoState.faulted[index]) {
        return;
    }

    angle = constrain(
        angle,
        SERVO_MIN_ANGLE,
        SERVO_MAX_ANGLE
    );

    servos[index].write(angle);
}

void update() {

    if (
        millis() - robot.lastServoUpdate <
        SERVO_UPDATE_INTERVAL
    ) {
        return;
    }

    robot.lastServoUpdate = millis();

    for (int i = 0; i < NUM_SERVOS; i++) {

        float diff =
            robot.targetPos[i] -
            robot.currentPos[i];

        if (abs(diff) > MOVEMENT_THRESHOLD) {

            robot.currentPos[i] +=
                diff * SMOOTH_INTERPOLATION;
        }
        else {

            robot.currentPos[i] =
                robot.targetPos[i];
        }

        writeServo(
            i,
            robot.currentPos[i]
        );
    }
}

void returnHome() {

    for (int i = 0; i < NUM_SERVOS; i++) {

        robot.targetPos[i] =
            homePosition[i];
    }

    robot.mode = MANUAL;

    robot.lastInputTime = millis();
}

void setGrip(bool open) {

    robot.gripOpen = open;

    robot.targetPos[3] =
        open ? GRIP_OPEN : GRIP_CLOSE;

    if (open) {
        AudioEngine::servoRelease();
    } else {
        AudioEngine::servoLock();
    }
}

} 
// =====================================================
// SECTION 10: INPUT MANAGER
// =====================================================

namespace InputManager {

float applyDeadzone(int value) {

    int diff = value - JOYSTICK_CENTER;

    if (abs(diff) < JOYSTICK_DEADZONE) {
        return 0.0f;
    }

    float normalized =
        (float)(abs(diff) - JOYSTICK_DEADZONE) /
        JOYSTICK_MAX_RANGE;

    normalized =
        constrain(normalized, 0.0f, 1.0f);

    return (diff > 0)
        ? normalized
        : -normalized;
}

void processJoysticks() {

    if (
        robot.mode == GAME_MENU ||
        robot.mode == GAME_ACTIVE
    ) {
        return;
    }

    int raw[3] = {

        analogRead(joyX1),
        analogRead(joyY1),
        analogRead(joyY2)
    };

    bool moved = false;
    float maxSpeed = 0;

    for (int i = 0; i < 3; i++) {

        float input = applyDeadzone(raw[i]);

        if (i == 1 || i == 2) {
            input = -input;
        }

        if (input != 0.0f) {

            float speed =
                input *
                abs(input) *
                SERVO_SPEED_MULTIPLIER;

            robot.targetPos[i] += speed;

            robot.targetPos[i] = constrain(
                robot.targetPos[i],
                SERVO_MIN_ANGLE,
                SERVO_MAX_ANGLE
            );

            moved = true;

            if (abs(speed) > maxSpeed) {
                maxSpeed = abs(speed);
            }
        }
    }

    if (moved) {

        robot.lastInputTime = millis();
        robot.mode = MANUAL;

        AudioEngine::targetingHum(maxSpeed);
    }
}

void processButtons() {

    bool sw1 =
        digitalRead(joySW1) == LOW;

    bool sw2 =
        digitalRead(joySW2) == LOW;

    // =====================================
    // BOTH BUTTONS HELD
    // =====================================

    if (sw1 && sw2) {

        if (!input.bothHeld) {

            input.bothHeld = true;
            input.bothHeldStart = millis();
        }

        uint32_t heldTime =
            millis() - input.bothHeldStart;

        if (
            heldTime >= GAME_MODE_ENTRY_TIMEOUT &&
            robot.mode != GAME_MENU &&
            robot.mode != GAME_ACTIVE
        ) {

            robot.mode = GAME_MENU;

            game.currentGame = TARGET_HUNT;

            AudioEngine::gameSelect();

            delay(300);
        }

        return;
    }

    // =====================================
    // BOTH RELEASED
    // =====================================

    if (input.bothHeld) {

        uint32_t heldTime =
            millis() - input.bothHeldStart;

        if (
            heldTime < DUAL_PRESS_TIMEOUT &&
            robot.mode == GAME_MENU
        ) {

            robot.mode = GAME_ACTIVE;

            game.gameStartTime = millis();

            game.score = 0;
            game.gameWon = false;
            game.gameLost = false;

            AudioEngine::gameSelect();

            switch (game.currentGame) {

                case 0:

                    game.targetX =
                        random(10, 118);

                    game.targetY =
                        random(14, 54);

                    game.playerX = 64;
                    game.playerY = 32;

                    game.targetScore = 5;

                    break;

                case 1:

                    game.reactionReady = false;
                    game.reactionPressed = false;
                    game.targetScore = 1;

                    break;

                case 2:

                    game.obstacleX = 120;
                    game.playerX = 64;
                    game.targetScore = 10;

                    break;
            }
        }

        input.bothHeld = false;
    }

    // =====================================
    // SW1
    // =====================================

    if (
        sw1 &&
        !input.lastSW1
    ) {

        if (robot.mode == GAME_ACTIVE) {

            robot.mode = MANUAL;

            ServoController::returnHome();
        }
        else if (
            robot.mode != GAME_MENU
        ) {

            ServoController::returnHome();

            AudioEngine::servoRelease();
        }
    }

    // =====================================
    // SW2
    // =====================================

    if (
        sw2 &&
        !input.lastSW2
    ) {

        if (
            robot.mode == GAME_ACTIVE &&
            game.currentGame == 1
        ) {

            if (
                game.reactionReady &&
                !game.reactionPressed
            ) {

                game.reactionPressed = true;

                game.reactionTime =
                    millis() -
                    game.reactionSignalTime;

                game.gameWon = true;

                AudioEngine::gameSuccess();
            }
        }
        else if (
            robot.mode != GAME_MENU
        ) {

            ServoController::setGrip(
                !robot.gripOpen
            );
        }
    }

    input.lastSW1 = sw1;
    input.lastSW2 = sw2;
}

void processMenuNavigation() {

    if (robot.mode != GAME_MENU) {
        return;
    }

    static uint32_t lastMove = 0;

    if (
        millis() - lastMove <
        GAME_MENU_DEBOUNCE
    ) {
        return;
    }

    int value = analogRead(joyX2);

    int diff = value - JOYSTICK_CENTER;

    if (abs(diff) < JOYSTICK_DEADZONE) {
        return;
    }

    if (diff > 0) {

        game.currentGame =
            (GameType)((game.currentGame + 1) % NUM_GAMES);
    }
    else {

        game.currentGame =
           (GameType)((game.currentGame - 1 + NUM_GAMES) % NUM_GAMES);
    }

    AudioEngine::gameSelect();

    lastMove = millis();
}

} // namespace InputManager

// =====================================================
// SECTION 11: DEMO MODE
// =====================================================

namespace DemoMode {

void update() {

    if (
        millis() - robot.lastInputTime <
        IDLE_TIMEOUT
    ) {

        if (robot.mode == DEMO) {
            robot.mode = MANUAL;
        }

        return;
    }

    robot.mode = DEMO;

    if (
        millis() - robot.lastDemoSwitch <
        DEMO_POSE_INTERVAL
    ) {
        return;
    }

    robot.lastDemoSwitch = millis();

    robot.currentDemoPose++;

    if (
        robot.currentDemoPose >=
        NUM_DEMO_POSES
    ) {

        robot.currentDemoPose = 0;
    }

    for (int i = 0; i < NUM_SERVOS; i++) {

        robot.targetPos[i] =
            demoPoses
            [robot.currentDemoPose]
            [i];
    }
}

} // namespace DemoMode

// =====================================================
// SECTION 12: GAME ENGINE
// =====================================================

namespace GameEngine {

// =====================================
// TARGET HUNT
// =====================================

void updateTargetHunt() {

    int x =
        analogRead(joyX1);

    int y =
        analogRead(joyY1);

    if (
        abs(x - JOYSTICK_CENTER) >
        JOYSTICK_DEADZONE
    ) {

        game.playerX +=
            (x > JOYSTICK_CENTER)
            ? 2 : -2;
    }

    if (
        abs(y - JOYSTICK_CENTER) >
        JOYSTICK_DEADZONE
    ) {

        game.playerY +=
            (y < JOYSTICK_CENTER)
            ? 2 : -2;
    }

    game.playerX = constrain(
        game.playerX,
        8,
        120
    );

    game.playerY = constrain(
        game.playerY,
        14,
        56
    );

    if (
        abs(game.playerX - game.targetX) < 5 &&
        abs(game.playerY - game.targetY) < 5
    ) {

        game.score++;

        AudioEngine::gameSuccess();

        game.targetX =
            random(10, 118);

        game.targetY =
            random(14, 54);

        if (
            game.score >=
            game.targetScore
        ) {

            game.gameWon = true;
        }
    }

    DisplayManager::header(
        "TARGET HUNT"
    );

    display.drawCircle(
        game.targetX,
        game.targetY,
        4,
        WHITE
    );

    display.fillRect(
        game.playerX - 3,
        game.playerY - 3,
        6,
        6,
        WHITE
    );

    display.setCursor(4, 54);

    display.printf(
        "SCORE %d/%d",
        game.score,
        game.targetScore
    );

    display.display();
}

// =====================================
// REACTION GAME
// =====================================

void updateReactionGame() {

    uint32_t elapsed =
        millis() -
        game.gameStartTime;

    if (elapsed < 2000) {

        DisplayManager::
            reactionGameWaiting();

        return;
    }

    if (elapsed < 5000) {

        if (!game.reactionReady) {

            game.reactionReady = true;

            game.reactionSignalTime =
                millis();

            AudioEngine::systemsOnline();
        }

        DisplayManager::
            reactionGameGo();

        return;
    }

    if (!game.gameWon) {

        game.gameLost = true;

        AudioEngine::gameFail();
    }
}
// =====================================
// DODGE GAME
// =====================================

void updateDodgeGame() {

    int x =
        analogRead(joyX2);

    if (
        abs(x - JOYSTICK_CENTER) >
        JOYSTICK_DEADZONE
    ) {

        game.playerX +=
            (x > JOYSTICK_CENTER)
            ? 3 : -3;
    }

    game.playerX = constrain(
        game.playerX,
        10,
        118
    );

    game.obstacleX -= 3;

    if (game.obstacleX < 0) {

        game.obstacleX = 128;

        game.score++;

        AudioEngine::gameSuccess();

        if (
            game.score >=
            game.targetScore
        ) {

            game.gameWon = true;
        }
    }

    if (
        abs(game.playerX - game.obstacleX) < 8
    ) {

        game.gameLost = true;

        AudioEngine::gameFail();
    }

    DisplayManager::header("DODGE");

    display.drawLine(
        0,
        30,
        127,
        30,
        WHITE
    );

    display.drawLine(
        0,
        50,
        127,
        50,
        WHITE
    );

    display.fillRect(
        game.obstacleX,
        35,
        4,
        10,
        WHITE
    );

    display.fillRect(
        game.playerX - 4,
        40,
        8,
        4,
        WHITE
    );

    display.setCursor(4, 54);

    display.printf(
        "DODGED %d/%d",
        game.score,
        game.targetScore
    );

    display.display();
}

// =====================================
// GAME UPDATE ROUTER
// =====================================

void update() {

    if (robot.mode != GAME_ACTIVE) {
        return;
    }

    switch (game.currentGame) {

        case 0:
            updateTargetHunt();
            break;

        case 1:
            updateReactionGame();
            break;

        case 2:
            updateDodgeGame();
            break;
    }

    // =================================
    // WIN SCREEN
    // =================================

    if (game.gameWon) {

        display.clearDisplay();

        display.setTextSize(2);

        display.setCursor(18, 22);
        display.println("YOU WIN");

        display.display();

        delay(2500);

        robot.mode = MANUAL;

        ServoController::returnHome();
    }

    // =================================
    // LOSS SCREEN
    // =================================

    if (game.gameLost) {

        display.clearDisplay();

        display.setTextSize(2);

        display.setCursor(8, 22);
        display.println("GAME OVER");

        display.display();

        delay(2500);

        robot.mode = MANUAL;

        ServoController::returnHome();
    }
}

} // namespace GameEngine

// =====================================================
// SECTION 13: WIFI + CLOCK
// =====================================================

void initializeWiFi() {

    prefs.begin("wifi", true);

    String storedSSID =
        prefs.getString("ssid", "");

    String storedPASS =
        prefs.getString("pass", "");

    prefs.end();

    if (
        storedSSID.length() == 0
    ) {

        Serial.println(
            "[WARN] No WiFi credentials stored"
        );

        return;
    }

    WiFi.begin(
        storedSSID.c_str(),
        storedPASS.c_str()
    );

    Serial.print(
        "[INFO] Connecting WiFi"
    );

    uint32_t start = millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - start < 10000
    ) {

        Serial.print(".");
        delay(250);
    }

    Serial.println();

    if (
        WiFi.status() ==
        WL_CONNECTED
    ) {

        Serial.println(
            "[OK] WiFi Connected"
        );

        configTzTime(
            "EST5EDT,M3.2.0,M11.1.0",
            "pool.ntp.org"
        );
    }
    else {

        Serial.println(
            "[WARN] WiFi timeout"
        );
    }
}

// =====================================================
// SECTION 14: BOOT DISPLAY
// =====================================================

void bootAnimation() {

    for (
        int i = 0;
        i < 60;
        i++
    ) {

        display.clearDisplay();

        display.setTextSize(1);

        display.setCursor(16, 8);
        display.println(
            "T-800 INITIALIZING"
        );

        display.drawRect(
            12,
            30,
            104,
            10,
            WHITE
        );

        int width = map(
            i,
            0,
            59,
            0,
            100
        );

        display.fillRect(
            14,
            32,
            width,
            6,
            WHITE
        );

        display.display();

        delay(35);
    }
}

// =====================================================
// SECTION 15: SETUP
// =====================================================

void setup() {

    Serial.begin(115200);

    delay(300);

    // =================================
    // GPIO
    // =================================

    pinMode(
        joySW1,
        INPUT_PULLUP
    );

    pinMode(
        joySW2,
        INPUT_PULLUP
    );

    pinMode(
        buzzerPin,
        OUTPUT
    );

    // =================================
    // DISPLAY
    // =================================

    Wire.begin(
        I2C_SDA,
        I2C_SCL
    );

    display.begin(
        SSD1306_SWITCHCAPVCC,
        0x3C
    );

    display.clearDisplay();
    display.display();

    // =================================
    // SERVO SETUP
    // =================================

    for (int i = 0; i < NUM_SERVOS; i++) {

        servoState.attached[i] =
            servos[i].attach(
                servoPins[i]
            );

        servoState.faulted[i] = false;

        robot.currentPos[i] = 90;
        robot.targetPos[i] = 90;
    }

    ServoController::setGrip(true);

    // =================================
    // ROBOT STATE
    // =================================

    robot.mode = MANUAL;

    robot.lastInputTime =
        millis();

    robot.lastDemoSwitch =
        millis();

    robot.lastServoUpdate =
        millis();

    robot.currentDemoPose = 0;

    // =================================
    // BOOT SEQUENCE
    // =================================

    bootAnimation();

    AudioEngine::systemsOnline();

    initializeWiFi();

    Serial.println();
    Serial.println(
        "T-800 SYSTEM ONLINE"
    );
}

// =====================================================
// SECTION 16: MAIN LOOP
// =====================================================

void loop() {

    // =================================
    // INPUT
    // =================================

    InputManager::processButtons();

    InputManager::processJoysticks();

    InputManager::processMenuNavigation();

    // =================================
    // DEMO MODE
    // =================================

    if (
        robot.mode != GAME_MENU &&
        robot.mode != GAME_ACTIVE
    ) {

        DemoMode::update();
    }

    // =================================
    // SERVO UPDATE
    // =================================

    ServoController::update();

    // =================================
    // DISPLAY + GAME STATE
    // =================================

    switch (robot.mode) {

        case MANUAL:

            DisplayManager::telemetry();

            break;

        case DEMO:

            DisplayManager::telemetry();

            break;

        case GAME_MENU:

            DisplayManager::gameMenu();

            break;

        case GAME_ACTIVE:

            GameEngine::update();

            break;
    }

    // =================================
    // HOLD FEEDBACK
    // =================================

    if (
        input.bothHeld &&
        robot.mode != GAME_MENU &&
        robot.mode != GAME_ACTIVE
    ) {

        DisplayManager::
            gameEntryProgress();
    }

    delay(10);
}

// =====================================================
// END OF FILE
// =====================================================
