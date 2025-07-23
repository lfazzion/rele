/* 
  Referências:
  Documentação da biblioteca ADS1X15: https://github.com/RobTillaart/ADS1X15
  Manipulação String -> Array: https://arduino.stackexchange.com/questions/77125/convert-string-to-array
  Exemplo BluetoothSerial: https://github.com/espressif/arduino-esp32/blob/master/libraries/BluetoothSerial/examples/SerialToSerialBT/SerialToSerialBT.ino
*/

#include <Wire.h>
#include "ADS1X15.h"
#include "BluetoothSerial.h"

#define LED_CONT 4
#define RGB_RED 14
#define RGB_GREEN 27
#define RGB_BLUE 26
#define RELAY_DC 2
#define RELAY_AC 12
#define BUTTON 15

#define MEAN 20
#define res_ref 99.781

ADS1115 ADS(0x48);
BluetoothSerial SerialBT;

float res[2][8] = { 0.0 };
float res_cal = 0.0;
bool relay_state = 0;
String res_result = "";
unsigned long startTime = 0;

// --- Variáveis Globais para controle de estado ---
int num_contacts = 0;
int relay_to_use = 0;
bool is_configured = false;
bool is_calibrated = false;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  SerialBT.begin("ESP32_Miliohmimetro");

  pinMode(LED_CONT, OUTPUT);
  pinMode(RGB_RED, OUTPUT);
  pinMode(RGB_GREEN, OUTPUT);
  pinMode(RGB_BLUE, OUTPUT);
  pinMode(RELAY_AC, OUTPUT);
  pinMode(RELAY_DC, OUTPUT);
  pinMode(BUTTON, INPUT);

  digitalWrite(RELAY_DC, 0);
  digitalWrite(RELAY_AC, 0);
  ADS.begin();
  ADS.setGain(1);
}

void setColor(int R, int G, int B) {
  analogWrite(RGB_RED,   R);
  analogWrite(RGB_GREEN, G);
  analogWrite(RGB_BLUE,  B);
}

void state_RGB(char state) {
  switch (state) {
    case 'O':
      digitalWrite(RGB_RED, 0);
      digitalWrite(RGB_GREEN, 0);
      digitalWrite(RGB_BLUE, 1);
      break;
    case 'B':
      digitalWrite(RGB_RED, 0);
      digitalWrite(RGB_GREEN, 1);
      digitalWrite(RGB_BLUE, 0);
      break;
    case 'R':
      digitalWrite(RGB_RED, 1);
      digitalWrite(RGB_GREEN, 0);
      digitalWrite(RGB_BLUE, 0);
      break;
  }
}

void reset_output() {
  digitalWrite(RGB_RED, 0);
  digitalWrite(RGB_GREEN, 0);
  digitalWrite(RGB_BLUE, 0);
  digitalWrite(RELAY_DC, 0);
  digitalWrite(RELAY_AC, 0);
}

float get_res() {
  if (ADS.isConnected()) {
    ADS.setDataRate(0);  //  0 = slow   4 = medium   7 = fast

    int16_t val_01 = ADS.readADC_Differential_0_1();
    int16_t val_13 = ADS.readADC_Differential_1_3();
    float volts_ref = ADS.toVoltage(val_01);
    float volts = ADS.toVoltage(val_13);

    return (res_ref * volts) / volts_ref;
  } else {
    //SerialBT.println("ERRO:A0");
    return 0.0;
  }
}

void action_relay(int relay_action) {
  relay_state = !relay_state;
  digitalWrite(relay_action, !relay_state);
}

void wait_confirmation() {
  // MENSAGEM ADICIONADA
  //Serial.println("Aperte o botão!");
  digitalWrite(LED_CONT, 1);
  while (digitalRead(BUTTON) == 0) {}
  digitalWrite(LED_CONT, 0);
}

void calibrate() {
  res_cal = 0.0;
  for (int i = 0; i < MEAN; i++) {
    res_cal += get_res();
  }
  res_cal = res_cal / MEAN;
  is_calibrated = true;
  //Serial.println(res_cal, 4);
}

