#include <SparkFun_LIS331.h>
// #include <Watchdog.h>
#include <Wire.h>
#include "BluetoothSerial.h"

//hardware
LIS331 accel1;
LIS331 accel2;
BluetoothSerial bt;
// Watchdog watchdog; //Watchdog is disabled until i fix it lol.

//Drive pins
int pinDrive = 4;

//LED pins
int pinLED = 5;

// Misc
float accelDist = 4.5;
int accel1Orientation = 1;  //orientation of accelerometer1: x=0 y=1 z=2
int accel2Orientation = 1;  //orientation of accelerometer2: x=0 y=1 z=2
bool accelSame = true;      //whether one accel is flipped
long guideLEDwidth = 300;   //width of the guide
long LEDoffset = -900;      //direction of LED relative to "Forwards" in degrees*10
long driveWidth = 450;      //valid times to go in direction
int ticksPerCheck = 5;
int ticksPerAccelCalc = 10;
int ticksPerSend = 500;
int timeout = 3000;               //milliseconds since last heartbeat to be declared dead
const int runningAvgLen = 90;     //how many datapoints to use for the running average
int driveTimeout = 1000;          //how long it drives in a direction with timedmove
int accelReadZone = 450;          // how large of an area after drive width that the accelerometers are allowed to be read in
int forceAccelReadTime = 500000;  //roughly the time it takes to read from the accelerometers in microseconds
int driveControlFreq = 8000;      //Oneshot125 is 8000hz


//VARIABLES
float rpm;
float meltyTick;     //number of microseconds per revolution
long direction = 0;  //direction that the bot is pointing in melty mode, degrees * 10
long lastTick = 0;   //last time time was calculated
long ticks = 0;      //loop counter
bool melting = 0;    //if in spinny mode
long heartbeat = 0;  //time of last heartbeat
long meltyStart;     //time of engaging meltymode
int driveTime = 0;   //last time timedmove was issued
TaskHandle_t accelLoop;


char currentCommand;
int driveAngle = 0;   //out of 360
bool driveSpeed = 0;  //0 to 1
int tempdriveSpeed = 0;
int tempdriveAngle = 0;
long leftMotor = 0;
long rightMotor = 0;


//accel outputs
int16_t a1x;
int16_t a2x;
int16_t a1y;
int16_t a2y;
int16_t a1z;
int16_t a2z;
int16_t a1val;
int16_t a2val;

//accel running avg
float a1Running = 0;
float a2Running = 0;
int a1ArrPos = 0;
int a2ArrPos = 0;
int avgArrPos = 0;
float a1Arr[runningAvgLen];
float a2Arr[runningAvgLen];
float avgArr[runningAvgLen];
long lastAccelRead = 0;

