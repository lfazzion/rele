document.addEventListener("DOMContentLoaded", () => {
    // --- Constantes e Configuração do Firebase ---
    const BLE_SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
    const BLE_RECEIVE_CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8"; // JS envia para esta (WRITE)
    const BLE_SEND_CHARACTERISTIC_UUID = "a4d23253-2778-436c-9c23-2c1b50d87635";    // JS recebe desta (NOTIFY)

    const firebaseConfig = {
        apiKey: "AIzaSyBBJWqqig7VM8wio5MNuv_UwF6TRGKK720",
        authDomain: "jiga-de-teste.firebaseapp.com",
        databaseURL: "https://jiga-de-teste-default-rtdb.firebaseio.com",
        projectId: "jiga-de-teste",
        storageBucket: "jiga-de-teste.appspot.com",
        messagingSenderId: "908087994475",
        appId: "1:908087994475:web:feaf5ae804b0c2838a692e",
    };

    if (!firebase.apps.length) {
        firebase.initializeApp(firebaseConfig);
    }
    const database = firebase.database();

    // --- Referências aos Elementos da UI ---
    const ui = {
        connectButton: document.getElementById("connect-button"),
        statusText: document.getElementById("status-text"),
        statusCircle: document.getElementById("status-circle"),
        appContent: document.getElementById("app-content"),
        welcomeMessage: document.getElementById("welcome-message"),
        themeToggle: document.getElementById("theme-toggle-checkbox"),
        moduleSelectionScreen: document.getElementById("module-selection-screen"),
        moduleFormScreen: document.getElementById("module-form-screen"),
        moduleManagementScreen: document.getElementById("module-management-screen"),
        progressScreen: document.getElementById("progress-screen"),
        historyScreen: document.getElementById("history-screen"),
        moduleList: document.getElementById("module-list"),
        addNewModuleButton: document.getElementById("add-new-module-button"),
        formTitle: document.getElementById("form-title"),
        moduleForm: document.getElementById("module-form"),
        moduleIdInput: document.getElementById("module-id"),
        moduleNameInput: document.getElementById("module-name"),
        moduleActuationTypeSelect: document.getElementById("module-actuation-type"),
        moduleTerminalsInput: document.getElementById("module-terminals"),
        cancelFormButton: document.getElementById("cancel-form-button"),
        saveModuleButton: document.getElementById("save-module-button"),
        backToSelectionButton: document.getElementById("back-to-selection-button"),
        managementTitle: document.getElementById("management-title"),
        manageRelayType: document.getElementById("manage-relay-type"),
        manageTerminals: document.getElementById("manage-terminals"),
        manageCalibrationDate: document.getElementById("manage-calibration-date"),
        manageCalibrationValue: document.getElementById("manage-calibration-value"),
        calibrateModuleButton: document.getElementById("calibrate-module-button"),
        startTestButton: document.getElementById("start-test-button"),
        editModuleButton: document.getElementById("edit-module-button"),
        deleteModuleButton: document.getElementById("delete-module-button"),
        manageViewHistoryButton: document.getElementById("manage-view-history-button"),
        progressTitle: document.getElementById("progress-title"),
        testIndicatorsContainer: document.getElementById("test-indicators-container"),
        progressStatusText: document.getElementById("progress-status-text"),
        testResultActions: document.getElementById("test-result-actions"),
        homeButtonAfterTest: document.getElementById("home-button-after-test"),
        viewHistoryButton: document.getElementById("view-history-button"),
        historyTitle: document.getElementById("history-title"),
        historyListContainer: document.getElementById("history-list-container"),
        backToManagementButton: document.getElementById("back-to-management-button"),
        globalAlert: document.getElementById("global-alert"),
        alertMessage: document.getElementById("alert-message"),
        alertActions: document.getElementById("alert-actions"),
    };

    // --- Estado da Aplicação ---
    let bleDevice = null;
    let sendCharacteristic = null;
    let connectionRetryCount = 0;
    let maxRetries = 3;
    let connectionTimeout = null;
    let heartbeatInterval = null;
    let reconnectInterval = null;
    let lastHeartbeat = 0;
    
    let state = {
        currentModule: null,
        isEditing: false,
        testResults: [], // Acumula os resultados do teste atual
    };


    // =================================================================
    // FUNÇÕES DE GERENCIAMENTO DA UI (ALERTAS, TELAS, ETC.)
    // =================================================================

    function showGlobalAlert(message, type = "info", buttons = [{ text: "OK" }]) {
        ui.alertMessage.textContent = message;
        ui.globalAlert.querySelector(".alert-box").className = `alert-box alert-${type}`;
        ui.alertActions.innerHTML = "";
        if (buttons && buttons.length > 0) {
            buttons.forEach((btnInfo) => {
                const button = document.createElement("button");
                button.textContent = btnInfo.text;
                button.className = btnInfo.class || "btn btn-primary";
                button.onclick = () => {
                    if (!btnInfo.action) {
                        hideGlobalAlert();
                    }
                    if (btnInfo.action) btnInfo.action();
                };
                ui.alertActions.appendChild(button);
            });
        }
        ui.globalAlert.classList.remove("hidden");
    }

    function hideGlobalAlert() {
        ui.globalAlert.classList.add("hidden");
    }

    function showScreen(screen) {
        [
            ui.moduleSelectionScreen,
            ui.moduleFormScreen,
            ui.moduleManagementScreen,
            ui.progressScreen,
            ui.historyScreen,
        ].forEach((s) => s.classList.add("hidden"));
        if (screen) screen.classList.remove("hidden");
    }

    function updateConnectionStatus(status) {
        ui.statusCircle.className = "circle";
        switch (status) {
            case "disconnected":
                ui.statusText.textContent = "Desconectado";
                ui.connectButton.textContent = "Conectar";
                ui.statusCircle.classList.add("disconnected");
                ui.appContent.classList.add("hidden");
                ui.welcomeMessage.classList.remove("hidden");
                ui.connectButton.disabled = false;
                break;
            case "connecting":
                ui.statusText.textContent = "Conectando...";
                ui.connectButton.textContent = "Aguarde...";
                ui.statusCircle.classList.add("connecting");
                ui.connectButton.disabled = true;
                break;
            case "connected":
                ui.statusText.textContent = "Conectado";
                ui.connectButton.textContent = "Desconectar";
                ui.statusCircle.classList.add("connected");
                ui.appContent.classList.remove("hidden");
                ui.welcomeMessage.classList.add("hidden");
                ui.connectButton.disabled = false;
                fetchAndRenderModules();
                showScreen(ui.moduleSelectionScreen);
                break;
        }
    }


    // =================================================================
    // LÓGICA DE COMUNICAÇÃO BLE
    // =================================================================

    async function connectBLE() {
        if (bleDevice && bleDevice.gatt.connected) {
            bleDevice.gatt.disconnect();
            return;
        }
        
        connectionRetryCount = 0;
        await attemptConnection();
    }

    async function attemptConnection() {
        try {
            updateConnectionStatus("connecting");
            
            // Timeout para a conexão
            connectionTimeout = setTimeout(() => {
                throw new Error("Timeout na conexão");
            }, 15000);

            bleDevice = await navigator.bluetooth.requestDevice({
                filters: [
                    { name: "Jiga-Teste-Reles" },
                    { namePrefix: "Jiga" }
                ],
                optionalServices: [BLE_SERVICE_UUID],
            });

            bleDevice.addEventListener("gattserverdisconnected", onDisconnected);
            
            const server = await bleDevice.gatt.connect();
            const service = await server.getPrimaryService(BLE_SERVICE_UUID);
            
            sendCharacteristic = await service.getCharacteristic(BLE_RECEIVE_CHARACTERISTIC_UUID);
            const notifyCharacteristic = await service.getCharacteristic(BLE_SEND_CHARACTERISTIC_UUID);
            
            await notifyCharacteristic.startNotifications();
            notifyCharacteristic.addEventListener("characteristicvaluechanged", handleDataReceived);
            
            clearTimeout(connectionTimeout);
            connectionRetryCount = 0;
            
            // Inicia monitoramento de heartbeat
            startHeartbeatMonitoring();
            
            updateConnectionStatus("connected");
            
        } catch (error) {
            clearTimeout(connectionTimeout);
            console.error("Erro na conexão BLE:", error);
            
            if (connectionRetryCount < maxRetries) {
                connectionRetryCount++;
                showGlobalAlert(
                    `Tentativa ${connectionRetryCount} de ${maxRetries} falhada. Tentando novamente...`, 
                    "warning"
                );
                
                // Aguarda antes de tentar novamente
                await new Promise(resolve => setTimeout(resolve, 2000));
                return await attemptConnection();
            } else {
                showGlobalAlert(
                    "Falha ao conectar após múltiplas tentativas. Verifique se o dispositivo está ligado e acessível.", 
                    "error"
                );
                updateConnectionStatus("disconnected");
            }
        }
    }

    function startHeartbeatMonitoring() {
        stopHeartbeatMonitoring();
        
        heartbeatInterval = setInterval(() => {
            const now = Date.now();
            // Se não recebeu heartbeat em 30 segundos, considera desconectado
            if (lastHeartbeat > 0 && (now - lastHeartbeat > 30000)) {
                console.warn("Heartbeat perdido - conexão pode estar instável");
                if (bleDevice && bleDevice.gatt.connected) {
                    // Tenta reestabelecer a conexão
                    onDisconnected();
                }
            }
        }, 5000);
    }

    function stopHeartbeatMonitoring() {
        if (heartbeatInterval) {
            clearInterval(heartbeatInterval);
            heartbeatInterval = null;
        }
    }

    function onDisconnected() {
        console.log("Dispositivo desconectado");
        
        bleDevice = null;
        sendCharacteristic = null;
        lastHeartbeat = 0;
        
        stopHeartbeatMonitoring();
        
        updateConnectionStatus("disconnected");
        
        // Tenta reconectar automaticamente após 5 segundos
        if (!reconnectInterval) {
            reconnectInterval = setTimeout(() => {
                reconnectInterval = null;
                if (confirm("Conexão perdida. Deseja tentar reconectar?")) {
                    connectBLE();
                }
            }, 5000);
        }
    }

    async function sendJsonCommand(json) {
        if (!sendCharacteristic) {
            showGlobalAlert("Não conectado. Conecte-se primeiro.", "error");
            return false;
        }
        
        const commandString = JSON.stringify(json);
        const maxAttempts = 3;
        
        for (let attempt = 1; attempt <= maxAttempts; attempt++) {
            try {
                await sendCharacteristic.writeValueWithoutResponse(new TextEncoder().encode(commandString));
                return true;
            } catch (error) {
                console.error(`Tentativa ${attempt} falhou ao enviar comando:`, error);
                
                if (attempt === maxAttempts) {
                    showGlobalAlert("Falha ao enviar comando após múltiplas tentativas.", "error");
                    return false;
                }
                
                // Aguarda antes de tentar novamente
                await new Promise(resolve => setTimeout(resolve, 500));
            }
        }
        
        return false;
    }

    /**
     * Envia o comando de confirmação para o firmware prosseguir para a próxima etapa.
     */
    async function sendConfirmation() {
        hideGlobalAlert();
        await sendJsonCommand({ comando: "confirmar_etapa" });
    }

    /**
     * Handler principal para todos os dados recebidos do ESP32.
     */
    function handleDataReceived(event) {
        const receivedText = new TextDecoder().decode(event.target.value);

        try {
            const json = JSON.parse(receivedText);
            
            // Processa heartbeat
            if (json.status === "heartbeat") {
                lastHeartbeat = Date.now();
                console.log("Heartbeat recebido");
                return;
            }

            switch (json.status) {
                case "test_init": {
                    // Inicializa todos os indicadores baseado no totalTests do ESP32
                    const totalTests = json.totalTests;
                    const numContatos = state.currentModule.quantidadeContatos;
                    
                    // Limpa e cria a quantidade correta de indicadores
                    ui.testIndicatorsContainer.innerHTML = "";
                    
                    if (numContatos === 1) {
                        // Caso especial: 1 contato tem 2 testes (COM-N# desenergizado e energizado)
                        ui.testIndicatorsContainer.innerHTML += `<div class="test-indicator status-pending">COM-N# Des</div>`;
                        ui.testIndicatorsContainer.innerHTML += `<div class="test-indicator status-pending">COM-N# Ene</div>`;
                    } else {
                        // Para múltiplos contatos: calcula quantos contatos NF e NA
                        const numContatosNF = Math.floor(numContatos / 2);
                        const numContatosNA = Math.floor(numContatos / 2);
                        
                        // Nova ordem: alterna desenergizado/energizado para cada contato
                        // NF1 Des -> NF1 Ene -> NA1 Des -> NA1 Ene -> NF2 Des -> NF2 Ene -> NA2 Des -> NA2 Ene...
                        const maxContatos = Math.max(numContatosNF, numContatosNA);
                        
                        for (let i = 0; i < maxContatos; i++) {
                            // Contato NF desenergizado e energizado
                            if (i < numContatosNF) {
                                ui.testIndicatorsContainer.innerHTML += `<div class="test-indicator status-pending">NF${i + 1} Des</div>`;
                                ui.testIndicatorsContainer.innerHTML += `<div class="test-indicator status-pending">NF${i + 1} Ene</div>`;
                            }
                            // Contato NA desenergizado e energizado
                            if (i < numContatosNA) {
                                ui.testIndicatorsContainer.innerHTML += `<div class="test-indicator status-pending">NA${i + 1} Des</div>`;
                                ui.testIndicatorsContainer.innerHTML += `<div class="test-indicator status-pending">NA${i + 1} Ene</div>`;
                            }
                        }
                    }
                    
                    ui.testIndicatorsContainer.classList.remove("hidden");
                    // A tabela já foi criada em setupTestIndicators, não sobrescrever
                    break;
                }

                case "test_current": {
                    // Faz o indicador atual piscar
                    const testIndex = json.testIndex;
                    const indicators = ui.testIndicatorsContainer.querySelectorAll(".test-indicator");
                    
                    // Reset todos para pending
                    indicators.forEach((indicator, index) => {
                        if (index < testIndex) {
                            indicator.className = "test-indicator status-done";
                        } else if (index === testIndex) {
                            indicator.className = "test-indicator status-testing";
                        } else {
                            indicator.className = "test-indicator status-pending";
                        }
                    });
                    break;
                }

                case "prompt":
                case "test_step": {
                    const message = json.message;
                    
                    if (json.status === "test_step") {
                        const pairNumber = json.pair; // 0-indexed
                        const isAcionado = json.state === "ACIONADO";
                        const numContatos = state.currentModule ? state.currentModule.quantidadeContatos : 0;
                        const currentIndex = isAcionado ? numContatos + pairNumber : pairNumber;
                        const indicators = ui.testIndicatorsContainer.querySelectorAll(".test-indicator");

                        indicators.forEach((indicator, index) => {
                            indicator.className = "test-indicator " + (index < currentIndex ? "status-done" : index === currentIndex ? "status-testing" : "status-pending");
                        });
                    }
                    
                    showGlobalAlert(message, "prompt", [{
                        text: "Pronto, medir",
                        class: "btn btn-primary",
                        action: sendConfirmation,
                    }]);
                    break;
                }

                case "button_pressed": {
                    // Botão da jiga foi pressionado, fechar o modal
                    hideGlobalAlert();
                    // Não sobrescreve a tabela, apenas fecha o modal
                    break;
                }

                case "calibration_init": {
                    ui.progressStatusText.innerHTML = "<h4>" + json.message + "</h4>";
                    break;
                }

                case "calibration_processing": {
                    ui.progressStatusText.innerHTML = `
                        <h4>${json.message}</h4>
                        <p>Realizando múltiplas medições para obter um valor preciso...</p>
                        <p style="color: var(--light-text-color); font-size: 0.9rem;">
                            Este processo pode levar alguns segundos.
                        </p>
                    `;
                    break;
                }

                case "test_starting": {
                    // Não sobrescreve a tabela, apenas atualiza o título se necessário
                    ui.progressTitle.textContent = "Executando Teste...";
                    break;
                }

                case "test_special_init": {
                    // Não sobrescreve a tabela, apenas atualiza o título se necessário
                    ui.progressTitle.textContent = "Teste Especial - 1 Par";
                    break;
                }

                case "calibration_result": {
                    ui.progressStatusText.innerHTML += `<p>Contato #${json.contato} calibrado: ${json.valor.toFixed(3)} Ω</p>`;
                    break;
                }

                case "test_result": {
                    const resultData = { 
                        contato: json.contato, 
                        estado: json.estado, 
                        resistencia: json.resistencia,
                        esperado: json.esperado,
                        passou: json.passou
                    };
                    state.testResults.push(resultData);

                    // Atualiza a tabela de resultados se existir
                    let rowId;
                    if (json.contato.includes("COM-N#")) {
                        // Caso especial de 1 contato
                        rowId = `result-row-COM-N#-1-${json.estado}`;
                    } else {
                        // Caso normal - O Arduino envia formato "COM-NF1" ou "COM-NA1"
                        // Converte para "result-row-COM-NF1-DESENERGIZADO" ou "result-row-COM-NA1-ENERGIZADO"
                        rowId = `result-row-${json.contato}-${json.estado}`;
                    }

                    const row = document.getElementById(rowId);
                    if (row) {
                        const resistanceCell = row.querySelector('.resistance-value');
                        const statusCell = row.querySelector('.status-value');
                        
                        if (resistanceCell) {
                            resistanceCell.textContent = json.resistencia;
                        }
                        if (statusCell) {
                            statusCell.textContent = json.passou ? "PASSOU" : "FALHOU";
                            statusCell.className = `status-value ${json.passou ? 'status-pass' : 'status-fail'}`;
                        }
                    }

                    // Atualiza o status do indicador atual para "concluído"
                    const testIndex = json.testIndex;
                    const indicators = ui.testIndicatorsContainer.querySelectorAll(".test-indicator");
                    
                    indicators.forEach((indicator, index) => {
                        if (index < testIndex) {
                            indicator.className = "test-indicator status-done";
                        } else if (index === testIndex) {
                            indicator.className = "test-indicator status-done";
                        } else {
                            indicator.className = "test-indicator status-pending";
                        }
                    });

                    break;
                }

                case "calibration_complete": {
                    hideGlobalAlert();
                    
                    // Mostra o resultado da calibração com o valor
                    const valorCalibrado = json.valor;
                    const mensagemSucesso = `
                        <div style="text-align: center;">
                            <h3 style="color: var(--success-color); margin: 0 0 1rem 0;">
                                ✅ Calibração Concluída!
                            </h3>
                            <p style="margin: 0.5rem 0;">
                                <strong>Valor medido:</strong>
                            </p>
                            <p style="font-family: monospace; font-size: 1.2rem; font-weight: bold; color: var(--primary-color); margin: 0.5rem 0;">
                                ${valorCalibrado.toFixed(6)} Ω
                            </p>
                            <p style="margin: 1rem 0 0 0; font-size: 0.9rem; color: var(--light-text-color);">
                                Este valor será usado como referência para compensar a resistência dos cabos e conectores.
                            </p>
                        </div>
                    `;
                    
                    // Cria botão personalizado para fechar
                    const closeButton = document.createElement('button');
                    closeButton.className = 'btn btn-primary';
                    closeButton.textContent = 'OK';
                    closeButton.onclick = () => hideGlobalAlert();
                    
                    // Mostra o alerta com HTML
                    ui.alertMessage.innerHTML = mensagemSucesso;
                    ui.globalAlert.querySelector(".alert-box").className = "alert-box alert-success";
                    ui.alertActions.innerHTML = "";
                    ui.alertActions.appendChild(closeButton);
                    ui.globalAlert.classList.remove("hidden");

                    const newCalibrationData = { 
                        data: new Date().toISOString(), 
                        valores: [valorCalibrado] // Usar o campo "valor" enviado pelo ESP32
                    };
                    database.ref(`modulos/${state.currentModule.id}/calibracao`).set(newCalibrationData);

                    const updatedModule = { ...state.currentModule, calibracao: newCalibrationData };
                    
                    // Aguarda um pouco antes de voltar para a tela de gerenciamento
                    setTimeout(() => {
                        hideGlobalAlert();
                        selectModule(state.currentModule.id, updatedModule);
                    }, 5000); // Aumentado para 5 segundos para dar tempo de ler
                    break;
                }

                case "test_complete": {
                    hideGlobalAlert();
                    ui.progressTitle.textContent = "Teste Finalizado";
                    ui.testResultActions.classList.remove("hidden");

                    // Marca todos os indicadores como concluídos
                    const indicators = ui.testIndicatorsContainer.querySelectorAll(".test-indicator");
                    indicators.forEach(ind => ind.className = "test-indicator status-done");

                    // Verifica se é caso especial (1 par)
                    const isSpecialCase = state.currentModule.quantidadeContatos === 1;
                    
                    let overallStatus;
                    if (isSpecialCase) {
                        // Caso especial: sempre aprovado (apenas medição)
                        overallStatus = "MEDIDO";
                        ui.progressTitle.textContent = "Teste Finalizado - Valores Medidos";
                        ui.progressTitle.style.color = "var(--primary-color)";
                    } else {
                        // Caso normal: verifica se algum teste falhou
                        const hasFailed = state.testResults.some(r => r.passou === false);
                        overallStatus = hasFailed ? "REPROVADO" : "APROVADO";
                        ui.progressTitle.textContent = `Teste Finalizado - ${overallStatus}`;
                        ui.progressTitle.style.color = hasFailed ? "var(--danger-color)" : "var(--success-color)";
                    }
                    
                    saveTestToHistory(state.testResults, overallStatus);
                    break;
                }

                case "error": {
                    hideGlobalAlert();
                    showGlobalAlert(`Erro no dispositivo: ${json.message}`, "error");
                    if (state.currentModule) {
                        showScreen(ui.moduleManagementScreen);
                    } else {
                        showScreen(ui.moduleSelectionScreen);
                    }
                    break;
                }
            }
        } catch (e) {
            console.error("Erro ao processar JSON recebido:", e, "Dados:", receivedText);
        }
    }


    // =================================================================
    // LÓGICA DE NEGÓCIO E ROTINAS DE TESTE
    // =================================================================

    function selectModule(id, moduleData) {
        state.currentModule = { id, ...moduleData };
        ui.managementTitle.textContent = state.currentModule.nome;
        const actuationTypeName = state.currentModule.tipoAcionamento === "TIPO_DC" ? "Relé DC (Tensão Contínua)" : "Relé AC (Tensão Alternada)";
        ui.manageRelayType.textContent = actuationTypeName;
        ui.manageTerminals.textContent = state.currentModule.quantidadeContatos;
        const calDate = state.currentModule.calibracao?.data;
        ui.manageCalibrationDate.textContent = calDate ? new Date(calDate).toLocaleString("pt-BR") : "Pendente";
        
        // Exibe o valor da calibração
        const calValue = state.currentModule.calibracao?.valores?.[0];
        if (calValue !== undefined && calValue !== null) {
            ui.manageCalibrationValue.textContent = `${calValue.toFixed(6)} Ω`;
            ui.manageCalibrationValue.style.color = "var(--success-color)";
        } else {
            ui.manageCalibrationValue.textContent = "Não calibrado";
            ui.manageCalibrationValue.style.color = "var(--danger-color)";
        }
        
        ui.startTestButton.disabled = !calDate;
        showScreen(ui.moduleManagementScreen);
    }

    async function startCalibration() {
        if (!state.currentModule) return;
        
        ui.progressTitle.textContent = "Calibrando Módulo: " + state.currentModule.nome;
        ui.progressStatusText.innerHTML = `
            <h4>Preparando calibração...</h4>
            <p>A calibração compensa a resistência dos cabos e conectores de teste.</p>
            <p style="color: var(--warning-color); font-weight: bold;">
                ⚠️ Importante: Conecte os fios de medição em curto-circuito antes de prosseguir.
            </p>
        `;
        ui.testIndicatorsContainer.classList.add("hidden");
        ui.testResultActions.classList.add("hidden");
        showScreen(ui.progressScreen);
        
        await sendJsonCommand({ 
            comando: "calibrar", 
            numTerminais: state.currentModule.quantidadeContatos 
        });
    }

    async function startTest() {
        if (!state.currentModule || !state.currentModule.calibracao?.valores) {
            showGlobalAlert("Este módulo precisa ser calibrado antes de iniciar um teste.", "info");
            return;
        }

        state.testResults = []; // Limpa resultados anteriores
        ui.progressTitle.textContent = "Executando Teste...";
        ui.testResultActions.classList.add("hidden");

        const numContatos = state.currentModule.quantidadeContatos;
        ui.testIndicatorsContainer.innerHTML = "";
        
        // Caso especial: apenas 1 contato
        if (numContatos === 1) {
            // Cria indicadores para o teste especial
            ui.testIndicatorsContainer.innerHTML += `<div class="test-indicator status-pending">COM-N# Des</div>`;
            ui.testIndicatorsContainer.innerHTML += `<div class="test-indicator status-pending">COM-N# Ene</div>`;
            ui.testIndicatorsContainer.classList.remove("hidden");
            
            let tableHTML = `<table class="results-table">
                <thead>
                    <tr>
                        <th>Contato</th>
                        <th>Estado do Relé</th>
                        <th>Resistência</th>
                        <th>Esperado</th>
                        <th>Resultado</th>
                    </tr>
                </thead>
                <tbody>`;
            
            tableHTML += `<tr id="result-row-COM-N#-1-DESENERGIZADO">
                <td>COM-N# 1</td>
                <td>DESENERGIZADO</td>
                <td class="resistance-value">--</td>
                <td>VARIÁVEL</td>
                <td class="status-value">--</td>
            </tr>`;
            tableHTML += `<tr id="result-row-COM-N#-1-ENERGIZADO">
                <td>COM-N# 1</td>
                <td>ENERGIZADO</td>
                <td class="resistance-value">--</td>
                <td>VARIÁVEL</td>
                <td class="status-value">--</td>
            </tr>`;
            tableHTML += "</tbody></table>";
            ui.progressStatusText.innerHTML = tableHTML;
        } else {
            // Para múltiplos contatos - nova ordem alternada do teste
            // Exemplo: relé 5 terminais com 2 contatos = 1 NF + 1 NA
            // Nova sequência: NF1 Des -> NF1 Ene -> NA1 Des -> NA1 Ene -> NF2 Des -> NF2 Ene -> NA2 Des -> NA2 Ene
            const numContatosNF = numContatos / 2;
            const numContatosNA = numContatos / 2;
            
            // Indicadores na nova ordem alternada
            const maxContatos = Math.max(numContatosNF, numContatosNA);
            
            for (let i = 0; i < maxContatos; i++) {
                // Contato NF desenergizado e energizado
                if (i < numContatosNF) {
                    ui.testIndicatorsContainer.innerHTML += `<div class="test-indicator status-pending">NF${i+1} Des</div>`;
                    ui.testIndicatorsContainer.innerHTML += `<div class="test-indicator status-pending">NF${i+1} Ene</div>`;
                }
                // Contato NA desenergizado e energizado
                if (i < numContatosNA) {
                    ui.testIndicatorsContainer.innerHTML += `<div class="test-indicator status-pending">NA${i+1} Des</div>`;
                    ui.testIndicatorsContainer.innerHTML += `<div class="test-indicator status-pending">NA${i+1} Ene</div>`;
                }
            }
            ui.testIndicatorsContainer.classList.remove("hidden");
            
            let tableHTML = `<table class="results-table">
                <thead>
                    <tr>
                        <th>Contato</th>
                        <th>Estado do Relé</th>
                        <th>Resistência</th>
                        <th>Esperado</th>
                        <th>Resultado</th>
                    </tr>
                </thead>
                <tbody>`;
            
            // Nova ordem alternada: NF1 Des -> NF1 Ene -> NA1 Des -> NA1 Ene -> NF2 Des -> NF2 Ene...
            const maxContatosTable = Math.max(numContatosNF, numContatosNA);
            
            for (let i = 0; i < maxContatosTable; i++) {
                // Contato NF desenergizado
                if (i < numContatosNF) {
                    tableHTML += `<tr id="result-row-COM-NF${i+1}-DESENERGIZADO">
                        <td>COM-NF${i+1}</td>
                        <td>DESENERGIZADO</td>
                        <td class="resistance-value">--</td>
                        <td>BAIXA</td>
                        <td class="status-value">--</td>
                    </tr>`;
                }
                
                // Contato NF energizado
                if (i < numContatosNF) {
                    tableHTML += `<tr id="result-row-COM-NF${i+1}-ENERGIZADO">
                        <td>COM-NF${i+1}</td>
                        <td>ENERGIZADO</td>
                        <td class="resistance-value">--</td>
                        <td>ABERTO</td>
                        <td class="status-value">--</td>
                    </tr>`;
                }
                
                // Contato NA desenergizado
                if (i < numContatosNA) {
                    tableHTML += `<tr id="result-row-COM-NA${i+1}-DESENERGIZADO">
                        <td>COM-NA${i+1}</td>
                        <td>DESENERGIZADO</td>
                        <td class="resistance-value">--</td>
                        <td>ABERTO</td>
                        <td class="status-value">--</td>
                    </tr>`;
                }
                
                // Contato NA energizado
                if (i < numContatosNA) {
                    tableHTML += `<tr id="result-row-COM-NA${i+1}-ENERGIZADO">
                        <td>COM-NA${i+1}</td>
                        <td>ENERGIZADO</td>
                        <td class="resistance-value">--</td>
                        <td>BAIXA</td>
                        <td class="status-value">--</td>
                    </tr>`;
                }
            }
            
            tableHTML += "</tbody></table>";
            ui.progressStatusText.innerHTML = tableHTML;
        }

        showScreen(ui.progressScreen);

        await sendJsonCommand({
            comando: "iniciar_teste",
            tipoAcionamento: state.currentModule.tipoAcionamento,
            quantidadeContatos: state.currentModule.quantidadeContatos,
            calibracao: state.currentModule.calibracao.valores,
        });
    }


    // =================================================================
    // FUNÇÕES DE PERSISTÊNCIA (FIREBASE) E CRUD DE MÓDULOS
    // =================================================================

    function fetchAndRenderModules() {
        const modulesRef = database.ref("modulos").orderByChild("nome");
        modulesRef.on("value", (snapshot) => {
            ui.moduleList.innerHTML = "";
            const modules = snapshot.val();
            if (modules) {
                Object.entries(modules).forEach(([id, module]) => {
                    const card = document.createElement("div");
                    card.className = "card module-card";
                    card.dataset.moduleId = id;
                    const typeName = module.tipoAcionamento === "TIPO_DC" ? "DC" : "AC";
                    card.innerHTML = `<h3>${module.nome}</h3><p>${typeName}</p>`;
                    card.addEventListener("click", () => selectModule(id, module));
                    ui.moduleList.appendChild(card);
                });
            } else {
                ui.moduleList.innerHTML = "<p>Nenhum módulo encontrado. Adicione um novo para começar.</p>";
            }
        });
    }

    async function saveModule(e) {
        e.preventDefault();
        const moduleData = {
            nome: ui.moduleNameInput.value.trim(),
            tipoAcionamento: ui.moduleActuationTypeSelect.value,
            quantidadeContatos: parseInt(ui.moduleTerminalsInput.value),
        };
        if (!moduleData.nome || !moduleData.tipoAcionamento || !moduleData.quantidadeContatos) {
            showGlobalAlert("Por favor, preencha todos os campos.", "error");
            return;
        }
        try {
            if (state.isEditing) {
                const id = ui.moduleIdInput.value;
                await database.ref(`modulos/${id}`).update(moduleData);
                // Re-seleciona o módulo para atualizar a tela de gerenciamento
                selectModule(id, { ...state.currentModule, ...moduleData });
            } else {
                const newModuleRef = database.ref("modulos").push();
                moduleData.calibracao = { data: null, valores: [] };
                await newModuleRef.set(moduleData);
                showScreen(ui.moduleSelectionScreen);
            }
        } catch (error) {
            console.error("Erro ao salvar módulo:", error);
            showGlobalAlert("Falha ao salvar o módulo.", "error");
        }
    }

    function deleteModule() {
        if (!state.currentModule) return;
        const confirmDelete = () => {
            database
                .ref(`modulos/${state.currentModule.id}`)
                .remove()
                .then(() => {
                    state.currentModule = null;
                    showScreen(ui.moduleSelectionScreen)
                })
                .catch((error) => {
                    console.error("Erro ao excluir módulo:", error);
                    showGlobalAlert("Falha ao excluir o módulo.", "error");
                });
        };
        showGlobalAlert(
            `Tem certeza que deseja excluir o módulo "${state.currentModule.nome}"?`,
            "prompt",
            [
                { text: "Cancelar", class: "btn btn-secondary" },
                { text: "Excluir", class: "btn btn-danger", action: confirmDelete },
            ]
        );
    }

    async function saveTestToHistory(resultsData, overallStatus) {
        if (!state.currentModule) return;
        const historyEntry = {
            moduloId: state.currentModule.id,
            nomeModulo: state.currentModule.nome,
            data: new Date().toISOString(),
            status: overallStatus,
            resultados: resultsData,
        };
        try {
            await database.ref("verificacoes").push(historyEntry);
            console.log("Resultado do teste salvo no histórico.");
        } catch (error) {
            console.error("Erro ao salvar histórico:", error);
        }
    }

    function handleModuleForm(isEditing) {
        state.isEditing = isEditing;
        ui.formTitle.textContent = isEditing ? "Editar Módulo" : "Adicionar Novo Módulo";
        ui.moduleForm.reset(); // Limpa o formulário
        if (isEditing && state.currentModule) {
            ui.moduleIdInput.value = state.currentModule.id;
            ui.moduleNameInput.value = state.currentModule.nome;
            ui.moduleActuationTypeSelect.value = state.currentModule.tipoAcionamento;
            ui.moduleTerminalsInput.value = state.currentModule.quantidadeContatos;
        } else {
            ui.moduleTerminalsInput.value = "1";
        }
        showScreen(ui.moduleFormScreen);
    }

    async function renderHistoryScreen() {
        if (!state.currentModule) return;
        ui.historyTitle.textContent = `Histórico de: ${state.currentModule.nome}`;
        ui.historyListContainer.innerHTML = "<p>Carregando histórico...</p>";
        showScreen(ui.historyScreen);

        const historyRef = database.ref("verificacoes").orderByChild("moduloId").equalTo(state.currentModule.id);
        try {
            const snapshot = await historyRef.once("value");
            const historyData = snapshot.val();
            ui.historyListContainer.innerHTML = "";
            if (!historyData) {
                ui.historyListContainer.innerHTML = "<p>Nenhum teste encontrado no histórico para este módulo.</p>";
                return;
            }
            const sortedEntries = Object.values(historyData).sort((a, b) => new Date(b.data) - new Date(a.data));
            sortedEntries.forEach((entry) => {
                const card = document.createElement("div");
                card.className = "history-card";
                
                // Determina a classe do status baseado no resultado
                let statusClass;
                if (entry.status === "APROVADO") {
                    statusClass = "status-ok";
                } else if (entry.status === "REPROVADO") {
                    statusClass = "status-fail";
                } else {
                    statusClass = "status-measured"; // Para caso especial
                }
                
                const formattedDate = new Date(entry.data).toLocaleString("pt-BR");
                
                let tableHTML = `<table class="results-table">
                    <thead>
                        <tr>
                            <th>Contato</th>
                            <th>Estado</th>
                            <th>Resistência</th>
                            <th>Esperado</th>
                            <th>Resultado</th>
                        </tr>
                    </thead>
                    <tbody>`;
                
                // Ordena os resultados na nova ordem alternada do teste
                const orderedResults = entry.resultados.sort((a, b) => {
                    // Função para determinar a ordem baseada no contato e estado
                    const getOrder = (res) => {
                        if (res.contato.includes("COM-N#")) {
                            // Caso especial de 1 contato
                            return res.estado === "DESENERGIZADO" ? 0 : 1;
                        }
                        
                        // Extrai o número do contato (ex: "COM-NF1" -> 1)
                        const contatoMatch = res.contato.match(/(\d+)/);
                        const numero = contatoMatch ? parseInt(contatoMatch[1]) : 0;
                        
                        // Nova ordem alternada: NF1 Des -> NF1 Ene -> NA1 Des -> NA1 Ene -> NF2 Des -> NF2 Ene...
                        const baseIndex = (numero - 1) * 4;
                        
                        if (res.contato.includes("NF") && res.estado === "DESENERGIZADO") {
                            return baseIndex + 0; // NF desenergizado
                        } else if (res.contato.includes("NF") && res.estado === "ENERGIZADO") {
                            return baseIndex + 1; // NF energizado
                        } else if (res.contato.includes("NA") && res.estado === "DESENERGIZADO") {
                            return baseIndex + 2; // NA desenergizado
                        } else if (res.contato.includes("NA") && res.estado === "ENERGIZADO") {
                            return baseIndex + 3; // NA energizado
                        }
                        return 999; // Fallback
                    };
                    
                    return getOrder(a) - getOrder(b);
                });
                
                orderedResults.forEach((res) => {
                    const esperado = res.esperado || "N/A";
                    let passou, resultColor;
                    
                    if (res.esperado === "VARIÁVEL") {
                        // Caso especial
                        passou = "MEDIDO";
                        resultColor = "color: var(--primary-color)";
                    } else {
                        // Caso normal
                        passou = res.passou !== undefined ? (res.passou ? "✓ PASSOU" : "✗ FALHOU") : "N/A";
                        resultColor = res.passou === false ? "color: var(--danger-color)" : 
                                     res.passou === true ? "color: var(--success-color)" : "";
                    }
                    
                    tableHTML += `<tr>
                        <td>${res.contato}</td>
                        <td>${res.estado}</td>
                        <td>${res.resistencia}</td>
                        <td>${esperado}</td>
                        <td style="${resultColor}; font-weight: bold;">${passou}</td>
                    </tr>`;
                });
                
                tableHTML += `</tbody></table>`;
                card.innerHTML = `<div class="history-card-header"><h4>Teste de ${formattedDate}</h4><span class="${statusClass}">${entry.status}</span></div>${tableHTML}`;
                ui.historyListContainer.appendChild(card);
            });
        } catch (error) {
            console.error("Erro ao buscar histórico:", error);
            ui.historyListContainer.innerHTML = "<p>Ocorreu um erro ao carregar o histórico.</p>";
        }
    }


    // =================================================================
    // EVENT LISTENERS - Conectam as ações do usuário às funções
    // =================================================================

    ui.connectButton.addEventListener("click", connectBLE);
    ui.addNewModuleButton.addEventListener("click", () => handleModuleForm(false));
    ui.editModuleButton.addEventListener("click", () => handleModuleForm(true));
    ui.moduleForm.addEventListener("submit", saveModule);
    ui.cancelFormButton.addEventListener("click", () => showScreen(state.isEditing ? ui.moduleManagementScreen : ui.moduleSelectionScreen));
    ui.deleteModuleButton.addEventListener("click", deleteModule);
    ui.backToSelectionButton.addEventListener("click", () => showScreen(ui.moduleSelectionScreen));
    ui.calibrateModuleButton.addEventListener("click", startCalibration);
    ui.startTestButton.addEventListener("click", startTest);
    ui.homeButtonAfterTest.addEventListener("click", () => showScreen(ui.moduleSelectionScreen));
    ui.viewHistoryButton.addEventListener("click", renderHistoryScreen);
    ui.manageViewHistoryButton.addEventListener("click", renderHistoryScreen);
    ui.backToManagementButton.addEventListener("click", () => {
        if (state.currentModule) {
            selectModule(state.currentModule.id, state.currentModule);
        } else {
            showScreen(ui.moduleSelectionScreen);
        }
    });

    // Lógica do seletor de tema
    ui.themeToggle.addEventListener("change", () => {
        const newTheme = ui.themeToggle.checked ? "dark" : "light";
        localStorage.setItem("theme", newTheme);
        document.body.classList.toggle("dark-mode", ui.themeToggle.checked);
    });

    // Carrega o tema salvo e inicializa a UI
    const savedTheme = localStorage.getItem("theme") || "light";
    if (savedTheme === "dark") {
        document.body.classList.add("dark-mode");
        ui.themeToggle.checked = true;
    }

    updateConnectionStatus("disconnected");
    
    // Limpa intervalos quando a página é fechada
    window.addEventListener("beforeunload", () => {
        stopHeartbeatMonitoring();
        if (reconnectInterval) {
            clearTimeout(reconnectInterval);
        }
        if (connectionTimeout) {
            clearTimeout(connectionTimeout);
        }
    });
});