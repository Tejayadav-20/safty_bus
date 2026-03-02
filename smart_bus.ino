#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);  // UART1
HardwareSerial gsmSerial(2);  // UART2

String phoneNumber = "+919392379850";
#define ENGINE_PIN 4
#define BUZZER 23
#define HAMMER_RELAY 25
#define CO2_RELAY 33
#define SERVO_PIN 13

#define ALCOHOL_PIN 34
#define SMOKE_PIN 35
#define FLAME_PIN 12
#define RAIN_PIN 14
#define POT_SPEED 32

#define TRIG1 5
#define ECHO1 18

#define IR_OUTSIDE 27
#define IR_INSIDE 26

LiquidCrystal_I2C lcd(0x26, 16, 2);
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified();
Servo guardServo;

int passengerCount = 0;
float busSpeed = 0;
int enginePWM = 0;
int distance = 0;
int accelPWM;
int alcoholThreshold = 600;
int smokeThreshold = 600;

bool emergencyLock = false;

int engineSpeed = 255;


void setup() {

  Serial.begin(115200);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();

  accel.begin();

  pinMode(ENGINE_PIN, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(HAMMER_RELAY, OUTPUT);
  pinMode(CO2_RELAY, OUTPUT);

  pinMode(FLAME_PIN, INPUT);
  pinMode(RAIN_PIN, INPUT);
  pinMode(IR_OUTSIDE, INPUT);
  pinMode(IR_INSIDE, INPUT);

  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);
  // GPS
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);

  // GSM
  gsmSerial.begin(9600, SERIAL_8N1, 2, 15);

  delay(3000);

  gsmSerial.println("AT");
  delay(1000);
  gsmSerial.println("AT+CMGF=1");
  delay(1000);

  guardServo.attach(SERVO_PIN);

  analogWrite(ENGINE_PIN, 255);

  lcd.setCursor(0, 0);
  lcd.print("SYSTEM STARTED");
  Serial.println("SYSTEM STARTED");

  delay(2000);
  lcd.clear();
}


void loop() {

  if (emergencyLock) {
    analogWrite(ENGINE_PIN, 0);
    digitalWrite(BUZZER, HIGH);
    return;
  }

  updateSpeedFromPot();
  passengerCounter();
  checkAlcohol();
  checkAccident();
  checkFireAndCO2();
  checkUltrasonic();
  displayStatus();

  delay(200);
}

// ...................SPEED.............
void updateSpeedFromPot() {

  int val = analogRead(POT_SPEED);
  busSpeed = map(val, 0, 4095, 0, 120);

  Serial.print("Speed: ");
  Serial.println(busSpeed);

  analogWrite(ENGINE_PIN, busSpeed);
}
// ---------------- ULTRASONIC ----------------
void checkUltrasonic() {

  digitalWrite(TRIG1, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG1, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG1, LOW);

  long duration = pulseIn(ECHO1, HIGH, 30000);
  distance = duration * 0.034 / 2;

  Serial.print("Ultrasonic Distance: ");
  Serial.println(distance);

  if ((busSpeed < 45 && distance < 20) || (busSpeed > 45 && distance < 50)) {
    guardServo.write(90);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Speed:");
    lcd.print(busSpeed);
    lcd.setCursor(0, 1);
    lcd.print("GUARD ACTIVATED ");

  } else {
    guardServo.write(0);
  }
}
// ---------------- DURING RAIN ----------------
void updateEngineFromAccelerator() {

  int rainValue = analogRead(RAIN_PIN);
  int rainLevel = map(rainValue, 0, 4095, 0, 100);

  int speedLimit = busSpeed;

  if (rainLevel > 70) speedLimit = 70;
  else if (rainLevel >= 40 && rainLevel <= 50) speedLimit = 150;

  if (accelPWM > speedLimit)
    enginePWM = speedLimit;
  else
    enginePWM = accelPWM;

  analogWrite(ENGINE_PIN, enginePWM);
}

