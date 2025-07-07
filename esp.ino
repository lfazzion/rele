/*
 * Código adaptado para controle via Web Bluetooth.
 * Comandos:
 * - Recebe '1' via BLE para iniciar a calibração.
 * - Recebe '2' via BLE para iniciar a verificação de resistência.
 * Respostas são enviadas como texto via notificação BLE.
 */

// Bibliotecas do sensor e comunicação
#include "ADS1X15.h"
#include <Wire.h>

// Bibliotecas do Bluetooth Low Energy (BLE)
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

// --- UUIDs para o Serviço e Características BLE ---
// Devem ser os mesmos definidos no arquivo script.js da página web
#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
// Característica para ENVIAR dados (Respostas) PARA a página web (Notificação)
#define TX_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
// Característica para RECEBER dados (Comandos) DA página web (Escrita)
#define RX_CHARACTERISTIC_UUID "a4d23253-2778-436c-9c23-2c1b50d87635"

// Pinos do multiplexador
#define A 15
#define B 2
#define C 4

// Configurações do sensor ADS1115
ADS1115 ADS(0x48);
const float res_ref = 99.781;

// Variáveis globais
static char binaryString[3];
float res;
float r0[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

// Variáveis globais do BLE
BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;

// --- Funções do Sensor e Multiplexador (seu código original) ---

float get_res(){
  int16_t val_01 = ADS.readADC_Differential_0_1();
  int16_t val_13 = ADS.readADC_Differential_1_3();
  float volts_ref = ADS.toVoltage(val_01);
  float volts = ADS.toVoltage(val_13);
  return (res_ref * volts) / volts_ref;
}

void toBinary(int num) {
  for (int i = 2; i >= 0; i--) {
    binaryString[2 - i] = ((num >> i) & 1) ? '1' : '0';
  }
}

void multiplx_controler(int OT){
  toBinary(OT);
  digitalWrite(A, (binaryString[0] == '1') ? HIGH : LOW);
  digitalWrite(B, (binaryString[1] == '1') ? HIGH : LOW);
  digitalWrite(C, (binaryString[2] == '1') ? HIGH : LOW);
}

// --- Funções de Lógica de Controle ---

void run_calibrate(){
  for(int j = 0; j < 8; j++){
    float r0_base = 0.0;
    for(int i = 0; i < 10; i++){
      r0_base += get_res();
    }
    r0[j] = r0_base / 10.0;
  }
  String response = "Calibracao finalizada.";
  Serial.println(response);
  pTxCharacteristic->setValue(response.c_str());
  pTxCharacteristic->notify();
}

void run_verify(){
  String fullResponse = ""; // String para acumular todas as respostas
  for(int j = 0; j < 2; j++){
    multiplx_controler(j);
    delay(500);
    res = 0;
    for(int i = 0; i < 20; i++){
      res += get_res() - r0[j];
    }
    res = res / 20.0;

    // Constrói a linha de resposta
    String line = "Resistencia " + String(j) + ":\t" + String(res, 3) + "\n";
    fullResponse += line; // Acumula a linha na string principal
  }
  Serial.print("Enviando Resposta:\n" + fullResponse);
  pTxCharacteristic->setValue(fullResponse.c_str());
  pTxCharacteristic->notify();
}

// --- Callbacks do BLE ---

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Dispositivo Conectado!");
    }
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Dispositivo Desconectado!");
      BLEDevice::startAdvertising();
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue().c_str();
      if (value.length() > 0) {
        Serial.print("Comando recebido: ");
        int command = value.toInt(); // Converte o valor recebido para inteiro
        Serial.println(command);

        switch(command){
          case 1: // Comando para Calibrar
            run_calibrate();
            break;
          case 2: // Comando para Verificar
            run_verify();
            break;
          default:
            Serial.println("Comando invalido");
            String response = "Comando " + value + " invalido.";
            pTxCharacteristic->setValue(response.c_str());
            pTxCharacteristic->notify();
        }
      }
    }
};

void setup() {
  Serial.begin(115200); // Usar 115200 para melhor performance
  Wire.begin();

  pinMode(A, OUTPUT);
  pinMode(B, OUTPUT);
  pinMode(C, OUTPUT);

  ADS.begin();
  ADS.setGain(0);

  // --- Configuração do Servidor BLE ---
  Serial.println("Iniciando o servidor BLE...");
  BLEDevice::init("ESP32-ADS1115");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pTxCharacteristic = pService->createCharacteristic(TX_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(RX_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setCallbacks(new MyCallbacks());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);

  BLEDevice::startAdvertising();
  Serial.println("Servidor BLE iniciado e aguardando conexões.");
}

void loop() {
  // O loop principal fica vazio pois toda a lógica é baseada nos eventos do BLE
  delay(2000);
}
