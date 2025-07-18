/*
 * Jiga de Teste de Relés - Versão Final
 * Combina hardware real do miliohmimetrov2.ino com comunicação BLE do
 * espsolo.ino
 *
 * Referências:
 * - Documentação da biblioteca ADS1X15: https://github.com/RobTillaart/ADS1X15
 * - Manipulação String -> Array:
 * https://arduino.stackexchange.com/questions/77125/convert-string-to-array
 */

// --- Bibliotecas ---
#include <ArduinoJson.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Wire.h>

#include "ADS1X15.h"

// --- Definições de Hardware ---
#define LED_CONT 4
#define RGB_RED 14
#define RGB_GREEN 27
#define RGB_BLUE 26
#define RELAY_DC 2
#define RELAY_AC 12
#define BUTTON 15

// --- Constantes de Medição ---
#define MEAN 20
#define res_ref 99.781
#define LIMITE_RESISTENCIA_ABERTO 1000.0

// --- UUIDs para o Serviço BLE ---
#define BLE_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_RECEIVE_CHARACTERISTIC_UUID \
    "beb5483e-36e1-4688-b7f5-ea07361b26a8"  // WebApp -> ESP32
#define BLE_SEND_CHARACTERISTIC_UUID \
    "a4d23253-2778-436c-9c23-2c1b50d87635"  // ESP32 -> WebApp

// --- Objetos e Variáveis Globais ---
ADS1115 ADS(0x48);
BLECharacteristic* pSendCharacteristic;
BLEServer* pServer;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Variáveis para comunicação assíncrona e controle de fluxo
volatile bool g_comandoRecebidoFlag = false;
volatile bool g_aguardandoConfirmacao = false;
String g_comandoJson;

// Variáveis para estabilidade da conexão
unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 10000;  // 10 segundos
unsigned long lastConnectionCheck = 0;
const unsigned long CONNECTION_CHECK_INTERVAL = 1000;  // 1 segundo
bool connectionLost = false;

// Variáveis de medição
float res[2][8] = {0.0};
float res_cal = 0.0;
int value = 0;
int num = 0;
bool relay_state = false;

// Estrutura para armazenar a configuração do teste recebida da WebApp
struct TestConfig {
    String tipoAcionamento;
    int quantidadeContatos;  // Número total de contatos a testar (NF + NA)
    JsonArrayConst calibracao;
};

// =================================================================
// FUNÇÕES DE HARDWARE
// =================================================================

void reset_output() {
    digitalWrite(RGB_RED, 0);
    digitalWrite(RGB_GREEN, 0);
    digitalWrite(RGB_BLUE, 0);
    digitalWrite(RELAY_DC, 0);
    digitalWrite(RELAY_AC, 0);
}

void state_RGB(char state) {
    switch (state) {
        case 'O':  // Azul - Aguardando
            digitalWrite(RGB_RED, 0);
            digitalWrite(RGB_GREEN, 0);
            digitalWrite(RGB_BLUE, 1);
            break;
        case 'B':  // Verde - Pronto/Sucesso
            digitalWrite(RGB_RED, 0);
            digitalWrite(RGB_GREEN, 1);
            digitalWrite(RGB_BLUE, 0);
            break;
        case 'R':  // Vermelho - Erro/Falha
            digitalWrite(RGB_RED, 1);
            digitalWrite(RGB_GREEN, 0);
            digitalWrite(RGB_BLUE, 0);
            break;
        default:
            reset_output();
            break;
    }
}

float get_res() {
    if (ADS.isConnected()) {
        ADS.setDataRate(0);  // 0 = slow, 4 = medium, 7 = fast

        int16_t val_01 = ADS.readADC_Differential_0_1();
        int16_t val_13 = ADS.readADC_Differential_1_3();
        float volts_ref = ADS.toVoltage(val_01);
        float volts = ADS.toVoltage(val_13);

        return (res_ref * volts) / volts_ref;
    } else {
        Serial.println("ADS não encontrado");
        return 0.0;
    }
}

void action_relay(int relay_action) {
    relay_state = !relay_state;
    digitalWrite(relay_action, relay_state);
    Serial.println("Relé " + String(relay_action == RELAY_DC ? "DC" : "AC") +
                   " " + String(relay_state ? "ENERGIZADO" : "DESENERGIZADO"));
}

