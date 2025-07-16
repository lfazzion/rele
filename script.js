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
        try {
            updateConnectionStatus("connecting");
            bleDevice = await navigator.bluetooth.requestDevice({
                filters: [{ name: "Jiga - Bluetooth Esp32" }],
                optionalServices: [BLE_SERVICE_UUID],
            });
            bleDevice.addEventListener("gattserverdisconnected", onDisconnected);
            const server = await bleDevice.gatt.connect();
            const service = await server.getPrimaryService(BLE_SERVICE_UUID);
            sendCharacteristic = await service.getCharacteristic(BLE_RECEIVE_CHARACTERISTIC_UUID);
            const notifyCharacteristic = await service.getCharacteristic(BLE_SEND_CHARACTERISTIC_UUID);
            await notifyCharacteristic.startNotifications();
            notifyCharacteristic.addEventListener("characteristicvaluechanged", handleDataReceived);
            updateConnectionStatus("connected");
        } catch (error) {
            console.error("Erro na conexão BLE:", error);
            showGlobalAlert("Falha ao conectar. Verifique se o dispositivo está ligado e acessível.", "error");
            updateConnectionStatus("disconnected");
        }
    }

    function onDisconnected() {
        bleDevice = null;
        sendCharacteristic = null;
        updateConnectionStatus("disconnected");
        showGlobalAlert("Dispositivo desconectado.", "error");
    }

    async function sendJsonCommand(json) {
        if (!sendCharacteristic) {
            showGlobalAlert("Não conectado. Conecte-se primeiro.", "error");
            return;
        }
        const commandString = JSON.stringify(json);
        try {
            await sendCharacteristic.writeValueWithoutResponse(new TextEncoder().encode(commandString));
        } catch (error) {
            console.error("Erro ao enviar comando:", error);
            showGlobalAlert("Falha ao enviar comando para o dispositivo.", "error");
        }
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

            switch (json.status) {
                case "test_init": {
                    // Inicializa todos os indicadores baseado no totalTests do ESP32
                    const totalTests = json.totalTests;
                    const numContatos = state.currentModule.quantidadePares;
                    
                    // Limpa e cria a quantidade correta de indicadores
                    ui.testIndicatorsContainer.innerHTML = "";
                    
                    if (numContatos === 1) {
                        // Caso especial: 1 contato tem 2 testes
                        ui.testIndicatorsContainer.innerHTML += `<div class="test-indicator status-pending">COM-N# Des</div>`;
                        ui.testIndicatorsContainer.innerHTML += `<div class="test-indicator status-pending">COM-N# Ene</div>`;
                    } else {
                        // Para múltiplos contatos: primeiro todos desenergizados, depois todos energizados
                        // Alterna entre NF e NA baseado no índice
                        for (let i = 0; i < numContatos; i++) {
                            const isNF = (i % 2 === 0);  // Par = NF, Ímpar = NA
                            const tipoContato = isNF ? "NF" : "NA";
                            const numeroContato = Math.floor(i / 2) + 1;
                            ui.testIndicatorsContainer.innerHTML += `<div class="test-indicator status-pending">${tipoContato}${numeroContato} Des</div>`;
                        }
                        // Fase 2: Energizado - mesma alternância
                        for (let i = 0; i < numContatos; i++) {
                            const isNF = (i % 2 === 0);  // Par = NF, Ímpar = NA
                            const tipoContato = isNF ? "NF" : "NA";
                            const numeroContato = Math.floor(i / 2) + 1;
                            ui.testIndicatorsContainer.innerHTML += `<div class="test-indicator status-pending">${tipoContato}${numeroContato} Ene</div>`;
                        }
                    }
                    
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
                        const numPairs = state.currentModule ? state.currentModule.quantidadePares : 0;
                        const currentIndex = isAcionado ? numPairs + pairNumber : pairNumber;
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

                case "calibration_result": {
                    ui.progressStatusText.innerHTML += `<p>Par #${json.par} calibrado: ${json.valor.toFixed(3)} Ω</p>`;
                    break;
                }

                case "test_result": {
                    const resultData = { 
                        par: json.par, 
                        estado: json.estado, 
                        resistencia: json.resistencia,
                        esperado: json.esperado,
                        passou: json.passou
                    };
                    state.testResults.push(resultData);

                    // Atualiza a tabela de resultados
                    let rowId;
                    if (json.par.includes("COM-N#")) {
                        // Caso especial de 1 par
                        rowId = `result-row-COM-N#-1-${json.estado}`;
                    } else {
                        // Caso normal - O Arduino envia formato "NF 1" ou "NA 1"
                        // Converte para "result-row-NF-1-REPOUSO" ou "result-row-NA-1-ENERGIZADO"
                        const parParts = json.par.split(" ");
                        const tipo = parParts[0];  // "NF" ou "NA"
                        const numero = parParts[1];  // "1", "2", etc.
                        rowId = `result-row-${tipo}-${numero}-${json.estado}`;
                    }
                    
                    const row = document.getElementById(rowId);
                    if (row) {
                        const resistanceCell = row.cells[2];
                        const resultCell = row.cells[4];
                        
                        resistanceCell.textContent = resultData.resistencia;
                        
                        if (resultData.esperado === "VARIÁVEL") {
                            // Caso especial: apenas mostra o valor medido
                            resultCell.textContent = "MEDIDO";
                            resultCell.style.color = "var(--primary-color)";
                            resultCell.style.fontWeight = "bold";
                        } else {
                            // Caso normal: mostra se passou ou falhou
                            resultCell.textContent = resultData.passou ? "✓ PASSOU" : "✗ FALHOU";
                            
                            // Aplica cores baseado no resultado
                            if (resultData.passou) {
                                resistanceCell.style.color = "var(--success-color)";
                                resultCell.style.color = "var(--success-color)";
                                resultCell.style.fontWeight = "bold";
                            } else {
                                resistanceCell.style.color = "var(--danger-color)";
                                resultCell.style.color = "var(--danger-color)";
                                resultCell.style.fontWeight = "bold";
                            }
                        }
                    }

                    // Atualiza o indicador para azul (concluído)
                    if (json.testIndex !== undefined) {
                        const indicators = ui.testIndicatorsContainer.querySelectorAll(".test-indicator");
                        if (indicators[json.testIndex]) {
                            indicators[json.testIndex].className = "test-indicator status-done";
                        }
                    }
                    break;
                }

                case "calibration_complete": {
                    hideGlobalAlert();
                    showGlobalAlert("Calibração finalizada com sucesso!", "success");

                    const newCalibrationData = { data: new Date().toISOString(), valores: json.data };
                    database.ref(`modulos/${state.currentModule.id}/calibracao`).set(newCalibrationData);

                    const updatedModule = { ...state.currentModule, calibracao: newCalibrationData };
                    selectModule(state.currentModule.id, updatedModule);
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
                    const isSpecialCase = state.currentModule.quantidadePares === 1;
                    
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
        const actuationTypeName = state.currentModule.tipoAcionamento === "TIPO_PADRAO" ? "Padrão (Tensão Contínua)" : "Com Travamento (Pulso)";
        ui.manageRelayType.textContent = actuationTypeName;
        ui.manageTerminals.textContent = state.currentModule.quantidadePares;
        const calDate = state.currentModule.calibracao?.data;
        ui.manageCalibrationDate.textContent = calDate ? new Date(calDate).toLocaleString("pt-BR") : "Pendente";
        ui.startTestButton.disabled = !calDate;
        showScreen(ui.moduleManagementScreen);
    }

    async function startCalibration() {
        if (!state.currentModule) return;
        
        ui.progressTitle.textContent = "Calibrando...";
        ui.progressStatusText.innerHTML = "<h4>Aguardando instruções do dispositivo...</h4>";
        ui.testIndicatorsContainer.classList.add("hidden");
        ui.testResultActions.classList.add("hidden");
        showScreen(ui.progressScreen);
        
        await sendJsonCommand({ comando: "calibrar", numTerminais: state.currentModule.quantidadePares });
    }

    async function startTest() {
        if (!state.currentModule || !state.currentModule.calibracao?.valores) {
            showGlobalAlert("Este módulo precisa ser calibrado antes de iniciar um teste.", "info");
            return;
        }

        state.testResults = []; // Limpa resultados anteriores
        ui.progressTitle.textContent = "Executando Teste...";
        ui.testResultActions.classList.add("hidden");

        const numPairs = state.currentModule.quantidadePares;
        ui.testIndicatorsContainer.innerHTML = "";
        
        // Caso especial: apenas 1 par
        if (numPairs === 1) {
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
                <td>--</td>
                <td>VARIÁVEL</td>
                <td>--</td>
            </tr>`;
            tableHTML += `<tr id="result-row-COM-N#-1-ENERGIZADO">
                <td>COM-N# 1</td>
                <td>ENERGIZADO</td>
                <td>--</td>
                <td>VARIÁVEL</td>
                <td>--</td>
            </tr>`;
            tableHTML += "</tbody></table>";
            ui.progressStatusText.innerHTML = tableHTML;
        } else {
            // Para múltiplos contatos NF
            ui.testIndicatorsContainer.innerHTML = `<div class="test-indicator status-pending">Aguardando...</div>`;
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
            
            // Cria linhas dinamicamente baseado no número de contatos
            // Alterna entre NF e NA para cada contato
            for (let i = 0; i < numPairs; i++) {
                const isNF = (i % 2 === 0);  // Par = NF, Ímpar = NA
                const tipoContato = isNF ? "NF" : "NA";
                const numeroContato = Math.floor(i / 2) + 1;
                const esperadoRepouso = isNF ? "BAIXA (~0Ω)" : "INFINITA";
                const esperadoEnergizado = isNF ? "INFINITA" : "BAIXA (~0Ω)";
                
                // Linha para teste em repouso
                tableHTML += `<tr id="result-row-${tipoContato}-${numeroContato}-REPOUSO">
                    <td>${tipoContato} ${numeroContato}</td>
                    <td>REPOUSO</td>
                    <td>--</td>
                    <td>${esperadoRepouso}</td>
                    <td>--</td>
                </tr>`;
            }
            
            // Agora as linhas para teste energizado (mesma alternância)
            for (let i = 0; i < numPairs; i++) {
                const isNF = (i % 2 === 0);  // Par = NF, Ímpar = NA
                const tipoContato = isNF ? "NF" : "NA";
                const numeroContato = Math.floor(i / 2) + 1;
                const esperadoEnergizado = isNF ? "INFINITA" : "BAIXA (~0Ω)";
                
                // Linha para teste energizado
                tableHTML += `<tr id="result-row-${tipoContato}-${numeroContato}-ENERGIZADO">
                    <td>${tipoContato} ${numeroContato}</td>
                    <td>ENERGIZADO</td>
                    <td>--</td>
                    <td>${esperadoEnergizado}</td>
                    <td>--</td>
                </tr>`;
            }
            tableHTML += "</tbody></table>";
            ui.progressStatusText.innerHTML = tableHTML;
        }

        showScreen(ui.progressScreen);

        await sendJsonCommand({
            comando: "iniciar_teste",
            config: {
                tipoAcionamento: state.currentModule.tipoAcionamento,
                quantidadePares: state.currentModule.quantidadePares,
                calibracao: state.currentModule.calibracao.valores,
            },
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
                    const typeName = module.tipoAcionamento === "TIPO_PADRAO" ? "Padrão" : "Com Travamento";
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
            quantidadePares: parseInt(ui.moduleTerminalsInput.value),
        };
        if (!moduleData.nome || !moduleData.tipoAcionamento || !moduleData.quantidadePares) {
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
            ui.moduleTerminalsInput.value = state.currentModule.quantidadePares;
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
                
                entry.resultados.forEach((res) => {
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
                        <td>${res.par}</td>
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
});