/*
 * Jiga de Teste de Relés - Versão Final
 * Combina hardware real do miliohmimetrov2.ino com comunicação BLE do
 * espsolo.ino
 *
 * MELHORIAS DE PERFORMANCE DE MEMÓRIA IMPLEMENTADAS:
 * - Substituição de objetos String por char arrays para reduzir uso de heap
 * - Uso de buffers estáticos com tamanhos padronizados e seguros
 * - Função auxiliar para conversão padronizada de resistência para string
 * - Validação de tamanho de strings com terminação nula garantida
 * - Uso de strncpy() e snprintf() para operações seguras de string
 * - Redução significativa da fragmentação de memória heap
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

// --- Tamanhos de Buffers ---
#define BUFFER_SIZE_COMANDO 512
#define BUFFER_SIZE_MENSAGEM 128
#define BUFFER_SIZE_CONTATO 16
#define BUFFER_SIZE_RESISTENCIA 16
#define BUFFER_SIZE_DEBUG 64

// --- Controle de Debug ---
#define DEBUG_ENABLED 0  // 0 = Desabilitado, 1 = Habilitado

// Macro para debug condicional
#if DEBUG_ENABLED
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

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
char g_comandoJson[BUFFER_SIZE_COMANDO];

// Variáveis para estabilidade da conexão
unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL =
    5000;  // 5 segundos - reduzido para manter conexão
unsigned long lastConnectionCheck = 0;
const unsigned long CONNECTION_CHECK_INTERVAL =
    1000;  // 1 segundo - mais frequente durante medições
bool connectionLost = false;

// Variáveis para otimização BLE
unsigned long lastDataSent = 0;
const unsigned long MIN_DATA_INTERVAL = 50;  // Mínimo 50ms entre envios
uint8_t retryCount = 0;
const uint8_t MAX_RETRY_COUNT = 5;  // Mais tentativas

// Buffer para reduzir fragmentação
char jsonBuffer[512];

// Variáveis de medição
float res[2][8] = {0.0};
float res_cal = 0.0;
int value = 0;
int num = 0;
bool relay_state = false;

// Estrutura para armazenar a configuração do teste recebida da WebApp
struct TestConfig {
    char tipoAcionamento[16];  // "TIPO_DC" ou "TIPO_AC"
    int quantidadeContatos;    // Número total de contatos a testar (NF + NA)
    JsonArrayConst calibracao;
};

// =================================================================
// FUNÇÕES AUXILIARES PARA CONVERSÃO DE DADOS
// =================================================================

/**
 * @brief Converte valor de resistência para string padronizada
 * @param resistencia Valor da resistência em ohms
 * @param buffer Buffer de destino para a string
 * @param bufferSize Tamanho do buffer
 */
void resistenciaParaString(float resistencia, char* buffer, size_t bufferSize) {
    if (resistencia > LIMITE_RESISTENCIA_ABERTO || resistencia < 0) {
        strncpy(buffer, "ABERTO", bufferSize - 1);
    } else {
        snprintf(buffer, bufferSize, "%.3f", resistencia);
    }
    buffer[bufferSize - 1] = '\0';  // Garante terminação nula
}

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
        ADS.setDataRate(4);  // 4 = medium - mais rápido que 0

        int16_t val_01 = ADS.readADC_Differential_0_1();

        // Verificação rápida de conexão durante medição
        if (!deviceConnected) {
            return -1.0;
        }

        int16_t val_13 = ADS.readADC_Differential_1_3();
        float volts_ref = ADS.toVoltage(val_01);
        float volts = ADS.toVoltage(val_13);

        // Validação dos valores lidos
        if (isnan(volts_ref) || isnan(volts) || isinf(volts_ref) ||
            isinf(volts)) {
            return -1.0;  // Retorna valor de erro
        }

        // Verifica se volts_ref é muito pequeno - mais tolerante para medições
        // normais
        if (abs(volts_ref) < 0.00001) {  // Mais tolerante que antes
            return -1.0;                 // Retorna valor de erro
        }

        float resistencia = (res_ref * volts) / volts_ref;

        // Validação do resultado
        if (isnan(resistencia) || isinf(resistencia)) {
            return -1.0;  // Retorna valor de erro
        }

        return resistencia;
    } else {
        return -1.0;  // Retorna valor de erro
    }
}