// =================================================================
// FUNÇÕES DE COMUNICAÇÃO BLE
// =================================================================

bool sendJsonResponse(const JsonDocument& doc) {
    if (deviceConnected && pSendCharacteristic) {
        try {
            String jsonString;
            serializeJson(doc, jsonString);

            // Verifica se o tamanho da mensagem não excede o limite BLE
            if (jsonString.length() > 512) {
                Serial.println("AVISO: Mensagem muito longa, truncando...");
                jsonString = jsonString.substring(0, 512);
            }

            pSendCharacteristic->setValue(jsonString.c_str());
            pSendCharacteristic->notify();
            Serial.println("JSON enviado: " + jsonString);

            // Pequeno delay para evitar sobrecarregar o buffer BLE
            delay(10);

            return true;
        } catch (...) {
            Serial.println("Erro ao enviar JSON - exception capturada");
            return false;
        }
    } else {
        Serial.println(
            "Dispositivo não conectado - não foi possível enviar JSON");
        return false;
    }
}

void sendError(const String& message) {
    StaticJsonDocument<200> doc;
    doc["status"] = "error";
    doc["message"] = message;
    sendJsonResponse(doc);
}

void sendHeartbeat() {
    StaticJsonDocument<100> doc;
    doc["status"] = "heartbeat";
    doc["timestamp"] = millis();
    sendJsonResponse(doc);
}

bool checkConnection() {
    unsigned long currentTime = millis();

    // Verifica se a conexão mudou de estado
    if (deviceConnected != oldDeviceConnected) {
        if (deviceConnected) {
            Serial.println("Conexão estabelecida");
            state_RGB('B');  // Verde - conectado
            connectionLost = false;
        } else {
            Serial.println("Conexão perdida");
            reset_output();
            connectionLost = true;
        }
        oldDeviceConnected = deviceConnected;
    }

    // Envia heartbeat periodicamente
    if (deviceConnected && (currentTime - lastHeartbeat > HEARTBEAT_INTERVAL)) {
        sendHeartbeat();
        lastHeartbeat = currentTime;
    }

    return deviceConnected;
}

/**
 * @brief Aguarda o usuário pressionar o botão físico na jiga
 * Envia status para o WebApp e aguarda confirmação física
 */
void aguardarBotaoJiga(String mensagem = "") {
    Serial.println(">>> Aguardando botão da jiga...");

    // Envia prompt para a WebApp
    StaticJsonDocument<300> promptDoc;
    promptDoc["status"] = "prompt";
    if (mensagem.length() > 0) {
        promptDoc["message"] = mensagem;
    } else {
        promptDoc["message"] = "Pressione o botão na jiga para continuar";
    }

    if (!sendJsonResponse(promptDoc)) {
        Serial.println("Falha ao enviar prompt - conexão instável");
        return;
    }

    // Acende LED indicativo
    digitalWrite(LED_CONT, 1);
    state_RGB('O');  // Azul - aguardando

    // Aguarda botão ser pressionado com verificação de conexão
    unsigned long startTime = millis();
    while (digitalRead(BUTTON) == 0) {
        delay(50);

        // Verifica conexão a cada segundo
        if (millis() - startTime > 1000) {
            if (!checkConnection()) {
                digitalWrite(LED_CONT, 0);
                reset_output();
                Serial.println("Conexão perdida durante aguardo do botão");
                return;
            }
            startTime = millis();
        }
    }

    // Botão pressionado - confirma para WebApp
    digitalWrite(LED_CONT, 0);
    state_RGB('B');  // Verde - confirmado

    StaticJsonDocument<100> confirmDoc;
    confirmDoc["status"] = "button_pressed";

    if (!sendJsonResponse(confirmDoc)) {
        Serial.println("Falha ao enviar confirmação do botão");
    }

    delay(500);  // Debounce
    Serial.println(">>> Botão pressionado! Continuando...");
}

/**
 * @brief Aguarda confirmação da WebApp (usado em alguns casos específicos)
 */
