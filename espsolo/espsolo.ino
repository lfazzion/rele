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

// Declaração das funções
void executarTesteEspecialUmPar(const TestConfig& config);
void handleCommand(const JsonDocument& doc);

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
 * @brief SIMULAÇÃO: Pede ao usuário para inserir um valor de resistência via
 * Monitor Serial.
 * @param contato Descrição do contato sendo testado (ex: "COM - NF")
 * @param estadoRele Estado atual do relé ("REPOUSO" ou "ACIONADO")
 * @return O valor de resistência fornecido pelo usuário.
 */
float medirResistenciaSimulada(String contato, String estadoRele) {
    Serial.println("\n====================================================");
    Serial.println(">>> MEDIÇÃO DE RESISTÊNCIA - SIMULAÇÃO");
    Serial.println(">>> Contato: " + contato);
    Serial.println(">>> Estado do Relé: " + estadoRele);
    Serial.println("----------------------------------------------------");

    // Orientação para o usuário baseada na lógica do relé
    if (contato.indexOf("COM - N#") != -1) {  // Caso especial de 1 par
        Serial.println(
            ">>> CASO ESPECIAL: Não é possível determinar se é NF ou NA");
        Serial.println(">>> Meça a resistência e anote o valor para análise");
        Serial.println(
            ">>> Digite o valor medido ou pressione Enter para infinito");
    } else if (contato.indexOf("NF") != -1) {  // Contato Normalmente Fechado
        if (estadoRele == "REPOUSO") {
            Serial.println(
                ">>> ESPERADO: Resistência baixa (~0Ω) - Contato fechado");
            Serial.println(
                ">>> Digite um valor baixo (ex: 0.05) ou pressione Enter para "
                "infinito");
        } else {  // ACIONADO
            Serial.println(
                ">>> ESPERADO: Resistência infinita - Contato aberto");
            Serial.println(
                ">>> Digite um valor alto (ex: 1500) ou pressione Enter para "
                "infinito");
        }
    } else {  // Contato Normalmente Aberto (NA)
        if (estadoRele == "REPOUSO") {
            Serial.println(
                ">>> ESPERADO: Resistência infinita - Contato aberto");
            Serial.println(
                ">>> Digite um valor alto (ex: 1500) ou pressione Enter para "
                "infinito");
        } else {  // ACIONADO
            Serial.println(
                ">>> ESPERADO: Resistência baixa (~0Ω) - Contato fechado");
            Serial.println(
                ">>> Digite um valor baixo (ex: 0.05) ou pressione Enter para "
                "infinito");
        }
    }
    Serial.println("====================================================");

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

            // Se entrada vazia (apenas Enter), retorna valor infinito
            if (input.length() == 0) {
                Serial.println(
                    "[SIMULAÇÃO] Resistência infinita (contato aberto)");
                return LIMITE_RESISTENCIA_ABERTO *
                       10.0;  // Valor muito maior que o limite
            }

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
 * @brief SIMULAÇÃO: Simula o envio de um pulso para relé com trava.
 */
void pulsarReleSimulado() {
    Serial.println("====================================================");
    Serial.println("[SIMULAÇÃO] Enviando pulso para relé com trava...");
    Serial.println(">>> O relé mudará de estado (REPOUSO ↔ ACIONADO)");
    Serial.println("====================================================");
    delay(500);  // Simula tempo do pulso
}

// =================================================================
// ROTINAS PRINCIPAIS DE CALIBRAÇÃO E TESTE (USANDO FUNÇÕES SIMULADAS)
// =================================================================

/**
 * @brief Executa teste especial para apenas 1 par de contatos
 * Como não sabemos se é NF ou NA, testamos COM-N# em ambos os estados
 * sem determinar aprovação/reprovação
 */
void executarTesteEspecialUmPar(const TestConfig& config) {
    Serial.println("=== TESTE ESPECIAL PARA 1 PAR DE CONTATOS ===");
    Serial.println(
        "Como há apenas 1 par, não é possível determinar se é NF ou NA.");
    Serial.println("Testando COM-N# em ambos os estados do relé.");
    Serial.println("================================================");

    // Total de 2 testes: desenergizado e energizado
    int totalTestes = 2;
    int testeAtual = 0;

    // Envia mensagem inicial
    StaticJsonDocument<200> initDoc;
    initDoc["status"] = "test_init";
    initDoc["totalTests"] = totalTestes;
    sendJsonResponse(initDoc);

    // --- TESTE 1: ESTADO DESENERGIZADO ---
    Serial.println("\n=== TESTE 1: RELÉ DESENERGIZADO ===");

    StaticJsonDocument<200> promptDoc;
    promptDoc["status"] = "prompt";
    promptDoc["message"] =
        String("TESTE Par #1 - Contato COM-N#: ") +
        String("Relé DESENERGIZADO. Conecte o multímetro entre COM e N#. ") +
        String("Clique 'Pronto' para medir.");
    sendJsonResponse(promptDoc);

    aguardarConfirmacaoWebApp();
    if (!deviceConnected)
        return;

    // Sinaliza teste atual
    StaticJsonDocument<200> testingDoc;
    testingDoc["status"] = "test_current";
    testingDoc["testIndex"] = testeAtual;
    testingDoc["pair"] = 0;
    testingDoc["state"] = "DESENERGIZADO";
    sendJsonResponse(testingDoc);

    // Realiza medição
    float resDesenergizado =
        medirResistenciaSimulada("COM - N#", "DESENERGIZADO") -
        config.calibracao[0].as<float>();

    // Envia resultado sem avaliação de aprovação/reprovação
    StaticJsonDocument<200> resultDoc;
    resultDoc["status"] = "test_result";
    resultDoc["testIndex"] = testeAtual;
    resultDoc["par"] = "COM-N# 1";
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
    if (config.tipoAcionamento == "TIPO_PADRAO") {
        Serial.println(
            "[SIMULAÇÃO] Relé LIGADO (aplicando tensão contínua na bobina).");
    } else {
        Serial.println(
            "[SIMULAÇÃO] Aplicando pulso de acionamento (relé com trava).");
        pulsarReleSimulado();
    }

    // --- TESTE 2: ESTADO ENERGIZADO ---
    Serial.println("\n=== TESTE 2: RELÉ ENERGIZADO ===");

    promptDoc["status"] = "prompt";
    promptDoc["message"] =
        String("TESTE Par #1 - Contato COM-N#: ") +
        String("Relé ENERGIZADO. Conecte o multímetro entre COM e N#. ") +
        String("Clique 'Pronto' para medir.");
    sendJsonResponse(promptDoc);

    aguardarConfirmacaoWebApp();
    if (!deviceConnected)
        return;

    // Sinaliza teste atual
    testingDoc["testIndex"] = testeAtual;
    testingDoc["pair"] = 0;
    testingDoc["state"] = "ENERGIZADO";
    sendJsonResponse(testingDoc);

    // Realiza medição
    float resEnergizado = medirResistenciaSimulada("COM - N#", "ENERGIZADO") -
                          config.calibracao[0].as<float>();

    // Envia resultado sem avaliação de aprovação/reprovação
    resultDoc["testIndex"] = testeAtual;
    resultDoc["par"] = "COM-N# 1";
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
    if (config.tipoAcionamento == "TIPO_TRAVAMENTO") {
        Serial.println(
            "[SIMULAÇÃO] Aplicando pulso de desacionamento (relé com trava).");
        pulsarReleSimulado();
    }
    Serial.println("[SIMULAÇÃO] Relé retornado ao estado DESENERGIZADO.");

    StaticJsonDocument<100> completeDoc;
    completeDoc["status"] = "test_complete";
    sendJsonResponse(completeDoc);
    Serial.println("=== TESTE ESPECIAL FINALIZADO ===\n");
}

void executarTesteConfiguravel(const TestConfig& config) {
    Serial.println(
        "=== INICIANDO TESTE DE RELÉ COM CONFIRMAÇÃO (SIMULADO) ===");
    Serial.println("Configuração:");
    Serial.println("- Tipo de Acionamento: " + config.tipoAcionamento);
    Serial.println("- Quantidade de Pares: " + String(config.quantidadePares));
    Serial.println(
        "==========================================================");

    // Verifica se é o caso especial de apenas 1 par
    if (config.quantidadePares == 1) {
        executarTesteEspecialUmPar(config);
        return;
    }

    // Calcula o número total de testes
    // Para N contatos: cada contato tem 2 testes (desenergizado e energizado)
    int totalTestes =
        config.quantidadePares * 2;  // N contatos × 2 testes por contato
    int testeAtual = 0;

    // Envia mensagem inicial para manter todos os indicadores em vermelho
    StaticJsonDocument<200> initDoc;
    initDoc["status"] = "test_init";
    initDoc["totalTests"] = totalTestes;
    sendJsonResponse(initDoc);

    // --- FASE 1: ESTADO DESENERGIZADO (TODOS OS CONTATOS) ---
    Serial.println("\n=== FASE 1: RELÉ DESENERGIZADO (TODOS OS CONTATOS) ===");

    for (int i = 0; i < config.quantidadePares; i++) {
        // Determina se é NF ou NA baseado no índice
        bool isNF = (i % 2 == 0);  // Par = NF, Ímpar = NA
        String tipoContato = isNF ? "NF" : "NA";
        int numeroContato = (i / 2) + 1;  // Número do contato (1, 2, 3...)

        // ========== TESTE DO CONTATO DESENERGIZADO ==========
        StaticJsonDocument<200> promptDoc;
        promptDoc["status"] = "prompt";
        promptDoc["message"] =
            String("TESTE ") + tipoContato + String(" ") +
            String(numeroContato) + String(" (COM-") + tipoContato +
            String("): ") +
            String("Relé em REPOUSO. Clique 'Pronto' para medir.");
        sendJsonResponse(promptDoc);

        aguardarConfirmacaoWebApp();
        if (!deviceConnected)
            return;

        // Sinaliza qual teste está sendo executado (piscando)
        StaticJsonDocument<200> testingDoc;
        testingDoc["status"] = "test_current";
        testingDoc["testIndex"] = testeAtual;
        testingDoc["pair"] = i;
        testingDoc["state"] = "REPOUSO";
        sendJsonResponse(testingDoc);

        // Realiza a medição do contato em repouso
        float resContato =
            medirResistenciaSimulada("COM - " + tipoContato, "REPOUSO") -
            config.calibracao[i].as<float>();

        // Avalia se o teste passou baseado no tipo de contato
        bool testeOK;
        String resistenciaStr;
        String esperado;

        if (isNF) {
            // NF em repouso deve ter resistência baixa
            testeOK =
                (resContato <= LIMITE_RESISTENCIA_ABERTO && resContato >= 0);
            resistenciaStr = testeOK ? String(resContato, 3) : "ABERTO";
            esperado = "BAIXA (~0Ω)";
        } else {
            // NA em repouso deve ter resistência infinita
            testeOK =
                (resContato > LIMITE_RESISTENCIA_ABERTO || resContato < 0);
            resistenciaStr = testeOK ? "ABERTO" : String(resContato, 3);
            esperado = "INFINITA";
        }

        // Envia o resultado
        StaticJsonDocument<200> resultDoc;
        resultDoc["status"] = "test_result";
        resultDoc["testIndex"] = testeAtual;
        resultDoc["par"] = tipoContato + String(" ") + String(numeroContato);
        resultDoc["estado"] = "REPOUSO";
        resultDoc["resistencia"] = resistenciaStr;
        resultDoc["esperado"] = esperado;
        resultDoc["passou"] = testeOK;
        sendJsonResponse(resultDoc);

        testeAtual++;
    }

    // --- ACIONAMENTO DO RELÉ ---
    Serial.println("\n=== ACIONANDO O RELÉ (ENERGIZANDO A BOBINA) ===");
    if (config.tipoAcionamento == "TIPO_PADRAO") {
        Serial.println(
            "[SIMULAÇÃO] Relé LIGADO (aplicando tensão contínua na bobina).");
    } else {
        Serial.println(
            "[SIMULAÇÃO] Aplicando pulso de acionamento (relé com trava).");
        pulsarReleSimulado();
    }

    // --- FASE 2: ESTADO ENERGIZADO (TODOS OS CONTATOS) ---
    Serial.println("\n=== FASE 2: RELÉ ENERGIZADO (TODOS OS CONTATOS) ===");

    for (int i = 0; i < config.quantidadePares; i++) {
        // Determina se é NF ou NA baseado no índice
        bool isNF = (i % 2 == 0);  // Par = NF, Ímpar = NA
        String tipoContato = isNF ? "NF" : "NA";
        int numeroContato = (i / 2) + 1;  // Número do contato (1, 2, 3...)

        // ========== TESTE DO CONTATO ENERGIZADO ==========
        StaticJsonDocument<200> promptDoc;
        promptDoc["status"] = "prompt";
        promptDoc["message"] =
            String("TESTE ") + tipoContato + String(" ") +
            String(numeroContato) + String(" (COM-") + tipoContato +
            String("): ") +
            String("Relé ENERGIZADO. Clique 'Pronto' para medir.");
        sendJsonResponse(promptDoc);

        aguardarConfirmacaoWebApp();
        if (!deviceConnected)
            return;

        // Sinaliza qual teste está sendo executado (piscando)
        StaticJsonDocument<200> testingDoc;
        testingDoc["status"] = "test_current";
        testingDoc["testIndex"] = testeAtual;
        testingDoc["pair"] = i;
        testingDoc["state"] = "ENERGIZADO";
        sendJsonResponse(testingDoc);

        // Realiza a medição do contato energizado
        float resContatoEnerizado =
            medirResistenciaSimulada("COM - " + tipoContato, "ENERGIZADO") -
            config.calibracao[i].as<float>();

        // Avalia se o teste passou baseado no tipo de contato
        bool testeOK;
        String resistenciaStr;
        String esperado;

        if (isNF) {
            // NF energizado deve ter resistência infinita
            testeOK = (resContatoEnerizado > LIMITE_RESISTENCIA_ABERTO ||
                       resContatoEnerizado < 0);
            resistenciaStr =
                testeOK ? "ABERTO" : String(resContatoEnerizado, 3);
            esperado = "INFINITA";
        } else {
            // NA energizado deve ter resistência baixa
            testeOK = (resContatoEnerizado <= LIMITE_RESISTENCIA_ABERTO &&
                       resContatoEnerizado >= 0);
            resistenciaStr =
                testeOK ? String(resContatoEnerizado, 3) : "ABERTO";
            esperado = "BAIXA (~0Ω)";
        }

        // Envia o resultado
        StaticJsonDocument<200> resultDoc;
        resultDoc["status"] = "test_result";
        resultDoc["testIndex"] = testeAtual;
        resultDoc["par"] = tipoContato + String(" ") + String(numeroContato);
        resultDoc["estado"] = "ENERGIZADO";
        resultDoc["resistencia"] = resistenciaStr;
        resultDoc["esperado"] = esperado;
        resultDoc["passou"] = testeOK;
        sendJsonResponse(resultDoc);

        testeAtual++;
    }

    // --- DESLIGAMENTO DO RELÉ ---
    Serial.println("\n=== DESLIGANDO O RELÉ (DESENERGIZANDO A BOBINA) ===");
    if (config.tipoAcionamento == "TIPO_PADRAO") {
        Serial.println(
            "[SIMULAÇÃO] Relé DESLIGADO (removendo tensão da bobina).");
    } else {
        Serial.println(
            "[SIMULAÇÃO] Aplicando pulso de desligamento (relé com trava).");
        pulsarReleSimulado();
    }

    // --- FINALIZAÇÃO DO TESTE ---
    Serial.println("\n=== FINALIZAÇÃO DOS TESTES ===");
    StaticJsonDocument<200> finalDoc;
    finalDoc["status"] = "test_complete";
    finalDoc["totalTests"] = testeAtual;
    finalDoc["testType"] = "MÚLTIPLOS_PARES";
    sendJsonResponse(finalDoc);

    Serial.println(String("Testes finalizados. Total: ") + String(testeAtual) +
                   String(" testes."));
}

void executarCalibracao(int numTerminais) {
    Serial.println("=== INICIANDO CALIBRAÇÃO (SIMULADO) ===");
    Serial.println(
        "A calibração serve para compensar a resistência dos fios e "
        "conectores.");
    Serial.println("========================================================");

    StaticJsonDocument<512> finalCalibrationDoc;
    finalCalibrationDoc["status"] = "calibration_complete";
    JsonArray valores = finalCalibrationDoc.createNestedArray("data");

    for (int i = 0; i < numTerminais; i++) {
        // Envia notificação para a interface web
        StaticJsonDocument<200> promptDoc;
        promptDoc["status"] = "prompt";
        promptDoc["message"] = String("CALIBRAÇÃO Par ") + String(i + 1) +
                               String(": ") +
                               String(
                                   "Para medir a resistência dos fios. Clique "
                                   "'Pronto'.");
        sendJsonResponse(promptDoc);

        aguardarConfirmacaoWebApp();
        if (!deviceConnected)
            return;

        // Medição de calibração (sempre em curto-circuito)
        float cal =
            medirResistenciaSimulada("Curto-circuito (fios)", "CALIBRAÇÃO");
        valores.add(cal);

        StaticJsonDocument<200> resultDoc;
        resultDoc["status"] = "calibration_result";
        resultDoc["par"] = i + 1;
        resultDoc["valor"] = cal;
        sendJsonResponse(resultDoc);

        Serial.println("Par #" + String(i + 1) + " calibrado com " +
                       String(cal, 3) + " Ω");
    }

    sendJsonResponse(finalCalibrationDoc);
    Serial.println("=== CALIBRAÇÃO CONCLUÍDA ===\n");
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
    Serial.println("   JIGA DE TESTE PARA RELÉS - SIMULAÇÃO");
    Serial.println("===============================================");
    Serial.println("Este firmware simula o teste de relés através");
    Serial.println("do Monitor Serial. Funcionamento dos relés:");
    Serial.println("");
    Serial.println("RELÉ EM REPOUSO (bobina desenergizada):");
    Serial.println("  • COM-NF: Resistência BAIXA (~0Ω)");
    Serial.println("  • COM-NA: Resistência INFINITA");
    Serial.println("");
    Serial.println("RELÉ ACIONADO (bobina energizada):");
    Serial.println("  • COM-NF: Resistência INFINITA");
    Serial.println("  • COM-NA: Resistência BAIXA (~0Ω)");
    Serial.println("");
    Serial.println("Durante a simulação:");
    Serial.println(
        "  • Digite valores baixos (ex: 0.05) para contatos fechados");
    Serial.println("  • Digite valores altos (ex: 1500) para contatos abertos");
    Serial.println("  • Pressione ENTER para simular resistência infinita");
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