// --- Bibliotecas ---
#include <ArduinoJson.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// --- Constantes de Simulação ---
const float LIMITE_RESISTENCIA_ABERTO = 1000.0;

// --- UUIDs para o Serviço BLE (Idênticos à versão de produção) ---
#define BLE_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_RECEIVE_CHARACTERISTIC_UUID \
    "beb5483e-36e1-4688-b7f5-ea07361b26a8"  // WebApp -> ESP32
#define BLE_SEND_CHARACTERISTIC_UUID \
    "a4d23253-2778-436c-9c23-2c1b50d87635"  // ESP32 -> WebApp

// --- Objetos e Variáveis Globais ---
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

void sendJsonResponse(const JsonDocument& doc) {
    if (deviceConnected) {
        String jsonOutput;
        serializeJson(doc, jsonOutput);
        Serial.println("TX: " + jsonOutput);
        pSendCharacteristic->setValue(jsonOutput.c_str());
        pSendCharacteristic->notify();
        delay(50);
    } else {
        Serial.println("ERRO: Tentativa de envio sem conexão.");
    }
}

void sendError(const String& message) {
    StaticJsonDocument<200> doc;
    doc["status"] = "error";
    doc["message"] = message;
    sendJsonResponse(doc);
}

// =================================================================
// FUNÇÕES DE HARDWARE (MODO DE SIMULAÇÃO)
// =================================================================

/**
 * @brief Pausa a execução e aguarda um comando de confirmação da WebApp.
 * Esta função é real e faz parte do protocolo de comunicação.
 */
void aguardarConfirmacaoWebApp() {
    Serial.println(">>> Aguardando confirmação da WebApp...");
    g_aguardandoConfirmacao = true;
    while (g_aguardandoConfirmacao) {
        // Processa comandos BLE enquanto aguarda confirmação
        if (g_comandoRecebidoFlag) {
            g_comandoRecebidoFlag = false;
            StaticJsonDocument<1024> doc;
            DeserializationError error = deserializeJson(doc, g_comandoJson);
            if (!error) {
                handleCommand(doc);
            }
        }

        if (!deviceConnected) {
            Serial.println("!!! DESCONECTADO DURANTE A ESPERA. ABORTANDO.");
            g_aguardandoConfirmacao = false;
            return;
        }
        delay(100);
    }
    Serial.println(">>> Confirmação recebida! Continuando...");
}

/**
 * @brief SIMULAÇÃO: Aguarda o usuário pressionar 'b' no Monitor Serial.
 */
void aguardarComandoB() {
    Serial.println("\n----------------------------------------------------");
    Serial.println(">>> Pressione 'b' e Enter para continuar...");
    Serial.println("----------------------------------------------------");

    while (true) {
        // Processa comandos BLE enquanto aguarda entrada do teclado
        if (g_comandoRecebidoFlag) {
            g_comandoRecebidoFlag = false;
            StaticJsonDocument<1024> doc;
            DeserializationError error = deserializeJson(doc, g_comandoJson);
            if (!error) {
                handleCommand(doc);
            }
        }

        // Verifica entrada do Monitor Serial
        if (Serial.available() > 0) {
            String input = Serial.readStringUntil('\n');
            input.trim();
            input.toLowerCase();
            Serial.println(">>> Entrada recebida: '" + input + "'");
            if (input == "b") {
                Serial.println(
                    "[SIMULAÇÃO] Comando 'b' recebido! Continuando...");
                break;
            } else {
                Serial.println(">>> Comando inválido. Pressione 'b' e Enter.");
            }
        }

        // Verifica se ainda está conectado
        if (!deviceConnected) {
            Serial.println("!!! DESCONECTADO DURANTE A ESPERA. ABORTANDO.");
            return;
        }

        delay(100);
    }
}

/**
 * @brief SIMULAÇÃO: Pede ao usuário para inserir um valor de resistência via
 * Monitor Serial.
 * @return O valor de resistência fornecido pelo usuário.
 */
