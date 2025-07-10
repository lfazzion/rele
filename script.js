document.addEventListener("DOMContentLoaded", () => {
  // --- Definições de UUIDs e Configs ---
  const BLE_SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
  const BLE_RECEIVE_CHARACTERISTIC_UUID =
    "beb5483e-36e1-4688-b7f5-ea07361b26a8";
  const BLE_SEND_CHARACTERISTIC_UUID = "a4d23253-2778-436c-9c23-2c1b50d87635";
  const MAX_RECONNECTION_ATTEMPTS = 3;

  // --- Configurações do Firebase ---
  const firebaseConfig = {
    apiKey: "AIzaSyBBJWqqig7VM8wio5MNuv_UwF6TRGKK720",
    authDomain: "jiga-de-teste.firebaseapp.com",
    databaseURL: "https://jiga-de-teste-default-rtdb.firebaseio.com",
    projectId: "jiga-de-teste",
    storageBucket: "jiga-de-teste.firebasestorage.app",
    messagingSenderId: "908087994475",
    appId: "1:908087994475:web:feaf5ae804b0c2838a692e",
  };

  // --- Inicialização do Firebase ---
  if (!firebase.apps.length) {
    firebase.initializeApp(firebaseConfig);
  }
  const database = firebase.database();

  // --- Referências aos Elementos da UI ---
  const ui = {
    connectButton: document.getElementById("connect-button"),
    viewHistoryButton: document.getElementById("view-history-button"),
    statusCircle: document.getElementById("status-circle"),
    statusText: document.getElementById("status-text"),
    appContent: document.getElementById("app-content"),
    welcomeMessage: document.getElementById("welcome-message"),
    setupScreen: document.getElementById("setup-screen"),
    calibrateTerminalsInput: document.getElementById("calibrate-terminals"),
    startCalibrationButton: document.getElementById("start-calibration-button"),
    verifyTerminalsInput: document.getElementById("verify-terminals"),
    startVerificationButton: document.getElementById(
      "start-verification-button",
    ),
    progressScreen: document.getElementById("progress-screen"),
    progressTitle: document.getElementById("progress-title"),
    progressStatusText: document.getElementById("progress-status-text"),
    progressBar: document.getElementById("progress-bar"),
    stepIndicators: document.getElementById("step-indicators"),
    resultsScreen: document.getElementById("results-screen"),
    resultsTableContainer: document.getElementById("results-table-container"),
    runNewTestButton: document.getElementById("run-new-test-button"),
    historyScreen: document.getElementById("history-screen"),
    historyContainer: document.getElementById("history-container"),
    backToSetupButton: document.getElementById("back-to-setup-button"),
    themeToggle: document.getElementById("theme-toggle-checkbox"),
    actionNotification: document.getElementById("action-notification"),
  };

  // --- Variáveis de Estado da Aplicação ---
  let bleDevice = null;
  let sendCharacteristic = null;
  let isConnecting = false;
  let reconnectionAttempts = 0;
  let state = {
    operation: null,
    numTerminals: 0,
    totalSteps: 0,
    currentStep: 0,
    results: [],
  };
  let historyListener = null;

  // --- Funções de Gerenciamento de UI ---
  function showScreen(screenName) {
    const screens = [
      "setupScreen",
      "progressScreen",
      "resultsScreen",
      "historyScreen",
    ];
    screens.forEach((screen) => {
      if (ui[screen]) ui[screen].classList.add("hidden");
    });
    if (ui[screenName]) ui[screenName].classList.remove("hidden");
  }

  // --- ALTERAÇÃO REALIZADA ---
  // Adicionado o estado "reconnecting" e o parâmetro "attempt"
  function updateConnectionStatus(status, attempt = 0) {
    ui.statusCircle.className = "circle";
    switch (status) {
      case "disconnected":
        ui.statusText.textContent = "Desconectado";
        ui.connectButton.textContent = "Conectar";
        ui.statusCircle.classList.add("disconnected");
        ui.appContent.classList.add("hidden");
        ui.welcomeMessage.classList.remove("hidden");
        ui.viewHistoryButton.classList.add("hidden");
        ui.connectButton.disabled = false;
        break;
      case "connecting":
        ui.statusText.textContent = "Conectando...";
        ui.connectButton.textContent = "Aguarde...";
        ui.statusCircle.classList.add("connecting");
        ui.connectButton.disabled = true;
        break;
      case "reconnecting": // Novo estado para feedback do usuário
        ui.statusText.textContent = `Reconectando... (${attempt}/${MAX_RECONNECTION_ATTEMPTS})`;
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
        ui.viewHistoryButton.classList.remove("hidden");
        showScreen("setupScreen");
        ui.connectButton.disabled = false;
        break;
    }
  }

  function startProcess(operation, numTerminals) {
    state.operation = operation;
    state.numTerminals = numTerminals;
    state.currentStep = 0;
    state.results = [];
    state.totalSteps = operation === "verify" ? numTerminals * 2 : numTerminals;
    ui.progressTitle.textContent =
      operation === "calibrate" ? "Calibrando..." : "Verificando...";
    ui.stepIndicators.innerHTML = "";
    for (let i = 1; i <= state.totalSteps; i++) {
      const indicator = document.createElement("div");
      indicator.className = "step-indicator";
      indicator.textContent = i;
      indicator.id = `step-${i}`;
      ui.stepIndicators.appendChild(indicator);
    }
    showScreen("progressScreen");
    updateProgressUI();
    sendCommand(operation === "calibrate" ? "C" : "V");
  }

  function updateProgressUI() {
    const progressPercentage =
      state.totalSteps > 0 ? (state.currentStep / state.totalSteps) * 100 : 0;
    ui.progressBar.style.width = `${progressPercentage}%`;
    for (let i = 1; i <= state.totalSteps; i++) {
      const indicator = document.getElementById(`step-${i}`);
      if (i < state.currentStep) {
        indicator.className = "step-indicator completed";
      } else if (i === state.currentStep) {
        indicator.className = "step-indicator active";
      } else {
        indicator.className = "step-indicator";
      }
    }
    if (state.currentStep > 0) {
      const currentTerminal =
        state.operation === "verify"
          ? Math.ceil(state.currentStep / 2)
          : state.currentStep;
      const currentCycle =
        state.operation === "verify"
          ? state.currentStep % 2 === 1
            ? 1
            : 2
          : 1;
      ui.progressStatusText.textContent = `Lendo terminal ${currentTerminal}, ciclo ${currentCycle}...`;
    } else {
      ui.progressStatusText.textContent = "Enviando comando inicial...";
    }
  }

  function renderResults() {
    let tableHTML = `<table><thead><tr><th>Ciclo</th><th>Terminal</th><th>Resistência (Ω)</th></tr></thead><tbody>`;
    state.results.forEach((res) => {
      tableHTML += `<tr><td>${res.cycle}</td><td>${res.terminal}</td><td>${res.value}</td></tr>`;
    });
    tableHTML += `</tbody></table>`;
    ui.resultsTableContainer.innerHTML = tableHTML;
    showScreen("resultsScreen");
  }

  // --- Funções do Firebase ---
  function saveVerificationResults() {
    if (state.operation !== "verify" || state.results.length === 0) return;
    const timestamp = new Date().toISOString();
    const newVerification = {
      date: timestamp,
      terminals: state.numTerminals,
      results: state.results,
    };
    database
      .ref("verifications")
      .push(newVerification)
      .then(() => console.log("Resultado salvo no Firebase."))
      .catch((error) => console.error("Erro ao salvar no Firebase:", error));
  }

  function loadAndShowHistory() {
    showScreen("historyScreen");
    ui.historyContainer.innerHTML = "<p>Carregando histórico...</p>";
    const verificationsRef = database.ref("verifications").orderByChild("date");
    historyListener = verificationsRef.on(
      "value",
      (snapshot) => {
        const data = snapshot.val();
        if (data) {
          renderHistory(data);
        } else {
          ui.historyContainer.innerHTML = "<p>Nenhum histórico encontrado.</p>";
        }
      },
      (error) => {
        console.error("Erro ao carregar histórico:", error);
        ui.historyContainer.innerHTML =
          "<p>Erro ao carregar o histórico. Tente novamente.</p>";
      },
    );
  }

  function renderHistory(data) {
    ui.historyContainer.innerHTML = "";
    const historyItems = Object.keys(data)
      .map((key) => ({ id: key, ...data[key] }))
      .reverse();
    historyItems.forEach((item) => {
      const itemEl = document.createElement("div");
      itemEl.className = "history-item";
      const date = new Date(item.date).toLocaleString("pt-BR");
      let detailsTable = `<table><thead><tr><th>Ciclo</th><th>Terminal</th><th>Resistência</th></tr></thead><tbody>`;
      item.results.forEach((res) => {
        detailsTable += `<tr><td>${res.cycle}</td><td>${res.terminal}</td><td>${res.value} Ω</td></tr>`;
      });
      detailsTable += `</tbody></table>`;
      itemEl.innerHTML = `
                <div class="history-item-header">
                    <span><strong>Data:</strong> ${date}</span>
                    <button class="delete-btn" data-key="${item.id}" title="Excluir Registro">&times;</button>
                </div>
                <p><strong>Terminais Testados:</strong> ${item.terminals}</p>
                <details>
                    <summary>Ver Detalhes</summary>
                    ${detailsTable}
                </details>`;
      ui.historyContainer.appendChild(itemEl);
    });
    ui.historyContainer.querySelectorAll(".delete-btn").forEach((button) => {
      button.addEventListener("click", (e) => {
        const key = e.target.getAttribute("data-key");
        deleteHistoryItem(key);
      });
    });
  }

  function deleteHistoryItem(key) {
    if (confirm("Tem certeza que deseja excluir este registro do histórico?")) {
      database
        .ref("verifications/" + key)
        .remove()
        .then(() => console.log("Registro excluído com sucesso."))
        .catch((error) => alert("Erro ao excluir: " + error.message));
    }
  }

  // --- Funções do Web Bluetooth ---
  function parseResultString(text) {
    const match = text.match(
      /Resultado \[ciclo (\d+)\].*Res (\d+): ([\d\.-]+)/,
    );
    if (match) {
      return {
        cycle: parseInt(match[1]) + 1,
        terminal: parseInt(match[2]) + 1,
        value: parseFloat(match[3]),
      };
    }
    return null;
  }

  function handleDataReceived(event) {
    const receivedText = new TextDecoder().decode(event.target.value).trim();
    console.log("Recebido:", receivedText);

    if (receivedText.includes("Entre com a quantidade de terminais")) {
      sendCommand(state.numTerminals);
    } else if (receivedText.includes("Preparado para a proxima leitura")) {
      ui.actionNotification.classList.remove("hidden");
      state.currentStep++;
      updateProgressUI();
    } else if (receivedText.startsWith("Resultado")) {
      const result = parseResultString(receivedText);
      if (result) state.results.push(result);
    } else if (receivedText.includes("finalizada")) {
      if (state.operation === "verify") {
        saveVerificationResults();
        renderResults();
      } else {
        alert("Calibração finalizada com sucesso!");
        showScreen("setupScreen");
      }
    }
  }

  async function sendCommand(command) {
    if (!sendCharacteristic) return;
    try {
      const encoder = new TextEncoder();
      await sendCharacteristic.writeValueWithoutResponse(
        encoder.encode(command.toString()),
      );
      ui.actionNotification.classList.add("hidden");
      console.log("Enviado:", command);
    } catch (error) {
      console.error("Erro ao enviar comando:", error);
      alert(`Erro ao enviar comando: ${error.message}`);
    }
  }

  // --- LÓGICA DE CONEXÃO E RECONEXÃO CORRIGIDA ---

  // --- ALTERAÇÃO REALIZADA ---
  // Função onDisconnected agora controla o status da UI e reseta ao final.
  function onDisconnected() {
    if (!bleDevice) return;

    sendCharacteristic = null;
    // Se o usuário não desconectou de propósito, tenta reconectar
    if (reconnectionAttempts < MAX_RECONNECTION_ATTEMPTS) {
      reconnectionAttempts++;
      console.log(
        `Conexão perdida. Tentando reconectar... (Tentativa ${reconnectionAttempts})`,
      );
      updateConnectionStatus("reconnecting", reconnectionAttempts);
      setTimeout(connectToDevice, 2000);
    } else {
      console.log("Falha na reconexão automática. Aguardando ação do usuário.");
      bleDevice = null;
      updateConnectionStatus("disconnected"); // CORREÇÃO: Reseta o status da UI
    }
  }

  async function connectToDevice() {
    if (isConnecting || !bleDevice) {
      return;
    }
    isConnecting = true;

    try {
      const server = await bleDevice.gatt.connect();
      const service = await server.getPrimaryService(BLE_SERVICE_UUID);
      const receiveCharacteristic = await service.getCharacteristic(
        BLE_SEND_CHARACTERISTIC_UUID,
      );
      await receiveCharacteristic.startNotifications();
      receiveCharacteristic.addEventListener(
        "characteristicvaluechanged",
        handleDataReceived,
      );
      sendCharacteristic = await service.getCharacteristic(
        BLE_RECEIVE_CHARACTERISTIC_UUID,
      );
      console.log("Dispositivo conectado com sucesso!");
      updateConnectionStatus("connected");
      reconnectionAttempts = 0;
    } catch (error) {
      console.error("Falha na tentativa de conexão:", error);
    } finally {
      isConnecting = false;
    }
  }

  // --- Gerenciamento do Tema (Modo Escuro) ---
  function applyTheme(theme) {
    if (theme === "dark") {
      document.body.classList.add("dark-mode");
      ui.themeToggle.checked = true;
    } else {
      document.body.classList.remove("dark-mode");
      ui.themeToggle.checked = false;
    }
  }

  // --- Listeners de Eventos ---
  ui.connectButton.addEventListener("click", async () => {
    if (bleDevice && bleDevice.gatt.connected) {
      reconnectionAttempts = MAX_RECONNECTION_ATTEMPTS; // Impede reconexão ao desconectar manualmente
      bleDevice.gatt.disconnect();
    } else {
      try {
        updateConnectionStatus("connecting"); // Atualiza status antes de pedir o dispositivo
        const device = await navigator.bluetooth.requestDevice({
          filters: [{ name: "Jiga de Teste Interativa" }],
          optionalServices: [BLE_SERVICE_UUID],
        });
        bleDevice = device;
        bleDevice.addEventListener("gattserverdisconnected", onDisconnected);
        reconnectionAttempts = 0;
        await connectToDevice();
      } catch (error) {
        console.error("Erro ao selecionar o dispositivo:", error);
        updateConnectionStatus("disconnected"); // Volta ao estado desconectado se o usuário cancelar
      }
    }
  });

  ui.startCalibrationButton.addEventListener("click", () => {
    const num = parseInt(ui.calibrateTerminalsInput.value);
    if (num > 0 && num <= 8) startProcess("calibrate", num);
  });

  ui.startVerificationButton.addEventListener("click", () => {
    const num = parseInt(ui.verifyTerminalsInput.value);
    if (num > 0 && num <= 8) startProcess("verify", num);
  });

  ui.runNewTestButton.addEventListener("click", () =>
    showScreen("setupScreen"),
  );

  ui.viewHistoryButton.addEventListener("click", () => {
    loadAndShowHistory();
  });

  ui.backToSetupButton.addEventListener("click", () => {
    if (historyListener) {
      database.ref("verifications").off("value", historyListener);
      historyListener = null;
    }
    showScreen("setupScreen");
  });

  ui.themeToggle.addEventListener("change", () => {
    const newTheme = ui.themeToggle.checked ? "dark" : "light";
    localStorage.setItem("theme", newTheme);
    applyTheme(newTheme);
  });

  // --- Estado Inicial da Aplicação ---
  const savedTheme = localStorage.getItem("theme") || "light";
  applyTheme(savedTheme);
  updateConnectionStatus("disconnected");
});
