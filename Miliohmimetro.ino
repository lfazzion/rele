/*
 * Documentação da biblioteca ADS1X15: https://github.com/RobTillaart/ADS1X15
 * 
 */

#include "ADS1X15.h"
#include <Wire.h>

ADS1115 ADS(0x48);

const float res_ref = 100.10;

float res, r0 = 0;
 
void setup() {
  Serial.begin(9600);  // Initialize serial communication with 9600 baud rate
  Wire.begin();        // Initialize I2C communication

  ADS.begin();
  ADS.setGain(0);
}

float get_res(){
  int16_t val_01 = ADS.readADC_Differential_0_1();
  int16_t val_13 = ADS.readADC_Differential_1_3();
  float volts_ref = ADS.toVoltage(val_01);
  float volts = ADS.toVoltage(val_13);  

  return (res_ref*volts)/volts_ref;
}

void calibrate(){
  for(int i = 0; i<10; i++){
    r0 += get_res();
  }
  r0 = r0/10;
  Serial.print("Calibração finalizada. Valor R0: ");
  Serial.println(r0);
}

void loop() {
  if(Serial.available()>0){
    char input = Serial.read();
    switch(input){
      case 'C':
        calibrate();
        break;
      case 'V':
        res = 0;
        for(int i = 0; i<20; i++){
          res += get_res() - r0;
        }
        res = res/20;

        Serial.print("Resistencia: ");
        Serial.print("\t");
        Serial.println(res, 3);
        break;
      default:
        Serial.println("Opção inválida");
    }
  }
}