void aguardarConfirmacaoWebApp() {
    Serial.println(">>> Aguardando confirmação da WebApp...");
    g_aguardandoConfirmacao = true;

    unsigned long startTime = millis();
    while (g_aguardandoConfirmacao) {
        delay(50);

        // Timeout de 30 segundos
        if (millis() - startTime > 30000) {
            Serial.println("Timeout aguardando confirmação da WebApp");
            g_aguardandoConfirmacao = false;
            return;
        }

        // Verifica conexão
        if (!checkConnection()) {
            Serial.println("Conexão perdida durante aguardo de confirmação");
            g_aguardandoConfirmacao = false;
            return;
        }
    }

    Serial.println(">>> Confirmação recebida! Continuando...");
}

// =================================================================
// FUNÇÕES DE MEDIÇÃO E CALIBRAÇÃO
// =================================================================

void calibrate() {
    Serial.println("=== INICIANDO CALIBRAÇÃO ===");

    // Envia status inicial
    StaticJsonDocument<200> statusDoc;
    statusDoc["status"] = "calibration_init";
    statusDoc["message"] = "Iniciando calibração...";
    sendJsonResponse(statusDoc);

    aguardarBotaoJiga(
        "Calibração: Conecte os fios de medição em curto-circuito e pressione "
        "o botão na jiga");

    if (!deviceConnected)
        return;

    // Verifica se a conexão ainda está ativa antes de prosseguir
    if (!checkConnection()) {
        Serial.println("Conexão perdida durante calibração");
        return;
    }

    // Envia status de processamento
    statusDoc["status"] = "calibration_processing";
    statusDoc["message"] = "Realizando medições de calibração...";
    sendJsonResponse(statusDoc);

    state_RGB('R');  // Vermelho - processando

    res_cal = 0.0;
    for (int i = 0; i < MEAN; i++) {
        res_cal += get_res();
        delay(50);  // Aumenta delay entre medições
    }
    res_cal = res_cal / MEAN;

    state_RGB('B');  // Verde - sucesso

    Serial.println("Calibração concluída: " + String(res_cal, 6));

    // Envia resultado da calibração
    StaticJsonDocument<200> calDoc;
    calDoc["status"] = "calibration_complete";
    calDoc["valor"] = res_cal;
    calDoc["message"] = "Calibração concluída com sucesso!";
    sendJsonResponse(calDoc);

    delay(1000);
    reset_output();
}

float medirResistencia() {
    state_RGB('R');  // Vermelho - medindo

    float resistencia = 0.0;
    for (int i = 0; i < MEAN; i++) {
        resistencia += get_res();
        delay(10);
    }
    resistencia = (resistencia / MEAN) - res_cal;

    state_RGB('B');  // Verde - medição concluída
    delay(300);
    reset_output();

    return resistencia;
}

// =================================================================
// ROTINAS DE TESTE
// =================================================================

/**
 * @brief Executa teste especial para apenas 1 contato
 * Como não sabemos se o usuário vai testar NF ou NA, medimos COM-N# nos dois
 * estados sem avaliar se passou ou falhou, apenas registramos os valores
 */
