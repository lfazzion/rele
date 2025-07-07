// --- Configuração do Firebase ---
const firebaseConfig = {
  apiKey: "AIzaSyBBJWqqig7VM8wio5MNuv_UwF6TRGKK720",
  authDomain: "jiga-de-teste.firebaseapp.com",
  databaseURL: "https://jiga-de-teste-default-rtdb.firebaseio.com",
  projectId: "jiga-de-teste",
  storageBucket: "jiga-de-teste.firebasestorage.app",
  messagingSenderId: "908087994475",
  appId: "1:908087994475:web:feaf5ae804b0c2838a692e",
};

// --- Definições de UUIDs do Bluetooth ---
const BLE_SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
const BLE_RECEIVE_CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
const BLE_SEND_CHARACTERISTIC_UUID = "a4d23253-2778-436c-9c23-2c1b50d87635";

// --- Referências aos Elementos da UI ---
const connectButton = document.getElementById("connect-button");
const calibrateButton = document.getElementById("calibrate-button");
const verifyButton = document.getElementById("verify-button");
const statusCircle = document.getElementById("status-circle");
const statusText = document.getElementById("status-text");
const resultsArea = document.getElementById("results-area");

// Variáveis globais de estado
let bleDevice = null;
let sendCharacteristic = null;
let db = null; // Variável para a instância do banco de dados

// --- Inicialização do Firebase ---
try {
  firebase.initializeApp(firebaseConfig);
  db = firebase.database();
  console.log("Firebase inicializado com sucesso.");
} catch (e) {
  console.error("Erro ao inicializar o Firebase.", e);
  alert(
    "Nao foi possivel conectar ao Firebase. Verifique a configuracao no script.js e as regras de seguranca no seu projeto Firebase.",
  );
}

// --- Funções de Controle da UI ---
function updateConnectionStatus(status) {
  statusCircle.className = "circle";
  let isConnected = false;

  switch (status) {
    case "disconnected":
      statusText.textContent = "Desconectado";
      statusCircle.classList.add("disconnected");
      connectButton.textContent = "Conectar ao ESP32";
      break;
    case "connecting":
      statusText.textContent = "Conectando...";
      statusCircle.classList.add("connecting");
      break;
    case "connected":
      statusText.textContent = "Conectado";
      statusCircle.classList.add("connected");
      connectButton.textContent = "Desconectar";
      isConnected = true;
      break;
  }

  connectButton.disabled = status === "connecting";
  calibrateButton.disabled = !isConnected;
  verifyButton.disabled = !isConnected;
}

// --- Funções do Web Bluetooth e Firebase ---

// Função para salvar os resultados no Firebase
function saveResultsToFirebase(resultsText) {
  if (!db) {
    console.error(
      "Conexão com Firebase não estabelecida. Não é possível salvar.",
    );
    return;
  }

  // Processa o texto para criar um objeto estruturado
  const lines = resultsText.trim().split("\n");
  const leituras = {};
  lines.forEach((line) => {
    // Exemplo de linha: "Resistencia 0:	12.345"
    const parts = line.split(":");
    if (parts.length === 2) {
      const channelMatch = parts[0].match(/\d+/); // Pega o número do canal
      const channel = channelMatch ? channelMatch[0] : null;
      const value = parseFloat(parts[1].trim());
      if (channel !== null && !isNaN(value)) {
        leituras[channel] = value;
      }
    }
  });

  // Cria o objeto final para salvar
  const dataToSave = {
    timestamp: new Date().toISOString(),
    resultadoCompleto: resultsText,
    leituras: leituras,
  };

  // Usa push() para criar um registro com ID único
  db.ref("resultados_verificacao")
    .push(dataToSave)
    .then(() => {
      console.log("Resultados salvos no Firebase com sucesso!");
    })
    .catch((error) => {
      console.error("Erro ao salvar resultados no Firebase:", error);
    });
}

// Lida com dados recebidos do ESP32
function handleDataReceived(event) {
  const value = event.target.value;
  const receivedText = new TextDecoder().decode(value);

  console.log(`Texto Recebido: ${receivedText}`);
  resultsArea.textContent = receivedText;

  // NOVO: Salva no Firebase APENAS se for um resultado de verificação
  if (db && receivedText.includes("Resistencia")) {
    saveResultsToFirebase(receivedText);
  }
}

// Conecta/Desconecta do dispositivo BLE
async function connectToESP32() {
  if (bleDevice && bleDevice.gatt.connected) {
    bleDevice.gatt.disconnect();
    return;
  }
  try {
    updateConnectionStatus("connecting");
    resultsArea.textContent = "Procurando por dispositivos...";

    bleDevice = await navigator.bluetooth.requestDevice({
      filters: [{ services: [BLE_SERVICE_UUID] }],
      optionalServices: [BLE_SERVICE_UUID],
    });

    bleDevice.addEventListener("gattserverdisconnected", onDisconnected);
    const server = await bleDevice.gatt.connect();
    const service = await server.getPrimaryService(BLE_SERVICE_UUID);

    const receiveCharacteristic = await service.getCharacteristic(
      BLE_RECEIVE_CHARACTERISTIC_UUID,
    );
    await receiveCharacteristic.startNotifications();
    receiveCharacteristic.addEventListener(
      "characteristicvaluechanged",
      handleDataReceived,
    );

    sendCharacteristic = await service.getCharacteristic(
      BLE_SEND_CHARACTERISTIC_UUID,
    );

    updateConnectionStatus("connected");
    resultsArea.textContent =
      "Conectado com sucesso! Pronto para receber comandos.";
  } catch (error) {
    console.error("Erro na conexão Bluetooth:", error);
    resultsArea.textContent = `Erro: ${error.message}`;
    updateConnectionStatus("disconnected");
  }
}

function onDisconnected() {
  console.log("Dispositivo desconectado.");
  bleDevice = null;
  sendCharacteristic = null;
  updateConnectionStatus("disconnected");
  resultsArea.textContent = "Dispositivo desconectado.";
}

// Envia um comando numérico para o ESP32
async function sendCommand(command) {
  if (!sendCharacteristic) {
    alert("Não está conectado para enviar comandos.");
    return;
  }
  try {
    const encoder = new TextEncoder();
    await sendCharacteristic.writeValue(encoder.encode(command.toString()));
    resultsArea.textContent = `Comando '${command}' enviado. Aguardando resposta...`;
  } catch (error) {
    console.error("Erro ao enviar comando:", error);
    resultsArea.textContent = `Erro ao enviar comando: ${error.message}`;
  }
}

// --- Listeners de Eventos ---
connectButton.addEventListener("click", connectToESP32);
calibrateButton.addEventListener("click", () => sendCommand(1));
verifyButton.addEventListener("click", () => sendCommand(2));

// Estado inicial da UI
updateConnectionStatus("disconnected");
