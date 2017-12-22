#include <Wire.h>
#include <ST7032.h>       // https://github.com/tomozh/arduino_ST7032
#include <DS3232RTC.h>    // https://github.com/JChristensen/DS3232RTC
#include <TimeLib.h>      // https://www.pjrc.com/teensy/td_libs_Time.html
#include <MD_KeySwitch.h> // https://github.com/MajicDesigns/MD_KeySwitch

#include "./timeUtil.h"

int sensorPin       = A0; // TWE-LITE humidity sensor pin
int volumePin       = A1; // volume config pin
int modeBtnPin      = 10; // mode button pin
int btn1Pin         =  9; // button1 pin
int btn2Pin         =  8; // button2 pin
int buzzerPin       = 11; // buzzer pin
int actionBtnLEDPin =  6; // action button (for stopping alert) pin
int actionBtnPin    =  7; // LED pin of action button
const unsigned int SENSOR_READ_INTERVAL = 500; // in millis

const int LIGHT_OFF_TIME = 1201; // time to check if light is off
const int LIGHT_ON_TIME  = 1301; // time to check if light is on
bool alerted = false;            // true if alert is already notified on above times

ST7032 lcd; // LCD display object

unsigned long lastSensorReadTime  = 0; // to avoid continuous reading white SENSOR_READ_INTERVAL
unsigned long lastSensorReadValue = 0; // to response for value reading function call (response last value in SENSOR_READ_INTERVAL)

// application mode
enum mode {
  normal,
  timeset,
  calibration,
  test
};
mode appMode = normal;

// timeset configuring item
enum timsetItem {
  ti_year,
  ti_month,
  ti_day,
  ti_hour,
  ti_minute,
  ti_second
};
timsetItem currentTimesetItem = ti_year; // current configuring item in timset mode
TimeElements currentTimesetTime;         // configuring time elements in timeset

time_t lastNotificationTime = 0;

// helpers for momentary switch action
MD_KeySwitch modeSwitch(modeBtnPin, LOW);
MD_KeySwitch btn1Switch(btn1Pin, LOW);
MD_KeySwitch btn2Switch(btn2Pin, LOW);
MD_KeySwitch actionSwitch(actionBtnPin, LOW);

void setup() {

  Serial.begin(9600);

  // set pin modes (except pins managed by MD_KeySwitch)
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);
  pinMode(actionBtnLEDPin, OUTPUT);
  digitalWrite(actionBtnLEDPin, LOW);

  // LCD display setup
  lcd.begin(8, 2);
  lcd.setContrast(30);

  // MD_KeySwitch setup
  modeSwitch.begin();
  modeSwitch.enableDoublePress(true); // only mode switch supports double click
  btn1Switch.begin();
  btn2Switch.begin();
  actionSwitch.begin();
}

// print event name and value to Serial
void debug(char *s, int i) {
  Serial.print(s);
  Serial.print(": ");
  Serial.println(i);
}

// get illumination value in percent.
// reading value will be done on SENSOR_READ_INTERVAL interval.
// before interval, the function will return last value.
int getIllumPercent() {
  if (timeInterval(lastSensorReadTime, millis()) <= SENSOR_READ_INTERVAL) {
    return lastSensorReadValue;
  }

  int v = pulseIn(sensorPin, HIGH, 3000);
  lastSensorReadTime = millis();
  int p = 0;
  if (v != 0) {
    p = int((1024 - v) / 1024.0 * 100);
  }
  debug("illumPercent", p);
  return lastSensorReadValue = p;
}

// get volume config value in percent.
int getVolumePercent() {
  return floor((651 - analogRead(volumePin)) / 651.0 * 100); // 651 is measured actual max (nearly equals to 1024 * (3.3v / 5v))
}


