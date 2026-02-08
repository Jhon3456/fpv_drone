/*
 * Receptor RC de 10 canales usando LoRa y Seeed XIAO nRF52840
 * Compatible con el transmisor ESP32 LoRa (10 canales)
 * Salida iBUS para conectar a controladores de vuelo
 * VERSIÓN OPTIMIZADA - iBUS con failsafe INMEDIATO y REFORZADO
 * AUX1 SE ACTIVA AL MÁXIMO (2000µs) CUANDO SE PIERDE LA SEÑAL
 * LED RGB: Verde = Conectado, Rojo = Sin señal
 * Canales: 4 joystick + 6 auxiliares (AUX1-AUX6)
 */

#include <SPI.h>
#include <LoRa.h>

// Pines conexión LoRa para XIAO nRF52840
#define LORA_NSS   D2   // CS/NSS
#define LORA_RST   D1   // Reset 
#define LORA_DIO0  D0   // DIO0

// Pines del LED RGB interno del XIAO nRF52840
#define LED_RED    11   // LED Rojo
#define LED_GREEN  12   // LED Verde
#define LED_BLUE   13   // LED Azul

// Configuración iBUS
#define IBUS_TX_PIN   D6       // TX pin para UART
#define IBUS_CHANNELS 14       // Número total de canales iBUS
#define IBUS_BAUDRATE 115200

// Límites PWM para iBUS (valores en microsegundos)
#define SERVO_MIN_US    1000
#define SERVO_MAX_US    2000
#define SERVO_CENTER_US 1500

// Array de canales iBUS - TODOS inician en valores seguros
uint16_t ibus_channels[IBUS_CHANNELS] = {
  1500, 1500, 1000, 1500,  // CH1-CH4: Roll, Pitch, Throttle, Yaw
  2000, 1000, 1000, 1000,  // CH5-CH8: AUX1=ON (failsafe), AUX2-4=OFF
  1000, 1000,              // CH9-CH10: AUX5-AUX6 (OFF)
  1000, 1000, 1000, 1000   // CH11-CH14: Reservados OFF
};

// Variables de canales principales
int throttle = 1000;
int pitch = 1500;
int roll = 1500;
int yaw = 1500;
int aux1 = 2000;  // ⚠️ MODIFICADO: Inicia en 2000 (activo en failsafe)
int aux2 = 1000;
int aux3 = 1000;
int aux4 = 1000;
int aux5 = 1000;
int aux6 = 1000;

// Límite máximo de valores recibidos
int max_received = 4096;

// Variables de control de señal
bool signal_lost = true;  // Empezar en modo failsafe
bool failsafe_applied = true;

// Estructura de datos - DEBE coincidir EXACTAMENTE con el transmisor
struct MyData {
  long ch1;    // Canal 1 (0-4095) - Throttle
  long ch2;    // Canal 2 (0-4095) - Yaw
  long ch3;    // Canal 3 (0-4095) - Pitch  
  long ch4;    // Canal 4 (0-4095) - Roll
  bool AUX1;   // Canal 5 - Botón toggle
  bool AUX2;   // Canal 6 - Botón toggle
  bool AUX3;   // Canal 7 - Botón toggle
  bool AUX4;   // Canal 8 - Botón toggle
  bool AUX5;   // Canal 9 - Botón toggle
  bool AUX6;   // Canal 10 - Botón toggle
};

MyData data;

// Control de tiempo para pérdida de señal
unsigned long lastRecvTime = 0;
#define SIGNAL_TIMEOUT 500  // Timeout REDUCIDO a 500ms para respuesta más rápida

// Funciones para control del LED RGB
void setLED(bool red, bool green, bool blue) {
  digitalWrite(LED_RED, !red);
  digitalWrite(LED_GREEN, !green);
  digitalWrite(LED_BLUE, !blue);
}

void ledOff() {
  setLED(false, false, false);
}

void ledRed() {
  setLED(true, false, false);
}

void ledGreen() {
  setLED(false, false, true);
}

void ledYellow() {
  setLED(true, true, false);
}

void resetData() {
  // Valores por defecto SEGUROS en caso de pérdida de señal
  data.ch1 = 0;        // Throttle en mínimo por seguridad
  data.ch2 = 2048;     // Yaw en centro
  data.ch3 = 2048;     // Pitch en centro
  data.ch4 = 2048;     // Roll en centro
  data.AUX1 = true;    // ⚠️ MODIFICADO: AUX1 ON en failsafe
  data.AUX2 = false;   // AUX2 OFF
  data.AUX3 = false;   // AUX3 OFF
  data.AUX4 = false;   // AUX4 OFF
  data.AUX5 = false;   // AUX5 OFF
  data.AUX6 = false;   // AUX6 OFF
}

