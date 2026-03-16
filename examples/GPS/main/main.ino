

#include <TinyGPS++.h>
#include "ExtensionIOXL9555.hpp"


#define SENSOR_SDA  39
#define SENSOR_SCL  40
#define SENSOR_IRQ  -1

#define BOARD_GPS_RXD 44
#define BOARD_GPS_TXD 43
#define BOARD_GPS_PPS 1
#define SerialMon Serial
#define SerialGPS Serial1

void displayInfo();
static bool GPS_Recovery();
bool detectGPSHardware(uint32_t timeoutMs = 2000);
bool gps_init(void);
bool setupGPS();

ExtensionIOXL9555 io;

// The TinyGPSPlus object
TinyGPSPlus gps;
uint8_t buffer[256];

void setup(void)
{
    SerialMon.begin(115200);

    const uint8_t chip_address = XL9555_SLAVE_ADDRESS0;

    if (!io.init(Wire, SENSOR_SDA, SENSOR_SCL, chip_address)) {
        while (1) {
            Serial.println("Failed to find XL9555 - check your wiring!");
            delay(1000);
        }
    }
    // io.configPort(ExtensionIOXL9555::PORT0, 0x00); // Set PORT0 as output ,mask = 0x00 = all pin output
    // io.configPort(ExtensionIOXL9555::PORT1, 0x00);  // Set PORT1 as output ,mask = 0x00 = all pin output

    // LoRa and GPS share the power supply VCC3V3.
    // The VCC3V3 power supply is controlled by LORA_EN.
    // Raise the enable signal of IO00 of the expansion chip PCA9535 to LORA_EN
    io.pinMode(ExtensionIOXL9555::IO0, OUTPUT);
    io.digitalWrite(ExtensionIOXL9555::IO0, HIGH); // Enable Lora & GPS power

    gps_init();

    // delay(1500);
}

void loop(void)
{
    while (SerialMon.available()) {
        SerialGPS.write(SerialMon.read());
    }

    while (SerialGPS.available()) {
        int c = SerialGPS.read();
        if (gps.encode(c)) {
            displayInfo();
        }
    }

    if (millis() > 30000 && gps.charsProcessed() < 10) {
        SerialMon.println(F("No GPS detected: check wiring."));
        delay(1000);
    }

    delay(100);
}

bool gps_init(void)
{   
    bool result = false;
    // L76K GPS USE 9600 BAUDRATE
    SerialGPS.begin(9600, SERIAL_8N1, BOARD_GPS_RXD, BOARD_GPS_TXD);

    if (!detectGPSHardware(2000)) {
        Serial.println("No GPS hardware detected (no serial output). Skipping GPS init.");
        return false;
    }

    result = setupGPS();
    if(!result) {
        // Set u-blox m10q gps baudrate 38400
        SerialGPS.begin(38400, SERIAL_8N1, BOARD_GPS_RXD, BOARD_GPS_TXD);
        result = GPS_Recovery();
        if (!result) {
            SerialGPS.updateBaudRate(9600);
            result = GPS_Recovery();
            if (!result) {
                Serial.println("GPS Connect failed~!");
                result = false;
            }
            SerialGPS.updateBaudRate(38400);
        }
    }
    return result;
}

void displayInfo()
{
    Serial.print(F("Location: "));
    if (gps.location.isValid())
    {
        Serial.print(gps.location.lat(), 6);
        Serial.print(F(","));
        Serial.print(gps.location.lng(), 6);
    }
    else
    {
        Serial.print(F("INVALID"));
    }

    Serial.print(F("  Date/Time: "));
    if (gps.date.isValid())
    {
        Serial.print(gps.date.month());
        Serial.print(F("/"));
        Serial.print(gps.date.day());
        Serial.print(F("/"));
        Serial.print(gps.date.year());
    }
    else
    {
        Serial.print(F("INVALID"));
    }

    Serial.print(F(" "));
    if (gps.time.isValid())
    {
        if (gps.time.hour() < 10)
            Serial.print(F("0"));
        Serial.print(gps.time.hour());
        Serial.print(F(":"));
        if (gps.time.minute() < 10)
            Serial.print(F("0"));
        Serial.print(gps.time.minute());
        Serial.print(F(":"));
        if (gps.time.second() < 10)
            Serial.print(F("0"));
        Serial.print(gps.time.second());
        Serial.print(F("."));
    }
    else
    {
        Serial.print(F("INVALID"));
    }

    Serial.print(F("  Satellites: "));
    if(gps.satellites.isValid())
    {
        Serial.print(gps.satellites.value());
        Serial.print(F(" "));
    }

    Serial.print(F("  Speed: "));
    if(gps.speed.isValid())
    {
        Serial.print(gps.speed.kmph());
        Serial.print(F(" "));
    }

    Serial.println();
}

