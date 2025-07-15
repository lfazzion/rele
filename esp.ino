// --- Bibliotecas ---
#include <ArduinoJson.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Wire.h>

#include "ADS1X15.h"

// --- Constantes ---
const int RELAY_PIN = 2;
const int BUTTON_PIN = 15;  // Mantido para possível uso futuro
const int LED_PIN = 4;
const float RESISTOR_REFERENCIA = 99.781;
const int AMOSTRAS_MEDIA = 20;
const float LIMITE_RESISTENCIA_ABERTO = 1000.0;

// --- UUIDs para o Serviço BLE ---
#define BLE_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_RECEIVE_CHARACTERISTIC_UUID \
    "beb5483e-36e1-4688-b7f5-ea07361b26a8"  // WebApp -> ESP32
#define BLE_SEND_CHARACTERISTIC_UUID \
    "a4d23253-2778-436c-9c23-2c1b50d87635"  // ESP32 -> WebApp

// --- Objetos e Variáveis Globais ---
ADS1115 ADS(0x48);
BLECharacteristic* pSendCharacteristic;
bool deviceConnected = false;

// Variáveis para comunicação assíncrona e controle de fluxo
volatile bool g_comandoRecebidoFlag = false;
volatile bool g_aguardandoConfirmacao = false;
String g_comandoJson;

// Estrutura para armazenar a configuração do teste recebida da WebApp
struct TestConfig {
    String tipoAcionamento;
    int quantidadePares;
    JsonArrayConst calibracao;
};

// =================================================================
// FUNÇÕES DE COMUNICAÇÃO
// =================================================================

/**
 * @brief Serializa um documento JSON e o envia via BLE.
 * @param doc O documento JSON a ser enviado.
 */
void sendJsonResponse(const JsonDocument& doc) {
    if (deviceConnected) {
        String jsonOutput;
        serializeJson(doc, jsonOutput);
        Serial.println("TX: " + jsonOutput);
        pSendCharacteristic->setValue(jsonOutput.c_str());
        pSendCharacteristic->notify();
        delay(50);  // Delay para garantir o envio completo do pacote
    } else {
        Serial.println("ERRO: Tentativa de envio sem conexão.");
    }
}

/**
 * @brief Envia uma mensagem de erro padronizada para a WebApp.
 * @param message A mensagem de erro a ser enviada.
 */
void sendError(const String& message) {
    StaticJsonDocument<200> doc;
    doc["status"] = "error";
    doc["message"] = message;
    sendJsonResponse(doc);
}

// =================================================================
// FUNÇÕES DE HARDWARE E LÓGICA DE CONTROLE
// =================================================================

/**
 * @brief Pausa a execução e aguarda um comando de confirmação da WebApp.
 * A flag `g_aguardandoConfirmacao` é resetada por uma interrupção BLE.
 */
void aguardarConfirmacaoWebApp() {
    Serial.println(">>> Aguardando confirmação da WebApp...");
    g_aguardandoConfirmacao = true;
    while (g_aguardandoConfirmacao) {
        if (!deviceConnected) {  // Condição de saída de emergência
            Serial.println("!!! DESCONECTADO DURANTE A ESPERA. ABORTANDO.");
            g_aguardandoConfirmacao = false;
            return;
        }
        delay(100);
    }
    Serial.println(">>> Confirmação recebida! Continuando...");
}

/**
 * @brief Realiza a leitura da resistência no contato do relé usando o ADC.
 * @return O valor da resistência em Ohms, ou -1.0 em caso de erro.
 */
float medirResistenciaContato() {
    if (!ADS.isConnected()) {
        sendError("Conversor ADC (ADS1115) nao encontrado.");
        return -1.0;
    }
    float soma = 0;
    for (int i = 0; i < AMOSTRAS_MEDIA; i++) {
        int16_t val_ref = ADS.readADC_Differential_0_1();
        int16_t val_contato = ADS.readADC_Differential_2_3();
        float volts_ref = ADS.toVoltage(val_ref);
        float volts_contato = ADS.toVoltage(val_contato);
        if (volts_ref == 0)
            return -1.0;  // Evita divisão por zero
        soma += (RESISTOR_REFERENCIA * volts_contato) / volts_ref;
        delay(10);
    }
    return soma / AMOSTRAS_MEDIA;
}

/**
 * @brief Envia um pulso para acionar relés do tipo travamento (latching).
 */
void pulsarRele() {
    digitalWrite(RELAY_PIN, HIGH);
    delay(100);
    digitalWrite(RELAY_PIN, LOW);
}

