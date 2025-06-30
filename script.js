// --------------- CONFIGURAÇÕES (ALTERE AQUI) ---------------

// 1. Cole aqui a configuração do seu projeto Firebase
const firebaseConfig = {
  apiKey: "AIzaSyBBJWqqig7VM8wio5MNuv_UwF6TRGKK720",
  authDomain: "jiga-de-teste.firebaseapp.com",
  databaseURL: "https://jiga-de-teste-default-rtdb.firebaseio.com",
  projectId: "jiga-de-teste",
  storageBucket: "jiga-de-teste.firebasestorage.app",
  messagingSenderId: "908087994475",
  appId: "1:908087994475:web:feaf5ae804b0c2838a692e",
};

// 2. Defina os UUIDs para o serviço e características Bluetooth
//    ESTES VALORES DEVEM SER OS MESMOS NO SEU CÓDIGO DO ESP32!
const BLE_SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
// Característica para receber dados do ESP32 (Tensão)
const BLE_RECEIVE_CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
// Característica para enviar dados para o ESP32 (Comando de Calibração)
const BLE_SEND_CHARACTERISTIC_UUID = "a4d23253-2778-436c-9c23-2c1b50d87635";

// --------------- FIM DAS CONFIGURAÇÕES ---------------

// Referências aos elementos da UI
const connectButton = document.getElementById("connect-button");
const calibrateButton = document.getElementById("calibrate-button");
const statusCircle = document.getElementById("status-circle");
const statusText = document.getElementById("status-text");
const voltageDisplay = document.getElementById("voltage-display");
const historyList = document.getElementById("history-list");

// Variáveis de estado globais
let bleDevice = null;
let bleServer = null;
let receiveCharacteristic = null;
let sendCharacteristic = null;
let db = null;

// Inicialização do Firebase
try {
  firebase.initializeApp(firebaseConfig);
  db = firebase.database();
  console.log("Firebase inicializado com sucesso.");
  fetchHistoryFromFirebase();
} catch (e) {
  console.error(
    "Erro ao inicializar o Firebase. Verifique suas configurações.",
    e,
  );
  historyList.innerHTML =
    "<li>Erro ao conectar ao Firebase. Verifique as credenciais no script.js.</li>";
}

// Atualiza o status visual da conexão
function updateConnectionStatus(status) {
  statusCircle.className = "circle"; // Reseta as classes
  switch (status) {
    case "disconnected":
      statusText.textContent = "Desconectado";
      statusCircle.classList.add("disconnected");
      connectButton.disabled = false;
      calibrateButton.disabled = true;
      connectButton.textContent = "Conectar ao ESP32";
      break;
    case "connecting":
      statusText.textContent = "Conectando...";
      statusCircle.classList.add("connecting");
      connectButton.disabled = true;
      break;
    case "connected":
      statusText.textContent = "Conectado";
      statusCircle.classList.add("connected");
      connectButton.disabled = false;
      calibrateButton.disabled = false;
      connectButton.textContent = "Desconectar";
      break;
  }
}

// Função para lidar com o recebimento de dados do ESP32
function handleDataReceived(event) {
  const value = event.target.value;
  // O ESP32 deve enviar a tensão como um float de 32 bits (4 bytes)
  // Usamos `true` para little-endian, que é o padrão para ESP32
  const voltage = value.getFloat32(0, true).toFixed(2);

  console.log(`Tensão recebida: ${voltage} V`);

  // Atualiza a UI
  voltageDisplay.textContent = `${voltage} V`;

  // Envia para o Firebase
  sendDataToFirebase(parseFloat(voltage));
}

// Função para enviar dados para o Firebase Realtime Database
function sendDataToFirebase(voltage) {
  if (!db) return;
  const timestamp = new Date().toISOString();

  // O método `push` cria um ID único para cada leitura
  db.ref("leituras")
    .push({
      tensao: voltage,
      timestamp: timestamp,
    })
    .catch((error) =>
      console.error("Erro ao enviar dados para o Firebase:", error),
    );
}

