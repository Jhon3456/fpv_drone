/*Using LVGL with Arduino requires some extra steps:
 *Be sure to read the docs here: https://docs.lvgl.io/master/integration/framework/arduino.html  */
#include <SPI.h>
#include <LoRa.h>
#include <lvgl.h>
#include <EEPROM.h>
#include "ui.h"
#if LV_USE_TFT_ESPI
#include <TFT_eSPI.h>
#endif

// --- Pines LoRa ---
#define LORA_CS    5
#define LORA_RST   2
#define LORA_DIO0  16

// --- Pines analógicos (joysticks/potenciómetros) ---
#define ch1_pin 36   // Canal 1 - Throttle (NO CALIBRAR)
#define ch2_pin 39   // Canal 2 - Yaw (CALIBRAR)
#define ch3_pin 34   // Canal 3 - Pitch (CALIBRAR)
#define ch4_pin 35   // Canal 4 - Roll (CALIBRAR)

// --- Pines botones auxiliares ---
#define aux1_pin 21  // Botón AUX1
#define aux2_pin 22  // Botón AUX2
#define aux3_pin 33  // Botón AUX3
#define aux4_pin 32  // Botón AUX4
#define aux5_pin 26  // Botón AUX5 - NUEVO
#define aux6_pin 27  // Botón AUX6 - NUEVO

// --- LED de estado ---
#define led_status_pin 17

// --- EEPROM Addresses ---
#define EEPROM_SIZE 512
#define EEPROM_MAGIC_ADDR 0
#define EEPROM_MAGIC_VALUE 0xCAFE  // Valor mágico para verificar si hay datos válidos
#define EEPROM_CH2_MIN_ADDR 2
#define EEPROM_CH2_MAX_ADDR 6
#define EEPROM_CH2_CENTRO_ADDR 10
#define EEPROM_CH3_MIN_ADDR 14
#define EEPROM_CH3_MAX_ADDR 18
#define EEPROM_CH3_CENTRO_ADDR 22
#define EEPROM_CH4_MIN_ADDR 26
#define EEPROM_CH4_MAX_ADDR 30
#define EEPROM_CH4_CENTRO_ADDR 34

// --- Configuración pantalla ---
#define TFT_HOR_RES   240
#define TFT_VER_RES   320
#define TFT_ROTATION  LV_DISPLAY_ROTATION_270

#define DRAW_BUF_SIZE (TFT_HOR_RES * TFT_VER_RES / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];

// --- Variables para suavizado de lecturas analógicas ---
#define NUM_MUESTRAS 5
long muestras_ch1[NUM_MUESTRAS] = {0};
long muestras_ch2[NUM_MUESTRAS] = {0};
long muestras_ch3[NUM_MUESTRAS] = {0};
long muestras_ch4[NUM_MUESTRAS] = {0};
int indiceMuestra = 0;

// --- Estructura para calibración de canales ---
struct CalibracionCanal {
  int minimo;
  int maximo;
  int centro;
  bool calibrado;
};

// --- Variables de calibración SOLO para PRY (Pitch, Roll, Yaw) ---
CalibracionCanal cal_ch2 = {0, 4095, 2048, false};  // Yaw - CALIBRAR
CalibracionCanal cal_ch3 = {0, 4095, 2048, false};  // Pitch - CALIBRAR
CalibracionCanal cal_ch4 = {0, 4095, 2048, false};  // Roll - CALIBRAR

// --- Estados de calibración general ---
enum EstadoCalibracion {
  CALIBRANDO_CENTRO,      // Calibrar posiciones centrales PRY
  CALIBRANDO_EXTREMOS,    // Calibrar posiciones extremas PRY
  CALIBRACION_COMPLETA    // Calibración terminada
};

EstadoCalibracion estadoCalibracion = CALIBRACION_COMPLETA;
unsigned long tiempoInicioCalibración = 0;
bool calibracionCentroCompleta = false;

// --- Variables para captura de valores de calibración ---
#define MUESTRAS_CALIBRACION 100  // Muestras para calibración de centro
int muestrasCalibración = 0;
long sumasCalibracion[3] = {0, 0, 0}; // Solo para CH2, CH3, CH4

// --- Hardware Timers ESP32 (Nueva API) ---
hw_timer_t *timer_transmision = NULL;
hw_timer_t *timer_debug = NULL;
hw_timer_t *timer_gui = NULL;
hw_timer_t *timer_led = NULL;

// --- Flags volátiles para ISR ---
volatile bool flag_transmision = false;
volatile bool flag_debug = false;
volatile bool flag_gui = false;
volatile bool flag_led = false;

#if LV_USE_LOG != 0
void my_print( lv_log_level_t level, const char * buf )
{
    LV_UNUSED(level);
    Serial.println(buf);
    Serial.flush();
}
#endif

/* LVGL calls it when a rendered image needs to copied to the display*/
void my_disp_flush( lv_display_t *disp, const lv_area_t *area, uint8_t * px_map)
{
    lv_display_flush_ready(disp);
}

/*use Arduinos millis() as tick source*/
static uint32_t my_tick(void)
{
    return millis();
}

// --- Variables para interrupciones ---
volatile bool aux1Presionado = false;
volatile bool aux2Presionado = false;
volatile bool aux3Presionado = false;
volatile bool aux4Presionado = false;
volatile bool aux5Presionado = false;  // NUEVO
volatile bool aux6Presionado = false;  // NUEVO

// --- Estados toggle de botones ---
bool estadoAUX1 = false;
bool estadoAUX2 = false;
bool estadoAUX3 = false;
bool estadoAUX4 = false;
bool estadoAUX5 = false;  // NUEVO
bool estadoAUX6 = false;  // NUEVO