// =================================================================
// ROTINAS PRINCIPAIS DE CALIBRAÇÃO E TESTE
// =================================================================

/**
 * @brief Executa o ciclo de teste completo de forma interativa.
 * @param config A estrutura contendo os parâmetros do teste.
 */
void executarTesteConfiguravel(const TestConfig& config) {
    Serial.println("=== INICIANDO TESTE COM CONFIRMAÇÃO ===");

    // --- CICLO 1: ESTADO DE REPOUSO (CONTATOS NF) ---
    digitalWrite(RELAY_PIN, LOW);
    for (int i = 0; i < config.quantidadePares; i++) {
        // Envia prompt e aguarda
        StaticJsonDocument<200> stepDoc;
        stepDoc["status"] = "test_step";
        stepDoc["pair"] = i;
        stepDoc["state"] = "REPOUSO";
        stepDoc["message"] =
            "REPOUSO: Conecte as pontas de prova no Par de Contatos #" +
            String(i + 1) + ".";
        sendJsonResponse(stepDoc);

        aguardarConfirmacaoWebApp();
        if (!deviceConnected)
            return;

        // Mede e envia o resultado individual
        float res =
            medirResistenciaContato() - config.calibracao[i].as<float>();
        StaticJsonDocument<200> resultDoc;
        resultDoc["status"] = "test_result";
        resultDoc["par"] = "NF " + String(i + 1);
        resultDoc["estado"] = "REPOUSO";
        resultDoc["resistencia"] = (res > LIMITE_RESISTENCIA_ABERTO || res < 0)
                                       ? "ABERTO"
                                       : String(res, 3);
        sendJsonResponse(resultDoc);
    }

    // --- ACIONAMENTO DO RELÉ ---
    if (config.tipoAcionamento == "TIPO_PADRAO") {
        digitalWrite(RELAY_PIN, HIGH);
    } else {
        pulsarRele();
    }

    // --- CICLO 2: ESTADO ACIONADO (CONTATOS NA) ---
    for (int i = 0; i < config.quantidadePares; i++) {
        // Envia prompt e aguarda
        StaticJsonDocument<200> stepDoc;
        stepDoc["status"] = "test_step";
        stepDoc["pair"] = i;
        stepDoc["state"] = "ACIONADO";
        stepDoc["message"] =
            "ACIONADO: Mantenha as pontas no Par #" + String(i + 1) + ".";
        sendJsonResponse(stepDoc);

        aguardarConfirmacaoWebApp();
        if (!deviceConnected)
            return;

        // Mede e envia o resultado individual
        float res =
            medirResistenciaContato() - config.calibracao[i].as<float>();
        StaticJsonDocument<200> resultDoc;
        resultDoc["status"] = "test_result";
        resultDoc["par"] = "NA " + String(i + 1);
        resultDoc["estado"] = "ACIONADO";
        resultDoc["resistencia"] = (res > LIMITE_RESISTENCIA_ABERTO || res < 0)
                                       ? "ABERTO"
                                       : String(res, 3);
        sendJsonResponse(resultDoc);
    }

    // --- FINALIZAÇÃO E RESET ---
    if (config.tipoAcionamento == "TIPO_TRAVAMENTO") {
        pulsarRele();  // Pulsa para voltar ao estado de repouso
    }
    digitalWrite(RELAY_PIN, LOW);

    // Envia sinal de teste completo para a WebApp
    StaticJsonDocument<100> completeDoc;
    completeDoc["status"] = "test_complete";
    sendJsonResponse(completeDoc);
    Serial.println("=== TESTE FINALIZADO ===\n");
}

/**
 * @brief Executa o ciclo de calibração de forma interativa.
 * @param numTerminais A quantidade de pares de contatos a calibrar.
 */