void executarTesteEspecialUmContato(const TestConfig& config) {
    Serial.println("=== TESTE ESPECIAL PARA 1 CONTATO ===");

    // Envia status inicial
    StaticJsonDocument<200> statusDoc;
    statusDoc["status"] = "test_special_init";
    statusDoc["message"] = "Iniciando teste especial para 1 contato...";
    sendJsonResponse(statusDoc);

    int totalTestes = 2;
    int testeAtual = 0;

    // Envia mensagem inicial
    StaticJsonDocument<200> initDoc;
    initDoc["status"] = "test_init";
    initDoc["totalTests"] = totalTestes;
    sendJsonResponse(initDoc);

    // --- TESTE 1: ESTADO DESENERGIZADO ---
    Serial.println("\n=== TESTE 1: RELÉ DESENERGIZADO ===");

    // Sinaliza teste atual
    StaticJsonDocument<200> testingDoc;
    testingDoc["status"] = "test_current";
    testingDoc["testIndex"] = testeAtual;
    testingDoc["pair"] = 0;
    testingDoc["state"] = "DESENERGIZADO";
    sendJsonResponse(testingDoc);

    aguardarBotaoJiga(
        "TESTE Contato #1 - COM-N#: Relé DESENERGIZADO. Conecte o "
        "multímetro entre COM e o contato N# (NF ou NA) e pressione o botão");

    if (!deviceConnected)
        return;

    // Realiza medição
    float resDesenergizado = medirResistencia();

    // Envia resultado sem avaliação de aprovação
    StaticJsonDocument<200> resultDoc;
    resultDoc["status"] = "test_result";
    resultDoc["testIndex"] = testeAtual;
    resultDoc["contato"] = "COM-N# 1";
    resultDoc["estado"] = "DESENERGIZADO";
    resultDoc["resistencia"] =
        (resDesenergizado > LIMITE_RESISTENCIA_ABERTO || resDesenergizado < 0)
            ? "ABERTO"
            : String(resDesenergizado, 3);
    resultDoc["esperado"] = "VARIÁVEL";
    resultDoc["passou"] = true;  // Sempre passa no teste especial
    sendJsonResponse(resultDoc);

    testeAtual++;

    // --- ACIONAMENTO DO RELÉ ---
    Serial.println("\n=== ACIONANDO O RELÉ ===");
    if (config.tipoAcionamento == "TIPO_DC") {
        digitalWrite(RELAY_DC, 1);
        state_RGB('O');  // Azul - energizado
    } else {             // TIPO_AC
        digitalWrite(RELAY_AC, 1);
        state_RGB('O');  // Azul - energizado
    }

    delay(500);  // Tempo para estabilização

    // --- TESTE 2: ESTADO ENERGIZADO ---
    Serial.println("\n=== TESTE 2: RELÉ ENERGIZADO ===");

    testingDoc["testIndex"] = testeAtual;
    testingDoc["pair"] = 0;
    testingDoc["state"] = "ENERGIZADO";
    sendJsonResponse(testingDoc);

    aguardarBotaoJiga(
        "TESTE Contato #1 - COM-N#: Relé ENERGIZADO. Conecte o multímetro "
        "entre COM e o contato N# (NF ou NA) e pressione o botão");

    if (!deviceConnected)
        return;

    // Realiza medição
    float resEnergizado = medirResistencia();

    // Envia resultado sem avaliação de aprovação
    resultDoc["testIndex"] = testeAtual;
    resultDoc["contato"] = "COM-N# 1";
    resultDoc["estado"] = "ENERGIZADO";
    resultDoc["resistencia"] =
        (resEnergizado > LIMITE_RESISTENCIA_ABERTO || resEnergizado < 0)
            ? "ABERTO"
            : String(resEnergizado, 3);
    resultDoc["esperado"] = "VARIÁVEL";
    resultDoc["passou"] = true;  // Sempre passa no teste especial
    sendJsonResponse(resultDoc);

    // --- FINALIZAÇÃO ---
    Serial.println("\n=== FINALIZANDO TESTE ESPECIAL ===");
    if (config.tipoAcionamento == "TIPO_DC") {
        digitalWrite(RELAY_DC, 0);
    } else {  // TIPO_AC
        digitalWrite(RELAY_AC, 0);
    }

    reset_output();

    StaticJsonDocument<100> completeDoc;
    completeDoc["status"] = "test_complete";
    sendJsonResponse(completeDoc);

    Serial.println("=== TESTE ESPECIAL FINALIZADO ===\n");
}

/**
 * @brief Executa teste configurável para múltiplos contatos
 *
 * Nova sequência alternada para um relé de 5 terminais (2 contatos):
 * 1. COM-NF1 DESENERGIZADO (deve ter baixa resistência ~0Ω)
 * 2. COM-NF1 ENERGIZADO (deve estar aberto ~∞Ω)
 * 3. COM-NA1 DESENERGIZADO (deve estar aberto ~∞Ω)
 * 4. COM-NA1 ENERGIZADO (deve ter baixa resistência ~0Ω)
 * 5. COM-NF2 DESENERGIZADO (deve ter baixa resistência ~0Ω)
 * 6. COM-NF2 ENERGIZADO (deve estar aberto ~∞Ω)
 * 7. COM-NA2 DESENERGIZADO (deve estar aberto ~∞Ω)
 * 8. COM-NA2 ENERGIZADO (deve ter baixa resistência ~0Ω)
 *
 * Ordem alternada: testa completamente cada contato (des/ene) antes de passar
 * ao próximo Benefício: menos trocas de contatos, mais prático para o operador
 */