// --- Variables para debounce ---
unsigned long ultimaInterrupcionAUX1 = 0;
unsigned long ultimaInterrupcionAUX2 = 0;
unsigned long ultimaInterrupcionAUX3 = 0;
unsigned long ultimaInterrupcionAUX4 = 0;
unsigned long ultimaInterrupcionAUX5 = 0;  // NUEVO
unsigned long ultimaInterrupcionAUX6 = 0;  // NUEVO
const unsigned long tiempoDebounce = 300; // 300ms

// --- Variables para LED y calibración ---
bool loraInicializado = false;
bool mandoListo = false;
bool estadoLED = false;

// --- Variables para detección de pulsación MANTENIDA (MODIFICADO) ---
unsigned long tiempoInicioMantener = 0;
const unsigned long tiempoMantenerRequerido = 2000; // 2 segundos manteniendo ambos
bool calibracionActivandose = false;

// --- Variables para secuencia de inicio (Throttle arriba-abajo) ---
enum EstadoInicio {
  ESPERANDO_THROTTLE_ALTO,    // Esperando throttle al máximo
  ESPERANDO_THROTTLE_BAJO,    // Esperando throttle al mínimo
  SISTEMA_ACTIVO              // Sistema completamente activo
};

EstadoInicio estadoInicio = ESPERANDO_THROTTLE_ALTO;
bool sistemaActivado = false;
const int THROTTLE_ALTO_UMBRAL = 3800;  // ~93% del rango (ajustable)
const int THROTTLE_BAJO_UMBRAL = 300;   // ~7% del rango (ajustable)
unsigned long tiempoUltimoMensajeInicio = 0;
const unsigned long intervaloMensajeInicio = 2000; // Mensaje cada 2 segundos

// --- Estructura de datos para transmisión (10 CANALES) ---
struct MyData {
  long ch1;      // Canal 1 - Throttle (0-4095) - SIN CALIBRAR
  long ch2;      // Canal 2 - Yaw (0-4095) - CALIBRADO
  long ch3;      // Canal 3 - Pitch (0-4095) - CALIBRADO
  long ch4;      // Canal 4 - Roll (0-4095) - CALIBRADO
  bool AUX1;     // Canal 5 - Botón toggle
  bool AUX2;     // Canal 6 - Botón toggle
  bool AUX3;     // Canal 7 - Botón toggle
  bool AUX4;     // Canal 8 - Botón toggle
  bool AUX5;     // Canal 9 - Botón toggle - NUEVO
  bool AUX6;     // Canal 10 - Botón toggle - NUEVO
};

MyData data;

