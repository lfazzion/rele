#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h> // Necessário para as notificações funcionarem corretamente

// --- DEFINIÇÕES QUE DEVEM SER IDÊNTICAS ÀS DO ARQUIVO script.js ---
#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
// Característica para ENVIAR dados PARA a página web (Notificação)
#define TX_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8" 
// Característica para RECEBER dados DA página web (Escrita)
#define RX_CHARACTERISTIC_UUID "a4d23253-2778-436c-9c23-2c1b50d87635"

// Variáveis globais para o BLE
BLECharacteristic *pTxCharacteristic; // Ponteiro para a característica de transmissão
BLECharacteristic *pRxCharacteristic; // Ponteiro para a característica de recepção
bool deviceConnected = false;         // Flag para controlar o status da conexão

// Variável para o sensor fake
float voltage = 3.0;

// LED integrado para feedback visual
#define LED_PIN 2

// Classe de Callback para eventos do servidor (conexão e desconexão)
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      digitalWrite(LED_PIN, HIGH); // Acende o LED quando conectado
      Serial.println("Dispositivo Conectado!");
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      digitalWrite(LED_PIN, LOW); // Apaga o LED quando desconectado
      Serial.println("Dispositivo Desconectado!");
      // Reinicia o advertising para que possa ser encontrado novamente
      BLEDevice::startAdvertising(); 
    }
};

// Classe de Callback para eventos de escrita na característica de recepção (RX)
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      // Pega o valor recebido da página web
      // CORREÇÃO APLICADA AQUI: Usando o tipo "String" do Arduino
      String rxValue = pCharacteristic->getValue();

      if (rxValue.length() > 0) {
        Serial.print("Comando recebido: ");
        // O comando é enviado como um array de bytes. Verifica-se o primeiro byte.
        if (rxValue[0] == 1) {
          Serial.println("Comando de Calibração (1)!");
          // Aqui fica a lógica da função de calibração
          // Por exemplo, piscar o LED 3 vezes para confirmar
          for(int i=0; i<3; i++) {
            digitalWrite(LED_PIN, HIGH); delay(100);
            digitalWrite(LED_PIN, LOW); delay(100);
          }
          digitalWrite(LED_PIN, HIGH); // Deixa aceso para indicar que continua conectado
        } else {
          Serial.print("Comando desconhecido: ");
          Serial.println((int)rxValue[0]);
        }
      }
    }
};


void setup() {
  // Inicia a comunicação serial para debug
  Serial.begin(115200);
  Serial.println("Iniciando o servidor BLE...");

  // Configura o pino do LED como saída
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Começa com o LED apagado

  // --- CONFIGURAÇÃO DO SERVIDOR BLE ---

  // 1. Inicializa o dispositivo BLE com um nome
  BLEDevice::init("Meu ESP32");

  // 2. Cria o servidor BLE
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks()); // Define os callbacks de conexão/desconexão

  // 3. Cria o serviço BLE
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // 4. Cria a Característica de Transmissão (TX - Notificação)
  pTxCharacteristic = pService->createCharacteristic(
                      TX_CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  // Adiciona um descritor BLE2902, essencial para que as notificações funcionem com a web
  pTxCharacteristic->addDescriptor(new BLE2902());

  // 5. Cria a Característica de Recepção (RX - Escrita)
  pRxCharacteristic = pService->createCharacteristic(
                       RX_CHARACTERISTIC_UUID,
                       BLECharacteristic::PROPERTY_WRITE
                      );
  pRxCharacteristic->setCallbacks(new MyCallbacks()); // Define o callback para quando recebermos dados

  // 6. Inicia o serviço
  pService->start();

  // 7. Inicia o advertising (anúncio) para que outros dispositivos possam encontrar o ESP32
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID); // Adiciona o UUID do serviço ao anúncio
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); 
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("Servidor BLE iniciado e aguardando conexões.");
}

void loop() {
  // Apenas executa a lógica se um dispositivo estiver conectado
  if (deviceConnected) {
    // Simula uma leitura de sensor.
    // Função seno para gerar a variação na tensão fake.
    voltage = 3.3 + 0.5 * sin(millis() / 1000.0);

    Serial.print("Enviando tensão: ");
    Serial.println(voltage);

    // Envia o novo valor da tensão via notificação BLE
    // O valor deve ser enviado como um array de bytes.
    // Um float tem 4 bytes, então enviamos o ponteiro para a variável e o seu tamanho.
    pTxCharacteristic->setValue((uint8_t*)&voltage, 4);
    pTxCharacteristic->notify();

    // Aguarda 1 segundo antes de enviar a próxima leitura
    delay(1000); 
  }
}
