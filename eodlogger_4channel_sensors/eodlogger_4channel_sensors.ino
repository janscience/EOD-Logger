#include <InputADC.h>
#include <SDWriter.h>
#include <ESensors.h>
#include <TemperatureDS18x20.h>
#include <LightBH1750.h>
//#include <SenseBME280.h>
#include <RTClock.h>
#include <Blink.h>
#include <TestSignals.h>
#include <Configurator.h>
#include <Settings.h>
#include <InputADCSettings.h>


// Default settings: ----------------------------------------------------------
// (may be overwritten by config file logger.cfg)

#define SAMPLING_RATE 44100 // samples per second and channel in Hertz
#define BITS             12 // resolution: 10bit 12bit, or 16bit
#define AVERAGING         4 // number of averages per sample: 0, 4, 8, 16, 32
#define CONVERSION    ADC_CONVERSION_SPEED::HIGH_SPEED
#define SAMPLING      ADC_SAMPLING_SPEED::HIGH_SPEED
#define REFERENCE     ADC_REFERENCE::REF_3V3
int8_t channels0[] = {A2, A3, -1};   // input pins for ADC0
int8_t channels1[] = {A10, A11, -1}; // input pins for ADC1

#define TEMP_PIN         5     // pin for DATA of thermometer
#define SENSORS_INTERVAL 10.0  // interval between sensors readings in seconds

#define PATH          "recordings" // directory where to store files on SD card.
#define FILENAME      "logger3-SDATETIME" // may include DATE, SDATE, TIME, STIME, DATETIME, SDATETIME, ANUM, NUM
#define FILE_SAVE_TIME 5*60 // seconds
#define INITIAL_DELAY 10.0  // seconds

#define PULSE_FREQUENCY 230 // Hertz
//int signalPins[] = {2, 3, 4, 5, -1};  // pins where to put out test signals


// ----------------------------------------------------------------------------
 
#define VERSION "1.2"

RTClock rtclock;

DATA_BUFFER(AIBuffer, NAIBuffer, 256*256)
InputADC aidata(AIBuffer, NAIBuffer, channels0, channels1);

SDCard sdcard;
SDWriter file(sdcard, aidata);

Configurator config;
Settings settings(PATH, FILENAME, FILE_SAVE_TIME, PULSE_FREQUENCY,
                  0.0, INITIAL_DELAY, SENSORS_INTERVAL);
InputADCSettings aisettings(SAMPLING_RATE, BITS, AVERAGING,
			    CONVERSION, SAMPLING, REFERENCE);
String prevname; // previous file name
Blink blink(LED_BUILTIN);

ESensors sensors;
TemperatureDS18x20 temp(&sensors);
LightBH1750 light(&sensors);
//SenseBME280 bme;
//TemperatureBME280 airtemp(&bme, &sensors);
//HumidityBME280 hum(&bme, &sensors);
//PressureBME280 pres(&bme, &sensors);
int restarts = 0;


void setupSensors() {
  temp.begin(TEMP_PIN);
  temp.setName("water temperature", "Tw");
  Wire1.begin();
  light.begin(Wire1);
  light.setAutoRanging();
  //Wire2.begin();
  //bme.beginI2C(Wire2, 0x77);
  //hum.setPercent();
  //pres.setHecto();
  sensors.setInterval(settings.sensorsInterval());
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
  String name = rtclock.makeStr(settings.fileName(), t, true);
  if (name != prevname) {
    file.sdcard()->resetFileCounter();
    prevname = name;
  }
  name = file.sdcard()->incrementFileName(name + ".wav");
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
  if (settings.fileTime() > 30)
    blink.setTiming(5000);
  if (file.sdcard()->dataDir(settings.path()))
    Serial.printf("Save recorded data in folder \"%s\".\n\n", settings.path());
  file.setWriteInterval();
  file.setMaxFileTime(settings.fileTime());
  char ss[40] = "eodlogger_4channel_sensors v";
  strcat(ss, VERSION);
  file.header().setSoftware(ss);
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
        char mfs[30];
        sprintf(mfs, "error%d-%d.msg", restarts+1, -samples);
        File mf = sdcard.openWrite(mfs);
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
        sensors.openCSV(sdcard, sname.c_str());
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
  while (!Serial && millis() < 500) {};
  rtclock.init();
  rtclock.check();
  sdcard.begin();
  rtclock.setFromFile(sdcard);
  rtclock.report();
  settings.disable("DisplayTime");
  config.setConfigFile("logger.cfg");
  config.configure(sdcard);
  if (Serial)
    config.configure(Serial);
  config.report();
  //setupTestSignals(signalPins, settings.pulseFrequency());
  setupStorage();
  setupSensors();
  aisettings.configure(&aidata);
  aidata.check();
  aidata.start();
  aidata.report();
  blink.switchOff();
  if (settings.initialDelay() >= 2.0) {
    delay(1000);
    blink.setDouble();
    blink.delay(uint32_t(1000.0*settings.initialDelay()) - 1000);
  }
  else
    delay(uint32_t(1000.0*settings.initialDelay()));
  String name = makeFileName();
  if (name.length() == 0) {
    Serial.println("-> halt");
    aidata.stop();
    while (1) {};
  }
  String sname = name + "-sensors";
  sensors.openCSV(sdcard, sname.c_str());
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
