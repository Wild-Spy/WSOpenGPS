#include <SPIFlash.h>
#include <TinyGPS.h>
#include <stdarg.h>
#include <RH_RF95.h>
#include <LowPower.h>
#include <RTCZero.h>

// CONFIG
#define LOGGERID  1 // set this value from 0 to 65535
#define FIX_MAX_TIME_S 90 //Max fix time in seconds, it will give up after this much time
#define FIX_PERIOD_H_M_S  0, 10, 0 //Fix period (specify as "hours, minutes, seconds" need all three and the two commas!)
// END CONFIG

#define LED 13
#define Flash_CS 6
#define RFM95_CS 8
#define RFM95_RST 4
#define RFM95_INT 3

#define RF95_FREQ 434.0

RH_RF95 rf95(RFM95_CS, RFM95_INT);
TinyGPS gps;
SPIFlash flash(Flash_CS, &SPI);
RTCZero rtc;

// 100 = 1.0 HDOP, 200 = 2.0 HDOP
#define MIN_HDOP_FOR_FIX 2000

#define gpsSerial Serial1

float flat, flon;
int year;
byte month, day, hour, minute, second;
unsigned long chars;
unsigned short sentences, failed_checksum;

long recordCount = -1;

enum dropoffStatus_t {
  dropoffStatus_normal,
  dropoffStatus_dropped
};

struct fix_t {
  int year;
  byte month;
  byte day;
  byte hour;
  byte minute;
  byte second;
  float flat;
  float flon;
  uint16_t HDOP;
};

struct radiotx_t {
  uint16_t loggerId;
  fix_t fix;
  enum dropoffStatus_t dropoffStatus;
};

fix_t currentFix;

void p (char *fmt, ... ) {
  char buf[128]; // resulting string limited to 128 chars
  va_list args;
  va_start (args, fmt );
  vsnprintf(buf, 128, fmt, args);
  va_end (args);
  Serial.print(buf);
}

void pinStr( uint32_t ulPin, unsigned strength) // works like pinMode(), but to set drive strength
{
  // Handle the case the pin isn't usable as PIO
  if ( g_APinDescription[ulPin].ulPinType == PIO_NOT_A_PIN )
  {
    Serial.print("Not a pin\n");
    return ;
  }
  if (strength) strength = 1;      // set drive strength to either 0 or 1 copied
  PORT->Group[g_APinDescription[ulPin].ulPort].PINCFG[g_APinDescription[ulPin].ulPin].bit.DRVSTR = strength ;
}

void gpsOn () {
  pinMode(15, OUTPUT);
  pinMode(16, OUTPUT);
  pinMode(17, OUTPUT);
  pinMode(18, OUTPUT);
  pinMode(19, OUTPUT);
  pinStr(15, 1);
  pinStr(16, 1);
  pinStr(17, 1);
  pinStr(18, 1);
  pinStr(19, 1);
  digitalWrite(15, HIGH);
  digitalWrite(16, HIGH);
  digitalWrite(17, HIGH);
  digitalWrite(18, HIGH);
  digitalWrite(19, HIGH);
  gpsSerial.begin(9600);
}

void gpsOff () {
  gpsSerial.end();
  // ...
  digitalWrite(15, LOW);
  digitalWrite(16, LOW);
  digitalWrite(17, LOW);
  digitalWrite(18, LOW);
  digitalWrite(19, LOW);
  pinMode(15, INPUT);
  pinMode(16, INPUT);
  pinMode(17, INPUT);
  pinMode(18, INPUT);
  pinMode(19, INPUT);
  //pinMode(0, INPUT);
  //pinMode(1, OUTPUT);
}

/**
 * Load the record count from flash into recordCount variable.
 * If the record count is invalid (the chip has just been erased)
 * then this function will set the record count on the chip to
 * zero and setup the recordCount variable to reflect that.
 */
void loadRecordCountFromFlash () {
  recordCount = readRecordCountFromFlash();
  if (recordCount == -1) {
    writeRecordCountToFlash(0);
    recordCount = 0;
  }
}

/**
 * Reads the number of fixes from the flash.
 * Will be -1 if the chip is brand new (0xFFFFFFFF == -1)
 */
long readRecordCountFromFlash () {
  return flash.readLong(0);
}

/**
 * Writes the number of fixes to the Flash
 */
bool writeRecordCountToFlash (long recordCount) {
  flash.eraseSector(0);
  return flash.writeLong(0, recordCount);
}

/**
 * Completely erases the entire flash chip.
 */
void eraseFlash () {
  flash.eraseChip();
  loadRecordCountFromFlash();
}

/**
 * Converts a fixAddress into a flash address.
 * @param fixAddress  uint32_t  The index of the fix.  For example 0 is the first fix,
 *                              1 is the second fix, etc.
 */