// Función para enviar paquete iBUS
void sendIBUS() {
  uint8_t packet[32];
  
  // Header del paquete iBUS
  packet[0] = 0x20;  // Longitud del paquete (32 bytes)
  packet[1] = 0x40;  // Comando iBUS
  
  // Datos de canales (2 bytes por canal, little endian)
  for (int i = 0; i < IBUS_CHANNELS; i++) {
    packet[2 + i * 2] = ibus_channels[i] & 0xFF;
    packet[2 + i * 2 + 1] = (ibus_channels[i] >> 8) & 0xFF;
  }
  
  // Calcular checksum
  uint16_t checksum = 0xFFFF;
  for (int i = 0; i < 30; i++) {
    checksum -= packet[i];
  }
  
  // Agregar checksum al final del paquete
  packet[30] = checksum & 0xFF;
  packet[31] = (checksum >> 8) & 0xFF;
  
  // Enviar paquete por UART
  Serial1.write(packet, 32);
}

void updateChannels() {
  // Mapear los valores recibidos (0-4095) a microsegundos PWM (1000-2000)
  int new_throttle = map(data.ch1, 0, 4095, 1000, 2000);
  int new_yaw = map(data.ch2, 0, 4095, 2000, 1000);
  int new_pitch = map(data.ch3, 0, 4095, 1000, 2000);
  int new_roll = map(data.ch4, 0, 4095, 2000, 1000);
  
  // Canales auxiliares: 1000µs = LOW/OFF, 2000µs = HIGH/ON
  int new_aux1 = data.AUX1 ? 2000 : 1000;
  int new_aux2 = data.AUX2 ? 2000 : 1000;
  int new_aux3 = data.AUX3 ? 2000 : 1000;
  int new_aux4 = data.AUX4 ? 2000 : 1000;
  int new_aux5 = data.AUX5 ? 2000 : 1000;
  int new_aux6 = data.AUX6 ? 2000 : 1000;
  
  // Validar rangos antes de aplicar
  new_throttle = constrain(new_throttle, SERVO_MIN_US, SERVO_MAX_US);
  new_yaw = constrain(new_yaw, SERVO_MIN_US, SERVO_MAX_US);
  new_pitch = constrain(new_pitch, SERVO_MIN_US, SERVO_MAX_US);
  new_roll = constrain(new_roll, SERVO_MIN_US, SERVO_MAX_US);
  new_aux1 = constrain(new_aux1, SERVO_MIN_US, SERVO_MAX_US);
  new_aux2 = constrain(new_aux2, SERVO_MIN_US, SERVO_MAX_US);
  new_aux3 = constrain(new_aux3, SERVO_MIN_US, SERVO_MAX_US);
  new_aux4 = constrain(new_aux4, SERVO_MIN_US, SERVO_MAX_US);
  new_aux5 = constrain(new_aux5, SERVO_MIN_US, SERVO_MAX_US);
  new_aux6 = constrain(new_aux6, SERVO_MIN_US, SERVO_MAX_US);
  
  // Actualizar variables locales
  throttle = new_throttle;
  yaw = new_yaw;
  pitch = new_pitch;
  roll = new_roll;
  aux1 = new_aux1;
  aux2 = new_aux2;
  aux3 = new_aux3;
  aux4 = new_aux4;
  aux5 = new_aux5;
  aux6 = new_aux6;
  
  // Actualizar array iBUS con mapeo correcto para controlador de vuelo
  ibus_channels[0] = roll;     // CH1 - Aileron (Roll)
  ibus_channels[1] = pitch;    // CH2 - Elevator (Pitch) 
  ibus_channels[2] = throttle; // CH3 - Throttle
  ibus_channels[3] = yaw;      // CH4 - Rudder (Yaw)
  ibus_channels[4] = aux1;     // CH5 - AUX1
  ibus_channels[5] = aux2;     // CH6 - AUX2
  ibus_channels[6] = aux3;     // CH7 - AUX3
  ibus_channels[7] = aux4;     // CH8 - AUX4
  ibus_channels[8] = aux5;     // CH9 - AUX5
  ibus_channels[9] = aux6;     // CH10 - AUX6
  
  // Canales 11-14 mantienen valores por defecto
  for (int i = 10; i < IBUS_CHANNELS; i++) {
    ibus_channels[i] = 1500;
  }
}

