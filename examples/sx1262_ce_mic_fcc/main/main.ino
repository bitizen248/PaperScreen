/*
  RadioLib SX126x Transmit with Interrupts Example

  This example transmits LoRa packets with one second delays
  between them. Each packet contains up to 256 bytes
  of data, in the form of:
  - Arduino String
  - null-terminated char array (C-string)
  - arbitrary binary data (byte array)

  Other modules from SX126x family can also be used.

  For default module settings, see the wiki page
  https://github.com/jgromes/RadioLib/wiki/Default-configuration#sx126x---lora-modem

  For full API reference, see the GitHub Pages
  https://jgromes.github.io/RadioLib/
*/

// include the library
#include <RadioLib.h>
#include "utilities.h"
#include "ExtensionIOXL9555.hpp"

#define Certification_MIC
// #define Certification_CE
// #define Certification_FCC

// MIC
#if defined(Certification_MIC)
#define RADIO_TX_POWER 5.0
#define RADIO_BANDWIDTH 125.0
#define RADIO_FREQ 923.0
#define TX_INTERVAL 500
// CE
#elif defined(Certification_CE)
#define RADIO_TX_POWER 5.0
#define RADIO_BANDWIDTH 125.0
#define RADIO_FREQ 923.0
#define TX_INTERVAL 500
// FCC
#elif defined(Certification_FCC)
#define RADIO_TX_POWER 7.0
#define RADIO_BANDWIDTH 500.0
#define RADIO_FREQ 915.0
#define TX_INTERVAL 500
#endif

ExtensionIOXL9555 io;
// SX1262 has the following connections:
// NSS pin:   10
// DIO1 pin:  2
// NRST pin:  3
// BUSY pin:  9
SX1262 radio = new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_BUSY);

// or using RadioShield
// https://github.com/jgromes/RadioShield
// SX1262 radio = RadioShield.ModuleA;

// or using CubeCell
// SX1262 radio = new Module(RADIOLIB_BUILTIN_MODULE);

// save transmission state between loops
int transmissionState = RADIOLIB_ERR_NONE;
uint32_t runningMillis = 0;

void io_extend_lora_gps_power_on(bool en)
{
    const uint8_t chip_address = XL9555_SLAVE_ADDRESS0;

    if (!io.init(Wire, BOARD_SDA, BOARD_SCL, chip_address))
    {
        while (1)
        {
            Serial.println("Failed to find XL9555 - check your wiring!");
            delay(1000);
        }
    }

    // Set PORT0 as input,mask = 0xFF = all pin input
    io.configPort(ExtensionIOXL9555::PORT0, 0x00);
    // Set PORT1 as input,mask = 0xFF = all pin input
    io.configPort(ExtensionIOXL9555::PORT1, 0xFF);

    Serial.println("Power on LoRa and GPS!");
    if (en)
        io.digitalWrite(ExtensionIOXL9555::IO0, HIGH);
    else
        io.digitalWrite(ExtensionIOXL9555::IO0, LOW);

    delay(1500);
}