//....................... PASSENGER COUNT....................
void passengerCounter() {

  static int state = 0;

  if (state == 0 && digitalRead(IR_OUTSIDE) == LOW)
    state = 1;

  else if (state == 0 && digitalRead(IR_INSIDE) == LOW)
    state = 2;

  if (state == 1 && digitalRead(IR_INSIDE) == LOW) {
    passengerCount++;
    Serial.println("Passenger Entered");
    state = 0;
    delay(300);
  }

  if (state == 2 && digitalRead(IR_OUTSIDE) == LOW) {
    if (passengerCount > 0)
      passengerCount--;
    Serial.println("Passenger Exited");
    state = 0;
    delay(300);
  }

  Serial.print("Passenger Count: ");
  Serial.println(passengerCount);

  if (passengerCount > 10)
    triggerEmergency("OVER PASSENGER");
}

//......................... ALCOHOL......................
void checkAlcohol() {

  int alcoholValue = analogRead(ALCOHOL_PIN);

  Serial.print("Alcohol Value: ");
  Serial.println(alcoholValue);

  if (alcoholValue > alcoholThreshold) {
    triggerEmergency("DRUNK DRIVER");
  }
}

// ................ACCIDENT........................
void checkAccident() {

  sensors_event_t event;
  accel.getEvent(&event);

  float totalForce = sqrt(
    event.acceleration.x * event.acceleration.x + event.acceleration.y * event.acceleration.y + event.acceleration.z * event.acceleration.z);

  Serial.print("Acceleration: ");
  Serial.println(totalForce);

  if (totalForce > 12)
    triggerEmergency("ACCIDENT!");
  else
    digitalWrite(BUZZER, LOW);
}

//................ FIRE + SMOKE..............
void checkFireAndCO2() {

  int smokeValue = analogRead(SMOKE_PIN);
  bool flameDetected = (digitalRead(FLAME_PIN) == HIGH);

  Serial.print("Smoke: ");
  Serial.println(smokeValue);

  if (smokeValue > smokeThreshold || flameDetected) {

    digitalWrite(CO2_RELAY, HIGH);
    digitalWrite(HAMMER_RELAY, HIGH);

    triggerEmergency("FIRE DETECTED");
  } else {
    digitalWrite(CO2_RELAY, LOW);
    digitalWrite(HAMMER_RELAY, LOW);
    digitalWrite(BUZZER, LOW);
  }
}

// ..............MASTER EMERGENCY FUNCTION.........
void triggerEmergency(String message) {

  if (emergencyLock) return;

  Serial.println("***** EMERGENCY ACTIVATED *****");
  Serial.println(message);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(message);

  slowEngineStop();

  sendSMS(message);

  emergencyLock = true;
}
// ...................GPS.............
void updateGPS() {
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }
}
void sendSMS(String alertMessage) {

  updateGPS();

  float latitude = gps.location.lat();
  float longitude = gps.location.lng();

  String message = "BUS ALERT!\n";
  message += alertMessage;
  message += "https://maps.google.com/?q=";
  message += String(gps.location.lat(), 6);
  message += ",";
  message += String(gps.location.lng(), 6);
 gsmSerial.println("AT+CMGF=1");
  delay(1000);
  Serial.println("Sending SMS...");

  gsmSerial.print("AT+CMGS=\"");
  gsmSerial.print(phoneNumber);
  gsmSerial.println("\"");
  delay(1000);

  gsmSerial.print(message);
  delay(500);

  gsmSerial.write(26);
  delay(5000);

  Serial.println("SMS Sent!");
}

// ...............ENGINE SLOW STOP............
void slowEngineStop() {

  Serial.println("Engine Slowing Down...");

  for (int i = busSpeed; i >= 0; i--) {
    analogWrite(ENGINE_PIN, i);
    delay(10);
  }

  engineSpeed = 0;
  digitalWrite(BUZZER, HIGH);
  delay(300);
  digitalWrite(BUZZER, LOW);
  delay(300);
  Serial.println("Engine Stopped.");
}
// ....................LCD DISPLAY..........
void displayStatus() {

  lcd.setCursor(0, 0);
  lcd.print("Speed:");
  lcd.print(busSpeed);
  lcd.print("   ");

  lcd.setCursor(0, 1);
  lcd.print("P:");
  lcd.print(passengerCount);
  lcd.print("   ");
}