void executarTesteConfiguravel(const TestConfig& config) {
    Serial.println("=== INICIANDO TESTE DE RELÉ CONFIGURÁVEL ===");
    Serial.println("Configuração:");
    Serial.println("- Tipo de Acionamento: " + config.tipoAcionamento);
    Serial.println("- Quantidade de Contatos: " +
                   String(config.quantidadeContatos));
    Serial.println(
        "==========================================================");

    // Envia status inicial do teste
    StaticJsonDocument<200> statusDoc;
    statusDoc["status"] = "test_starting";
    statusDoc["message"] = "Iniciando teste do módulo...";
    sendJsonResponse(statusDoc);

    // Verifica se é o caso especial de apenas 1 contato
    if (config.quantidadeContatos == 1) {
        Serial.println("=== EXECUTANDO TESTE ESPECIAL (1 CONTATO) ===");
        executarTesteEspecialUmContato(config);
        return;
    }

    // Calcula quantos contatos NF e NA temos
    // Se o usuário inseriu 2 contatos = 1 NF + 1 NA
    // Se o usuário inseriu 4 contatos = 2 NF + 2 NA
    // Se o usuário inseriu 6 contatos = 3 NF + 3 NA
    int numContatosNF = config.quantidadeContatos / 2;
    int numContatosNA = config.quantidadeContatos / 2;
    int totalTestes =
        config.quantidadeContatos * 2;  // Cada contato testado em 2 estados
    int testeAtual = 0;

    Serial.println("=== EXECUTANDO TESTE PADRÃO ===");
    Serial.println("Número de contatos NF: " + String(numContatosNF));
    Serial.println("Número de contatos NA: " + String(numContatosNA));
    Serial.println("Total de testes: " + String(totalTestes));

    // Envia mensagem inicial
    StaticJsonDocument<200> initDoc;
    initDoc["status"] = "test_init";
    initDoc["totalTests"] = totalTestes;
    sendJsonResponse(initDoc);

    // --- NOVA ORDEM ALTERNADA: TESTA CADA CONTATO COMPLETAMENTE ---
    Serial.println("\n=== EXECUTANDO TESTES EM ORDEM ALTERNADA ===");

    // Calcula o número máximo de contatos para iterar
    int maxContatos =
        (numContatosNF > numContatosNA) ? numContatosNF : numContatosNA;

    for (int i = 0; i < maxContatos; i++) {
        if (!deviceConnected)
            return;

        // --- CONTATO NF DESENERGIZADO ---
        if (i < numContatosNF) {
            StaticJsonDocument<200> testingDoc;
            testingDoc["status"] = "test_current";
            testingDoc["testIndex"] = testeAtual;
            testingDoc["pair"] = i;
            testingDoc["state"] = "DESENERGIZADO";
            sendJsonResponse(testingDoc);

            String contato = "COM-NF" + String(i + 1);
            aguardarBotaoJiga(
                "TESTE " + contato +
                ": Relé DESENERGIZADO. Conecte o multímetro entre COM e NF" +
                String(i + 1) + " e pressione o botão");

            if (!deviceConnected)
                return;

            // Realiza medição
            float resistencia = medirResistencia();

            // Envia resultado - NF desenergizado deve ter baixa resistência
            StaticJsonDocument<200> resultDoc;
            resultDoc["status"] = "test_result";
            resultDoc["testIndex"] = testeAtual;
            resultDoc["contato"] = contato;
            resultDoc["estado"] = "DESENERGIZADO";
            resultDoc["resistencia"] =
                (resistencia > LIMITE_RESISTENCIA_ABERTO || resistencia < 0)
                    ? "ABERTO"
                    : String(resistencia, 3);
            resultDoc["esperado"] = "BAIXA";
            resultDoc["passou"] = (resistencia >= 0 && resistencia <= 10.0);
            sendJsonResponse(resultDoc);

            testeAtual++;
        }

        // --- CONTATO NF ENERGIZADO ---
        if (i < numContatosNF) {
            // Aciona o relé antes do teste energizado
            if (config.tipoAcionamento == "TIPO_DC") {
                digitalWrite(RELAY_DC, 1);
                state_RGB('O');  // Azul - energizado
            } else {
                digitalWrite(RELAY_AC, 1);
                state_RGB('O');  // Azul - energizado
            }
            delay(500);  // Tempo para estabilização

            StaticJsonDocument<200> testingDoc;
            testingDoc["status"] = "test_current";
            testingDoc["testIndex"] = testeAtual;
            testingDoc["pair"] = i;
            testingDoc["state"] = "ENERGIZADO";
            sendJsonResponse(testingDoc);

            String contato = "COM-NF" + String(i + 1);
            aguardarBotaoJiga(
                "TESTE " + contato +
                ": Relé ENERGIZADO. Conecte o multímetro entre COM e NF" +
                String(i + 1) + " e pressione o botão");

            if (!deviceConnected)
                return;

            // Realiza medição
            float resistencia = medirResistencia();

            // Envia resultado - NF energizado deve estar aberto
            StaticJsonDocument<200> resultDoc;
            resultDoc["status"] = "test_result";
            resultDoc["testIndex"] = testeAtual;
            resultDoc["contato"] = contato;
            resultDoc["estado"] = "ENERGIZADO";
            resultDoc["resistencia"] =
                (resistencia > LIMITE_RESISTENCIA_ABERTO || resistencia < 0)
                    ? "ABERTO"
                    : String(resistencia, 3);
            resultDoc["esperado"] = "ABERTO";
            resultDoc["passou"] =
                (resistencia > LIMITE_RESISTENCIA_ABERTO || resistencia < 0);
            sendJsonResponse(resultDoc);

            testeAtual++;

            // Desaciona o relé após o teste
            if (config.tipoAcionamento == "TIPO_DC") {
                digitalWrite(RELAY_DC, 0);
            } else {
                digitalWrite(RELAY_AC, 0);
            }
            reset_output();
            delay(500);  // Tempo para estabilização
        }

        // --- CONTATO NA DESENERGIZADO ---
        if (i < numContatosNA) {
            StaticJsonDocument<200> testingDoc;
            testingDoc["status"] = "test_current";
            testingDoc["testIndex"] = testeAtual;
            testingDoc["pair"] = i;
            testingDoc["state"] = "DESENERGIZADO";
            sendJsonResponse(testingDoc);

            String contato = "COM-NA" + String(i + 1);
            aguardarBotaoJiga(
                "TESTE " + contato +
                ": Relé DESENERGIZADO. Conecte o multímetro entre COM e NA" +
                String(i + 1) + " e pressione o botão");

            if (!deviceConnected)
                return;

            // Realiza medição
            float resistencia = medirResistencia();

            // Envia resultado - NA desenergizado deve estar aberto
            StaticJsonDocument<200> resultDoc;
            resultDoc["status"] = "test_result";
            resultDoc["testIndex"] = testeAtual;
            resultDoc["contato"] = contato;
            resultDoc["estado"] = "DESENERGIZADO";
            resultDoc["resistencia"] =
                (resistencia > LIMITE_RESISTENCIA_ABERTO || resistencia < 0)
                    ? "ABERTO"
                    : String(resistencia, 3);
            resultDoc["esperado"] = "ABERTO";
            resultDoc["passou"] =
                (resistencia > LIMITE_RESISTENCIA_ABERTO || resistencia < 0);
            sendJsonResponse(resultDoc);

            testeAtual++;
        }

        // --- CONTATO NA ENERGIZADO ---
        if (i < numContatosNA) {
            // Aciona o relé antes do teste energizado
            if (config.tipoAcionamento == "TIPO_DC") {
                digitalWrite(RELAY_DC, 1);
                state_RGB('O');  // Azul - energizado
            } else {
                digitalWrite(RELAY_AC, 1);
                state_RGB('O');  // Azul - energizado
            }
            delay(500);  // Tempo para estabilização

            StaticJsonDocument<200> testingDoc;
            testingDoc["status"] = "test_current";
            testingDoc["testIndex"] = testeAtual;
            testingDoc["pair"] = i;
            testingDoc["state"] = "ENERGIZADO";
            sendJsonResponse(testingDoc);

            String contato = "COM-NA" + String(i + 1);
            aguardarBotaoJiga(
                "TESTE " + contato +
                ": Relé ENERGIZADO. Conecte o multímetro entre COM e NA" +
                String(i + 1) + " e pressione o botão");

            if (!deviceConnected)
                return;

            // Realiza medição
            float resistencia = medirResistencia();

            // Envia resultado - NA energizado deve ter baixa resistência
            StaticJsonDocument<200> resultDoc;
            resultDoc["status"] = "test_result";
            resultDoc["testIndex"] = testeAtual;
            resultDoc["contato"] = contato;
            resultDoc["estado"] = "ENERGIZADO";
            resultDoc["resistencia"] =
                (resistencia > LIMITE_RESISTENCIA_ABERTO || resistencia < 0)
                    ? "ABERTO"
                    : String(resistencia, 3);
            resultDoc["esperado"] = "BAIXA";
            resultDoc["passou"] = (resistencia >= 0 && resistencia <= 10.0);
            sendJsonResponse(resultDoc);

            testeAtual++;

            // Desaciona o relé após o teste
            if (config.tipoAcionamento == "TIPO_DC") {
                digitalWrite(RELAY_DC, 0);
            } else {
                digitalWrite(RELAY_AC, 0);
            }
            reset_output();
            delay(500);  // Tempo para estabilização
        }
    }

    // --- FINALIZAÇÃO ---
    Serial.println("\n=== FINALIZANDO TESTE ===");

    // Garante que o relé está desacionado
    if (config.tipoAcionamento == "TIPO_DC") {
        digitalWrite(RELAY_DC, 0);
    } else {
        digitalWrite(RELAY_AC, 0);
    }
    reset_output();

    StaticJsonDocument<100> completeDoc;
    completeDoc["status"] = "test_complete";
    sendJsonResponse(completeDoc);

    Serial.println("=== TESTE FINALIZADO ===\n");
}

