#include <LiquidCrystal.h>
#include <Wire.h>

// ---------- Pins ----------
const int HELMET_SW1_PIN  = 5;   // helmet switch 1 - LOW = pressed
const int HELMET_SW2_PIN  = 7;   // helmet switch 2 - LOW = pressed
const int ALCOHOL_PIN     = A1;  // alcohol sensor analog pin
const int SAFE_BUTTON_PIN = 4;   // "I am safe" button (active LOW)
const int BUZZER_PIN      = A0;  // buzzer + LED pin

// ---------- LCD ----------
LiquidCrystal lcd(8, 9, 10, 11, 12, 13);  // RS, EN, D4, D5, D6, D7

// ---------- MPU6050 ----------
const byte  ACC_ADDR         = 0x68;   // MPU6050 I2C address
const float FALL_THRESHOLD_G = 2.5;    // tune this threshold (in g)
const float G_PER_LSB        = 16384.0;

long fallCount = 0;
bool inFall    = false;

// ---------- Alcohol sensor ----------
const int ALCOHOL_THRESHOLD = 50;  // tune from readings
bool alcoholDetected = false;

// ---------- State ----------
bool helmetWorn = false;

// ---------- Fall timer (30 s confirm) ----------
bool fallPending           = false;          // fall detected, waiting for confirmation
unsigned long fallStartMs  = 0;
const unsigned long FALL_CONFIRM_MS = 30000; // 30 seconds

// After timeout alert active
bool alertActive = false;  // true when time over and rider did not press safe

// ---------- Alcohol beep state ----------
bool alcoholBeepDone = false;  // to avoid repeating beeps continuously

// ---------- Prototypes ----------
void initMPU();
void readAccel(int16_t &ax, int16_t &ay, int16_t &az);
bool detectFallEvent();
void beepThreeTimes();