void action_relay(int relay_action) {
    relay_state = !relay_state;
    digitalWrite(relay_action, relay_state);

    // Usar arrays de caracteres para debug
    char relay_type[8];
    strcpy(relay_type, (relay_action == RELAY_DC) ? "DC" : "AC");

    char relay_status[16];
    strcpy(relay_status, relay_state ? "ENERGIZADO" : "DESENERGIZADO");

    DEBUG_PRINT("Relé ");
    DEBUG_PRINT(relay_type);
    DEBUG_PRINT(" ");
    DEBUG_PRINTLN(relay_status);
}

// =================================================================
// FUNÇÕES DE COMUNICAÇÃO BLE
// =================================================================

bool sendJsonResponse(const JsonDocument& doc) {
    if (!deviceConnected || !pSendCharacteristic) {
        return false;
    }

    // Controle de taxa de envio
    unsigned long currentTime = millis();
    if (currentTime - lastDataSent < MIN_DATA_INTERVAL) {
        delay(MIN_DATA_INTERVAL - (currentTime - lastDataSent));
    }

    for (uint8_t attempt = 0; attempt < MAX_RETRY_COUNT; attempt++) {
        try {
            // Usa buffer estático para reduzir fragmentação de memória
            serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));

            // Verifica se ainda está conectado
            if (!deviceConnected) {
                return false;
            }

            pSendCharacteristic->setValue(jsonBuffer);
            pSendCharacteristic->notify();

            lastDataSent = millis();
            return true;

        } catch (...) {
            delay(50 * (attempt + 1));  // Delay crescente entre tentativas
            if (!checkConnection()) {
                return false;
            }
        }
    }

    return false;
}

void sendError(const char* message) {
    StaticJsonDocument<200> doc;
    doc["status"] = "error";
    doc["message"] = message;
    sendJsonResponse(doc);
}

void sendHeartbeat() {
    // Só envia heartbeat se não enviou dados recentemente
    if (millis() - lastDataSent > HEARTBEAT_INTERVAL / 2) {
        StaticJsonDocument<100> doc;
        doc["status"] = "heartbeat";
        doc["timestamp"] = millis();
        sendJsonResponse(doc);
    }
}

bool checkConnection() {
    unsigned long currentTime = millis();

    // Verifica se a conexão mudou de estado
    if (deviceConnected != oldDeviceConnected) {
        if (deviceConnected) {
            DEBUG_PRINTLN("Conexão estabelecida");
            state_RGB('B');  // Verde - conectado
            connectionLost = false;
            retryCount = 0;  // Reset contador de tentativas
        } else {
            DEBUG_PRINTLN("Conexão perdida");
            reset_output();
            connectionLost = true;
        }
        oldDeviceConnected = deviceConnected;
    }

    // Envia heartbeat periodicamente (somente se necessário)
    if (deviceConnected && (currentTime - lastHeartbeat > HEARTBEAT_INTERVAL)) {
        // Verifica se realmente precisa enviar heartbeat
        if (currentTime - lastDataSent > HEARTBEAT_INTERVAL / 2) {
            sendHeartbeat();
        }
        lastHeartbeat = currentTime;
    }

    return deviceConnected;
}

/**
 * @brief Aguarda o usuário pressionar o botão físico na jiga
 * Envia status para o WebApp e aguarda confirmação física
 */
void aguardarBotaoJiga(const char* mensagem = "") {
    DEBUG_PRINTLN(">>> Aguardando botão da jiga...");

    // Envia prompt para a WebApp
    StaticJsonDocument<300> promptDoc;
    promptDoc["status"] = "prompt";
    if (strlen(mensagem) > 0) {
        promptDoc["message"] = mensagem;
    } else {
        promptDoc["message"] = "Pressione o botão na jiga para continuar";
    }

    if (!sendJsonResponse(promptDoc)) {
        DEBUG_PRINTLN("Falha ao enviar prompt - conexão instável");
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
                DEBUG_PRINTLN("Conexão perdida durante aguardo do botão");
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
        DEBUG_PRINTLN("Falha ao enviar confirmação do botão");
    }

    delay(500);  // Debounce
    DEBUG_PRINTLN(">>> Botão pressionado! Continuando...");
}

