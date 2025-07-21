/* 
  Referências:
  Documentação da biblioteca ADS1X15: https://github.com/RobTillaart/ADS1X15
  Manipulação String -> Array: https://arduino.stackexchange.com/questions/77125/convert-string-to-array
  Exemplo BluetoothSerial: https://github.com/espressif/arduino-esp32/blob/master/libraries/BluetoothSerial/examples/SerialToSerialBT/SerialToSerialBT.ino
*/

#include <Wire.h>
#include "ADS1X15.h"
#include "BluetoothSerial.h"

#define LED_CONT 4
#define RGB_RED 14
#define RGB_GREEN 27
#define RGB_BLUE 26
#define RELAY_DC 2
#define RELAY_AC 12
#define BUTTON 15

#define MEAN 20
#define res_ref 99.781

ADS1115 ADS(0x48);
BluetoothSerial SerialBT;

float res[2][8] = {0.0};
float res_cal = 0.0;
bool relay_state = 0;

// --- Variáveis Globais para controle de estado ---
int num_contacts = 0;
int relay_to_use = 0;
bool is_configured = false;
bool is_calibrated = false;

void setup() {
    Serial.begin(115200);
    
    if (!SerialBT.begin("ESP32_Miliohmimetro")) {
        Serial.println("Ocorreu um erro ao inicializar o Bluetooth");
    } else {
        Serial.println("Dispositivo Bluetooth pronto para parear!");
        SerialBT.println("Aguardando configuração do App (ex: 8DC).");
    }
    
    Wire.begin(); 

    pinMode(LED_CONT, OUTPUT);
    pinMode(RGB_RED, OUTPUT);
    pinMode(RGB_GREEN, OUTPUT);
    pinMode(RGB_BLUE, OUTPUT);
    pinMode(RELAY_AC, OUTPUT);
    pinMode(RELAY_DC, OUTPUT);
    pinMode(BUTTON, INPUT);

    digitalWrite(RELAY_DC, 0);
    digitalWrite(RELAY_AC, 0);
    ADS.begin();
    ADS.setGain(1);
}

void reset_output() {
    digitalWrite(RGB_RED, 0);
    digitalWrite(RGB_GREEN, 0);
    digitalWrite(RGB_BLUE, 0);
    digitalWrite(RELAY_DC, 0);
    digitalWrite(RELAY_AC, 0);
}

float get_res() {
    if (ADS.isConnected()) {
        ADS.setDataRate(0); //  0 = slow   4 = medium   7 = fast

        int16_t val_01 = ADS.readADC_Differential_0_1();
        int16_t val_13 = ADS.readADC_Differential_1_3();
        float volts_ref = ADS.toVoltage(val_01);
        float volts = ADS.toVoltage(val_13);

        return (res_ref * volts) / volts_ref;
    } else {
        Serial.println("ADS não encontrado");
        SerialBT.println("ERRO: ADS não encontrado");
        return 0.0;
    }
}

void action_relay(int relay_action) {
    relay_state = !relay_state;
    digitalWrite(relay_action, !relay_state);
}

void wait_confirmation() {
    // MENSAGEM ADICIONADA
    Serial.println("Aperte o botão!");
    digitalWrite(LED_CONT, 1);
    while (digitalRead(BUTTON) == 0) {
        delay(50);
    }
    digitalWrite(LED_CONT, 0);
}

void calibrate() {
    wait_confirmation();
    res_cal = 0.0;
    for (int i = 0; i < MEAN; i++) {
        res_cal += get_res();
    }
    res_cal = res_cal / MEAN;
    is_calibrated = true; 
    
    // MENSAGEM ADICIONADA
    Serial.print("Valor Calibração: ");
    Serial.println(res_cal, 4); // Exibe com 4 casas decimais para precisão
}

void read_res(int n, int relay_action) {
    for (int k = 0; k < 2; k++) {
        for (int j = 0; j < n; j++) {
            SerialBT.println("Pressione o botão para medir o contato " + String(j + 1));
            wait_confirmation();
            res[k][j] = 0.0;
            for (int i = 0; i < MEAN; i++) {
                res[k][j] += get_res() - res_cal;
            }
            res[k][j] = res[k][j] / MEAN;
            
            // MENSAGEM ADICIONADA
            Serial.print("Valor Teste (Contato " + String(j + 1) + "): ");
            Serial.println(res[k][j], 4); // Exibe com 4 casas decimais
        }
        action_relay(relay_action);
    }
}

void loop() {
    if (SerialBT.available() > 0) {
        String receivedData = SerialBT.readStringUntil('\n');
        receivedData.trim();

        Serial.print("Comando recebido: ");
        Serial.println(receivedData);

        // --- ESTADO 2: COMANDO DE CALIBRAÇÃO 'C' ---
        if (receivedData.equalsIgnoreCase("C")) {
            if (is_configured) {
                SerialBT.println("Pressione o botão para iniciar a calibração.");
                calibrate();
                SerialBT.println("Calibração concluída. Envie 'V' para iniciar os testes.");
            } else {
                SerialBT.println("ERRO: Configure o dispositivo primeiro (ex: 8DC).");
            }
        }
        
        // --- ESTADO 3: COMANDO DE TESTE 'V' ---
        else if (receivedData.equalsIgnoreCase("V")) {
            if (is_configured && is_calibrated) {
                SerialBT.println("Iniciando medição para " + String(num_contacts) + " contatos.");
                read_res(num_contacts, relay_to_use);
                
                // Envia os resultados para o app
                for (int k = 0; k < 2; k++) {
                    for (int i = 0; i < num_contacts; i++) {
                        SerialBT.println("Res " + String(i + 1) + ": " + String(res[k][i], 3) + " Ohms");
                    }
                }
                SerialBT.println("Medição finalizada. Reconfigure para um novo teste.");
                is_configured = false;
                is_calibrated = false;

            } else if (!is_configured) {
                 SerialBT.println("ERRO: Configure o dispositivo primeiro (ex: 8DC).");
            } else { 
                 SerialBT.println("ERRO: Calibre o dispositivo antes (comando 'C').");
            }
        }
        
        // --- ESTADO 1: COMANDO DE CONFIGURAÇÃO (DEFAULT) ---
        else if (receivedData.length() >= 2) {
            String type = receivedData.substring(receivedData.length() - 2);
            String contactsStr = receivedData.substring(0, receivedData.length() - 2);
            
            num_contacts = contactsStr.toInt();
            is_configured = false; 
            
            if (type.equalsIgnoreCase("AC")) {
                relay_to_use = RELAY_AC;
            } else if (type.equalsIgnoreCase("DC")) {
                relay_to_use = RELAY_DC;
            } else {
                SerialBT.println("ERRO: Tipo inválido na configuração. Use AC ou DC.");
                return; 
            }

            if (num_contacts > 0 && num_contacts <= 8) {
                is_configured = true; 
                is_calibrated = false;
                SerialBT.println("Configurado: " + String(num_contacts) + " contatos, tipo " + type + ". Envie 'C' para calibrar.");
            } else {
                SerialBT.println("ERRO: Número de contatos inválido. Use de 1 a 8.");
            }
        } else {
             SerialBT.println("Comando desconhecido.");
        }
    }
}