void loop() {

  // read mode button
  switch(modeSwitch.read())
  {
    case MD_KeySwitch::KS_PRESS:
      // switch app mode cyclically
      switch (appMode) {
        case normal     : appMode = calibration; break;
        case test       : appMode = normal; break;
        case calibration: appMode = test; break;
        case timeset    : chageTimesetItem(); break;
      }
      break;
    case MD_KeySwitch::KS_DPRESS:
      // switch to/from timeset mode
      if (appMode == timeset) {
        saveTimeset();
        appMode = normal;
      }
      else {
        startTimeset();
        appMode = timeset;
      }
      break;
  }

  // read button1
  // this will be work only in timset mode
  switch(btn1Switch.read())
  {
    case MD_KeySwitch::KS_PRESS:
      if (appMode == timeset) {
        decrementTimset();
      }
      break;
  }

  // read button2
  // this will be work only in timset mode
  switch(btn2Switch.read())
  {
    case MD_KeySwitch::KS_PRESS:
      if (appMode == timeset) {
        incrementTimset();
      }
      break;
  }

  // read action button
  // and stop alert
  switch(actionSwitch.read())
  {
    case MD_KeySwitch::KS_PRESS:
      clearAlert();
      break;
  }

  // clear ignoring alert (stop alert after 3min)
  if (lastNotificationTime != 0 && RTC.get() - lastNotificationTime > 180) {
    clearAlert();
  }

  // check if alert is needed to be notified
  checkAndAlert();

  // show LCD display
  switch (appMode) {
    case normal     : displayNormalMode(false); break;
    case calibration: displayCalibrationMode(); break;
    case test       : displayNormalMode(true); break;
    case timeset    : displayTimesetMode(); break;
    default: break;
  }
}

// judge if it's time to check alert.
// check alert is needed to be notified.
// then notify it.
void checkAndAlert() {
  bool needAlert = false;

  // get current time from RTC
  time_t currentTime = RTC.get();
  TimeElements currentTimeElements;
  breakTime(currentTime, currentTimeElements);
  int currentHourMinute = currentTimeElements.Hour * 100 + currentTimeElements.Minute;

  bool isLightOnTime = LIGHT_ON_TIME == currentHourMinute &&
                       (currentTimeElements.Wday != 1 && currentTimeElements.Wday != 7);
  bool isLightOffTime = LIGHT_OFF_TIME == currentHourMinute &&
                       (currentTimeElements.Wday != 1 && currentTimeElements.Wday != 7);

  if (appMode == test) {
    // check any time in test mode
    if (getVolumePercent() > getIllumPercent()) {
      needAlert = true;
    }
  }
  else if (isLightOffTime) {
    // time to check if light is off
    if (getVolumePercent() < getIllumPercent()) {
      needAlert = true;
    }
  }
  else if (isLightOnTime) {
    // time to check if light is on
    if (getVolumePercent() > getIllumPercent()) {
      needAlert = true;
    }
  }
  else {
    // reset alerted flag at non-specified time
    alerted = false;
  }

  // if not alerted, play music and turn on LED of action button
  if (needAlert && !alerted) {
    Serial.println("alert");
    alerted = true;
    digitalWrite(buzzerPin, HIGH);
    digitalWrite(actionBtnLEDPin, HIGH);

    lastNotificationTime = currentTime;
  }
}

// stop music and turn off LED of action button
void clearAlert() {
  Serial.println("clear alert");
  digitalWrite(buzzerPin, LOW);
  digitalWrite(actionBtnLEDPin, LOW);

  lastNotificationTime = 0;
}

// print percent value with padding space to the left.
// this will print just 4 characters. (like "100%", "001%")
void lcdPrintPaddingPercent(int v) {
  if (v < 100) lcd.print(" ");
  if (v < 10)  lcd.print(" ");
  lcd.print(v);
  lcd.print("%");
}

// print time value with padding 0 to the left.
// this will print just 2 characters. (like "10", "01")
void lcdPrintPaddingTime(int v) {
  if (v < 10)  lcd.print("0");
  lcd.print(v);
}

// show information of normal mode to LCD display
void displayNormalMode(bool test) {

  // first line
  lcd.setCursor(0, 0);

  // get current time fron RTC
  time_t currentTime;
  currentTime = RTC.get();
  TimeElements tm_current;
  breakTime(currentTime, tm_current);

  // print day of week
  switch(tm_current.Wday) {
    case 1: lcd.print("Su"); break;
    case 2: lcd.print("Mo"); break;
    case 3: lcd.print("Tu"); break;
    case 4: lcd.print("We"); break;
    case 5: lcd.print("Th"); break;
    case 6: lcd.print("Fr"); break;
    case 7: lcd.print("Sa"); break;
  }
  lcd.print(" ");

  // print hour and minute
  lcdPrintPaddingTime(tm_current.Hour);
  lcd.print(":");
  lcdPrintPaddingTime(tm_current.Minute);


  // second line
  lcd.setCursor(0, 1);

  // show mode character
  lcd.print(test ? "T" : "N");
  lcd.print("   ");

  // show illumination density
  lcdPrintPaddingPercent(getIllumPercent());
}