void applyFailsafe() {
  // FORZAR valores de failsafe INMEDIATAMENTE en todas las variables
  
  // 1. Resetear estructura de datos
  resetData();
  
  // 2. FORZAR variables locales a valores seguros
  throttle = 1000;  // CRÍTICO: Throttle al mínimo
  yaw = 1500;       // Centro
  pitch = 1500;     // Centro
  roll = 1500;      // Centro
  aux1 = 2000;      // ⚠️ MODIFICADO: AUX1 AL MÁXIMO (ON)
  aux2 = 1000;      // OFF
  aux3 = 1000;      // OFF
  aux4 = 1000;      // OFF
  aux5 = 1000;      // OFF
  aux6 = 1000;      // OFF
  
  // 3. FORZAR array iBUS con valores seguros - TODOS LOS 14 CANALES
  ibus_channels[0] = 1500;   // CH1  - Roll centro
  ibus_channels[1] = 1500;   // CH2  - Pitch centro
  ibus_channels[2] = 1000;   // CH3  - Throttle MÍNIMO ⚠️ CRÍTICO
  ibus_channels[3] = 1500;   // CH4  - Yaw centro
  ibus_channels[4] = 2000;   // CH5  - AUX1 ON ⚠️ MODIFICADO
  ibus_channels[5] = 1000;   // CH6  - AUX2 OFF
  ibus_channels[6] = 1000;   // CH7  - AUX3 OFF
  ibus_channels[7] = 1000;   // CH8  - AUX4 OFF
  ibus_channels[8] = 1000;   // CH9  - AUX5 OFF
  ibus_channels[9] = 1000;   // CH10 - AUX6 OFF
  ibus_channels[10] = 1000;  // CH11 - OFF
  ibus_channels[11] = 1000;  // CH12 - OFF
  ibus_channels[12] = 1000;  // CH13 - OFF
  ibus_channels[13] = 1000;  // CH14 - OFF
  
  // 4. ENVIAR INMEDIATAMENTE múltiples paquetes failsafe
  for (int i = 0; i < 5; i++) {
    sendIBUS();
    delayMicroseconds(100);
  }
  
  failsafe_applied = true;
  Serial.println("⚠️ FAILSAFE APLICADO - AUX1 ACTIVADO AL MÁXIMO (2000µs)");
}

void initIBUS() {
  Serial1.begin(IBUS_BAUDRATE);
  
  // Inicializar TODOS los 14 canales con valores SEGUROS desde el inicio
  ibus_channels[0] = 1500;   // CH1  - Roll centro
  ibus_channels[1] = 1500;   // CH2  - Pitch centro
  ibus_channels[2] = 1000;   // CH3  - Throttle mínimo
  ibus_channels[3] = 1500;   // CH4  - Yaw centro
  ibus_channels[4] = 2000;   // CH5  - AUX1 ON ⚠️ MODIFICADO
  ibus_channels[5] = 1000;   // CH6  - AUX2 OFF
  ibus_channels[6] = 1000;   // CH7  - AUX3 OFF
  ibus_channels[7] = 1000;   // CH8  - AUX4 OFF
  ibus_channels[8] = 1000;   // CH9  - AUX5 OFF
  ibus_channels[9] = 1000;   // CH10 - AUX6 OFF
  ibus_channels[10] = 1000;  // CH11 - OFF
  ibus_channels[11] = 1000;  // CH12 - OFF
  ibus_channels[12] = 1000;  // CH13 - OFF
  ibus_channels[13] = 1000;  // CH14 - OFF
  
  Serial.println("✓ iBUS inicializado - 14 canales con failsafe reforzado");
  Serial.print("  Baudrate: "); Serial.println(IBUS_BAUDRATE);
  Serial.println("  Mapeo: CH1=Roll, CH2=Pitch, CH3=Throttle, CH4=Yaw");
  Serial.println("  Auxiliares: CH5-10=AUX1-6, CH11-14=Reservados");
  Serial.println("  ⚠️ FAILSAFE: AUX1 se ACTIVA (2000µs) al perder señal");
  Serial.println("  TX Pin: D6");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Configurar LED RGB
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  
  ledYellow();
  
  // Inicializar con valores seguros
  resetData();
  initIBUS();
  applyFailsafe();  // Aplicar failsafe desde el inicio
  
  Serial.println("\n╔═══════════════════════════════════════════════════════╗");
  Serial.println("║  Receptor RC LoRa - 10 CANALES - iBUS Failsafe      ║");
  Serial.println("║  AUX1 ACTIVO AL MÁXIMO EN PÉRDIDA DE SEÑAL          ║");
  Serial.println("╚═══════════════════════════════════════════════════════╝");
  Serial.println("  Canales: 4 Joystick + 6 Auxiliares (AUX1-AUX6)");
  Serial.println("  LED: 🟢 Verde = Conectado | 🔴 Rojo = Sin señal");
  Serial.println("  Timeout: 500ms | Frecuencia: ~140Hz");
  
  SPI.begin();
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  
  if (!LoRa.begin(433E6)) {
    Serial.println("❌ Error: No se pudo inicializar LoRa");
    while (1) {
      ledRed();
      delay(250);
      ledOff();
      delay(250);
    }
  }
  
  // Configuración LoRa - DEBE coincidir con el transmisor
  LoRa.setTxPower(20);
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);
  
  Serial.println("✓ LoRa inicializado correctamente");
  Serial.println("\n⏳ Esperando datos del transmisor...\n");
  
  lastRecvTime = millis();
  ledRed();
  signal_lost = true;
}