float medirResistenciaSimulada() {
    Serial.println("\n----------------------------------------------------");
    Serial.println(">>> AÇÃO NECESSÁRIA: Simular Medição de Resistência");
    Serial.println(">>> Digite um valor (ex: 0.12 ou 1500) e pressione Enter:");
    Serial.println("----------------------------------------------------");

    while (true) {
        // Processa comandos BLE enquanto aguarda entrada do teclado
        if (g_comandoRecebidoFlag) {
            g_comandoRecebidoFlag = false;
            StaticJsonDocument<1024> doc;
            DeserializationError error = deserializeJson(doc, g_comandoJson);
            if (!error) {
                handleCommand(doc);
            }
        }

        // Verifica entrada do Monitor Serial
        if (Serial.available() > 0) {
            String input = Serial.readStringUntil('\n');
            input.trim();
            float valor = input.toFloat();
            Serial.println("[SIMULAÇÃO] Valor de resistência fornecido: " +
                           String(valor, 3) + " Ω");
            return valor;
        }

        // Verifica se ainda está conectado
        if (!deviceConnected) {
            Serial.println("!!! DESCONECTADO DURANTE A ESPERA. ABORTANDO.");
            return 0.0;
        }

        delay(100);
    }
}

/**
 * @brief SIMULAÇÃO: Apenas imprime no console que um pulso foi enviado.
 */
void pulsarReleSimulado() {
    Serial.println("[SIMULAÇÃO] Pulso enviado ao relé de travamento.");
}

// =================================================================
// ROTINAS PRINCIPAIS DE CALIBRAÇÃO E TESTE (USANDO FUNÇÕES SIMULADAS)
// =================================================================

void executarTesteConfiguravel(const TestConfig& config) {
    Serial.println("=== INICIANDO TESTE COM CONFIRMAÇÃO (SIMULADO) ===");

    // Calcula o número total de testes (NF + NA)
    int totalTestes = config.quantidadePares * 2;
    int testeAtual = 0;

    // Envia mensagem inicial para manter todos os indicadores em vermelho
    StaticJsonDocument<200> initDoc;
    initDoc["status"] = "test_init";
    initDoc["totalTests"] = totalTestes;
    sendJsonResponse(initDoc);

    // --- CICLO 1: ESTADO DE REPOUSO (CONTATOS NF) ---
    Serial.println("[SIMULAÇÃO] Relé em estado de REPOUSO.");
    for (int i = 0; i < config.quantidadePares; i++) {
        // Aguarda o usuário pressionar 'b'
        aguardarComandoB();

        // Sinaliza qual teste está sendo executado (piscando)
        StaticJsonDocument<200> testingDoc;
        testingDoc["status"] = "test_current";
        testingDoc["testIndex"] = testeAtual;
        testingDoc["pair"] = i;
        testingDoc["state"] = "REPOUSO";
        sendJsonResponse(testingDoc);

        // Realiza a medição
        float res =
            medirResistenciaSimulada() - config.calibracao[i].as<float>();

        // Envia o resultado
        StaticJsonDocument<200> resultDoc;
        resultDoc["status"] = "test_result";
        resultDoc["testIndex"] = testeAtual;
        resultDoc["par"] = "NF " + String(i + 1);
        resultDoc["estado"] = "REPOUSO";
        resultDoc["resistencia"] = (res > LIMITE_RESISTENCIA_ABERTO || res < 0)
                                       ? "ABERTO"
                                       : String(res, 3);
        sendJsonResponse(resultDoc);

        testeAtual++;
    }

    // --- ACIONAMENTO DO RELÉ ---
    if (config.tipoAcionamento == "TIPO_PADRAO") {
        Serial.println("[SIMULAÇÃO] Relé LIGADO (tensão contínua).");
    } else {
        pulsarReleSimulado();
    }

    // --- CICLO 2: ESTADO ACIONADO (CONTATOS NA) ---
    for (int i = 0; i < config.quantidadePares; i++) {
        // Aguarda o usuário pressionar 'b'
        aguardarComandoB();

        // Sinaliza qual teste está sendo executado (piscando)
        StaticJsonDocument<200> testingDoc;
        testingDoc["status"] = "test_current";
        testingDoc["testIndex"] = testeAtual;
        testingDoc["pair"] = i;
        testingDoc["state"] = "ACIONADO";
        sendJsonResponse(testingDoc);

        // Realiza a medição
        float res =
            medirResistenciaSimulada() - config.calibracao[i].as<float>();

        // Envia o resultado
        StaticJsonDocument<200> resultDoc;
        resultDoc["status"] = "test_result";
        resultDoc["testIndex"] = testeAtual;
        resultDoc["par"] = "NA " + String(i + 1);
        resultDoc["estado"] = "ACIONADO";
        resultDoc["resistencia"] = (res > LIMITE_RESISTENCIA_ABERTO || res < 0)
                                       ? "ABERTO"
                                       : String(res, 3);
        sendJsonResponse(resultDoc);

        testeAtual++;
    }

    // --- FINALIZAÇÃO E RESET ---
    if (config.tipoAcionamento == "TIPO_TRAVAMENTO") {
        pulsarReleSimulado();
    }
    Serial.println("[SIMULAÇÃO] Relé em estado de REPOUSO (final).");

    StaticJsonDocument<100> completeDoc;
    completeDoc["status"] = "test_complete";
    sendJsonResponse(completeDoc);
    Serial.println("=== TESTE SIMULADO FINALIZADO ===\n");
}