void executarCalibracao(int numTerminais) {
    // Documento para acumular todos os valores de calibração
    StaticJsonDocument<512> finalCalibrationDoc;
    finalCalibrationDoc["status"] = "calibration_complete";
    JsonArray valores = finalCalibrationDoc.createNestedArray("data");

    for (int i = 0; i < numTerminais; i++) {
        // Envia prompt e aguarda
        StaticJsonDocument<200> promptDoc;
        promptDoc["status"] = "prompt";
        promptDoc["message"] =
            "CALIBRAÇÃO: Curto-circuite as pontas para o Par #" +
            String(i + 1) + " e clique em 'Pronto'.";
        sendJsonResponse(promptDoc);

        aguardarConfirmacaoWebApp();
        if (!deviceConnected)
            return;

        // Mede, envia resultado individual e acumula
        float cal = medirResistenciaContato();
        if (cal < 0) {
            sendError("Falha ao medir calibração do Par #" + String(i + 1));
            return;
        }
        valores.add(cal);

        StaticJsonDocument<200> resultDoc;
        resultDoc["status"] = "calibration_result";
        resultDoc["par"] = i + 1;
        resultDoc["valor"] = cal;
        sendJsonResponse(resultDoc);
    }

    // Envia o lote completo de dados de calibração no final
    sendJsonResponse(finalCalibrationDoc);
}

/**
 * @brief Interpreta o comando JSON recebido e direciona para a função correta.
 * @param doc O documento JSON com o comando.
 */
void handleCommand(const JsonDocument& doc) {
    String comando = doc["comando"];
    if (comando == "iniciar_teste") {
        TestConfig config;
        config.tipoAcionamento = doc["config"]["tipoAcionamento"].as<String>();
        config.quantidadePares = doc["config"]["quantidadePares"];
        config.calibracao = doc["config"]["calibracao"];
        executarTesteConfiguravel(config);
    } else if (comando == "calibrar") {
        int numTerminais = doc["numTerminais"];
        executarCalibracao(numTerminais);
    } else if (comando == "confirmar_etapa") {
        g_aguardandoConfirmacao = false;
    } else {
        sendError("Comando desconhecido: " + comando);
    }
}

// =================================================================
// CLASSES DE CALLBACKS BLE
// =================================================================

/**
 * @brief Callback para eventos de escrita na característica BLE.
 * Apenas armazena o comando e define uma flag para processamento no loop
 * principal.
 */
class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        String value = pCharacteristic->getValue().c_str();
        if (value.length() > 0) {
            // Não processa aqui para evitar bloquear a interrupção BLE
            g_comandoJson = value;
            g_comandoRecebidoFlag = true;
        }
    }
};

/**
 * @brief Callback para eventos de conexão e desconexão do servidor BLE.
 */
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("Dispositivo conectado.");
    }
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        g_aguardandoConfirmacao =
            false;  // Garante que qualquer espera seja cancelada
        Serial.println("Dispositivo desconectado.");
        BLEDevice::startAdvertising();  // Permite que se reconectem
    }
};

// =================================================================
// FUNÇÕES PADRÃO ARDUINO: SETUP E LOOP
// =================================================================

void setup() {
    Serial.begin(115200);
    Wire.begin();

    // Configuração dos Pinos
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(LED_PIN, LOW);

    // Inicialização do Conversor ADC
    if (!ADS.begin()) {
        Serial.println("Falha ao inicializar o ADS.");
    }
    ADS.setGain(0);      // Ganho +/- 6.144V
    ADS.setDataRate(4);  // Taxa de 128 amostras por segundo
    ADS.setMode(0);      // Modo de conversão contínua

    // Configuração do Servidor BLE
    BLEDevice::init("Jiga de Teste Pro");
    BLEServer* pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService* pService = pServer->createService(BLE_SERVICE_UUID);

    // Característica para Envio (ESP32 -> WebApp)
    pSendCharacteristic = pService->createCharacteristic(
        BLE_SEND_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    pSendCharacteristic->addDescriptor(new BLE2902());

    // Característica para Recebimento (WebApp -> ESP32)
    BLECharacteristic* pReceiveCharacteristic = pService->createCharacteristic(
        BLE_RECEIVE_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_WRITE);
    pReceiveCharacteristic->setCallbacks(new MyCharacteristicCallbacks());

    pService->start();

    // Inicia o "Advertising" para que o dispositivo seja descoberto
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(pService->getUUID());
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();

    Serial.println("Servidor BLE iniciado. Aguardando conexões...");
}

void loop() {
    if (g_comandoRecebidoFlag) {
        g_comandoRecebidoFlag = false;  // Reseta a flag imediatamente

        StaticJsonDocument<1024> doc;  // Buffer para o JSON recebido
        DeserializationError error = deserializeJson(doc, g_comandoJson);

        if (error) {
            Serial.print(F("deserializeJson() falhou: "));
            Serial.println(error.c_str());
            sendError("JSON invalido recebido.");
        } else {
            handleCommand(doc);  // Processa o comando válido
        }
    }

    delay(10);  // Pequeno delay para evitar sobrecarga da CPU
}