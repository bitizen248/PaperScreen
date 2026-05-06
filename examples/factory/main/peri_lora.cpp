

#include <RadioLib.h>
#include "utilities.h"
#include "peripheral.h"
#include "ui_port.h"

TaskHandle_t lora_handle = NULL;

static SX1262 radio = new Module(BOARD_LORA_CS, BOARD_LORA_IRQ, BOARD_LORA_RST, BOARD_LORA_BUSY);
static int lora_mode = LORA_MODE_SEND;
static String lora_recv_data;
static bool lora_recv_success = false;
static int lora_recv_rssi = 0;
static bool lora_ready = false;

// transmit 
static int transmissionState = RADIOLIB_ERR_NONE;
static volatile bool transmittedFlag = false;

static void set_transmit_flag(void){
    transmittedFlag = true;
}

// receive
static int receivedState = RADIOLIB_ERR_NONE;
static volatile bool receivedFlag = false;

static void set_receive_flag(void){
    receivedFlag = true;
}

void lora_task(void *param)
{
    while (1)
    {
        if (lora_ready) {
            lora_receive_loop();
        }
        delay(500);
    }
}

bool lora_sx1262_init(void)
{
    lora_ready = false;
    lora_handle = NULL;
    lora_recv_success = false;
    transmittedFlag = false;
    receivedFlag = false;

    // initialize SX1262 with default settings
    Serial.print(F("[SX1262] Initializing ... "));
    int state = radio.begin();
    if (state == RADIOLIB_ERR_NONE)
    {
        Serial.println(F("success!"));
    }
    else
    {
        Serial.print(F("failed, code "));
        Serial.println(state);
        return false;
    }

    // set the function that will be called
    // when packet transmission is finished
    radio.setPacketSentAction(set_transmit_flag);

    // SX1262 : Allowed values are in range from 150.0 to 960.0 MHz.
    if (radio.setFrequency(LORA_FREQUENNCY) == RADIOLIB_ERR_INVALID_FREQUENCY)
    {
        Serial.println(F("Selected frequency is invalid for this module!"));
        // while (true)
            ;
    }

    // SX1268/SX1262 : Allowed values are 7.8, 10.4, 15.6, 20.8, 31.25, 41.7, 62.5, 125.0, 250.0 and 500.0 kHz.
    if (radio.setBandwidth(LORA_BANDWIDTH) == RADIOLIB_ERR_INVALID_BANDWIDTH)
    {
        Serial.println(F("Selected bandwidth is invalid for this module!"));
        // while (true)
            ;
    }

    // set spreading factor to 10
    if (radio.setSpreadingFactor(LORA_SPREAD_FACTOR) == RADIOLIB_ERR_INVALID_SPREADING_FACTOR)
    {
        Serial.println(F("Selected spreading factor is invalid for this module!"));
        // while (true)
            ;
    }

    // SX1262:  Allowed values range from 5 to 12.
    if (radio.setCodingRate(6) == RADIOLIB_ERR_INVALID_CODING_RATE)
    {
        Serial.println(F("Selected coding rate is invalid for this module!"));
        // while (true)
            ;
    }

    // set LoRa sync word to 0xAB
    if (radio.setSyncWord(0xAB) != RADIOLIB_ERR_NONE)
    {
        Serial.println(F("Unable to set sync word!"));
        // while (true)
            ;
    }

    // SX1262 : Allowed values are in range from -9 to 22 dBm. This method is virtual to allow override from the SX1261 class.
    if (radio.setOutputPower(LORA_OUTPUT_POWER) == RADIOLIB_ERR_INVALID_OUTPUT_POWER)
    {
        Serial.println(F("Selected output power is invalid for this module!"));
        // while (true)
            ;
    }

    // set over current protection limit to 80 mA (accepted range is 45 - 240 mA)
    // NOTE: set value to 0 to disable overcurrent protection
    if (radio.setCurrentLimit(140) == RADIOLIB_ERR_INVALID_CURRENT_LIMIT)
    {
        Serial.println(F("Selected current limit is invalid for this module!"));
        // while (true)
            ;
    }

    // SX1262/SX1268 : Allowed values range from 1 to 65535.
    if (radio.setPreambleLength(15) == RADIOLIB_ERR_INVALID_PREAMBLE_LENGTH)
    {
        Serial.println(F("Selected preamble length is invalid for this module!"));
        // while (true)
            ;
    }

    // disable CRC
    if (radio.setCRC(false) == RADIOLIB_ERR_INVALID_CRC_CONFIGURATION)
    {
        Serial.println(F("Selected CRC is invalid for this module!"));
        // while (true)
            ;
    }

    // Some SX126x modules have TCXO (temperature compensated crystal
    // oscillator). To configure TCXO reference voltage,
    // the following method can be used.
    if (radio.setTCXO(2.4) == RADIOLIB_ERR_INVALID_TCXO_VOLTAGE)
    {
        Serial.println(F("Selected TCXO voltage is invalid for this module!"));
        // while (true)
            ;
    }

    // Some SX126x modules use DIO2 as RF switch. To enable
    // this feature, the following method can be used.
    // NOTE: As long as DIO2 is configured to control RF switch,
    //       it can't be used as interrupt pin!
    if (radio.setDio2AsRfSwitch() != RADIOLIB_ERR_NONE)
    {
        Serial.println(F("Failed to set DIO2 as RF switch!"));
        // while (true)
            ;
    }

    Serial.println(F("All settings succesfully changed!"));

    // start transmitting the first packet
    Serial.print(F("[SX1262] Sending first packet ... "));

    // you can transmit C-string or Arduino string up to
    // 256 characters long
    transmissionState = radio.startTransmit("Hello World!");

    Serial.print(F("[SX1262] recv task Suspend ... "));
    if (xTaskCreate(lora_task, "lora_task", 1024 * 3, NULL, LORA_PRIORITY, &lora_handle) != pdPASS) {
        Serial.println(F("failed to create LoRa task!"));
        lora_handle = NULL;
        return false;
    }
    lora_ready = true;
    vTaskSuspend(lora_handle);

    return true;
}