uint32_t fixAddressToFlashAddress(uint32_t fixAddress) {
  return 4096 + fixAddress * sizeof(fix_t);
}

/**
 * Reads a fix from the flash.
 * @param fixAddress  uint32_t  The index of the fix.  For example 0 is the first fix,
 *                              1 is the second fix, etc.
 * @param fix         fix_t&    The location to put the data that is read from the flash chip.
 */
void readFixFromFlash (uint32_t fixAddress, fix_t& fix) {
  uint32_t flashAddr = fixAddressToFlashAddress(fixAddress);
  flash.readAnything(flashAddr, fix);
}

/**
 * Writes a fix to the flash memory.
 * @param fixAddress  uint32_t  The fix location to write to.  For example 0 is the first fix,
 *                              1 is the second fix, etc.
 * @param fix         fix_t&    The data to write to the flash chip.
 */
bool writeFixToFlash (uint32_t fixAddress, fix_t& fix) {
  uint32_t flashAddr = fixAddressToFlashAddress(fixAddress);
  return flash.writeAnything(flashAddr, fix);
}

bool writeNewFixToFlash (fix_t& fix) {
  if (writeFixToFlash(recordCount, fix) == false) return false;
  recordCount++;
  return writeRecordCountToFlash(recordCount);
}

void printFix (fix_t& fix) {
  p("Time: %02d-%02d-%04d at %02d:%02d:%02d\n", fix.day, fix.month, fix.year, fix.hour, fix.minute, fix.second);
  Serial.print("Location: ");
  Serial.print(fix.flat, 6);
  Serial.print(", ");
  Serial.print(fix.flon, 6);
  Serial.print("\nHDOP: ");
  Serial.print(fix.HDOP/100.0);
  Serial.print("\n");
}

void makeFakeFix(fix_t& fix) {
  fix.year = 2018;
  fix.month = 4;
  fix.day = 27;
  fix.hour = 13;
  fix.minute = 42;
  fix.second = 10;
  fix.flat = 123.456;
  fix.flon = -50.11111;
  fix.HDOP = 12345;
}

void setRTCFromFix (fix_t& fix) {
  rtc.setTime(fix.hour, fix.minute, fix.second);
  rtc.setDate(fix.day, fix.month, fix.year);
}

void showMenu () {
  const char menu[] = "\nArdui RI GPS V0.0.1\n"
  " 1. Print number of Fixes Stored in Flash\n"
  " 2. Print Flash storage remaining\n"
  " 3. Print fix from index\n"
  " 4. Save a new fake fix\n"
  " 5. Print all fixes\n"
  " g. Turn GPS on\n"
  " s. Turn GPS off\n"
  " ec. Erase Flash\n"
  " x. Exit menu\n"
  "> ";

  int incomingByte = 0;
  char tmpStr[15];
  int charsRead = 0;

  Serial.print(menu);

  while (1) {
    if (Serial.available() > 0) {
      incomingByte = Serial.read();
      Serial.write(incomingByte);
      Serial.print("\n");
      switch (incomingByte) {
        case '1':
          p("Fixes: %lu\n", recordCount);
          break;
        case '2':
          p("not implemented");
          break;
        case '3':
        {
          bool done = false;
          charsRead = 0;
          while (done == false) {
            if (Serial.available() > 0) {
              incomingByte = Serial.read();
              if (incomingByte >= '0' && incomingByte <= '9') {
                tmpStr[charsRead++] = incomingByte;
              } else {
                tmpStr[charsRead++] = '\0';
                done = true;
              }
            }
          }
          while (Serial.available() > 0) Serial.read();  //Flush buffer
          int n = atoi(tmpStr);
          if (n >= recordCount || n < 0) {
            p("There is no fix at index %d.\n", n);
            break;
          }
          p("Reading fix at index %d...\n", n);
          readFixFromFlash(n, currentFix);
          printFix(currentFix);
          break;
        }
          break;
        case '4':
          makeFakeFix(currentFix);
          writeNewFixToFlash(currentFix);
          break;
        case '5':
        {
          for (uint32_t ii = 0; ii < recordCount; ii++) {
            readFixFromFlash(ii, currentFix);
            printFix(currentFix);
          }
          break;
        }
          break;
        case 'g':
          gpsOn();
          break;
        case 's':
          gpsOff();
          break;  
        case 'x':
          return;
        case 'e':
          if (Serial.read() == 'c') {
            eraseFlash();
          }
          break;
        default:
          Serial.print("Invalid selection.\n\n");
          break;
      }
      Serial.print(menu);
    }
  }
}

void setupFlash () {
  // Have to turn the RF module CS OFF!
//  pinMode(RFM95_CS, OUTPUT);
//  digitalWrite(RFM95_CS, HIGH);

  SPI.begin();
  flash.begin();
//  uint32_t jedecid = flash.getCapacity();
//  Serial.print("JEDEC ID: ");
//  Serial.print((uint32_t)jedecid);
//  Serial.print("\n");

  loadRecordCountFromFlash();
  
  Serial.print("Record Count: ");
  Serial.print(recordCount);
  Serial.print("\n");

  showMenu();
  //printFix(currentFix);
}