/**
 * @brief Aguarda confirmação da WebApp (usado em alguns casos específicos)
 */
void aguardarConfirmacaoWebApp() {
    DEBUG_PRINTLN(">>> Aguardando confirmação da WebApp...");
    g_aguardandoConfirmacao = true;

    unsigned long startTime = millis();
    while (g_aguardandoConfirmacao) {
        delay(50);

        // Timeout de 30 segundos
        if (millis() - startTime > 30000) {
            DEBUG_PRINTLN("Timeout aguardando confirmação da WebApp");
            g_aguardandoConfirmacao = false;
            return;
        }

        // Verifica conexão
        if (!checkConnection()) {
            DEBUG_PRINTLN("Conexão perdida durante aguardo de confirmação");
            g_aguardandoConfirmacao = false;
            return;
        }
    }

    DEBUG_PRINTLN(">>> Confirmação recebida! Continuando...");
}

// =================================================================
// FUNÇÕES DE MEDIÇÃO E CALIBRAÇÃO
// =================================================================

void calibrate() {
    DEBUG_PRINTLN("=== INICIANDO CALIBRAÇÃO ===");

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
        DEBUG_PRINTLN("Conexão perdida durante calibração");
        return;
    }

    // Envia status de processamento
    statusDoc["status"] = "calibration_processing";
    statusDoc["message"] = "Realizando medições de calibração...";
    sendJsonResponse(statusDoc);

    state_RGB('R');  // Vermelho - processando

    res_cal = 0.0;
    int leituras_validas = 0;
    int tentativas_max = MEAN * 3;  // Permite até 3x mais tentativas
    int tentativas = 0;

    while (leituras_validas < MEAN && tentativas < tentativas_max) {
        // Verifica conexão mais frequentemente
        if (tentativas % 5 == 0 && !checkConnection()) {
            state_RGB('R');  // Vermelho - erro
            delay(500);
            reset_output();
            return;
        }

        float leitura = get_res();
        tentativas++;

        if (leitura >= 0.0) {  // Leitura válida (não é -1.0)
            res_cal += leitura;
            leituras_validas++;
        } else {
            // Se muitas leituras falharam, notifica o usuário
            if (tentativas % 20 == 0) {
                StaticJsonDocument<200> warningDoc;
                warningDoc["status"] = "calibration_warning";
                warningDoc["message"] =
                    "Dificuldade na leitura - verifique as conexões";
                sendJsonResponse(warningDoc);
            }
        }

        delay(50);  // Delay menor entre leituras
    }

    // Verifica se conseguiu leituras suficientes
    if (leituras_validas < MEAN) {
        state_RGB('R');  // Vermelho - erro

        StaticJsonDocument<200> errorDoc;
        errorDoc["status"] = "calibration_error";
        errorDoc["message"] =
            "Falha na calibração - verifique se os fios estão conectados em "
            "curto-circuito";
        sendJsonResponse(errorDoc);

        delay(2000);
        reset_output();
        return;
    }

    res_cal = res_cal / leituras_validas;

    // Validação final do resultado
    if (isnan(res_cal) || isinf(res_cal)) {
        state_RGB('R');  // Vermelho - erro

        StaticJsonDocument<200> errorDoc;
        errorDoc["status"] = "calibration_error";
        errorDoc["message"] = "Valor de calibração inválido - tente novamente";
        sendJsonResponse(errorDoc);

        delay(2000);
        reset_output();
        return;
    }

    state_RGB('B');  // Verde - sucesso

    // Usar snprintf para conversão de float para char array
    char res_cal_str[32];
    snprintf(res_cal_str, sizeof(res_cal_str), "%.6f", res_cal);

    char leituras_str[64];
    snprintf(leituras_str, sizeof(leituras_str), "Leituras válidas: %d/%d",
             leituras_validas, tentativas);

    DEBUG_PRINT("Calibração concluída: ");
    DEBUG_PRINT(res_cal_str);
    DEBUG_PRINTLN(" Ω");
    DEBUG_PRINTLN(leituras_str);

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
    int leituras_validas = 0;
    int tentativas_max = MEAN * 2;
    int tentativas = 0;

    while (leituras_validas < MEAN && tentativas < tentativas_max) {
        // Verifica conexão antes de cada leitura
        if (!deviceConnected) {
            reset_output();
            return -1.0;  // Retorna erro se desconectado
        }

        float leitura = get_res();
        tentativas++;

        if (leitura >= 0.0) {  // Leitura válida
            resistencia += leitura;
            leituras_validas++;
        }

        delay(2);  // Delay mínimo para não sobrecarregar o ADC

        // Verifica conexão mais frequentemente
        if (tentativas % 3 == 0 && !checkConnection()) {
            reset_output();
            return -1.0;  // Retorna erro
        }
    }

    // Critério mais flexível: aceita pelo menos 70% das leituras
    int minimo_leituras = (MEAN * 7) / 10;  // 70% de 20 = 14 leituras
    if (leituras_validas < minimo_leituras) {
        state_RGB('R');  // Vermelho - erro
        delay(500);
        reset_output();
        return -1.0;  // Retorna erro
    }

    resistencia = (resistencia / leituras_validas) - res_cal;

    state_RGB('B');  // Verde - medição concluída
    delay(300);
    reset_output();

    return resistencia;
}  // =================================================================
// ROTINAS DE TESTE
// =================================================================