// show information of calibration mode to LCD display
void displayCalibrationMode() {

  // first line
  lcd.setCursor(0, 0);

  // show volume config value
  lcd.print("   T");
  lcdPrintPaddingPercent(getVolumePercent());


  // second line
  lcd.setCursor(0, 1);

  // show mode character
  lcd.print("C  L");

  // show illumination density
  int illumPercent = getIllumPercent();
  lcdPrintPaddingPercent(illumPercent);
}

// show information of timset mode to LCD display
void displayTimesetMode() {

  // first line
  lcd.setCursor(0, 0);

  // show current item of configuration
  switch (currentTimesetItem) {
    case ti_year  : lcd.print("    YEAR"); break;
    case ti_month : lcd.print("   MONTH"); break;
    case ti_day   : lcd.print("     DAY"); break;
    case ti_hour  : lcd.print("    HOUR"); break;
    case ti_minute: lcd.print("  MINUTE"); break;
    case ti_second: lcd.print("  SECOND"); break;
  }


  // second line
  lcd.setCursor(0, 1);

  // show mode character
  lcd.print("S   ");

  // show value of current item of configuration
  switch (currentTimesetItem) {
    case ti_year:
      lcd.print(1970 + currentTimesetTime.Year);
      break;
    case ti_month:
      lcd.print("  ");
      lcdPrintPaddingTime(currentTimesetTime.Month);
      break;
    case ti_day:
      lcd.print("  ");
      lcdPrintPaddingTime(currentTimesetTime.Day);
      break;
    case ti_hour:
      lcd.print("  ");
      lcdPrintPaddingTime(currentTimesetTime.Hour);
      break;
    case ti_minute:
      lcd.print("  ");
      lcdPrintPaddingTime(currentTimesetTime.Minute);
      break;
    case ti_second:
      lcd.print("  ");
      lcdPrintPaddingTime(currentTimesetTime.Second);
      break;
  }
}

// start to change current time
void startTimeset() {
  Serial.println("startTimeset");

  // first item is YEAR
  currentTimesetItem = ti_year;

  // keep current time to temporary time elements
  time_t currentTime;
  currentTime = RTC.get();
  breakTime(currentTime, currentTimesetTime);
}

// switch current timeset item to configure cyclically
void chageTimesetItem() {
  Serial.println("changeTimsetItem");
  switch (currentTimesetItem) {
    case ti_year  : currentTimesetItem = ti_month; break;
    case ti_month : currentTimesetItem = ti_day; break;
    case ti_day   : currentTimesetItem = ti_hour; break;
    case ti_hour  : currentTimesetItem = ti_minute; break;
    case ti_minute: currentTimesetItem = ti_second; break;
    case ti_second: currentTimesetItem = ti_year; break;
  }
}

// increment value of current timeset item
void incrementTimset() {
  Serial.println("incrementTimeset");
  switch (currentTimesetItem) {
    case ti_year:
      if (currentTimesetTime.Year < 98) currentTimesetTime.Year++;
      break;
    case ti_month:
      if (currentTimesetTime.Month < 12) currentTimesetTime.Month++;
      break;
    case ti_day:
      if (currentTimesetTime.Day < 31) currentTimesetTime.Day++;
      break;
    case ti_hour:
      if (currentTimesetTime.Hour < 23) currentTimesetTime.Hour++;
      break;
    case ti_minute:
      if (currentTimesetTime.Minute < 59) currentTimesetTime.Minute++;
      break;
    case ti_second:
      if (currentTimesetTime.Second < 59) currentTimesetTime.Second++;
      break;
  }
}

// decrement value of current timeset item
void decrementTimset() {
  Serial.println("decrementTimeset");
  switch (currentTimesetItem) {
    case ti_year:
      if (currentTimesetTime.Year > 0) currentTimesetTime.Year--;
      break;
    case ti_month:
      if (currentTimesetTime.Month > 1) currentTimesetTime.Month--;
      break;
    case ti_day:
      if (currentTimesetTime.Day > 1) currentTimesetTime.Day--;
      break;
    case ti_hour:
      if (currentTimesetTime.Hour > 0) currentTimesetTime.Hour--;
      break;
    case ti_minute:
      if (currentTimesetTime.Minute > 0) currentTimesetTime.Minute--;
      break;
    case ti_second:
      if (currentTimesetTime.Second > 0) currentTimesetTime.Second--;
      break;
  }
}

// apply RTC time from temporary time elements
void saveTimeset() {
  Serial.println("saveTimeset");
  setTime(currentTimesetTime.Hour, currentTimesetTime.Minute, currentTimesetTime.Second,
          currentTimesetTime.Day, currentTimesetTime.Month, currentTimesetTime.Year + 1970);
  RTC.set(now());
}
