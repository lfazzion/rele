// --- Bibliotecas ---
#include "ADS1X15.h"
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --- Definições de Pinos e Constantes ---
#define LED_CONT 4
#define RELAY 2
#define BUTTON 15
#define MEAN 20
#define res_ref 99.781

// --- UUIDs do Serviço e Características BLE ---
#define BLE_SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_RECEIVE_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_SEND_CHARACTERISTIC_UUID    "a4d23253-2778-436c-9c23-2c1b50d87635"

// --- Variáveis Globais ---
ADS1115 ADS(0x48);
float res[2][8] = {0.0};
float res_cal[8] = {0.0};
int num = 0;
bool relay_state = 0;
bool deviceConnected = false;

// Usando String do Arduino como nosso único tipo de string.
volatile bool hasNewData = false;
String receivedData = "";
BLECharacteristic *pSendCharacteristic;

// --- Funções ---

// ÚNICA VERSÃO de sendBleMessage, aceita String e não causa ambiguidade.
void sendBleMessage(const String& message) {
    if (deviceConnected) {
        pSendCharacteristic->setValue(message);
        pSendCharacteristic->notify();
        Serial.println("Enviado via BLE: " + message);
    }
}

String waitForBleInput() {
    hasNewData = false;
    while (!hasNewData && deviceConnected) {
        delay(50);
    }
    return receivedData;
}

float get_res() {
    if (ADS.isConnected()) {
        int16_t val_01 = ADS.readADC_Differential_0_1();
        int16_t val_13 = ADS.readADC_Differential_1_3();
        float volts_ref = ADS.toVoltage(val_01);
        float volts = ADS.toVoltage(val_13);
        float result = (res_ref * volts) / volts_ref;
        Serial.println(result);
        return result;
    } else {
        sendBleMessage("ADS nao encontrado");
        return 0.0;
    }
}

void action_relay() {
    digitalWrite(RELAY, !relay_state);
    relay_state = !relay_state;
}

void wait_confirmation(int j) {
    String msg = "Preparado para a proxima leitura do terminal " + String(j) + ". Precione o botão para continuar.";
    sendBleMessage(msg);
    digitalWrite(LED_CONT, 1);
    while(digitalRead(BUTTON)==0){}
    digitalWrite(LED_CONT, 0);
}

void calibrate(int n) {
    for (int j = 0; j < n; j++) {
        wait_confirmation(j);
        res_cal[j] = 0.0;
        for (int i = 0; i < MEAN; i++) {
            res_cal[j] += get_res();
        }
        res_cal[j] = res_cal[j] / MEAN;
    }
    sendBleMessage("Calibracao finalizada.");
}

void read_voltage(int n) {
    for (int k = 0; k < 2; k++) {
        for (int j = 0; j < n; j++) {
            wait_confirmation(j);
            res[k][j] = 0.0;
            for (int i = 0; i < MEAN; i++) {
                res[k][j] += get_res() - res_cal[j];
            }
            res[k][j] = res[k][j] / MEAN;
        }
        action_relay();
    }
}

// --- Callbacks do Servidor BLE ---

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { deviceConnected = true; }
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        BLEDevice::startAdvertising();
    }
};

class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
    // A CORREÇÃO DEFINITIVA ESTÁ AQUI
    void onWrite(BLECharacteristic* pCharacteristic) {
        // Assumindo que getValue() retorna um String, como o compilador indica.
        // A atribuição é de String para String. Sem conversão, sem erro.
        receivedData = pCharacteristic->getValue();

        if (receivedData.length() > 0) {
            hasNewData = true;
        }
    }
};

// --- Setup ---
void setup() {
    Serial.begin(115200);
    Wire.begin();
    pinMode(LED_CONT, OUTPUT);
    pinMode(RELAY, OUTPUT);
    digitalWrite(RELAY, 0);
    ADS.begin();
    ADS.setGain(0);
    ADS.setDataRate(4);
    ADS.setMode(1);

    BLEDevice::init("Jiga de Teste Interativa");
    BLEDevice::setPower(ESP_PWR_LVL_P9);
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(BLE_SERVICE_UUID);
    pSendCharacteristic = pService->createCharacteristic(
        BLE_SEND_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    pSendCharacteristic->addDescriptor(new BLE2902());
    BLECharacteristic *pReceiveCharacteristic = pService->createCharacteristic(
        BLE_RECEIVE_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_WRITE);
    pReceiveCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
    pService->start();
    BLEDevice::startAdvertising();
}

// --- Loop ---
void loop() {
    static bool welcomeMessageSent = false;
    if (deviceConnected && !welcomeMessageSent) {
        sendBleMessage("ESP32 pronto. Envie 'C' ou 'V'.");
        welcomeMessageSent = true;
    }
    if (!deviceConnected) {
        welcomeMessageSent = false;
    }

    if (deviceConnected && hasNewData) {
        char input = receivedData.charAt(0);
        hasNewData = false;

        switch (input) {
            case 'C':
                sendBleMessage("Entre com a quantidade de terminais");
                num = waitForBleInput().toInt();
                calibrate(num);
                sendBleMessage("ESP32 pronto. Envie 'C' ou 'V'.");
                break;
            case 'V':
                sendBleMessage("Entre com a quantidade de terminais");
                num = waitForBleInput().toInt();
                read_voltage(num);
                for (int k = 0; k < 2; k++) {
                    for (int i = 0; i < num; i++) {
                        String result = "Resultado [ciclo " + String(k) + "] Res " + String(i) + ": " + String(res[k][i], 3);
                        sendBleMessage(result);
                    }
                }
                sendBleMessage("Verificacao finalizada.");
                sendBleMessage("ESP32 pronto. Envie 'C' ou 'V'.");
                break;
        }
    }
    delay(100);
}