/**
 * @brief Executa teste especial para apenas 1 contato
 * Como não sabemos se o usuário vai testar NF ou NA, medimos COM-N# nos dois
 * estados sem avaliar se passou ou falhou, apenas registramos os valores
 */
void executarTesteEspecialUmContato(const TestConfig& config) {
    DEBUG_PRINTLN("=== TESTE ESPECIAL PARA 1 CONTATO ===");

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
    DEBUG_PRINTLN("\n=== TESTE 1: RELÉ DESENERGIZADO ===");

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

    // Converte resistência para string usando função auxiliar
    char res_str[BUFFER_SIZE_RESISTENCIA];
    resistenciaParaString(resDesenergizado, res_str, sizeof(res_str));

    // Envia resultado sem avaliação de aprovação
    StaticJsonDocument<200> resultDoc;
    resultDoc["status"] = "test_result";
    resultDoc["testIndex"] = testeAtual;
    resultDoc["contato"] = "COM-N# 1";
    resultDoc["estado"] = "DESENERGIZADO";
    resultDoc["resistencia"] = res_str;
    resultDoc["esperado"] = "VARIÁVEL";
    resultDoc["passou"] = true;  // Sempre passa no teste especial
    sendJsonResponse(resultDoc);

    testeAtual++;

    // --- ACIONAMENTO DO RELÉ ---
    DEBUG_PRINTLN("\n=== ACIONANDO O RELÉ ===");
    if (strcmp(config.tipoAcionamento, "TIPO_DC") == 0) {
        digitalWrite(RELAY_DC, 1);
        state_RGB('O');  // Azul - energizado
    } else {             // TIPO_AC
        digitalWrite(RELAY_AC, 1);
        state_RGB('O');  // Azul - energizado
    }

    delay(500);  // Tempo para estabilização

    // --- TESTE 2: ESTADO ENERGIZADO ---
    DEBUG_PRINTLN("\n=== TESTE 2: RELÉ ENERGIZADO ===");

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

    // Reutiliza o buffer res_str já declarado
    resistenciaParaString(resEnergizado, res_str, sizeof(res_str));

    // Envia resultado sem avaliação de aprovação
    resultDoc["testIndex"] = testeAtual;
    resultDoc["contato"] = "COM-N# 1";
    resultDoc["estado"] = "ENERGIZADO";
    resultDoc["resistencia"] = res_str;
    resultDoc["esperado"] = "VARIÁVEL";
    resultDoc["passou"] = true;  // Sempre passa no teste especial
    sendJsonResponse(resultDoc);

    // --- FINALIZAÇÃO ---
    DEBUG_PRINTLN("\n=== FINALIZANDO TESTE ESPECIAL ===");
    if (strcmp(config.tipoAcionamento, "TIPO_DC") == 0) {
        digitalWrite(RELAY_DC, 0);
    } else {  // TIPO_AC
        digitalWrite(RELAY_AC, 0);
    }

    reset_output();

    StaticJsonDocument<100> completeDoc;
    completeDoc["status"] = "test_complete";
    sendJsonResponse(completeDoc);

    DEBUG_PRINTLN("=== TESTE ESPECIAL FINALIZADO ===\n");
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
    DEBUG_PRINTLN("=== INICIANDO TESTE DE RELÉ CONFIGURÁVEL ===");
    DEBUG_PRINTLN("Configuração:");
    DEBUG_PRINT("- Tipo de Acionamento: ");
    DEBUG_PRINTLN(config.tipoAcionamento);
    DEBUG_PRINT("- Quantidade de Contatos: ");
    DEBUG_PRINTLN(config.quantidadeContatos);
    DEBUG_PRINTLN("==========================================================");

    // Envia status inicial do teste
    StaticJsonDocument<200> statusDoc;
    statusDoc["status"] = "test_starting";
    statusDoc["message"] = "Iniciando teste do módulo...";
    sendJsonResponse(statusDoc);

    // Verifica se é o caso especial de apenas 1 contato
    if (config.quantidadeContatos == 1) {
        DEBUG_PRINTLN("=== EXECUTANDO TESTE ESPECIAL (1 CONTATO) ===");
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

    DEBUG_PRINTLN("=== EXECUTANDO TESTE PADRÃO ===");

    char debug_str[BUFFER_SIZE_DEBUG];
    snprintf(debug_str, sizeof(debug_str), "Número de contatos NF: %d",
             numContatosNF);
    DEBUG_PRINTLN(debug_str);

    snprintf(debug_str, sizeof(debug_str), "Número de contatos NA: %d",
             numContatosNA);
    DEBUG_PRINTLN(debug_str);

    snprintf(debug_str, sizeof(debug_str), "Total de testes: %d", totalTestes);
    DEBUG_PRINTLN(debug_str);

    // Envia mensagem inicial
    StaticJsonDocument<200> initDoc;
    initDoc["status"] = "test_init";
    initDoc["totalTests"] = totalTestes;
    sendJsonResponse(initDoc);

    // --- NOVA ORDEM ALTERNADA: TESTA CADA CONTATO COMPLETAMENTE ---
    DEBUG_PRINTLN("\n=== EXECUTANDO TESTES EM ORDEM ALTERNADA ===");

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

            char contato_str[BUFFER_SIZE_CONTATO];
            snprintf(contato_str, sizeof(contato_str), "COM-NF%d", i + 1);

            char mensagem_str[BUFFER_SIZE_MENSAGEM];
            snprintf(mensagem_str, sizeof(mensagem_str),
                     "TESTE %s: Relé DESENERGIZADO. Conecte o multímetro entre "
                     "COM e NF%d e pressione o botão",
                     contato_str, i + 1);

            aguardarBotaoJiga(mensagem_str);

            if (!deviceConnected)
                return;

            // Realiza medição
            float resistencia = medirResistencia();

            // Verifica se houve erro na medição
            if (resistencia == -1.0) {
                // Em vez de parar o teste, marca como erro e continua
                StaticJsonDocument<200> resultDoc;
                resultDoc["status"] = "test_result";
                resultDoc["testIndex"] = testeAtual;
                resultDoc["contato"] = contato_str;
                resultDoc["estado"] = "DESENERGIZADO";
                resultDoc["resistencia"] = "ERRO";
                resultDoc["esperado"] = "BAIXA";
                resultDoc["passou"] = false;
                sendJsonResponse(resultDoc);

            } else {
                // Converte resistência para string usando função auxiliar
                char res_str[BUFFER_SIZE_RESISTENCIA];
                resistenciaParaString(resistencia, res_str, sizeof(res_str));

                // Envia resultado - NF desenergizado deve ter baixa resistência
                StaticJsonDocument<200> resultDoc;
                resultDoc["status"] = "test_result";
                resultDoc["testIndex"] = testeAtual;
                resultDoc["contato"] = contato_str;
                resultDoc["estado"] = "DESENERGIZADO";
                resultDoc["resistencia"] = res_str;
                resultDoc["esperado"] = "BAIXA";
                resultDoc["passou"] = (resistencia >= 0 && resistencia <= 10.0);
                sendJsonResponse(resultDoc);
            }
            testeAtual++;
        }

        // --- CONTATO NF ENERGIZADO ---
        if (i < numContatosNF) {
            // Aciona o relé antes do teste energizado
            if (strcmp(config.tipoAcionamento, "TIPO_DC") == 0) {
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

            char contato_str[BUFFER_SIZE_CONTATO];
            snprintf(contato_str, sizeof(contato_str), "COM-NF%d", i + 1);

            char mensagem_str[BUFFER_SIZE_MENSAGEM];
            snprintf(mensagem_str, sizeof(mensagem_str),
                     "TESTE %s: Relé ENERGIZADO. Conecte o multímetro entre "
                     "COM e NF%d e pressione o botão",
                     contato_str, i + 1);

            aguardarBotaoJiga(mensagem_str);

            if (!deviceConnected)
                return;

            // Realiza medição
            float resistencia = medirResistencia();

            // Converte resistência para string usando função auxiliar
            char res_str[BUFFER_SIZE_RESISTENCIA];
            resistenciaParaString(resistencia, res_str, sizeof(res_str));

            // Envia resultado - NF energizado deve estar aberto
            StaticJsonDocument<200> resultDoc;
            resultDoc["status"] = "test_result";
            resultDoc["testIndex"] = testeAtual;
            resultDoc["contato"] = contato_str;
            resultDoc["estado"] = "ENERGIZADO";
            resultDoc["resistencia"] = res_str;
            resultDoc["esperado"] = "ABERTO";
            resultDoc["passou"] =
                (resistencia > LIMITE_RESISTENCIA_ABERTO || resistencia < 0);
            sendJsonResponse(resultDoc);

            testeAtual++;

            // Desaciona o relé após o teste
            if (strcmp(config.tipoAcionamento, "TIPO_DC") == 0) {
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

            char contato_str[BUFFER_SIZE_CONTATO];
            snprintf(contato_str, sizeof(contato_str), "COM-NA%d", i + 1);

            char mensagem_str[BUFFER_SIZE_MENSAGEM];
            snprintf(mensagem_str, sizeof(mensagem_str),
                     "TESTE %s: Relé DESENERGIZADO. Conecte o multímetro entre "
                     "COM e NA%d e pressione o botão",
                     contato_str, i + 1);

            aguardarBotaoJiga(mensagem_str);

            if (!deviceConnected)
                return;

            // Realiza medição
            float resistencia = medirResistencia();

            // Converte resistência para string usando função auxiliar
            char res_str[BUFFER_SIZE_RESISTENCIA];
            resistenciaParaString(resistencia, res_str, sizeof(res_str));

            // Envia resultado - NA desenergizado deve estar aberto
            StaticJsonDocument<200> resultDoc;
            resultDoc["status"] = "test_result";
            resultDoc["testIndex"] = testeAtual;
            resultDoc["contato"] = contato_str;
            resultDoc["estado"] = "DESENERGIZADO";
            resultDoc["resistencia"] = res_str;
            resultDoc["esperado"] = "ABERTO";
            resultDoc["passou"] =
                (resistencia > LIMITE_RESISTENCIA_ABERTO || resistencia < 0);
            sendJsonResponse(resultDoc);

            testeAtual++;
        }

        // --- CONTATO NA ENERGIZADO ---
        if (i < numContatosNA) {
            // Aciona o relé antes do teste energizado
            if (strcmp(config.tipoAcionamento, "TIPO_DC") == 0) {
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

            char contato_str[BUFFER_SIZE_CONTATO];
            snprintf(contato_str, sizeof(contato_str), "COM-NA%d", i + 1);

            char mensagem_str[BUFFER_SIZE_MENSAGEM];
            snprintf(mensagem_str, sizeof(mensagem_str),
                     "TESTE %s: Relé ENERGIZADO. Conecte o multímetro entre "
                     "COM e NA%d e pressione o botão",
                     contato_str, i + 1);

            aguardarBotaoJiga(mensagem_str);

            if (!deviceConnected)
                return;

            // Realiza medição
            float resistencia = medirResistencia();

            // Converte resistência para string usando função auxiliar
            char res_str[BUFFER_SIZE_RESISTENCIA];
            resistenciaParaString(resistencia, res_str, sizeof(res_str));

            // Envia resultado - NA energizado deve ter baixa resistência
            StaticJsonDocument<200> resultDoc;
            resultDoc["status"] = "test_result";
            resultDoc["testIndex"] = testeAtual;
            resultDoc["contato"] = contato_str;
            resultDoc["estado"] = "ENERGIZADO";
            resultDoc["resistencia"] = res_str;
            resultDoc["esperado"] = "BAIXA";
            resultDoc["passou"] = (resistencia >= 0 && resistencia <= 10.0);
            sendJsonResponse(resultDoc);

            testeAtual++;

            // Desaciona o relé após o teste
            if (strcmp(config.tipoAcionamento, "TIPO_DC") == 0) {
                digitalWrite(RELAY_DC, 0);
            } else {
                digitalWrite(RELAY_AC, 0);
            }
            reset_output();
            delay(500);  // Tempo para estabilização
        }
    }

    // --- FINALIZAÇÃO ---
    DEBUG_PRINTLN("\n=== FINALIZANDO TESTE ===");

    // Garante que o relé está desacionado
    if (strcmp(config.tipoAcionamento, "TIPO_DC") == 0) {
        digitalWrite(RELAY_DC, 0);
    } else {
        digitalWrite(RELAY_AC, 0);
    }
    reset_output();

    StaticJsonDocument<100> completeDoc;
    completeDoc["status"] = "test_complete";
    sendJsonResponse(completeDoc);

    DEBUG_PRINTLN("=== TESTE FINALIZADO ===\n");
}

// =================================================================
// PROCESSAMENTO DE COMANDOS
// =================================================================

void handleCommand(const JsonDocument& doc) {
    const char* comando = doc["comando"];
    DEBUG_PRINTLN("=== COMANDO RECEBIDO ===");
    DEBUG_PRINT("Comando: ");
    DEBUG_PRINTLN(comando);

    if (strcmp(comando, "calibrar") == 0) {
        calibrate();
    } else if (strcmp(comando, "iniciar_teste") == 0) {
        DEBUG_PRINTLN("=== PROCESSANDO COMANDO INICIAR_TESTE ===");

        TestConfig config;
        // Copia string com segurança
        strncpy(config.tipoAcionamento, doc["tipoAcionamento"],
                sizeof(config.tipoAcionamento) - 1);
        config.tipoAcionamento[sizeof(config.tipoAcionamento) - 1] =
            '\0';  // Garante terminação nula

        config.quantidadeContatos = doc["quantidadeContatos"];
        config.calibracao = doc["calibracao"].as<JsonArrayConst>();

        DEBUG_PRINTLN("Configuração do teste:");
        DEBUG_PRINT("- Tipo de Acionamento: ");
        DEBUG_PRINTLN(config.tipoAcionamento);
        DEBUG_PRINT("- Quantidade de Contatos: ");
        DEBUG_PRINTLN(config.quantidadeContatos);
        DEBUG_PRINT("- Tamanho array calibração: ");
        DEBUG_PRINTLN(config.calibracao.size());

        if (config.calibracao.size() > 0) {
            res_cal = config.calibracao[0].as<float>();
            char res_cal_str[32];
            snprintf(res_cal_str, sizeof(res_cal_str), "%.6f", res_cal);
            DEBUG_PRINT("Valor de calibração carregado: ");
            DEBUG_PRINTLN(res_cal_str);
        } else {
            DEBUG_PRINTLN("ERRO: Nenhum valor de calibração encontrado!");
            sendError("Erro: Dados de calibração não encontrados");
            return;
        }

        executarTesteConfiguravel(config);
    } else if (strcmp(comando, "confirmar_etapa") == 0) {
        g_aguardandoConfirmacao = false;
    } else {
        // Usa buffer temporário para evitar overflow
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "Comando não reconhecido: %.20s",
                 comando);
        sendError(error_msg);
    }
}

// =================================================================
// CLASSES DE CALLBACKS BLE
// =================================================================

class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        String value = pCharacteristic->getValue().c_str();
        if (value.length() > 0 && value.length() < BUFFER_SIZE_COMANDO) {
            // Copia para char array com segurança
            strncpy(g_comandoJson, value.c_str(), BUFFER_SIZE_COMANDO - 1);
            g_comandoJson[BUFFER_SIZE_COMANDO - 1] =
                '\0';  // Garante terminação nula

            g_comandoRecebidoFlag = true;
            // Atualiza timestamp da última comunicação
            lastDataSent = millis();
        }
    }
};

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        DEBUG_PRINTLN("Cliente conectado via BLE");

        // Configurações otimizadas para conexão estabelecida
        unsigned long currentTime = millis();
        lastHeartbeat = currentTime;
        lastConnectionCheck = currentTime;
        lastDataSent = currentTime;
        retryCount = 0;

        // Parâmetros de conexão otimizados serão aplicados automaticamente
        // pela biblioteca BLE do ESP32
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        DEBUG_PRINTLN("Cliente desconectado");
        reset_output();

        // Resetar estado de aguardo se necessário
        g_aguardandoConfirmacao = false;
        g_comandoRecebidoFlag = false;

        // Pausa otimizada antes de reiniciar advertising
        delay(500);

        // Reinicia o advertising automaticamente com configurações otimizadas
        BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->stop();
        delay(100);
        pAdvertising->start();
        DEBUG_PRINTLN(
            "Advertising reiniciado - Dispositivo disponível novamente");
    }
};