void read_res(int n, int relay_action) {
  res_result += "V";
  for (int k = 0; k < 2; k++) {
    for (int j = 0; j < n; j++) {
      wait_confirmation();
      res[k][j] = 0.0;
      for (int i = 0; i < MEAN; i++) {
        res[k][j] += get_res() - res_cal;
      }
      res[k][j] = res[k][j] / MEAN;
      //Serial.println(res[k][j]);
      res_result = res_result + ";C" + String(j + 1) + String(k + 1) + ":" + String(res[k][j]);
    }
    action_relay(relay_action);
  }
}

void loop() {
  /*
    Opções para o primeiro caractere:
    C -> Calibração;
    V -> Iniciar Verificação dos contatos;
    S -> Configurar o relé;
    R -> Receber o resultado do relé;
  
    if (SerialBT.available() > 0) {
          String receivedData = SerialBT.readString();
          receivedData.trim();
          char Op = receivedData[0];
          receivedData = receivedData.substring(1, receivedData.length());
          Serial.println(Op);
          Serial.println(receivedData);
          if(Op == 'C'){
              if(is_configured){
                  calibrate();
              } else {
                  SerialBT.println("ERRO:S0");
              }
          }else if(Op == 'V'){
              if(is_calibrated){
                  state_RGB('O');
                  read_res(num_contacts, relay_to_use);
                  SerialBT.println(res_result.substring(0, res_result.length()-1));
                  reset_output();
              }else{
                  String erro = "ERRO:C0";
                  if(is_configured){
                      erro += ";S0";
                  }
                  SerialBT.println(erro);
              }
          }else if(Op == 'S'){
              is_calibrated = false;
              String type = receivedData.substring(receivedData.length() - 2);
              String contactsStr = receivedData.substring(0, receivedData.length() - 2);

              num_contacts = contactsStr.toInt();

              if (type.equalsIgnoreCase("AC")) {
                  relay_to_use = RELAY_AC;
              } else if (type.equalsIgnoreCase("DC")) {
                  relay_to_use = RELAY_DC;
              }

              if (num_contacts > 0 && num_contacts <= 8) {
                  is_configured = true;
              } else {
                  SerialBT.println("ERRO:S1");
              }

          }else if(Op == 'R'){
              state_RGB(receivedData[0]);
          }
      }
  */

  /*
    C -> CONTATO 1
    D -> CONTATO 2
    1 -> OFF
    2 -> NA
    3 -> NF
    ; -> ENCERRA MENSAGEM
    */
    String contactNumber = "1";
    String contactType = "2";

  if (SerialBT.available() > 0 && is_calibrated)
  {
    state_RGB('O');
    String message = SerialBT.readStringUntil(';');

    if (message.startsWith("C"))
    {
        contactNumber = "C";
      //indica que vai ler rele 1
    }
    else if (message.startsWith("D")) {
        contactNumber = "D";
      //indica que vai ler rele 2
    }
    message.remove(0, 1);  //apaga a letra inicial
    if (message == "2") 
    {
        contactType = "2";
      //contato NA
    }
    else if (message == "3") {
        contactType = "3";
      //contato NF
    }

    // --- Lógica de Medição e Envio (sem calibração) ---
    
    // Realiza a medição de resistência bruta diretamente.
    wait_confirmation();
    float measuredResistance = get_res() - res_cal;

    // Cria a string de resposta no formato que o App Inventor espera (ex: "C0.1234").
    String message_to_send = contactNumber + String(measuredResistance, 4);

    // Envia o resultado de volta para o aplicativo.
    SerialBT.print(message_to_send);
  }else{
    reset_output();
  }
  startTime = millis();
  while (digitalRead(BUTTON) == 1 && !is_calibrated){
    if(millis()-startTime>3000){
      setColor(255, 255, 0);
      calibrate();
      setColor(255, 0, 127);
      startTime = millis();
      while(millis()-startTime<1500){}
      reset_output();
    }
  }
}