void gpsGetFix (fix_t& fix) {
  gps.f_get_position(&fix.flat, &fix.flon);
  gps.crack_datetime(&fix.year, &fix.month, &fix.day, &fix.hour, &fix.minute, &fix.second);
  fix.HDOP = gps.hdop();
}

void setupRadio () {
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH);

  // manual reset
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);

  while (!rf95.init()) {
    Serial.println("LoRa radio init failed");
    while (1);
  }
  Serial.println("LoRa radio init OK!");

   // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM
  if (!rf95.setFrequency(RF95_FREQ)) {
    Serial.println("setFrequency failed");
    while (1);
  }
  Serial.print("Set Freq to: "); Serial.println(RF95_FREQ);
  
  // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on
 
  // The default transmitter power is 13dBm, using PA_BOOST.
  // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then 
  // you can set transmitter powers from 5 to 23 dBm:
  rf95.setTxPower(23, false);
}

void radioTxRaw (void* data, uint8_t len) {
  rf95.send((uint8_t *)data, len);
  rf95.waitPacketSent();
}

bool radioRxRaw (uint16_t timeoutMs, uint8_t* data, uint8_t* len, int8_t* rssi) {
  if (!rf95.waitAvailableTimeout(timeoutMs)) return false;
  if (!rf95.recv(data, len)) return false;
  *rssi = rf95.lastRssi();
  return true;
}

void radioTxFix (fix_t& fix) {
  radiotx_t rtx;
  rtx.loggerId = LOGGERID;
  rtx.fix = fix;
  rtx.dropoffStatus = dropoffStatus_normal;
  radioTxRaw(&rtx, sizeof(rtx));
}

void alarmMatch () {
  //rtc.disableAlarm();
  //rtc.detachInterrupt();
}

void resetRtcTime () {
  rtc.setTime(0, 0, 0);
  rtc.setDate(1, 1, 2000);
}

void setupRtc () {
  rtc.begin();
  rtc.setY2kEpoch(0); // Set to 1/1/2000 00:00:00 default time.  All other times expressed in 
}

bool takeFix () {
  //Serial.print("Taking a fix...\n");
  uint32_t abortEpoch = rtc.getEpoch() + FIX_MAX_TIME_S;

  while (rtc.getEpoch() < abortEpoch) {
    if (!gpsSerial.available()) continue;
    int c = gpsSerial.read();
    //Serial.write(c);
    if (!(gps.encode(c) && gps.hdop() < MIN_HDOP_FOR_FIX)) continue;
    gpsGetFix(currentFix);
    writeNewFixToFlash(currentFix);  // Save the new fix to flash.
    setRTCFromFix(currentFix);       // Update RTC time from fix.
    radioTxFix(currentFix);          // Transmit the fix over the radio.
    radioTxFix(currentFix);          // Transmit the fix over the radio.
    radioTxFix(currentFix);          // Transmit the fix over the radio.
    //p("Got Fix in %u seconds!\n", rtc.getEpoch() - abortEpoch - FIX_MAX_TIME_S);
    //printFix(currentFix);
    //Serial.print("\n");
    return true;
  }
  //Serial.print("Timed out taking fix!\n");
  return false;
}

void blinkLed () {
  digitalWrite(LED, HIGH);   // turn the LED on 
  delay(100);              
  digitalWrite(LED, LOW);    // turn the LED off
  delay(100);
  digitalWrite(LED, HIGH);   // turn the LED on 
  delay(100);              
  digitalWrite(LED, LOW);    // turn the LED off
}

void setup () {
  while (!Serial) {
    delay(1);
  }
  //gpsOn();
  Serial.print("Init\n");

  setupRtc();
  setupRadio();
  setupFlash();
  gpsOn();
  Serial.print("Starting.  USB serial will now be disconnected.");
  Serial.end();
  USBDevice.detach();
}

void sleepFor (uint8_t hours, uint8_t minutes, uint8_t seconds) {
  uint32_t now = rtc.getEpoch();
  uint32_t alarmEpoch = now + hours*3600 + minutes*60 + seconds;
  
  rtc.setAlarmEpoch(alarmEpoch);
  rtc.enableAlarm(rtc.MATCH_HHMMSS);
  rtc.attachInterrupt(alarmMatch);

  rtc.standbyMode();
//  USBDevice.attach();

  blinkLed();
  
//  Serial.begin(9600);
//  while(!Serial);  // TODO: remove this line if 'in the field'
}

void loop () {
  takeFix();
  sleepFor(FIX_PERIOD_H_M_S);
}