// ---------- SETUP ----------
void setup() {
  pinMode(HELMET_SW1_PIN, INPUT_PULLUP);
  pinMode(HELMET_SW2_PIN, INPUT_PULLUP);
  pinMode(SAFE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(ALCOHOL_PIN,     INPUT);
  pinMode(BUZZER_PIN,      OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.begin(115200);

  Wire.begin();
  initMPU();

  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Smart Helmet");
  lcd.setCursor(0, 1);
  lcd.print("System Init...");
  delay(2000);
  lcd.clear();
}

// ---------- MAIN LOOP ----------
void loop() {
  // ----- Helmet switches -----
  int rawHelmet1 = digitalRead(HELMET_SW1_PIN);
  int rawHelmet2 = digitalRead(HELMET_SW2_PIN);

  bool sw1Pressed = (rawHelmet1 == LOW);
  bool sw2Pressed = (rawHelmet2 == LOW);

  helmetWorn = (sw1Pressed && sw2Pressed);

  // If helmet removed while countdown or alert is running, cancel everything and buzzer
  if (!helmetWorn && (fallPending || alertActive)) {
    fallPending = false;
    alertActive = false;
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("Helmet removed, cancelling fall timer/alert.");
  }

  // ----- Alcohol sensor (always active) -----
  int alcoholValue = analogRead(ALCOHOL_PIN);
  bool alcoholNow  = (alcoholValue > ALCOHOL_THRESHOLD);

  // Edge-based detection to trigger beep only when alcohol becomes detected
  if (alcoholNow && !alcoholDetected) {
    // Alcohol just detected -> beep 3 times
    beepThreeTimes();
    alcoholBeepDone = true;
  } else if (!alcoholNow) {
    // Alcohol gone -> reset flag so next detection can beep again
    alcoholBeepDone = false;
  }
  alcoholDetected = alcoholNow;

  // If alcohol detected, we will stop the bike (show Bike stopped) in display logic

  // ----- Fall detection (ONLY when helmet worn and no pending timer/alert and no alcohol) -----
  // (If you want fall detection even when alcohol present, remove "!alcoholDetected" below)
  if (helmetWorn && !fallPending && !alertActive && !alcoholDetected) {
    bool fallEvent = detectFallEvent();
    if (fallEvent) {
      fallCount++;
      Serial.print("Fall event detected! Count = ");
      Serial.println(fallCount);

      fallPending  = true;
      fallStartMs  = millis();

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Fall detected!");
      lcd.setCursor(0, 1);
      lcd.print("Press btn if OK");
      Serial.println("Fall detected, starting 30s timer.");
      delay(1000);
    }
  }

  // ----- Handle 30 s confirmation timer (only if helmet is worn) -----
  if (fallPending && helmetWorn) {
    unsigned long now = millis();
    unsigned long elapsed = now - fallStartMs;
    unsigned long remainingMs = (elapsed >= FALL_CONFIRM_MS) ? 0 : (FALL_CONFIRM_MS - elapsed);
    unsigned int remainingSec = remainingMs / 1000;

    bool safePressed = (digitalRead(SAFE_BUTTON_PIN) == LOW);

    // While button pressed during countdown -> buzzer/LED ON, else OFF
    if (safePressed) {
      digitalWrite(BUZZER_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Fall! Confirm in");
    lcd.setCursor(0, 1);
    lcd.print("T:");
    lcd.print(remainingSec);
    lcd.print("s Safe:Btn4");

    Serial.print("Fall pending, remaining = ");
    Serial.print(remainingSec);
    Serial.print(" s | SafeBtn=");
    Serial.println(safePressed ? "PRESSED" : "NOT");

    if (safePressed) {
      // User confirmed safe
      fallPending = false;
      alertActive = false;
      digitalWrite(BUZZER_PIN, LOW);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Fall cancelled");
      lcd.setCursor(0, 1);
      lcd.print("User is SAFE");
      Serial.println("Fall cancelled by user button.");
      delay(2000);
    } else if (elapsed >= FALL_CONFIRM_MS) {
      // Time over, start continuous alert (buzzer+LED ON)
      fallPending = false;
      alertActive = true;
      digitalWrite(BUZZER_PIN, HIGH);

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("No response");
      lcd.setCursor(0, 1);
      lcd.print("ALERT: CHECK!");
      Serial.println("No response, ALERT ACTIVE.");
      delay(2000);
    }

    delay(200);
    return;  // stay in fall/alert handling
  }

  // ----- If alertActive after timeout, keep buzzer/LED ON and show alert -----
  if (alertActive && helmetWorn) {
    digitalWrite(BUZZER_PIN, HIGH);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Rider unresponsive");
    lcd.setCursor(0, 1);
    lcd.print("ALERT ACTIVE   ");
    Serial.println("Alert ongoing, buzzer ON.");
    delay(500);
    return;
  }

  // Normal state, ensure buzzer/LED OFF (except during alcohol beep function)
  digitalWrite(BUZZER_PIN, LOW);

  // ----- Normal main LCD screen -----
  lcd.clear();

  if (!helmetWorn) {
    lcd.setCursor(0, 0);
    lcd.print("Please wear helm");
    lcd.setCursor(0, 1);
    lcd.print("Bike stopped    ");
  } else {
    // Helmet worn
    if (alcoholDetected) {
      // Alcohol present: bike stopped
      lcd.setCursor(0, 0);
      lcd.print("Alcohol detected");
      lcd.setCursor(0, 1);
      lcd.print("Bike stopped    ");
    } else {
      lcd.setCursor(0, 0);
      lcd.print("Bike started    ");

      lcd.setCursor(0, 1);
      if (fallCount > 0) {
        lcd.print("Falls: ");
        lcd.print(fallCount);
        lcd.print("        ");
      } else {
        lcd.print("All OK           ");
      }
    }
  }

  // ----- Serial Monitor -----
  int16_t ax, ay, az;
  readAccel(ax, ay, az);
  float gx = ax / G_PER_LSB;
  float gy = ay / G_PER_LSB;
  float gz = az / G_PER_LSB;
  float mag = sqrt(gx * gx + gy * gy + gz * gz);

  Serial.print("HelmSW1=");
  Serial.print(sw1Pressed ? "ON" : "OFF");
  Serial.print(" HelmSW2=");
  Serial.print(sw2Pressed ? "ON" : "OFF");
  Serial.print(" | Helmet: ");
  Serial.print(helmetWorn ? "WORN" : "NOT WORN");
  Serial.print(" | FallCount=");
  Serial.print(fallCount);
  Serial.print(" | AlcoholVal=");
  Serial.print(alcoholValue);
  Serial.print(" | Alcohol=");
  Serial.print(alcoholDetected ? "DETECTED" : "SAFE");
  Serial.print(" | Ax=");
  Serial.print(ax);
  Serial.print(" Ay=");
  Serial.print(ay);
  Serial.print(az);
  Serial.print(" | Mag=");
  Serial.print(mag, 2);
  Serial.println("g");

  delay(200);
}

// ---------- MPU6050 FUNCTIONS ----------
void initMPU() {
  Wire.beginTransmission(ACC_ADDR);
  Wire.write(0x6B);   // PWR_MGMT_1
  Wire.write(0);      // wake up device
  Wire.endTransmission(true);
}

void readAccel(int16_t &ax, int16_t &ay, int16_t &az) {
  Wire.beginTransmission(ACC_ADDR);
  Wire.write(0x3B);   // ACCEL_XOUT_H
  Wire.endTransmission(false);
  Wire.requestFrom(ACC_ADDR, (uint8_t)6, (uint8_t)true);

  ax = (Wire.read() << 8) | Wire.read();
  ay = (Wire.read() << 8) | Wire.read();
  az = (Wire.read() << 8) | Wire.read();
}

bool detectFallEvent() {
  int16_t ax, ay, az;
  readAccel(ax, ay, az);

  float gx = ax / G_PER_LSB;
  float gy = ay / G_PER_LSB;
  float gz = az / G_PER_LSB;
  float mag = sqrt(gx * gx + gy * gy + gz * gz);

  bool fallNow = (mag > FALL_THRESHOLD_G);

  if (fallNow && !inFall) {
    inFall = true;
    return true;
  }
  if (!fallNow && inFall) {
    inFall = false;
  }
  return false;
}

// ---------- Buzzer helper (3 beeps, 2 s period) ----------
void beepThreeTimes() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH); // buzzer + LED ON
    delay(500);                     // 0.5 s ON
    digitalWrite(BUZZER_PIN, LOW);
    if (i < 2) {
      delay(1500);                  // remaining 1.5 s OFF -> 2 s total per beep
    }
  }
}
