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
const res1Display = document.getElementById("res1-display");
const res2Display = document.getElementById("res2-display");
const avgDisplay = document.getElementById("avg-display");
const historyList = document.getElementById("history-list");

// Variáveis globais
let bleDevice = null;
let sendCharacteristic = null;
let db = null;

// --- Inicialização e Funções do Firebase ---
try {
  firebase.initializeApp(firebaseConfig);
  db = firebase.database();
  console.log("Firebase inicializado.");
  fetchHistoryFromFirebase(); // Busca o histórico ao carregar
} catch (e) {
  console.error("Erro ao inicializar o Firebase.", e);
}

function saveVerificationResults(r1, r2, media) {
  if (!db) return;
  const dataToSave = {
    timestamp: new Date().toISOString(),
    resistencia1: r1,
    resistencia2: r2,
    media: media,
  };
  db.ref("verificacoes").push(dataToSave);
}

function fetchHistoryFromFirebase() {
  if (!db) return;
  const historyRef = db.ref("verificacoes").limitToLast(10);
  historyRef.on("value", (snapshot) => {
    historyList.innerHTML = "";
    if (!snapshot.exists()) {
      historyList.innerHTML = "<li>Nenhum histórico encontrado.</li>";
      return;
    }
    snapshot.forEach((childSnapshot) => {
      const data = childSnapshot.val();
      const date = new Date(data.timestamp).toLocaleString("pt-BR");
      const listItem = document.createElement("li");
      listItem.textContent = `[${date}] R1: ${data.resistencia1.toFixed(3)}Ω, R2: ${data.resistencia2.toFixed(3)}Ω, Média: ${data.media.toFixed(3)}Ω`;
      historyList.prepend(listItem);
    });
  });
}

// --- Funções de Controle da UI ---
function updateConnectionStatus(status) {
  statusCircle.className = "circle";
  let isConnected = false;
  switch (status) {
    case "disconnected":
      statusText.textContent = "Desconectado";
      statusCircle.classList.add("disconnected");
      break;
    case "connecting":
      statusText.textContent = "Conectando...";
      statusCircle.classList.add("connecting");
      break;
    case "connected":
      statusText.textContent = "Conectado";
      statusCircle.classList.add("connected");
      isConnected = true;
      break;
  }
  connectButton.disabled = status === "connecting";
  calibrateButton.disabled = !isConnected;
  verifyButton.disabled = !isConnected;
}

function setButtonsLoading(isLoading) {
  const buttons = [calibrateButton, verifyButton];
  buttons.forEach((button) => {
    button.disabled = isLoading;
    if (isLoading) {
      button.classList.add("loading");
    } else {
      button.classList.remove("loading");
    }
  });
  // Re-habilita os botões apenas se estiver conectado
  if (!isLoading && bleDevice && bleDevice.gatt.connected) {
    calibrateButton.disabled = false;
    verifyButton.disabled = false;
  }
}

// --- Funções do Web Bluetooth ---

// ALTERADO: Lida com dados e atualiza a nova UI
function handleDataReceived(event) {
  const receivedText = new TextDecoder().decode(event.target.value);
  console.log(`Recebido: ${receivedText}`);
  setButtonsLoading(false); // Para o feedback de carregamento

  if (receivedText.includes("Resistencia")) {
    const lines = receivedText.trim().split("\n");
    const values = [];
    lines.forEach((line) => {
      const value = parseFloat(line.split(":")[1].trim());
      if (!isNaN(value)) {
        values.push(value);
      }
    });

    if (values.length >= 2) {
      const [r1, r2] = values;
      const media = (r1 + r2) / 2;
      res1Display.textContent = `${r1.toFixed(3)} Ω`;
      res2Display.textContent = `${r2.toFixed(3)} Ω`;
      avgDisplay.textContent = `${media.toFixed(3)} Ω`;
      saveVerificationResults(r1, r2, media);
    }
  } else {
    // Mostra outras mensagens (como "Calibração finalizada") em um alerta
    alert(receivedText);
  }
}

// CORRIGIDO: Lida com a desconexão e limpa o estado
function onDisconnected() {
  console.log("Dispositivo desconectado.");
  updateConnectionStatus("disconnected");
  // Limpa os displays
  res1Display.textContent = "-- Ω";
  res2Display.textContent = "-- Ω";
  avgDisplay.textContent = "-- Ω";
  // Limpa a referência do dispositivo para forçar uma nova busca
  bleDevice = null;
  sendCharacteristic = null;
}

async function connectToESP32() {
  // A lógica de reconexão é implicitamente corrigida ao zerar bleDevice em onDisconnected
  try {
    updateConnectionStatus("connecting");
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
  } catch (error) {
    console.error("Erro na conexão Bluetooth:", error);
    alert(`Erro: ${error.message}`);
    updateConnectionStatus("disconnected");
  }
}

// ALTERADO: Envia comando com feedback visual
async function sendCommand(command) {
  if (!sendCharacteristic) return;
  try {
    setButtonsLoading(true);
    const encoder = new TextEncoder();
    await sendCharacteristic.writeValue(encoder.encode(command.toString()));
  } catch (error) {
    console.error("Erro ao enviar comando:", error);
    alert(`Erro ao enviar comando: ${error.message}`);
    setButtonsLoading(false);
  }
}

// --- Listeners de Eventos ---
connectButton.addEventListener("click", () => {
  // Se estiver conectado e o botão for clicado, ele desconecta.
  if (bleDevice && bleDevice.gatt.connected) {
    bleDevice.gatt.disconnect();
  } else {
    connectToESP32();
  }
});
calibrateButton.addEventListener("click", () => sendCommand(1));
verifyButton.addEventListener("click", () => sendCommand(2));

// Estado inicial da UI
updateConnectionStatus("disconnected");