// --- Funciones EEPROM ---
void guardarCalibracionEEPROM() {
  Serial.println("\n=== GUARDANDO CALIBRACION EN EEPROM ===");
  
  // Escribir valor mágico
  EEPROM.writeUShort(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
  
  // Guardar calibración CH2 (Yaw)
  EEPROM.writeInt(EEPROM_CH2_MIN_ADDR, cal_ch2.minimo);
  EEPROM.writeInt(EEPROM_CH2_MAX_ADDR, cal_ch2.maximo);
  EEPROM.writeInt(EEPROM_CH2_CENTRO_ADDR, cal_ch2.centro);
  
  // Guardar calibración CH3 (Pitch)
  EEPROM.writeInt(EEPROM_CH3_MIN_ADDR, cal_ch3.minimo);
  EEPROM.writeInt(EEPROM_CH3_MAX_ADDR, cal_ch3.maximo);
  EEPROM.writeInt(EEPROM_CH3_CENTRO_ADDR, cal_ch3.centro);
  
  // Guardar calibración CH4 (Roll)
  EEPROM.writeInt(EEPROM_CH4_MIN_ADDR, cal_ch4.minimo);
  EEPROM.writeInt(EEPROM_CH4_MAX_ADDR, cal_ch4.maximo);
  EEPROM.writeInt(EEPROM_CH4_CENTRO_ADDR, cal_ch4.centro);
  
  EEPROM.commit();
  
  Serial.println("Calibración guardada exitosamente en EEPROM");
}

bool cargarCalibracionEEPROM() {
  Serial.println("\n=== CARGANDO CALIBRACION DESDE EEPROM ===");
  
  // Verificar valor mágico
  uint16_t magic = EEPROM.readUShort(EEPROM_MAGIC_ADDR);
  if (magic != EEPROM_MAGIC_VALUE) {
    Serial.println("No se encontró calibración válida en EEPROM");
    return false;
  }
  
  // Cargar calibración CH2 (Yaw)
  cal_ch2.minimo = EEPROM.readInt(EEPROM_CH2_MIN_ADDR);
  cal_ch2.maximo = EEPROM.readInt(EEPROM_CH2_MAX_ADDR);
  cal_ch2.centro = EEPROM.readInt(EEPROM_CH2_CENTRO_ADDR);
  cal_ch2.calibrado = true;
  
  // Cargar calibración CH3 (Pitch)
  cal_ch3.minimo = EEPROM.readInt(EEPROM_CH3_MIN_ADDR);
  cal_ch3.maximo = EEPROM.readInt(EEPROM_CH3_MAX_ADDR);
  cal_ch3.centro = EEPROM.readInt(EEPROM_CH3_CENTRO_ADDR);
  cal_ch3.calibrado = true;
  
  // Cargar calibración CH4 (Roll)
  cal_ch4.minimo = EEPROM.readInt(EEPROM_CH4_MIN_ADDR);
  cal_ch4.maximo = EEPROM.readInt(EEPROM_CH4_MAX_ADDR);
  cal_ch4.centro = EEPROM.readInt(EEPROM_CH4_CENTRO_ADDR);
  cal_ch4.calibrado = true;
  
  Serial.println("Calibración cargada exitosamente:");
  Serial.printf("CH2 (Yaw)   - Min:%d Centro:%d Max:%d\n", cal_ch2.minimo, cal_ch2.centro, cal_ch2.maximo);
  Serial.printf("CH3 (Pitch) - Min:%d Centro:%d Max:%d\n", cal_ch3.minimo, cal_ch3.centro, cal_ch3.maximo);
  Serial.printf("CH4 (Roll)  - Min:%d Centro:%d Max:%d\n", cal_ch4.minimo, cal_ch4.centro, cal_ch4.maximo);
  
  return true;
}

// --- ISR de Hardware Timers (Nueva API) ---
void IRAM_ATTR onTimer_Transmision() {
  flag_transmision = true;
}

void IRAM_ATTR onTimer_Debug() {
  flag_debug = true;
}

void IRAM_ATTR onTimer_GUI() {
  flag_gui = true;
}

void IRAM_ATTR onTimer_LED() {
  flag_led = true;
}

// --- Funciones de interrupción de botones ---
void IRAM_ATTR aux1ISR() {
  unsigned long tiempoActual = millis();
  if (tiempoActual - ultimaInterrupcionAUX1 > tiempoDebounce) {
    aux1Presionado = true;
    ultimaInterrupcionAUX1 = tiempoActual;
  }
}

void IRAM_ATTR aux2ISR() {
  unsigned long tiempoActual = millis();
  if (tiempoActual - ultimaInterrupcionAUX2 > tiempoDebounce) {
    aux2Presionado = true;
    ultimaInterrupcionAUX2 = tiempoActual;
  }
}

void IRAM_ATTR aux3ISR() {
  unsigned long tiempoActual = millis();
  if (tiempoActual - ultimaInterrupcionAUX3 > tiempoDebounce) {
    aux3Presionado = true;
    ultimaInterrupcionAUX3 = tiempoActual;
  }
}

void IRAM_ATTR aux4ISR() {
  unsigned long tiempoActual = millis();
  if (tiempoActual - ultimaInterrupcionAUX4 > tiempoDebounce) {
    aux4Presionado = true;
    ultimaInterrupcionAUX4 = tiempoActual;
  }
}

// --- NUEVAS ISR para AUX5 y AUX6 ---
void IRAM_ATTR aux5ISR() {
  unsigned long tiempoActual = millis();
  if (tiempoActual - ultimaInterrupcionAUX5 > tiempoDebounce) {
    aux5Presionado = true;
    ultimaInterrupcionAUX5 = tiempoActual;
  }
}

void IRAM_ATTR aux6ISR() {
  unsigned long tiempoActual = millis();
  if (tiempoActual - ultimaInterrupcionAUX6 > tiempoDebounce) {
    aux6Presionado = true;
    ultimaInterrupcionAUX6 = tiempoActual;
  }
}

// --- Función para resetear datos ---
void resetData() {
  data.ch1 = 2048;  // Valor medio (centro)
  data.ch2 = 2048;  
  data.ch3 = 2048;
  data.ch4 = 2048;  
  data.AUX1 = false;
  data.AUX2 = false;
  data.AUX3 = false;
  data.AUX4 = false;
  data.AUX5 = false;  // NUEVO
  data.AUX6 = false;  // NUEVO
}

// --- Función para iniciar calibración automática ---
void iniciarCalibracionAutomatica() {
  Serial.println("\n=== INICIANDO CALIBRACION MANUAL ===");
  Serial.println("Calibrando solo canales: PITCH, ROLL, YAW");
  Serial.println("Throttle NO requiere calibración");
  
  // Resetear calibración PRY
  cal_ch2.calibrado = false;  // Yaw
  cal_ch3.calibrado = false;  // Pitch
  cal_ch4.calibrado = false;  // Roll
  
  mandoListo = false;
  calibracionCentroCompleta = false;
  estadoCalibracion = CALIBRANDO_CENTRO;
  muestrasCalibración = 0;
  
  // Resetear estados de botones durante calibración
  estadoAUX1 = false;
  estadoAUX2 = false;
  estadoAUX3 = false;
  estadoAUX4 = false;
  estadoAUX5 = false;  // NUEVO
  estadoAUX6 = false;  // NUEVO
  
  // Resetear sumas de calibración
  for (int i = 0; i < 3; i++) {
    sumasCalibracion[i] = 0;
  }
  
  // Detener timer de transmisión durante calibración
  timerStop(timer_transmision);
  
  // Cambiar frecuencia LED para indicar calibración
  timerStop(timer_led);
  timerAlarm(timer_led, 500000, true, 0); // 500ms para calibración
  timerStart(timer_led);
  
  Serial.println("PASO 1: CALIBRACION DE CENTRO");
  Serial.println("- Coloque joysticks PITCH, ROLL, YAW en posición CENTRAL");
  Serial.println("- Mantenga esta posición durante 3 segundos...");
  Serial.println("- La calibración iniciará automáticamente...");
  
  tiempoInicioCalibración = millis();
}

// --- Función para calibrar valores centrales SOLO PRY ---
void calibrarCentro() {
  // Leer valores actuales SOLO de PRY
  long ch2_raw = analogRead(ch2_pin);  // Yaw
  long ch3_raw = analogRead(ch3_pin);  // Pitch
  long ch4_raw = analogRead(ch4_pin);  // Roll
  
  // Acumular muestras
  sumasCalibracion[0] += ch2_raw;  // Yaw
  sumasCalibracion[1] += ch3_raw;  // Pitch
  sumasCalibracion[2] += ch4_raw;  // Roll
  
  muestrasCalibración++;
  
  // Mostrar progreso cada 20 muestras
  if (muestrasCalibración % 20 == 0) {
    Serial.printf("Calibrando centro PRY... %d/%d muestras\n", muestrasCalibración, MUESTRAS_CALIBRACION);
  }
  
  if (muestrasCalibración >= MUESTRAS_CALIBRACION) {
    // Calcular promedios para valores centrales PRY
    cal_ch2.centro = sumasCalibracion[0] / MUESTRAS_CALIBRACION;  // Yaw
    cal_ch3.centro = sumasCalibracion[1] / MUESTRAS_CALIBRACION;  // Pitch
    cal_ch4.centro = sumasCalibracion[2] / MUESTRAS_CALIBRACION;  // Roll
    
    // Establecer rangos iniciales basados en el centro calibrado
    // Para PRY, establecer rangos simétricos
    cal_ch2.minimo = max(0, cal_ch2.centro - 800);
    cal_ch2.maximo = min(4095, cal_ch2.centro + 800);
    cal_ch3.minimo = max(0, cal_ch3.centro - 800);  
    cal_ch3.maximo = min(4095, cal_ch3.centro + 800);
    cal_ch4.minimo = max(0, cal_ch4.centro - 800);
    cal_ch4.maximo = min(4095, cal_ch4.centro + 800);
    
    calibracionCentroCompleta = true;
    estadoCalibracion = CALIBRANDO_EXTREMOS;
    
    Serial.println("\n=== CALIBRACION CENTRO PRY COMPLETA ===");
    Serial.printf("CH2 (Yaw)      Centro: %d (Rango inicial: %d-%d)\n", cal_ch2.centro, cal_ch2.minimo, cal_ch2.maximo);
    Serial.printf("CH3 (Pitch)    Centro: %d (Rango inicial: %d-%d)\n", cal_ch3.centro, cal_ch3.minimo, cal_ch3.maximo); 
    Serial.printf("CH4 (Roll)     Centro: %d (Rango inicial: %d-%d)\n", cal_ch4.centro, cal_ch4.minimo, cal_ch4.maximo);
    
    Serial.println("\nPASO 2: CALIBRACION DE EXTREMOS");
    Serial.println("- Mueva los joysticks PITCH, ROLL, YAW a sus posiciones EXTREMAS");
    Serial.println("- Mueva cada control completamente en todas las direcciones");
    Serial.println("- Presione AUX1 cuando termine la calibración de extremos");
    
    // Reiniciar contadores para calibración de extremos
    muestrasCalibración = 0;
    for (int i = 0; i < 3; i++) {
      sumasCalibracion[i] = 0;
    }
  }
}

// --- Función para actualizar rangos extremos PRY durante el movimiento ---
void actualizarExtremosCalibración() {
  long ch2_raw = analogRead(ch2_pin);  // Yaw
  long ch3_raw = analogRead(ch3_pin);  // Pitch
  long ch4_raw = analogRead(ch4_pin);  // Roll
  
  // Actualizar mínimos y máximos si se detectan valores más extremos
  if (ch2_raw < cal_ch2.minimo) {
    cal_ch2.minimo = ch2_raw;
    Serial.printf("Nuevo mínimo YAW: %ld\n", ch2_raw);
  }
  if (ch2_raw > cal_ch2.maximo) {
    cal_ch2.maximo = ch2_raw;
    Serial.printf("Nuevo máximo YAW: %ld\n", ch2_raw);
  }
  
  if (ch3_raw < cal_ch3.minimo) {
    cal_ch3.minimo = ch3_raw;
    Serial.printf("Nuevo mínimo PITCH: %ld\n", ch3_raw);
  }
  if (ch3_raw > cal_ch3.maximo) {
    cal_ch3.maximo = ch3_raw;
    Serial.printf("Nuevo máximo PITCH: %ld\n", ch3_raw);
  }
  
  if (ch4_raw < cal_ch4.minimo) {
    cal_ch4.minimo = ch4_raw;
    Serial.printf("Nuevo mínimo ROLL: %ld\n", ch4_raw);
  }
  if (ch4_raw > cal_ch4.maximo) {
    cal_ch4.maximo = ch4_raw;
    Serial.printf("Nuevo máximo ROLL: %ld\n", ch4_raw);
  }
}

// --- Función para completar calibración ---
void completarCalibracion() {
  cal_ch2.calibrado = true;  // Yaw
  cal_ch3.calibrado = true;  // Pitch
  cal_ch4.calibrado = true;  // Roll
  
  estadoCalibracion = CALIBRACION_COMPLETA;
  mandoListo = true;
  
  Serial.println("\n=== CALIBRACION PRY COMPLETA ===");
  Serial.printf("CH2 (Yaw)      - Min:%d Centro:%d Max:%d\n", cal_ch2.minimo, cal_ch2.centro, cal_ch2.maximo);
  Serial.printf("CH3 (Pitch)    - Min:%d Centro:%d Max:%d\n", cal_ch3.minimo, cal_ch3.centro, cal_ch3.maximo);
  Serial.printf("CH4 (Roll)     - Min:%d Centro:%d Max:%d\n", cal_ch4.minimo, cal_ch4.centro, cal_ch4.maximo);
  Serial.println("CH1 (Throttle) - SIN CALIBRAR (uso directo 0-4095)");
  Serial.println("¡MANDO LISTO PARA USAR!");
  
  // Guardar calibración en EEPROM
  guardarCalibracionEEPROM();
  
  // Habilitar timers cuando esté calibrado
  timerRestart(timer_transmision);
  timerStart(timer_transmision);
  
  timerRestart(timer_debug);
  timerStart(timer_debug);
  
  // Cambiar frecuencia del LED a modo "operativo" 
  timerStop(timer_led);
  timerAlarm(timer_led, 1000000, true, 0); // 1 segundo
  timerStart(timer_led);
  
  digitalWrite(led_status_pin, HIGH);
}

// --- Función para filtro de media móvil ---
long filtroMediaMovil(long nuevaLectura, long* muestras) {
  muestras[indiceMuestra] = nuevaLectura;
  long suma = 0;
  for (int i = 0; i < NUM_MUESTRAS; i++) {
    suma += muestras[i];
  }
  return suma / NUM_MUESTRAS;
}

// --- Función para mapear valores calibrados con zona muerta SOLO PRY ---
long mapearValorCalibrado(long valor, CalibracionCanal& cal, int zonaMuerta = 50) {
  // Si no está calibrado, devolver valor crudo
  if (!cal.calibrado) {
    return constrain(valor, 0, 4095);
  }
  
  // Aplicar zona muerta alrededor del centro
  if (abs(valor - cal.centro) < zonaMuerta) {
    return 2048; // Centro del rango de salida
  }
  
  // Mapear a rango 0-4095 manteniendo el centro calibrado
  if (valor < cal.centro - zonaMuerta) {
    return map(valor, cal.minimo, cal.centro - zonaMuerta, 0, 2047);
  } else {
    return map(valor, cal.centro + zonaMuerta, cal.maximo, 2049, 4095);
  }
}

// --- Función para procesar CH1 (Throttle) sin calibración ---
long procesarThrottle(long valor) {
  // Aplicar filtro de ruido
  valor = (valor < 162) ? 0 : valor;
  
  // Devolver valor directo sin calibración
  return constrain(valor, 0, 4095);
}

// --- Funciones de procesamiento de timers ---
void procesarTransmision() {
  // Solo transmitir si el sistema está completamente activado
  if (sistemaActivado && mandoListo && loraInicializado) {
    LoRa.beginPacket();
    LoRa.write((uint8_t*)&data, sizeof(MyData));
    LoRa.endPacket();
  }
}

void procesarDebug() {
  if (!sistemaActivado) {
    // Durante secuencia de inicio, mostrar valor de throttle
    long throttleRaw = analogRead(ch1_pin);
    if (estadoInicio == ESPERANDO_THROTTLE_ALTO) {
      Serial.printf("[INICIO] Esperando THROTTLE ALTO - Actual:%4ld (>%d)\n", throttleRaw, THROTTLE_ALTO_UMBRAL);
    } else if (estadoInicio == ESPERANDO_THROTTLE_BAJO) {
      Serial.printf("[INICIO] Esperando THROTTLE BAJO - Actual:%4ld (<%d)\n", throttleRaw, THROTTLE_BAJO_UMBRAL);
    }
  } else if (mandoListo) {
    Serial.printf("THR:%4ld YAW:%4ld PIT:%4ld ROL:%4ld AUX1:%d AUX2:%d AUX3:%d AUX4:%d AUX5:%d AUX6:%d\n",
                  data.ch1, data.ch2, data.ch3, data.ch4,
                  estadoAUX1, estadoAUX2, estadoAUX3, estadoAUX4, estadoAUX5, estadoAUX6);
  } else if (estadoCalibracion == CALIBRANDO_EXTREMOS) {
    // Durante calibración de extremos, mostrar valores raw para monitoreo PRY
    long ch2_raw = analogRead(ch2_pin);
    long ch3_raw = analogRead(ch3_pin);
    long ch4_raw = analogRead(ch4_pin);
    Serial.printf("RAW PRY - YAW:%4ld PIT:%4ld ROL:%4ld | Rangos - YAW:%d-%d PIT:%d-%d ROL:%d-%d\n", 
                  ch2_raw, ch3_raw, ch4_raw,
                  cal_ch2.minimo, cal_ch2.maximo, cal_ch3.minimo, cal_ch3.maximo,
                  cal_ch4.minimo, cal_ch4.maximo);
  }
}

void procesarGUI() {
  // Throttle (CH1) - Convertir a porcentaje (0-100)
  int throttlePercent = map(data.ch1, 0, 4095, 0, 100);
  lv_arc_set_value(objects.arc1, throttlePercent);
  lv_label_set_text_fmt(objects.ind1, "%d%%", throttlePercent);
  
  // Yaw (CH2)
  int yawPercent = map(data.ch2, 0, 4095, -45, 45);
  lv_arc_set_value(objects.arc2, yawPercent);
  lv_label_set_text_fmt(objects.ind2, "%d°", yawPercent);

  // Pitch (CH3)
  int pitPercent = map(data.ch3, 0, 4095, -45, 45);
  lv_arc_set_value(objects.arc3, pitPercent);
  lv_label_set_text_fmt(objects.ind3, "%d°", pitPercent);
  
  // Roll (CH4)
  int rollPercent = map(data.ch4, 0, 4095, -45, 45);
  lv_arc_set_value(objects.arc4, rollPercent);
  lv_label_set_text_fmt(objects.ind4, "%d°", rollPercent);
  
  // Actualizar estados de botones auxiliares
  if (estadoAUX1) {
    lv_obj_add_state(objects.aux1, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(objects.aux1, LV_STATE_CHECKED);
  }

  if (estadoAUX2) {
    lv_obj_add_state(objects.aux2, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(objects.aux2, LV_STATE_CHECKED);
  }

  if (estadoAUX3) {
    lv_obj_add_state(objects.aux3, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(objects.aux3, LV_STATE_CHECKED);
  }

  if (estadoAUX4) {
    lv_obj_add_state(objects.aux4, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(objects.aux4, LV_STATE_CHECKED);
  }
  
  // NUEVO: Actualizar AUX5 y AUX6 si existen en la UI
  // Nota: Si tu UI no tiene estos objetos, comenta estas líneas
  /*
  if (estadoAUX5) {
    lv_obj_add_state(objects.aux5, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(objects.aux5, LV_STATE_CHECKED);
  }

  if (estadoAUX6) {
    lv_obj_add_state(objects.aux6, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(objects.aux6, LV_STATE_CHECKED);
  }
  */
}

void procesarLED() {
  if (!loraInicializado) {
    // Error LoRa - parpadeo rápido
    estadoLED = !estadoLED;
    digitalWrite(led_status_pin, estadoLED);
  } else if (!sistemaActivado) {
    // Esperando secuencia de inicio - parpadeo muy rápido (diferente a calibración)
    estadoLED = !estadoLED;
    digitalWrite(led_status_pin, estadoLED);
  } else if (!mandoListo) {
    // Esperando calibración - parpadeo lento
    estadoLED = !estadoLED;
    digitalWrite(led_status_pin, estadoLED);
  } else {
    // Todo OK - LED encendido constante
    digitalWrite(led_status_pin, HIGH);
  }
}

// --- Función para verificar secuencia de inicio del throttle ---
void verificarSecuenciaInicio(long throttleRaw) {
  unsigned long tiempoActual = millis();
  
  switch (estadoInicio) {
    case ESPERANDO_THROTTLE_ALTO:
      // Mostrar mensaje periódico
      if (tiempoActual - tiempoUltimoMensajeInicio > intervaloMensajeInicio) {
        Serial.println("\n>>> SECUENCIA DE INICIO <<<");
        Serial.println("PASO 1: Mueva el THROTTLE al MAXIMO");
        Serial.printf("Valor actual: %ld (necesita > %d)\n", throttleRaw, THROTTLE_ALTO_UMBRAL);
        tiempoUltimoMensajeInicio = tiempoActual;
      }
      
      // Verificar si throttle está en posición alta
      if (throttleRaw > THROTTLE_ALTO_UMBRAL) {
        Serial.println("\n✓ THROTTLE ALTO DETECTADO");
        Serial.println("PASO 2: Ahora mueva el THROTTLE al MINIMO");
        estadoInicio = ESPERANDO_THROTTLE_BAJO;
        tiempoUltimoMensajeInicio = tiempoActual;
      }
      break;
      
    case ESPERANDO_THROTTLE_BAJO:
      // Mostrar mensaje periódico
      if (tiempoActual - tiempoUltimoMensajeInicio > intervaloMensajeInicio) {
        Serial.println("\nPASO 2: Mueva el THROTTLE al MINIMO");
        Serial.printf("Valor actual: %ld (necesita < %d)\n", throttleRaw, THROTTLE_BAJO_UMBRAL);
        tiempoUltimoMensajeInicio = tiempoActual;
      }
      
      // Verificar si throttle está en posición baja
      if (throttleRaw < THROTTLE_BAJO_UMBRAL) {
        Serial.println("\n✓ THROTTLE BAJO DETECTADO");
        Serial.println("\n*** SECUENCIA DE INICIO COMPLETA ***");
        Serial.println("¡SISTEMA ACTIVADO!");
        
        estadoInicio = SISTEMA_ACTIVO;
        sistemaActivado = true;
        
        // Cambiar LED a modo normal si está todo OK
        if (mandoListo && loraInicializado) {
          timerStop(timer_led);
          timerAlarm(timer_led, 1000000, true, 0); // 1 segundo
          timerStart(timer_led);
          digitalWrite(led_status_pin, HIGH);
        }
      }
      break;
      
    case SISTEMA_ACTIVO:
      // Ya está activo, no hacer nada
      break;
  }
}

void setup()
{
    String LVGL_Arduino = "Hello Arduino! ";
    LVGL_Arduino += String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();

    Serial.begin( 115200 );
    Serial.println( LVGL_Arduino );

    lv_init();

    /*Set a tick source so that LVGL will know how much time elapsed. */
    lv_tick_set_cb(my_tick);

    /* register print function for debugging */
#if LV_USE_LOG != 0
    lv_log_register_print_cb( my_print );
#endif

    lv_display_t * disp;
#if LV_USE_TFT_ESPI
    /*TFT_eSPI can be enabled lv_conf.h to initialize the display in a simple way*/
    disp = lv_tft_espi_create(TFT_HOR_RES, TFT_VER_RES, draw_buf, sizeof(draw_buf));
    lv_display_set_rotation(disp, TFT_ROTATION);

#else
    /*Else create a display yourself*/
    disp = lv_display_create(TFT_HOR_RES, TFT_VER_RES);
    lv_display_set_flush_cb(disp, my_disp_flush);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif

    ui_init();
    
    // --- Inicializar EEPROM ---
    EEPROM.begin(EEPROM_SIZE);
    Serial.println("\n=== EEPROM Inicializada ===");
    
    // --- Configurar pines analógicos ---
    pinMode(ch1_pin, INPUT);
    pinMode(ch2_pin, INPUT);
    pinMode(ch3_pin, INPUT);
    pinMode(ch4_pin, INPUT);
    
    // --- Configurar botones ---
    pinMode(aux1_pin, INPUT_PULLUP);
    pinMode(aux2_pin, INPUT_PULLUP);
    pinMode(aux3_pin, INPUT_PULLUP);
    pinMode(aux4_pin, INPUT_PULLUP);
    pinMode(aux5_pin, INPUT_PULLUP);  // NUEVO
    pinMode(aux6_pin, INPUT_PULLUP);  // NUEVO
    
    // --- Configurar LED ---
    pinMode(led_status_pin, OUTPUT);
    digitalWrite(led_status_pin, LOW);
    
    // --- Configurar interrupciones de botones ---
    attachInterrupt(digitalPinToInterrupt(aux1_pin), aux1ISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(aux2_pin), aux2ISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(aux3_pin), aux3ISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(aux4_pin), aux4ISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(aux5_pin), aux5ISR, FALLING);  // NUEVO
    attachInterrupt(digitalPinToInterrupt(aux6_pin), aux6ISR, FALLING);  // NUEVO
    
    // --- Inicializar LoRa ---
    SPI.begin(18, 19, 23, LORA_CS);  // SCK, MISO, MOSI, CS
    LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
    
    if (!LoRa.begin(433E6)) {
      Serial.println("Error iniciando LoRa!");
      loraInicializado = false;
    } else {
      loraInicializado = true;
      Serial.println("LoRa inicializado correctamente.");
      
      // --- Configuración LoRa óptima ---
      LoRa.setTxPower(20);
      LoRa.setSpreadingFactor(7);
      LoRa.setSignalBandwidth(125E3);
      LoRa.setCodingRate4(5);
      LoRa.setSyncWord(0x12);
    }
    
    resetData();
    
    // --- Inicializar arrays de muestras ---
    for (int i = 0; i < NUM_MUESTRAS; i++) {
      muestras_ch1[i] = 2048;
      muestras_ch2[i] = 2048;
      muestras_ch3[i] = 2048;
      muestras_ch4[i] = 2048;
    }
    
    // --- Configurar Hardware Timers (Nueva API ESP32) ---
    
    // Timer Transmisión LoRa - 100Hz 
    timer_transmision = timerBegin(1000000); // 1 MHz base frequency
    timerAttachInterrupt(timer_transmision, &onTimer_Transmision);
    timerAlarm(timer_transmision, 10000, true, 0); // 10ms, auto-reload, no start
    
    // Timer Debug Serial - 5Hz (200,000 μs)
    timer_debug = timerBegin(1000000); 
    timerAttachInterrupt(timer_debug, &onTimer_Debug);
    timerAlarm(timer_debug, 200000, true, 0); // 200ms, auto-reload, no start
    
    // Timer GUI Update - 30Hz (33,333 μs)
    timer_gui = timerBegin(1000000);
    timerAttachInterrupt(timer_gui, &onTimer_GUI);
    timerAlarm(timer_gui, 33333, true, 0); // 33ms, auto-reload
    timerStart(timer_gui); // Iniciar inmediatamente
    
    // Timer LED Status - Variable según estado
    timer_led = timerBegin(1000000);
    timerAttachInterrupt(timer_led, &onTimer_LED);
    if (!loraInicializado) {
      timerAlarm(timer_led, 200000, true, 0); // Error: 200ms - parpadeo muy rápido
    } else {
      timerAlarm(timer_led, 250000, true, 0); // Esperando inicio: 250ms - parpadeo rápido
    }
    timerStart(timer_led); // Iniciar inmediatamente
    
    Serial.println("\n=== MANDO RC 10 CANALES - CALIBRACION MANUAL PRY ===");
    Serial.println("NOTA: Solo se calibran los canales PITCH, ROLL, YAW");
    Serial.println("      El THROTTLE funciona sin calibración (0-4095)");
    Serial.println("      6 botones auxiliares disponibles (AUX1-AUX6)");
    
    // --- INTENTAR CARGAR CALIBRACION DESDE EEPROM ---
    if (cargarCalibracionEEPROM()) {
      mandoListo = true;
      estadoCalibracion = CALIBRACION_COMPLETA;
      
      Serial.println("\n¡Calibración cargada desde EEPROM!");
      Serial.println("Mantenga presionado AUX1 + AUX4 por 2 segundos para recalibrar");
      
    } else {
      mandoListo = false;
      Serial.println("\nNo hay calibración guardada.");
      Serial.println("Mantenga presionado AUX1 + AUX4 por 2 segundos para iniciar calibración");
    }
    
    // --- SECUENCIA DE INICIO ---
    Serial.println("\n╔════════════════════════════════════════════════╗");
    Serial.println("║     SECUENCIA DE INICIO REQUERIDA             ║");
    Serial.println("╚════════════════════════════════════════════════╝");
    Serial.println("Para activar el sistema:");
    Serial.println("  1. Mueva el THROTTLE al MAXIMO");
    Serial.println("  2. Luego mueva el THROTTLE al MINIMO");
    Serial.println("\nEsto es una medida de seguridad para evitar");
    Serial.println("arranques accidentales del motor.");
    Serial.println("═════════════════════════════════════════════════\n");
    
    // Iniciar timer de debug para monitorear secuencia
    timerStart(timer_debug);
    
    // NO iniciar transmisión hasta completar secuencia de inicio
    // timerStart(timer_transmision); // Se iniciará después de la secuencia
    
    Serial.println( "Setup done" );
}

void loop() {
  // --- Lectura continua de canales analógicos con filtrado ---
  long ch1_raw = analogRead(ch1_pin);  // Throttle
  long ch2_raw = analogRead(ch2_pin);  // Yaw
  long ch3_raw = analogRead(ch3_pin);  // Pitch
  long ch4_raw = analogRead(ch4_pin);  // Roll
  
  // --- VERIFICAR SECUENCIA DE INICIO PRIMERO ---
  if (!sistemaActivado) {
    verificarSecuenciaInicio(ch1_raw);
    
    // Si aún no está activado, solo procesar GUI y LED
    if (flag_gui) {
      // Mostrar valores crudos en GUI durante secuencia de inicio
      data.ch1 = procesarThrottle(ch1_raw);
      data.ch2 = ch2_raw;
      data.ch3 = ch3_raw;
      data.ch4 = ch4_raw;
      procesarGUI();
      lv_timer_handler();
      flag_gui = false;
    }
    
    if (flag_led) {
      procesarLED();
      flag_led = false;
    }
    
    if (flag_debug) {
      procesarDebug();
      flag_debug = false;
    }
    
    delayMicroseconds(100);
    return; // No procesar nada más hasta completar secuencia
  }
  
  // --- SISTEMA ACTIVADO - Procesamiento normal ---
  
  // Activar timer de transmisión si aún no está activo
  static bool transmisionIniciada = false;
  if (!transmisionIniciada && mandoListo) {
    timerStart(timer_transmision);
    transmisionIniciada = true;
    Serial.println("✓ Transmisión LoRa activada");
  }
  
  // Aplicar filtro de media móvil
  long ch1 = filtroMediaMovil(ch1_raw, muestras_ch1);
  long ch2 = filtroMediaMovil(ch2_raw, muestras_ch2);
  long ch3 = filtroMediaMovil(ch3_raw, muestras_ch3);
  long ch4 = filtroMediaMovil(ch4_raw, muestras_ch4);
  
  // Incrementar índice de muestra (circular)
  indiceMuestra = (indiceMuestra + 1) % NUM_MUESTRAS;
  
  // --- Proceso de calibración automática ---
  switch (estadoCalibracion) {
    case CALIBRANDO_CENTRO:
      calibrarCentro();
      break;
      
    case CALIBRANDO_EXTREMOS:
      actualizarExtremosCalibración();
      break;
      
    case CALIBRACION_COMPLETA:
      // Procesar canales - CH1 sin calibración, PRY con calibración
      data.ch1 = procesarThrottle(ch1);  // Throttle directo
      data.ch2 = mapearValorCalibrado(ch2, cal_ch2, 50);  // Yaw calibrado
      data.ch3 = mapearValorCalibrado(ch3, cal_ch3, 50);  // Pitch calibrado
      data.ch4 = mapearValorCalibrado(ch4, cal_ch4, 50);  // Roll calibrado
      break;
  }
  
  // --- NUEVA LOGICA: Detectar pulsación MANTENIDA de AUX1 + AUX4 ---
  bool aux1ActualmentePresionado = (digitalRead(aux1_pin) == LOW);
  bool aux4ActualmentePresionado = (digitalRead(aux4_pin) == LOW);
  
  if (aux1ActualmentePresionado && aux4ActualmentePresionado) {
    // Ambos botones están presionados
    if (!calibracionActivandose) {
      // Primera vez que se detectan ambos - iniciar contador
      tiempoInicioMantener = millis();
      calibracionActivandose = true;
      Serial.println("Manteniendo AUX1+AUX4... (0/2 seg)");
    } else {
      // Ya estaban presionados - verificar tiempo transcurrido
      unsigned long tiempoTranscurrido = millis() - tiempoInicioMantener;
      
      // Mostrar progreso cada 500ms
      static unsigned long ultimoMensaje = 0;
      if (millis() - ultimoMensaje > 500) {
        Serial.printf("Manteniendo AUX1+AUX4... (%lu/2000 ms)\n", tiempoTranscurrido);
        ultimoMensaje = millis();
      }
      
      if (tiempoTranscurrido >= tiempoMantenerRequerido) {
        // ¡Se mantuvo el tiempo requerido!
        Serial.println("\n*** AUX1 + AUX4 MANTENIDOS 2 SEG - INICIANDO CALIBRACION ***");
        iniciarCalibracionAutomatica();
        
        // Resetear flags
        calibracionActivandose = false;
        aux1Presionado = false;
        aux4Presionado = false;
        
        // NO ESPERAR - Entrar directamente en calibración
        // El usuario puede seguir presionando o soltar cuando quiera
        Serial.println("Calibración iniciada - puede soltar los botones ahora");
      }
    }
  } else {
    // Al menos uno de los botones fue soltado
    if (calibracionActivandose) {
      Serial.println("Pulsación cancelada - no se mantuvo suficiente tiempo");
      calibracionActivandose = false;
    }
    
    // Procesar botones individuales normalmente
    if (aux1Presionado) {
      aux1Presionado = false;
      
      if (estadoCalibracion == CALIBRANDO_EXTREMOS) {
        // AUX1 presionado durante calibración de extremos = completar calibración
        completarCalibracion();
      } else if (mandoListo) {
        // Funcionamiento normal
        estadoAUX1 = !estadoAUX1;
        Serial.println("AUX1: " + String(estadoAUX1 ? "ON" : "OFF"));
      }
    }
    
    if (aux4Presionado) {
      aux4Presionado = false;
      
      if (mandoListo && estadoCalibracion == CALIBRACION_COMPLETA) {
        // Funcionamiento normal
        estadoAUX4 = !estadoAUX4;
        Serial.println("AUX4: " + String(estadoAUX4 ? "ON" : "OFF"));
      }
    }
  }
  
  // Procesar otros botones auxiliares
  if (aux2Presionado) {
    estadoAUX2 = !estadoAUX2;
    aux2Presionado = false;
    if (mandoListo) {
      Serial.println("AUX2: " + String(estadoAUX2 ? "ON" : "OFF"));
    }
  }
  
  if (aux3Presionado) {
    estadoAUX3 = !estadoAUX3;
    aux3Presionado = false;
    if (mandoListo) {
      Serial.println("AUX3: " + String(estadoAUX3 ? "ON" : "OFF"));
    }
  }
  
  // --- NUEVO: Procesar AUX5 y AUX6 ---
  if (aux5Presionado) {
    estadoAUX5 = !estadoAUX5;
    aux5Presionado = false;
    if (mandoListo) {
      Serial.println("AUX5: " + String(estadoAUX5 ? "ON" : "OFF"));
    }
  }
  
  if (aux6Presionado) {
    estadoAUX6 = !estadoAUX6;
    aux6Presionado = false;
    if (mandoListo) {
      Serial.println("AUX6: " + String(estadoAUX6 ? "ON" : "OFF"));
    }
  }
  
  // --- Actualizar datos de botones auxiliares siempre ---
  data.AUX1 = estadoAUX1;
  data.AUX2 = estadoAUX2;
  data.AUX3 = estadoAUX3;
  data.AUX4 = estadoAUX4;
  data.AUX5 = estadoAUX5;  // NUEVO
  data.AUX6 = estadoAUX6;  // NUEVO
  
  // --- Si no está calibrado, usar valores filtrados para GUI ---
  if (!mandoListo) {
    data.ch1 = procesarThrottle(ch1);  // Throttle siempre procesado
    data.ch2 = ch2;  // Yaw sin calibrar para GUI
    data.ch3 = ch3;  // Pitch sin calibrar para GUI
    data.ch4 = ch4;  // Roll sin calibrar para GUI
  }
  
  // --- Procesar flags de hardware timers ---
  if (flag_transmision) {
    procesarTransmision();
    flag_transmision = false;
  }
  
  if (flag_debug) {
    procesarDebug();
    flag_debug = false;
  }
  
  if (flag_gui) {
    procesarGUI();
    lv_timer_handler(); // Procesar LVGL
    flag_gui = false;
  }
  
  if (flag_led) {
    procesarLED();
    flag_led = false;
  }
  
  // --- Micro delay para estabilidad del ADC ---
  delayMicroseconds(100); // 0.1ms
}