void lora_set_mode(int mode) 
{
    if (!lora_ready) {
        Serial.println(F("[LORA] module is not available."));
        return;
    }

    if(mode == LORA_MODE_SEND){
        radio.setPacketSentAction(set_transmit_flag);
        Serial.println(F("[LORA] Sending first packet ... "));
        transmissionState = radio.startTransmit("Hello World!");
    } else if(mode == LORA_MODE_RECV){
        radio.setPacketReceivedAction(set_receive_flag);
        Serial.println(F("[LORA] Starting to listen ... "));
        receivedState = radio.startReceive();
        if (receivedState == RADIOLIB_ERR_NONE) {
            Serial.println(F("success!"));
        } else {
            Serial.print(F("failed, code "));
            Serial.println(receivedState);
        }
    }
    lora_mode = mode;
}

int lora_get_mode(void)
{
    return lora_mode;
}

void lora_receive_loop(void)
{
    if (!lora_ready) {
        return;
    }

    if(receivedFlag){
        receivedFlag = false;

        lora_recv_success = true;

        // String str;
        receivedState = radio.readData(lora_recv_data);
        if(receivedState == RADIOLIB_ERR_NONE){
            Serial.println(F("[SX1262] Received packet!"));

            Serial.print(F("[SX1262] Data:\t\t"));
            Serial.println(lora_recv_data.c_str());
            // Serial.println("%d", (int)lora_recv_data.toInt());

            Serial.print(F("[SX1262] RSSI:\t\t"));
            Serial.print(radio.getRSSI());
            lora_recv_rssi = (int) radio.getRSSI();
            Serial.println(F(" dBm"));
        }else{
            Serial.print(F("failed, code "));
            Serial.println(receivedState);
        }
    }
}

void lora_transmit(const char *str)
{
    if (!lora_ready) {
        return;
    }

    if(transmittedFlag){
        transmittedFlag = false;
        if(transmissionState == RADIOLIB_ERR_NONE){
            Serial.println(F("transmission finished!"));
        } else {
            Serial.print(F("failed, code "));
            Serial.println(transmissionState);
        }

        radio.finishTransmit();
        Serial.print(F("[Lora] Sending another packet ... "));
        transmissionState = radio.startTransmit(str);
    }
}

bool lora_get_recv(const char **str, int *rssi)
{
    if (str) *str = lora_recv_data.c_str();
    if (rssi) *rssi = lora_recv_rssi;
    return lora_ready && lora_recv_success;
}

void lora_set_recv_flag(void)
{
    lora_recv_success = false;
}

void lora_sleep(void)
{
    if (lora_ready) {
        radio.sleep();
    }
}

void lora_recv_suspend(void)
{
    if (lora_handle != NULL) {
        vTaskSuspend(lora_handle);
    }
}
void lora_recv_resume(void)
{
    if (lora_handle != NULL) {
        vTaskResume(lora_handle);
    }
}

void lora_param_set(void)
{
    if (!lora_ready) {
        Serial.println(F("[LORA] module is not available."));
        return;
    }

    // set carrier frequency to 433.5 MHz
    if (radio.setFrequency(ui_lora_get_freq()) == RADIOLIB_ERR_INVALID_FREQUENCY) {
        Serial.println(F("Selected frequency is invalid for this module!"));
        return;
    }

    // set bandwidth to 250 kHz
    if (radio.setBandwidth(ui_lora_get_bandwidth()) == RADIOLIB_ERR_INVALID_BANDWIDTH) {
        Serial.println(F("Selected bandwidth is invalid for this module!"));
        return;
    }

    // set output power to 10 dBm (accepted range is -17 - 22 dBm)
    if (radio.setOutputPower(ui_lora_get_power()) == RADIOLIB_ERR_INVALID_OUTPUT_POWER) {
        Serial.println(F("Selected output power is invalid for this module!"));
        return;
    }
}