void setup()
{
    // lora and sd use the same spi, in order to avoid mutual influence;
    // before powering on, all CS signals should be pulled high and in an unselected state;
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    Serial.begin(115200);

    // This must be turned on, otherwise LoRa and GPS will not work
    io_extend_lora_gps_power_on(true);

    SPI.begin(BOARD_SPI_SCLK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

    // initialize SX1262 with default settings
    Serial.print(F("[SX1262] Initializing ... "));
    int state = radio.begin(RADIO_FREQ);
    if (state == RADIOLIB_ERR_NONE)
    {
        Serial.println(F("success!"));
    }
    else
    {
        Serial.print(F("failed, code "));
        Serial.println(state);
        while (true)
            ;
    }

    // set the function that will be called
    // when packet transmission is finished
    radio.setPacketSentAction(setFlag);

    if (radio.setFrequency(RADIO_FREQ) == RADIOLIB_ERR_INVALID_FREQUENCY)
    {
        Serial.println(F("Selected frequency is invalid for this module!"));
        while (true)
            ;
    }

    // set bandwidth to 250 kHz
    if (radio.setBandwidth(RADIO_BANDWIDTH) == RADIOLIB_ERR_INVALID_BANDWIDTH)
    {
        Serial.println(F("Selected bandwidth is invalid for this module!"));
        while (true)
            ;
    }

    // set spreading factor to 10
    if (radio.setSpreadingFactor(10) == RADIOLIB_ERR_INVALID_SPREADING_FACTOR)
    {
        Serial.println(F("Selected spreading factor is invalid for this module!"));
        while (true)
            ;
    }

    // set coding rate to 6
    if (radio.setCodingRate(6) == RADIOLIB_ERR_INVALID_CODING_RATE)
    {
        Serial.println(F("Selected coding rate is invalid for this module!"));
        while (true)
            ;
    }

    // set LoRa sync word to 0xAB
    if (radio.setSyncWord(0xAB) != RADIOLIB_ERR_NONE)
    {
        Serial.println(F("Unable to set sync word!"));
        while (true)
            ;
    }

    // set output power to 22 dBm (accepted range is -17 - 22 dBm)
    if (radio.setOutputPower(RADIO_TX_POWER) == RADIOLIB_ERR_INVALID_OUTPUT_POWER)
    {
        Serial.println(F("Selected output power is invalid for this module!"));
        while (true)
            ;
    }

    // set over current protection limit to 80 mA (accepted range is 45 - 240 mA)
    // NOTE: set value to 0 to disable overcurrent protection
    if (radio.setCurrentLimit(140) == RADIOLIB_ERR_INVALID_CURRENT_LIMIT)
    {
        Serial.println(F("Selected current limit is invalid for this module!"));
        while (true)
            ;
    }

    // set LoRa preamble length to 15 symbols (accepted range is 0 - 65535)
    if (radio.setPreambleLength(15) == RADIOLIB_ERR_INVALID_PREAMBLE_LENGTH)
    {
        Serial.println(F("Selected preamble length is invalid for this module!"));
        while (true)
            ;
    }

    // disable CRC
    if (radio.setCRC(false) == RADIOLIB_ERR_INVALID_CRC_CONFIGURATION)
    {
        Serial.println(F("Selected CRC is invalid for this module!"));
        while (true)
            ;
    }

    // Some SX126x modules have TCXO (temperature compensated crystal
    // oscillator). To configure TCXO reference voltage,
    // the following method can be used.
    if (radio.setTCXO(2.4) == RADIOLIB_ERR_INVALID_TCXO_VOLTAGE)
    {
        Serial.println(F("Selected TCXO voltage is invalid for this module!"));
        while (true)
            ;
    }

    // Some SX126x modules use DIO2 as RF switch. To enable
    // this feature, the following method can be used.
    // NOTE: As long as DIO2 is configured to control RF switch,
    //       it can't be used as interrupt pin!
    if (radio.setDio2AsRfSwitch() != RADIOLIB_ERR_NONE)
    {
        Serial.println(F("Failed to set DIO2 as RF switch!"));
        while (true)
            ;
    }

    Serial.println(F("All settings succesfully changed!"));

    // start transmitting the first packet
    Serial.print(F("[SX1262] Sending first packet ... "));

    // you can transmit C-string or Arduino string up to
    // 256 characters long
    transmissionState = radio.startTransmit("Hello World!");

    // you can also transmit byte array up to 256 bytes long
    /*
      byte byteArr[] = {0x01, 0x23, 0x45, 0x67,
                        0x89, 0xAB, 0xCD, 0xEF};
      state = radio.startTransmit(byteArr, 8);
    */
}

// flag to indicate that a packet was sent
volatile bool transmittedFlag = false;

// this function is called when a complete packet
// is transmitted by the module
// IMPORTANT: this function MUST be 'void' type
//            and MUST NOT have any arguments!
#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void setFlag(void)
{
    // we sent a packet, set the flag
    transmittedFlag = true;
}

// counter to keep track of transmitted packets
int count = 0;
char buf[256];

void loop()
{
    if (millis() - runningMillis > TX_INTERVAL)
    {
        // check if the previous transmission finished
        if (transmittedFlag)
        {
            // reset flag
            transmittedFlag = false;

            if (transmissionState == RADIOLIB_ERR_NONE)
            {
                // packet was successfully sent
                Serial.println(F("transmission finished!"));

                // NOTE: when using interrupt-driven transmit method,
                //       it is not possible to automatically measure
                //       transmission data rate using getDataRate()
            }
            else
            {
                Serial.print(F("failed, code "));
                Serial.println(transmissionState);
            }

            // send another one
            snprintf(buf, 256, "[ %u ]TX %u finished\n", millis(), count++);

            Serial.println(buf);

            transmissionState = radio.startTransmit("Hello");
            
        }
        runningMillis = millis();
    }
    delay(1);
}
