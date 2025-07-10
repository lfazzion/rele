// --- Definições de UUIDs do Bluetooth ---
const BLE_SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
const BLE_RECEIVE_CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
const BLE_SEND_CHARACTERISTIC_UUID = "a4d23253-2778-436c-9c23-2c1b50d87635";

// --- Referências aos Elementos da UI ---
const connectButton = document.getElementById("connect-button");
const statusCircle = document.getElementById("status-circle");
const statusText = document.getElementById("status-text");
const logConsole = document.getElementById("log-console");

// Seções interativas
const commandSection = document.getElementById("command-section");
const interactionSection = document.getElementById("interaction-section");
const promptText = document.getElementById("prompt-text");
const inputArea = document.getElementById("input-area");
const responseInput = document.getElementById("response-input");
const sendResponseButton = document.getElementById("send-response-button");
const calibrateButton = document.getElementById("calibrate-button");
const verifyButton = document.getElementById("verify-button");

// REMOVIDO: Referências ao botão de confirmação, pois ele não existe mais
// const confirmationArea = document.getElementById("confirmation-area");
// const confirmButton = document.getElementById("confirm-button");

// --- Variáveis Globais ---
let bleDevice = null;
let sendCharacteristic = null;

// --- Funções de Controle da UI ---
function updateConnectionStatus(status) {
    statusCircle.className = "circle";
    switch (status) {
        case "disconnected":
            statusText.textContent = "Desconectado";
            statusCircle.classList.add("disconnected");
            commandSection.classList.add("hidden");
            interactionSection.classList.add("hidden");
            break;
        case "connecting":
            statusText.textContent = "Conectando...";
            statusCircle.classList.add("connecting");
            break;
        case "connected":
            statusText.textContent = "Conectado";
            statusCircle.classList.add("connected");
            break;
    }
}

// SIMPLIFICADO: A função agora só tem dois estados de UI a gerenciar
function showUiState(state) {
    // Esconde tudo primeiro
    commandSection.classList.add("hidden");
    interactionSection.classList.add("hidden");
    inputArea.classList.add("hidden");

    switch (state) {
        case "idle": // Esperando comando C ou V
            commandSection.classList.remove("hidden");
            break;
        case "awaiting_number": // ESP pediu "Entre com a quantidade..."
            interactionSection.classList.remove("hidden");
            inputArea.classList.remove("hidden");
            break;
        // REMOVIDO: o estado 'awaiting_confirmation' não é mais necessário
    }
}

function appendToLog(text) {
    logConsole.innerHTML += `> ${text}\n`;
    logConsole.scrollTop = logConsole.scrollHeight; // Auto-scroll
}

// --- Funções do Web Bluetooth ---

// ALTERADO: A lógica de confirmação agora é automática
function handleDataReceived(event) {
    const receivedText = new TextDecoder().decode(event.target.value).trim();
    appendToLog(receivedText);

    if (receivedText.includes("Entre com a quantidade de terminais")) {
        promptText.textContent = "Digite o número de terminais:";
        showUiState("awaiting_number");
    } else if (receivedText.includes("Confirme para continuar")) {
        // A MÁGICA ACONTECE AQUI!
        // Ao receber a mensagem, a confirmação é enviada automaticamente.
        appendToLog("Confirmação automática enviada.");
        sendCommand('1'); 
    } else if (receivedText.includes("finalizada") || receivedText.includes("pronto")) {
        promptText.textContent = "Aguardando comando...";
        showUiState("idle");
    }
}

function onDisconnected() {
    updateConnectionStatus("disconnected");
    bleDevice = null;
    sendCharacteristic = null;
}

async function connectToESP32() {
    try {
        updateConnectionStatus("connecting");
        bleDevice = await navigator.bluetooth.requestDevice({
            filters: [{ name: "Jiga de Teste Interativa" }],
            optionalServices: [BLE_SERVICE_UUID],
        });

        bleDevice.addEventListener("gattserverdisconnected", onDisconnected);
        const server = await bleDevice.gatt.connect();
        const service = await server.getPrimaryService(BLE_SERVICE_UUID);
        const receiveCharacteristic = await service.getCharacteristic(BLE_SEND_CHARACTERISTIC_UUID);
        
        await receiveCharacteristic.startNotifications();
        receiveCharacteristic.addEventListener("characteristicvaluechanged", handleDataReceived);
        
        sendCharacteristic = await service.getCharacteristic(BLE_RECEIVE_CHARACTERISTIC_UUID);
        updateConnectionStatus("connected");
        showUiState("idle"); 
    } catch (error) {
        console.error("Erro na conexão Bluetooth:", error);
        updateConnectionStatus("disconnected");
    }
}

async function sendCommand(command) {
    if (!sendCharacteristic) return;
    try {
        const encoder = new TextEncoder();
        await sendCharacteristic.writeValue(encoder.encode(command.toString()));
        appendToLog(`Enviado: ${command}`);
    } catch (error) {
        console.error("Erro ao enviar comando:", error);
    }
}

// --- Listeners de Eventos ---
connectButton.addEventListener("click", () => {
    bleDevice && bleDevice.gatt.connected ? bleDevice.gatt.disconnect() : connectToESP32();
});

calibrateButton.addEventListener("click", () => sendCommand('C'));
verifyButton.addEventListener("click", () => sendCommand('V'));

sendResponseButton.addEventListener("click", () => {
    const number = responseInput.value;
    if (number) {
        sendCommand(number);
        responseInput.value = "";
        promptText.textContent = "Processando...";
        interactionSection.classList.add("hidden"); 
    }
});

// REMOVIDO: O listener do botão de confirmação não é mais necessário.
// confirmButton.addEventListener("click", ...);

// --- Estado Inicial ---
updateConnectionStatus("disconnected");