// =================================================================
// PROCESSAMENTO DE COMANDOS
// =================================================================

void handleCommand(const JsonDocument& doc) {
    String comando = doc["comando"];
    Serial.println("=== COMANDO RECEBIDO ===");
    Serial.println("Comando: " + comando);

    if (comando == "calibrar") {
        calibrate();
    } else if (comando == "iniciar_teste") {
        Serial.println("=== PROCESSANDO COMANDO INICIAR_TESTE ===");

        TestConfig config;
        config.tipoAcionamento = doc["tipoAcionamento"].as<String>();
        config.quantidadeContatos = doc["quantidadeContatos"];
        config.calibracao = doc["calibracao"].as<JsonArrayConst>();

        Serial.println("Configuração do teste:");
        Serial.println("- Tipo de Acionamento: " + config.tipoAcionamento);
        Serial.println("- Quantidade de Contatos: " +
                       String(config.quantidadeContatos));
        Serial.println("- Tamanho array calibração: " +
                       String(config.calibracao.size()));

        if (config.calibracao.size() > 0) {
            res_cal = config.calibracao[0].as<float>();
            Serial.println("Valor de calibração carregado: " +
                           String(res_cal, 6));
        } else {
            Serial.println("ERRO: Nenhum valor de calibração encontrado!");
            sendError("Erro: Dados de calibração não encontrados");
            return;
        }

        executarTesteConfiguravel(config);
    } else if (comando == "confirmar_etapa") {
        g_aguardandoConfirmacao = false;
    } else {
        sendError("Comando não reconhecido: " + comando);
    }
}