void recvData() {
  int packetSize = LoRa.parsePacket();
  
  if (packetSize > 0) {
    if (packetSize == sizeof(MyData)) {
      LoRa.readBytes((uint8_t*)&data, sizeof(MyData));
      lastRecvTime = millis();
      
      // Recuperación de señal
      if (signal_lost) {
        signal_lost = false;
        failsafe_applied = false;
        ledGreen();
        Serial.println("✓ SEÑAL RECUPERADA - LED VERDE - AUX1 controlado por TX");
      }
    } else {
      // Limpiar buffer de paquetes corruptos
      while (LoRa.available()) {
        LoRa.read();
      }
    }
  }
}

void checkSignalTimeout() {
  unsigned long now = millis();
  
  // Verificar timeout de señal
  if (now - lastRecvTime > SIGNAL_TIMEOUT) {
    if (!signal_lost) {
      // PRIMERA detección de pérdida - aplicar failsafe INMEDIATAMENTE
      signal_lost = true;
      ledRed();
      applyFailsafe();
      Serial.println("⚠️ TIMEOUT - Failsafe activado - LED ROJO - AUX1 AL MÁXIMO");
    } else {
      // Ya estamos en failsafe - reforzar TODOS los canales cada 50ms
      static unsigned long lastFailsafeReinforce = 0;
      if (now - lastFailsafeReinforce > 50) {
        lastFailsafeReinforce = now;
        // Reforzar TODOS los canales críticos
        ibus_channels[2] = 1000;   // Throttle mínimo
        ibus_channels[4] = 2000;   // AUX1 ON ⚠️ MODIFICADO
        ibus_channels[5] = 1000;   // AUX2 OFF
        ibus_channels[6] = 1000;   // AUX3 OFF
        ibus_channels[7] = 1000;   // AUX4 OFF
        ibus_channels[8] = 1000;   // AUX5 OFF
        ibus_channels[9] = 1000;   // AUX6 OFF
        ibus_channels[10] = 1000;  // CH11 OFF
        ibus_channels[11] = 1000;  // CH12 OFF
        ibus_channels[12] = 1000;  // CH13 OFF
        ibus_channels[13] = 1000;  // CH14 OFF
      }
    }
  }
}

void loop() {
  recvData();
  checkSignalTimeout();
  
  unsigned long now = millis();
  
  // Control LED según estado de señal
  if (signal_lost) {
    // Parpadeo rojo cuando no hay señal
    if (millis() % 400 < 200) {
      ledRed();
    } else {
      ledOff();
    }
  } else {
    // Verde fijo con señal OK
    ledGreen();
  }
  
  // Procesar datos SOLO si hay señal válida
  if (!signal_lost) {
    // Filtrar valores fuera de rango
    if (data.ch1 > max_received) data.ch1 = 0;
    if (data.ch2 > max_received) data.ch2 = 2048;
    if (data.ch3 > max_received) data.ch3 = 2048;
    if (data.ch4 > max_received) data.ch4 = 2048;
    
    // Actualizar canales con datos válidos
    updateChannels();
  }
  // Si signal_lost=true, los valores de failsafe ya están aplicados
  
  // Enviar iBUS a ~140Hz (cada 7ms)
  static unsigned long lastIBUS = 0;
  if (now - lastIBUS > 7) {
    lastIBUS = now;
    sendIBUS();
  }
  
  // Monitor serial cada 100ms
  static unsigned long lastPrint = 0;
  if (now - lastPrint > 100) {
    lastPrint = now;
    
    Serial.print("ROL:");  Serial.print(ibus_channels[0]);   
    Serial.print(" PIT:"); Serial.print(ibus_channels[1]);         
    Serial.print(" THR:"); Serial.print(ibus_channels[2]);         
    Serial.print(" YAW:"); Serial.print(ibus_channels[3]);      
    Serial.print(" | AUX1:"); Serial.print(ibus_channels[4]);      
    Serial.print(" AUX2:"); Serial.print(ibus_channels[5]);
    Serial.print(" AUX3:"); Serial.print(ibus_channels[6]);      
    Serial.print(" AUX4:"); Serial.print(ibus_channels[7]);
    Serial.print(" AUX5:"); Serial.print(ibus_channels[8]);
    Serial.print(" AUX6:"); Serial.print(ibus_channels[9]);
    
    if (signal_lost) {
      Serial.print(" | ⚠️ FAILSAFE (AUX1=2000)");
    } else {
      Serial.print(" | ✓ RSSI:"); 
      Serial.print(LoRa.packetRssi());
      Serial.print("dBm");
    }
    
    Serial.println();
  }
  
  delay(1);
}