void executarCalibracao(int numTerminais) {
    StaticJsonDocument<512> finalCalibrationDoc;
    finalCalibrationDoc["status"] = "calibration_complete";
    JsonArray valores = finalCalibrationDoc.createNestedArray("data");

    for (int i = 0; i < numTerminais; i++) {
        // Aguarda o usuário pressionar 'b'
        aguardarComandoB();

        // Envia notificação para a interface web
        StaticJsonDocument<200> promptDoc;
        promptDoc["status"] = "prompt";
        promptDoc["message"] =
            "CALIBRAÇÃO: Curto-circuite as pontas para o Par #" +
            String(i + 1) + " e clique em 'Pronto'.";
        sendJsonResponse(promptDoc);

        aguardarConfirmacaoWebApp();
        if (!deviceConnected)
            return;

        float cal = medirResistenciaSimulada();
        valores.add(cal);

        StaticJsonDocument<200> resultDoc;
        resultDoc["status"] = "calibration_result";
        resultDoc["par"] = i + 1;
        resultDoc["valor"] = cal;
        sendJsonResponse(resultDoc);
    }

    sendJsonResponse(finalCalibrationDoc);
}

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

class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        String value = pCharacteristic->getValue().c_str();
        if (value.length() > 0) {
            Serial.println("RX: " + value);
            g_comandoJson = value;
            g_comandoRecebidoFlag = true;
        }
    }
};

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("\n*** Dispositivo conectado via BLE. ***\n");
    }
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        g_aguardandoConfirmacao = false;
        Serial.println(
            "\n*** Dispositivo desconectado. Reiniciando advertising... ***\n");
        BLEDevice::startAdvertising();
    }
};

// =================================================================
// FUNÇÕES PADRÃO ARDUINO: SETUP E LOOP
// =================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n===============================================");
    Serial.println("   Jiga de Teste - MODO DE SIMULAÇÃO");
    Serial.println("===============================================");
    Serial.println("Aguardando conexão da interface web...");

    // Configuração do Servidor BLE
    BLEDevice::init("Jiga - Bluetooth Esp32");
    BLEServer* pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService* pService = pServer->createService(BLE_SERVICE_UUID);

    pSendCharacteristic = pService->createCharacteristic(
        BLE_SEND_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    pSendCharacteristic->addDescriptor(new BLE2902());

    BLECharacteristic* pReceiveCharacteristic = pService->createCharacteristic(
        BLE_RECEIVE_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_WRITE);
    pReceiveCharacteristic->setCallbacks(new MyCharacteristicCallbacks());

    pService->start();
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(pService->getUUID());
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();

    Serial.println(
        "Servidor BLE iniciado. Abra o aplicativo web para conectar.");
}

void loop() {
    // Processa comandos BLE
    if (g_comandoRecebidoFlag) {
        g_comandoRecebidoFlag = false;

        StaticJsonDocument<1024> doc;
        DeserializationError error = deserializeJson(doc, g_comandoJson);

        if (error) {
            Serial.print(F("deserializeJson() falhou: "));
            Serial.println(error.c_str());
            sendError("JSON invalido recebido.");
        } else {
            handleCommand(doc);
        }
    }

    delay(10);
}