void setup() {
  Serial.begin(115200);
  bt.begin();
  Wire.begin();
  Serial.println(F("Hello!"));

  // accel initialize
  accel1.setI2CAddr(0x18);
  accel2.setI2CAddr(0x19);
  accel1.begin(LIS331::USE_I2C);
  accel2.begin(LIS331::USE_I2C);
  accel1.setFullScale(LIS331::HIGH_RANGE);
  accel2.setFullScale(LIS331::HIGH_RANGE);
  accel1.setODR(LIS331::DR_1000HZ);
  accel2.setODR(LIS331::DR_1000HZ);

  // motor initialize
  ledcSetup(0, 8000, 8);
  ledcAttachPin(pinDrive, 0);
  ledcWrite(0, 0);

  // LED initialize
  pinMode(pinLED, OUTPUT);
  digitalWrite(pinLED, HIGH);
  delay(500);
  digitalWrite(pinLED, LOW);
  delay(500);
  digitalWrite(pinLED, HIGH);
  delay(500);
  digitalWrite(pinLED, LOW);
  char str[20];
  int i = 0;
  float a1Total = 0;
  float a2Total = 0;
  while (true) {
    readAccel();
    a1Total += accel1.convertToG(400, a1val);
    a2Total += accel1.convertToG(400, a2val);
    if (i % 100 == 0) {
      btwrite("*Z0*");     //RPM
      btwrite("*Xdist:");  //Accel dist
      dtostrf(accelDist, 1, 2, str);
      btwrite(str);
      btwrite("*");
      btwrite("*V-69*");  //TPS

      dtostrf(a1Running, 1, 2, str);  //gforce of a1
      btwrite("*BGs a1:");
      btwrite(str);
      dtostrf(a2Running, 1, 2, str);
      btwrite(", a2:");
      btwrite(str);
      btwrite("*");

      Serial.print("a1AVG: ");
      Serial.print(a1Running);
      Serial.print(" a2AVG: ");
      Serial.println(a2Running);

      Serial.print("X1:");
      Serial.print(accel1.convertToG(400, a1x));
      Serial.print(" Y1:");
      Serial.print(accel1.convertToG(400, a1y));
      Serial.print(" Z1:");
      Serial.print(accel1.convertToG(400, a1z));
      Serial.print("\n");

      Serial.print("X2:");
      Serial.print(accel2.convertToG(400, a2x));
      Serial.print(" Y2:");
      Serial.print(accel2.convertToG(400, a2y));
      Serial.print(" Z2:");
      Serial.print(accel2.convertToG(400, a2z));
      Serial.print("\n");
      Serial.println("-----------------------------------------");
    }
    i++;
    delay(1);
    if (i == 1000) {
      digitalWrite(pinLED, HIGH);
      a1Running = a1Running - (a1Total / i);
      a2Running = a2Running - (a2Total / i);
    } else if (i > 1000) {
      if ((char)bt.read() == '?') { break; }
    }
  }
  ledcWrite(0, 127);
  delay(1000);
  ledcWrite(0, 159);
  delay(100);
  ledcWrite(0, 127);
  delay(500);
  xTaskCreatePinnedToCore(accelLoopCode,"accelLoop",10000,NULL,1,&accelLoop, 0);

  // watchdog.enable(Watchdog::TIMEOUT_2S);
}

void loop() {
  while (true) {  //esp32 loop() doesnt run at full speed?????
    // watchdog.reset();
    if (heartbeat + timeout < millis()) {

      digitalWrite(pinLED, HIGH);
      driveSpeed = 0;
      driveAngle = 0;
      freeze();
      if (millis() % 1000 == 0) {
        Serial.print((millis() - heartbeat) / 1000);
        Serial.println(F(" since last beat"));
        delay(1);
      }
      ledcWrite(0, 0);
      checkCommands();

    } else {
      driveCheck();
      if (melting) {
        calc();
        meltyLED();
        melty();
        ticks++;

        //WE MULTITHREADED NOW
        // if (((direction - (driveWidth / 2) + (driveAngle * 10 * driveSpeed)) % 1800 < accelReadZone) or micros() > lastAccelRead + forceAccelReadTime) {
        //   readAccel();
        //   lastAccelRead = micros();
        // }

        if (ticks % ticksPerSend == 0) {
          send();
        }
        if (ticks % ticksPerCheck == 0) {
          checkCommands();
        }
      } else {
        checkCommands();
        // if (!driveSpeed) { readAccel(); }
        calc();
        send();
      }
    }
  }
}
void accelOrientation() {
  if (accel1Orientation = 0) {
    a1val = a1x;
  } else if (accel1Orientation = 1) {
    a1val = a1y;
  } else {  //it may seem like im picking the z axis as my favorite. Yes. Yes I am.
    a1val = a1z;
  }
  if (accel2Orientation = 0) {
    a2val = a2x;
  } else if (accel2Orientation = 1) {
    a2val = a2y;
  } else {  //it may seem like im picking the z axis as my favorite. Yes. Yes I am.
    a2val = a2z;
  }
}