bool detectGPSHardware(uint32_t timeoutMs)
{
    // Check if the serial port has GPS output (NMEA/UBX) to determine if there 
    // is an available GPS module
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (SerialGPS.available()) {
            // Read a portion of the data to avoid being disturbed by the 
            // old data during the next initialization process.
            while (SerialGPS.available()) {
                SerialGPS.read();
            }
            return true;
        }
        delay(10);
    }
    return false;
}

bool setupGPS()
{
    // L76K GPS USE 9600 BAUDRATE
    SerialGPS.begin(9600, SERIAL_8N1, BOARD_GPS_RXD, BOARD_GPS_TXD);
    bool result = false;
    uint32_t startTimeout ;
    for (int i = 0; i < 3; ++i) {
        SerialGPS.write("$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n");
        delay(5);
        // Get version information
        startTimeout = millis() + 3000;
        Serial.print("Try to init L76K . Wait stop .");
        while (SerialGPS.available()) {
            Serial.print(".");
            SerialGPS.readString();
            if (millis() > startTimeout) {
                Serial.println("Wait L76K stop NMEA timeout!");
                return false;
            }
        };
        Serial.println();
        SerialGPS.flush();
        delay(200);

        SerialGPS.write("$PCAS06,0*1B\r\n");
        startTimeout = millis() + 500;
        String ver = "";
        while (!SerialGPS.available()) {
            if (millis() > startTimeout) {
                Serial.println("Get L76K timeout!");
                return false;
            }
            delay(1);
        }
        SerialGPS.setTimeout(10);
        ver = SerialGPS.readStringUntil('\n');
        if (ver.startsWith("$GPTXT,01,01,02")) {
            Serial.println("L76K GNSS init succeeded, using L76K GNSS Module\n");
            result = true;
            break;
        }
        delay(500);
    }
    // Initialize the L76K Chip, use GPS + GLONASS
    SerialGPS.write("$PCAS04,5*1C\r\n");
    delay(250);
    SerialGPS.write("$PCAS03,1,1,1,1,1,1,1,1,1,1,,,0,0*02\r\n");
    delay(250);
    // Switch to Vehicle Mode, since SoftRF enables Aviation < 2g
    SerialGPS.write("$PCAS11,3*1E\r\n");

    Serial.println("Initialize the L76K Chip");
    return result;
}


static int getAck(uint8_t *buffer, uint16_t size, uint8_t requestedClass, uint8_t requestedID)
{
    uint16_t    ubxFrameCounter = 0;
    bool        ubxFrame = 0;
    uint32_t    startTime = millis();
    uint16_t    needRead;

    while (millis() - startTime < 800) {
        while (SerialGPS.available()) {
            int c = SerialGPS.read();
            switch (ubxFrameCounter) {
            case 0:
                if (c == 0xB5) {
                    ubxFrameCounter++;
                }
                break;
            case 1:
                if (c == 0x62) {
                    ubxFrameCounter++;
                } else {
                    ubxFrameCounter = 0;
                }
                break;
            case 2:
                if (c == requestedClass) {
                    ubxFrameCounter++;
                } else {
                    ubxFrameCounter = 0;
                }
                break;
            case 3:
                if (c == requestedID) {
                    ubxFrameCounter++;
                } else {
                    ubxFrameCounter = 0;
                }
                break;
            case 4:
                needRead = c;
                ubxFrameCounter++;
                break;
            case 5:
                needRead |=  (c << 8);
                ubxFrameCounter++;
                break;
            case 6:
                if (needRead >= size) {
                    ubxFrameCounter = 0;
                    break;
                }
                if (SerialGPS.readBytes(buffer, needRead) != needRead) {
                    ubxFrameCounter = 0;
                } else {
                    return needRead;
                }
                break;

            default:
                break;
            }
        }
        delay(1);
    }
    return 0;
}

static bool GPS_Recovery()
{
    uint8_t cfg_clear1[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x1C, 0xA2};
    uint8_t cfg_clear2[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x1B, 0xA1};
    uint8_t cfg_clear3[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x03, 0x1D, 0xB3};
    SerialGPS.write(cfg_clear1, sizeof(cfg_clear1));

    if (getAck(buffer, 256, 0x05, 0x01)) {
        Serial.println("Get ack successes!");
    }
    SerialGPS.write(cfg_clear2, sizeof(cfg_clear2));
    if (getAck(buffer, 256, 0x05, 0x01)) {
        Serial.println("Get ack successes!");
    }
    SerialGPS.write(cfg_clear3, sizeof(cfg_clear3));
    if (getAck(buffer, 256, 0x05, 0x01)) {
        Serial.println("Get ack successes!");
    }

    // UBX-CFG-RATE, Size 8, 'Navigation/measurement rate settings'
    uint8_t cfg_rate[] = {0xB5, 0x62, 0x06, 0x08, 0x00, 0x00, 0x0E, 0x30};
    SerialGPS.write(cfg_rate, sizeof(cfg_rate));
    if (getAck(buffer, 256, 0x06, 0x08)) {
        Serial.println("Get ack successes!");
    } else {
        return false;
    }
    return true;
}