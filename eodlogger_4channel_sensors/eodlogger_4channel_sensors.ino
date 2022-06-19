#include <Configurator.h>
#include <ContinuousADC.h>
#include <SDWriter.h>
#include <ESensors.h>
#include <TemperatureDS18x20.h>
#include <LightBH1750.h>
//#include <Wire.h>
//#include <DS1307RTC.h>
#include <RTClock.h>
#include <Settings.h>
#include <Blink.h>
#include <TestSignals.h>


// Default settings: ----------------------------------------------------------
// (may be overwritten by config file logger.cfg)

uint32_t samplingRate = 44000; // samples per second and channel in Hertz
int8_t channels0[] = {A2, A3, -1};   // input pins for ADC0
int8_t channels1[] = {A16, A17, -1}; // input pins for ADC1
int bits = 12;                 // resolution: 10bit 12bit, or 16bit
int averaging = 4;             // number of averages per sample: 0, 4, 8, 16, 32

uint8_t tempPin = 25;          // pin for DATA of thermometer
float sensorsInterval = 10.0;  // interval between sensors readings in seconds

char path[] = "recordings";    // directory where to store files on SD card.
char fileName[] = "logger1-SDATETIME"; // may include DATE, SDATE, TIME, STIME, DATETIME, SDATETIME, ANUM, NUM
float fileSaveTime = 10*60;    // seconds

int pulseFrequency = 200;      // Hertz
int signalPins[] = {2, 3, 4, 5, -1};  // pins where to put out test signals


// ----------------------------------------------------------------------------
 
const char version[4] = "1.0";

Configurator config;

DATA_BUFFER(AIBuffer, NAIBuffer, 256*256)
ContinuousADC aidata(AIBuffer, NAIBuffer);

SDCard sdcard;
SDWriter file(sdcard, aidata);
Settings settings("recordings", fileName, fileSaveTime, pulseFrequency);
String prevname; // previous file name
RTClock rtclock;
Blink blink(LED_BUILTIN);

ESensors sensors;
TemperatureDS18x20 temp(&sensors);
LightBH1750 light(&sensors);

int restarts = 0;


void setupADC() {
  aidata.setChannels(0, channels0);
  aidata.setChannels(1, channels1);
  aidata.setRate(samplingRate);
  aidata.setResolution(bits);
  aidata.setAveraging(averaging);
  aidata.setConversionSpeed(ADC_CONVERSION_SPEED::HIGH_SPEED);
  aidata.setSamplingSpeed(ADC_SAMPLING_SPEED::HIGH_SPEED);
  aidata.check();
}


void setupSensors() {
  temp.begin(tempPin);
  temp.setName("water temperature", "Tw");
  Wire1.begin();
  light.begin(Wire1);
  light.setAutoRanging();
  sensors.setInterval(sensorsInterval);
  sensors.setPrintTime(ESensors::ISO_TIME);
  sensors.report();
  Serial.println();
  // init sensors:
  sensors.start();
  sensors.read();
  //light.setTemperature(bme.temperature());
  sensors.read();
  sensors.start();
}


String makeFileName() {
  time_t t = now();
  String name = rtclock.makeStr(settings.FileName, t, true);
  if (name != prevname) {
    file.resetFileCounter();
    prevname = name;
  }
  name = file.incrementFileName(name + ".wav");
  if (name.length() <= 4) {
    Serial.println("WARNING: failed to increment file name.");
    Serial.println("SD card probably not inserted.");
    Serial.println();
    return "";
  }
  name.remove(name.length()-4);
  return name;
}


bool openNextFile(const String &name) {
  blink.clear();
  if (name.length() == 0)
    return false;
  String fname = name + ".wav";
  char dts[20];
  if (! file.openWave(fname.c_str(), -1, dts)) {
    Serial.println();
    Serial.println("WARNING: failed to open file on SD card.");
    Serial.println("SD card probably not inserted or full -> halt");
    aidata.stop();
    while (1) {};
    return false;
  }
  file.write();
  Serial.println(fname);
  blink.setSingle();
  blink.blinkSingle(0, 1000);
  return true;
}


void setupStorage() {
  prevname = "";
  if (settings.FileTime > 30)
    blink.setTiming(5000);
  if (file.dataDir(settings.Path))
    Serial.printf("Save recorded data in folder \"%s\".\n\n", settings.Path);
  file.setWriteInterval();
  file.setMaxFileTime(settings.FileTime);
  char ss[30] = "eodlogger_4channel_sensors v";
  strcat(ss, version);
  file.setSoftware(ss);
}


void storeData() {
  if (file.pending()) {
    ssize_t samples = file.write();
    if (samples <= 0) {
      blink.clear();
      Serial.println();
      Serial.println("ERROR in writing data to file:");
      switch (samples) {
        case 0:
          Serial.println("  Nothing written into the file.");
          Serial.println("  SD card probably full -> halt");
          aidata.stop();
          while (1) {};
          break;
        case -1:
          Serial.println("  File not open.");
          break;
        case -2:
          Serial.println("  File already full.");
          break;
        case -3:
          Serial.println("  No data available, data acquisition probably not running.");
          Serial.println("  sampling rate probably too high,");
          Serial.println("  given the number of channels, averaging, sampling and conversion speed.");
          break;
      }
      if (samples == -3) {
        aidata.stop();
        file.closeWave();
	sensors.closeCSV();
        char mfs[20];
        sprintf(mfs, "error%d-%d.msg", restarts+1, -samples);
        FsFile mf = sdcard.openWrite(mfs);
        mf.close();
      }
    }
    if (file.endWrite() || samples < 0) {
      file.close();  // file size was set by openWave()
      if (samples < 0) {
        restarts++;
        if (restarts >= 5) {
          Serial.println("ERROR: Too many file errors -> halt.");
          aidata.stop();
          while (1) {};
        }
      }
      String name = makeFileName();
      if (samples == -3) {
        String sname = name + "-sensors";
        sensors.openCSV(sdcard.sdcard(), sname.c_str());
        aidata.start();
        file.start();
      }
      openNextFile(name);
    }
  }
}


// ----------------------------------------------------------------------------

void setup() {
  blink.switchOn();
  Serial.begin(9600);
  while (!Serial && millis() < 5000) {};
  rtclock.check();
  setupADC();
  sdcard.begin();
  rtclock.setFromFile(sdcard);
  rtclock.report();
  config.setConfigFile("logger.cfg");
  config.configure(sdcard);
  setupTestSignals(signalPins, settings.PulseFrequency);
  setupStorage();
  setupSensors();
  aidata.check();
  aidata.start();
  aidata.report();
  blink.switchOff();
  if (settings.InitialDelay >= 2.0) {
    delay(1000);
    blink.setDouble();
    blink.delay(uint32_t(1000.0*settings.InitialDelay) - 1000);
  }
  else
    delay(uint32_t(1000.0*settings.InitialDelay));
  String name = makeFileName();
  if (name.length() == 0) {
    Serial.println("-> halt");
    aidata.stop();
    while (1) {};
  }
  String sname = name + "-sensors";
  sensors.openCSV(sdcard.sdcard(), sname.c_str());
  sensors.start();
  file.start();
  openNextFile(name);
}


void loop() {
  storeData();
  if (sensors.update()) {
    //tsl.setTemperature(bme.temperature());
    sensors.print();
  }
  if (file.writeTime() < 0.01 &&
      sensors.pendingCSV())
    sensors.writeCSV();
  blink.update();
}