// =================================================================
// CLASSES DE CALLBACKS BLE
// =================================================================

class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        String value = pCharacteristic->getValue().c_str();
        if (value.length() > 0) {
            g_comandoJson = value;
            g_comandoRecebidoFlag = true;
            Serial.println("Comando recebido: " + g_comandoJson);
        }
    }
};

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("Cliente conectado via BLE");

        // Resetar flags de heartbeat
        lastHeartbeat = millis();
        lastConnectionCheck = millis();
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Cliente desconectado");
        reset_output();

        // Resetar estado de aguardo se necessário
        g_aguardandoConfirmacao = false;
        g_comandoRecebidoFlag = false;

        // Pequena pausa antes de reiniciar o advertising
        delay(500);

        // Reinicia o advertising automaticamente
        BLEDevice::startAdvertising();
        Serial.println(
            "Advertising reiniciado - Dispositivo disponível novamente");
    }
};

// =================================================================
// SETUP E LOOP PRINCIPAL
// =================================================================

void setup() {
    Serial.begin(9600);
    Serial.println("=== JIGA DE TESTE DE RELÉS - VERSÃO FINAL ===");

    // Inicializa I2C e ADS1115
    Wire.begin();
    ADS.begin();
    ADS.setGain(1);

    // Configuração dos pinos
    pinMode(LED_CONT, OUTPUT);
    pinMode(RGB_RED, OUTPUT);
    pinMode(RGB_GREEN, OUTPUT);
    pinMode(RGB_BLUE, OUTPUT);
    pinMode(RELAY_AC, OUTPUT);
    pinMode(RELAY_DC, OUTPUT);
    pinMode(BUTTON, INPUT);

    // Estado inicial
    reset_output();

    // Inicializa BLE
    BLEDevice::init("Jiga-Teste-Reles");

    // Configurações para maior estabilidade
    BLEDevice::setMTU(512);
    BLEDevice::setPower(ESP_PWR_LVL_P9);

    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService* pService = pServer->createService(BLE_SERVICE_UUID);

    // Característica para receber comandos (WebApp -> ESP32)
    BLECharacteristic* pReceiveCharacteristic = pService->createCharacteristic(
        BLE_RECEIVE_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_WRITE_NR);
    pReceiveCharacteristic->setCallbacks(new MyCharacteristicCallbacks());

    // Característica para enviar respostas (ESP32 -> WebApp)
    pSendCharacteristic = pService->createCharacteristic(
        BLE_SEND_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
    pSendCharacteristic->addDescriptor(new BLE2902());

    pService->start();

    // Inicia advertising com configurações otimizadas para estabilidade
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);

    // Configurações de advertising otimizadas
    pAdvertising->setMinPreferred(0x06);  // 7.5ms - intervalo mínimo de conexão
    pAdvertising->setMaxPreferred(
        0x12);  // 22.5ms - intervalo máximo de conexão
    pAdvertising->setAdvertisementType(ADV_TYPE_IND);
    pAdvertising->setMinInterval(
        0x20);  // 20ms - intervalo mínimo de advertising
    pAdvertising->setMaxInterval(
        0x40);  // 40ms - intervalo máximo de advertising

    BLEDevice::startAdvertising();

    Serial.println(
        "Dispositivo BLE iniciado com configurações otimizadas - Aguardando "
        "conexão...");
    Serial.println("Nome: Jiga-Teste-Reles");
    Serial.println("UUID do Serviço: " + String(BLE_SERVICE_UUID));
    if (ADS.isConnected()) {
        Serial.println("ADS1115 conectado com sucesso");
        state_RGB('O');  // Azul - pronto para conectar
    } else {
        Serial.println("ERRO: ADS1115 não encontrado!");
        state_RGB('R');  // Vermelho - erro
    }
}

void loop() {
    // Verifica estado da conexão
    checkConnection();

    // Processa comandos recebidos via BLE
    if (g_comandoRecebidoFlag) {
        g_comandoRecebidoFlag = false;

        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, g_comandoJson);

        if (error) {
            Serial.println("Erro ao fazer parsing do JSON: " +
                           String(error.c_str()));
            sendError("JSON inválido: " + String(error.c_str()));
        } else {
            // Verifica se ainda está conectado antes de processar
            if (deviceConnected) {
                handleCommand(doc);
            } else {
                Serial.println("Comando ignorado - dispositivo desconectado");
            }
        }
    }

    // Delay otimizado para não sobrecarregar o sistema
    delay(50);
}