// Função para buscar e exibir o histórico do Firebase
function fetchHistoryFromFirebase() {
  if (!db) return;

  const leiturasRef = db.ref("leituras").limitToLast(10);

  // O listener 'value' é em tempo real. Ele será acionado sempre que os dados mudarem.
  leiturasRef.on(
    "value",
    (snapshot) => {
      historyList.innerHTML = ""; // Limpa a lista antiga
      if (!snapshot.exists()) {
        historyList.innerHTML = "<li>Nenhum dado de histórico encontrado.</li>";
        return;
      }

      const data = snapshot.val();
      // Firebase não garante a ordem, então pegamos as chaves e as exibimos
      // na ordem em que estão (limitToLast já as ordena)
      Object.keys(data).forEach((key) => {
        const leitura = data[key];
        const listItem = document.createElement("li");
        const date = new Date(leitura.timestamp);
        const formattedTime = date.toLocaleTimeString("pt-BR");
        listItem.textContent = `Tensão: ${leitura.tensao.toFixed(2)} V - Horário: ${formattedTime}`;
        historyList.prepend(listItem); // Adiciona no topo para ver os mais recentes primeiro
      });
    },
    (error) => {
      console.error("Erro ao buscar dados do Firebase:", error);
      historyList.innerHTML = "<li>Não foi possível carregar o histórico.</li>";
    },
  );
}

// Função para conectar/desconectar do dispositivo BLE
async function connectToESP32() {
  if (bleDevice && bleDevice.gatt.connected) {
    console.log("Desconectando...");
    bleDevice.gatt.disconnect();
    return;
  }

  try {
    updateConnectionStatus("connecting");

    console.log("Solicitando dispositivo Bluetooth...");
    bleDevice = await navigator.bluetooth.requestDevice({
      filters: [{ services: [BLE_SERVICE_UUID] }],
      optionalServices: [BLE_SERVICE_UUID],
    });

    console.log("Dispositivo selecionado:", bleDevice.name);
    bleDevice.addEventListener("gattserverdisconnected", onDisconnected);

    console.log("Conectando ao GATT Server...");
    bleServer = await bleDevice.gatt.connect();

    console.log("Obtendo o Serviço BLE...");
    const service = await bleServer.getPrimaryService(BLE_SERVICE_UUID);

    console.log("Obtendo a Característica de Recebimento...");
    receiveCharacteristic = await service.getCharacteristic(
      BLE_RECEIVE_CHARACTERISTIC_UUID,
    );

    console.log("Iniciando notificações...");
    await receiveCharacteristic.startNotifications();
    receiveCharacteristic.addEventListener(
      "characteristicvaluechanged",
      handleDataReceived,
    );

    console.log("Obtendo a Característica de Envio...");
    sendCharacteristic = await service.getCharacteristic(
      BLE_SEND_CHARACTERISTIC_UUID,
    );

    updateConnectionStatus("connected");
    console.log("Conectado com sucesso!");
  } catch (error) {
    console.error("Erro na conexão Bluetooth:", error);
    alert(`Erro: ${error.message}`);
    updateConnectionStatus("disconnected");
  }
}

// Função chamada quando o dispositivo é desconectado
function onDisconnected() {
  console.log("Dispositivo desconectado.");
  bleDevice = null;
  bleServer = null;
  receiveCharacteristic = null;
  sendCharacteristic = null;
  updateConnectionStatus("disconnected");
  voltageDisplay.textContent = "-- V";
}

// Função para enviar o comando de calibração
async function sendCalibrationCommand() {
  if (!sendCharacteristic) {
    alert("Não conectado à característica de envio.");
    return;
  }

  try {
    // Enviaremos o valor '1' como um comando de calibração.
    // O valor pode ser qualquer coisa que seu ESP32 espere.
    const command = new Uint8Array([1]);
    await sendCharacteristic.writeValue(command);
    console.log("Comando de calibração enviado!");
    alert("Comando de calibração enviado para o ESP32.");
  } catch (error) {
    console.error("Erro ao enviar comando:", error);
    alert(`Erro ao enviar comando: ${error.message}`);
  }
}

// Adiciona os listeners aos botões
connectButton.addEventListener("click", connectToESP32);
calibrateButton.addEventListener("click", sendCalibrationCommand);

// Estado inicial da UI
updateConnectionStatus("disconnected");