// =================================================================
// SETUP E LOOP PRINCIPAL
// =================================================================

void setup() {
#if DEBUG_ENABLED
    Serial.begin(9600);
#endif

    DEBUG_PRINTLN("=== JIGA DE TESTE DE RELÉS - VERSÃO FINAL ===");

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

    // Inicializa BLE com configurações otimizadas
    BLEDevice::init("Jiga-Teste-Reles");

    // Configurações avançadas para estabilidade e visibilidade
    BLEDevice::setMTU(256);  // Reduzido para melhor compatibilidade
    BLEDevice::setPower(ESP_PWR_LVL_P9);  // Máxima potência para melhor alcance

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

    // Inicia advertising com configurações otimizadas para descoberta
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);

    // Configurações de advertising para máxima visibilidade
    pAdvertising->setMinPreferred(0x20);  // 32ms - intervalo mínimo de conexão
    pAdvertising->setMaxPreferred(0x40);  // 64ms - intervalo máximo de conexão
    pAdvertising->setAdvertisementType(ADV_TYPE_IND);

    // Intervalos de advertising padrão para melhor compatibilidade
    pAdvertising->setMinInterval(0x20);  // 32ms - padrão
    pAdvertising->setMaxInterval(0x40);  // 64ms - padrão

    BLEDevice::startAdvertising();

    DEBUG_PRINTLN(
        "Dispositivo BLE iniciado com configurações otimizadas - Aguardando "
        "conexão...");
    DEBUG_PRINTLN("Nome: Jiga-Teste-Reles");
    DEBUG_PRINTLN("UUID do Serviço: " + String(BLE_SERVICE_UUID));
    if (ADS.isConnected()) {
        DEBUG_PRINTLN("ADS1115 conectado com sucesso");
        state_RGB('O');  // Azul - pronto para conectar
    } else {
        DEBUG_PRINTLN("ERRO: ADS1115 não encontrado!");
        state_RGB('R');  // Vermelho - erro
    }
}

void loop() {
    // Verifica estado da conexão (menos frequente para economizar recursos)
    static unsigned long lastConnectionCheck = 0;
    if (millis() - lastConnectionCheck > CONNECTION_CHECK_INTERVAL) {
        checkConnection();
        lastConnectionCheck = millis();
    }

    // Processa comandos recebidos via BLE
    if (g_comandoRecebidoFlag) {
        g_comandoRecebidoFlag = false;

        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, g_comandoJson);

        if (error) {
            sendError("JSON inválido");
        } else {
            // Verifica se ainda está conectado antes de processar
            if (deviceConnected) {
                handleCommand(doc);
            }
        }
    }

    // Delay otimizado baseado no estado da conexão
    if (deviceConnected) {
        delay(20);  // Mais responsivo quando conectado
    } else {
        delay(100);  // Menos recursos quando desconectado
    }
}