void accelLoopCode(void* pvParameters) {
  while (true) {
    readAccel();
  }
}
//Reads accelerometers and calculates RPM and ticktime
void readAccel() {
  accel1.readAxes(a1x, a1y, a1z);
  accel2.readAxes(a2x, a2y, a2z);
  accelOrientation();

  //progress running arrays arrays
  a1Running = a1Running + (accel1.convertToG(400, a1val) / runningAvgLen) - a1Arr[a1ArrPos];
  a1Arr[a1ArrPos] = accel1.convertToG(400, a1val) / runningAvgLen;
  a1ArrPos = (a1ArrPos + 1) % runningAvgLen;
  a2Running = a2Running + (accel2.convertToG(400, a2val) / runningAvgLen) - a2Arr[a2ArrPos];
  a2Arr[a2ArrPos] = accel2.convertToG(400, a2val) / runningAvgLen;
  a2ArrPos = (a2ArrPos + 1) % runningAvgLen;

  //calculate RPM and add to array
  if (accelSame) {  //if they are in the same direction, they subtract from each other
    avgArr[avgArrPos] = sqrt(abs(a1Running - a2Running) / (accelDist * 0.00001118));
  } else {  //double negatives ig
    avgArr[avgArrPos] = sqrt(abs(a1Running + a2Running) / (accelDist * 0.00001118));
  }


  rpm = avgArr[avgArrPos] * 1.5 - (avgArr[(avgArrPos + 1) % runningAvgLen] / 2);
  avgArrPos = (avgArrPos + 1) % runningAvgLen;
  meltyTick = 1000000 / abs(rpm / 60);
}


//calculates direction pointing (degrees*10)
void calc() {
  long newTime = micros();
  direction = long(direction + 3600 * ((newTime - lastTick) / meltyTick)) % 3600;
  lastTick = newTime;
}

void meltyLED() {
  //Guide LED
  if ((direction + LEDoffset + (guideLEDwidth / 2)) % 3600 < guideLEDwidth) {
    digitalWrite(pinLED, HIGH);
  } else {
    digitalWrite(pinLED, LOW);
  }
}

void melty() {

  if (driveSpeed > 0) {
    //LeftDrive
    if ((direction + (driveAngle * 10) + (driveWidth / 2)) % 3600 < driveWidth) {
      ledcWrite(0, 127);
    } else {
      ledcWrite(0, 159);
    }
    // // RightDrive
    // if ((direction + (driveAngle * 10) - 1800 + (driveWidth / 2)) % 3600 < driveWidth) {
    //   ledcWrite(1, 127);
    // } else {
    //   ledcWrite(1, 95);
    // }
  } else {
    ledcWrite(0, 159);
    // ledcWrite(1, 95);
  }
}

void melt() {
  melting = 1;
  ticks = 1;
  meltyStart = millis();
  Serial.println("MELTY TIME");
  delay(2);
}
void freeze() {
  melting = 0;
  ledcWrite(0, 127);
  digitalWrite(pinLED, HIGH);
}

