//#include <SPIFlash.h>
#include <stdarg.h>
#include <RH_RF95.h>
#include <LowPower.h>
#include <RTCZero.h>

// CONFIG
// END CONFIG

#define LED 13
//#define Flash_CS 6
#define RFM95_CS 8
#define RFM95_RST 4
#define RFM95_INT 3

#define RF95_FREQ 434.0

RH_RF95 rf95(RFM95_CS, RFM95_INT);
//SPIFlash flash(Flash_CS, &SPI);
RTCZero rtc;

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
} __attribute__((packed));

struct radiotx_t {
  uint16_t loggerId;
  fix_t fix;
  enum dropoffStatus_t dropoffStatus;
  uint16_t fixIndex;
};

radiotx_t receivedData;
//fix_t currentFix;
uint8_t rxBuffer[255];

void p (char *fmt, ... ) {
  char buf[128]; // resulting string limited to 128 chars
  va_list args;
  va_start (args, fmt );
  vsnprintf(buf, 128, fmt, args);
  va_end (args);
  Serial1.print(buf);
  Serial.print(buf);
}

void pinStr( uint32_t ulPin, unsigned strength) // works like pinMode(), but to set drive strength
{
  // Handle the case the pin isn't usable as PIO
  if ( g_APinDescription[ulPin].ulPinType == PIO_NOT_A_PIN )
  {
    p("Not a pin\n");
    return ;
  }
  if (strength) strength = 1;      // set drive strength to either 0 or 1 copied
  PORT->Group[g_APinDescription[ulPin].ulPort].PINCFG[g_APinDescription[ulPin].ulPin].bit.DRVSTR = strength ;
}

void printFix (fix_t& fix) {
  p("Time: %02d-%02d-%04d at %02d:%02d:%02d\n", fix.day, fix.month, fix.year, fix.hour, fix.minute, fix.second);
  p("Location: ");
  Serial1.print(fix.flat, 6);
  Serial.print(fix.flat, 6);
  p(", ");
  Serial1.print(fix.flon, 6);
  Serial.print(fix.flon, 6);
  p("\nHDOP: ");
  Serial1.print(fix.HDOP/100.0);
  Serial.print(fix.HDOP/100.0);
  p("\n");
}

void printRaidoTx (radiotx_t& rtx, int8_t rssi) {
  p("\nReceived Fix\n");
  p("Logger: %u\n", rtx.loggerId);
  p("RSSI: %d dBm\n", rssi);
  p("Fix #: %u\n", rtx.fixIndex);
  printFix(rtx.fix);
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

void setupRadio () {
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH);

  // manual reset
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);

  while (!rf95.init()) {
    p("LoRa radio init failed\n");
    while (1);
  }
  p("LoRa radio init OK!\n");

   // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM
  if (!rf95.setFrequency(RF95_FREQ)) {
    p("setFrequency failed\n");
    while (1);
  }
  p("Set Freq to: "); 
  Serial1.print(RF95_FREQ);
  Serial.print(RF95_FREQ);
  p("\n");
  
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

//void radioTxFix (fix_t& fix) {
//  radiotx_t rtx;
//  rtx.loggerId = LOGGERID;
//  rtx.fix = fix;
//  rtx.dropoffStatus = dropoffStatus_normal;
//  radioTxRaw(&rtx, sizeof(rtx));
//}

bool radioRxFix (radiotx_t& rtx, int8_t& rssi) {
  uint8_t receivedLength = sizeof(rtx);
  bool retval = radioRxRaw(10000, rxBuffer, &receivedLength, &rssi);
  if (receivedLength != sizeof(rtx)) {
    p("Received unknown packet with length %u\n", receivedLength);
    //TODO: print the packet contents.
    for (uint8_t i = 0; i < 255; i++) {
//      p("%02x", rxBuffer[i]);
//      if (i % 16) serDualPrint("\n");
    }
    return false;
  }
  memcpy(&rtx, rxBuffer, receivedLength);
  return retval;
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

  Serial1.begin(9600);

  p("Init\n");

  setupRtc();
  setupRadio();
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
  int8_t rssi = 127;
  if (radioRxFix(receivedData, rssi)) {
    printRaidoTx(receivedData, rssi);
    delay(50);
  }
}