void checkCommands() {
  while (bt.available() > 0) {
    char nextChar = (char)bt.read();
    Serial.print(nextChar);
    if (nextChar == '?') {
      heartbeat = millis();
    } else if (nextChar == 'R') {
      tempdriveSpeed = 0;
      currentCommand = 'R';
    } else if (nextChar == 'A') {
      tempdriveAngle = 0;
      currentCommand = 'A';
    } else if (nextChar == 'M') {
      if (currentCommand == 'M') { melt(); }
      currentCommand = 'M';
    } else if (nextChar == 'm') {
      if (currentCommand == 'm') { freeze(); }
      currentCommand = 'm';
    } else if (nextChar == 'U') {
      driveTime = millis();
      driveAngle = 0;
      driveSpeed = 1;
    } else if (nextChar == 'I') {
      driveTime = millis();
      driveAngle = 45;
      driveSpeed = 1;
    } else if (nextChar == 'O') {
      driveTime = millis();
      driveAngle = 90;
      driveSpeed = 1;
    } else if (nextChar == 'P') {
      driveTime = millis();
      driveAngle = 135;
      driveSpeed = 1;
    } else if (nextChar == 'H') {
      driveTime = millis();
      driveAngle = 180;
      driveSpeed = 1;
    } else if (nextChar == 'J') {
      driveTime = millis();
      driveAngle = 225;
      driveSpeed = 1;
    } else if (nextChar == 'K') {
      driveTime = millis();
      driveAngle = 270;
      driveSpeed = 1;
    } else if (nextChar == 'L') {
      driveTime = millis();
      driveAngle = 315;
      driveSpeed = 1;
    } else if (nextChar == '!') {
      set();
    } else {
      if (nextChar < 48 || nextChar > 57) { nextChar = 48; }
      switch (currentCommand) {
        case 'R':
          tempdriveSpeed = tempdriveSpeed * 10 + nextChar - 48;
          break;
        case 'A':
          tempdriveAngle = tempdriveAngle * 10 + nextChar - 48;
          break;
      }
    }
  }
}
void driveCheck() {
  if (millis() > driveTime + driveTimeout) { driveSpeed = 0; }
}
void set() {
  if (tempdriveAngle > 359 || tempdriveAngle < 0) {
    return;
  }
  driveSpeed = tempdriveSpeed > 0;
  driveAngle = tempdriveAngle;
  if (!melting) {
    //Left
    if (driveAngle < 90) {
      leftMotor = 255;
    } else if (driveAngle < 180) {
      leftMotor = 255 - (510 * (driveAngle - 90) / 90);
    } else if (driveAngle < 270) {
      leftMotor = -255;
    } else if (driveAngle < 360) {
      leftMotor = 510 * (driveAngle - 270) / 90 - 255;
    } else if (driveAngle == 360) {
      leftMotor = 255;
    } else {
      leftMotor = 0;
    }
    //Right
    if (driveAngle < 90) {
      rightMotor = 255 - (510 * driveAngle / 90);
    } else if (driveAngle < 180) {
      rightMotor = -255;
    } else if (driveAngle < 270) {
      rightMotor = 510 * (driveAngle - 180) / 90 - 255;
    } else if (driveAngle < 360) {
      rightMotor = 255;
    } else if (driveAngle == 360) {
      rightMotor = 255;
    } else {
      rightMotor = 0;
    }
    leftMotor = leftMotor * driveSpeed;
    rightMotor = rightMotor * driveSpeed;

    if (leftMotor > 0) {  //BACKWARDS
      ledcWrite(0, 95);
    } else if (leftMotor < 0) {  //FORWARDS
      ledcWrite(0, 159);
    } else {
      ledcWrite(0, 127);
    }
    // if (rightMotor > 0) {  //BACKWARDS
    //   digitalWrite(pinRF, LOW);
    //   digitalWrite(pinRR, HIGH);
    // } else if (rightMotor < 0) {  //FORWARDS
    //   digitalWrite(pinRR, LOW);
    //   digitalWrite(pinRF, HIGH);
    // } else {
    //   digitalWrite(pinRF, LOW);
    //   digitalWrite(pinRR, LOW);
    // }
  }
}
void send() {
  char str[20];

  // btwrite("*VGyro:");
  // dtostrf(g1Xrunning, 1, 0, str);
  // btwrite(str);
  // btwrite("*");

  // btwrite("*XCalc1:");
  // dtostrf(rpmBig, 1, 0, str);
  // btwrite(str);
  // btwrite("*");
  if (melting == 1) {
    btwrite("*VTPS:");
    dtostrf((ticks * 1000 / (millis() - meltyStart)), 1, 0, str);
    btwrite(str);
    btwrite("*");
    // delay(2);
  }


  btwrite("*ZRPM");
  dtostrf(60000000 / meltyTick, 1, 0, str);
  btwrite(str);
  btwrite("*");
  // delay(2);
  // btwrite("*Brpm accel:");
  // dtostrf(rpmBig, 1, 0, str);
  // btwrite(str);
  // btwrite("*");
}
void btwrite(String str) {
  uint8_t buf[str.length()];
  memcpy(buf, str.c_str(), str.length());
  bt.write(buf, str.length());
}