// Nextion HMI integration
// Berdasarkan I_131_SingleFillSingleDispense_v6_working.ino.
// Kalkulasi volume -> step, validasi, dan eksekusi siklus sekarang berada
// di Arduino. Nextion mengirim volume/faktor/jumlah kapsul, bukan step.
// ----------------------------------------------------------------------

#include <SoftwareSerial.h>
#include <stdlib.h>

// --- Konfigurasi pin ---
const int enablePins[4] = {8, 9, 10, 11};
const int stepPin = 7;
const int dirPin = 6;
const int limitPins[4] = {3, 4, 5, 2};

// --- Arah homing masing-masing motor ---
const int homingDir[4] = {1, 1, 0, 0};

// --- Profil kecepatan gerak normal per motor (indeks: M1, M2, M3, M4) ---
// Delay lebih kecil menghasilkan laju step lebih tinggi.
// M2 adalah syringe pump: diperlambat sebagai nilai awal yang perlu diuji.
// M1, M3, M4 memakai batas tercepat yang saat ini ditetapkan sketch (80 us).
// Nilai ini bukan jaminan batas maksimum mekanis motor/driver.
const int motorMinDelayUs[4] = {80, 300, 80, 80};
const int motorStartDelayUs[4] = {800, 800, 800, 800};
const int homingDelayUs = 80; // jeda terkecil yang digunakan sketch untuk homing

// --- Data mekanik lead screw ---
const float leadMotor1_mmPerRev = 2.0;
const float leadMotor2_mmPerRev = 1.0;
const float leadMotor3_mmPerRev = 1.0;
const float leadMotor4_mmPerRev = 8.0;
const long stepsPerRev = 1600;

// --- Nextion HMI link (Nano: SoftwareSerial) ---
// Nextion TX -> A0, Nextion RX -> A1.
// Jangan memberi daya Nextion dari pin 5V Nano; gunakan suplai 5V/2A terpisah.
#define NEXTION_RX_PIN A0
#define NEXTION_TX_PIN A1
#define NEXTION_BAUD 9600
#define NEXTION_LINE_MAX 48
#define NEXTION_SUSPEND_DURING_MOVE 1

SoftwareSerial nextionSerial(NEXTION_RX_PIN, NEXTION_TX_PIN);

char nextionLine[NEXTION_LINE_MAX];
size_t nextionLineLen = 0;
bool nextionLineOverflow = false;

// Batas validasi tahap awal. Batas step ini hanya penjaga overflow,
// bukan pengganti validasi mekanis/kalibrasi.
#define MAX_VOLUME_UL 10.0
#define MAX_STEP_PER_CAPSULE 2000000L
#define DEFAULT_CALIBRATION_FACTOR 2033.0f

enum NextionState {
  NEX_BOOT,
  NEX_HOMING,
  NEX_READY,
  NEX_RUNNING,
  NEX_ERROR
};

NextionState nextionState = NEX_BOOT;
int nextionProgress = 0;

enum CalcStatus {
  CALC_OK,
  CALC_VOLUME_RANGE,
  CALC_FACTOR_RANGE,
  CALC_CAPSULE_RANGE,
  CALC_STEP_RANGE
};

// --- Variabel operasional ---
long jumlahCapsule = 0;

// Nilai mentah dari HMI. stepPerCapsule adalah hasil kalkulasi Arduino.
float volumePerCapsule_uL = 0;
float calibrationFactor = DEFAULT_CALIBRATION_FACTOR;
long stepsPerCapsule = 0;
long totalVolumeSteps = 0;
bool configurationReady = false;
bool configurationHasVolume = false;

// --- Posisi & offset ---
long homingOffset[5] = {0, 0, 0, 0, 0}; // indeks 0..4, kita pakai 1..4
long posAbs[5] = {0, 0, 0, 0, 0};       // indeks 0..4

// --- Parser ---
String inputString = "";
bool stringComplete = false;

// ===== Prototypes =====
long homingMotor(int id);
void stepPulse(int delayUs);
void moveMotorAccel(int id, long langkah, int arah);
void processQueue(String data);
void executeCommand(String cmd);
long getValue(String data, String label);
long getValueLong(String data, String label);
void prepareIodium();     // dipanggil sekali saat INIT
void runAutoDispense();   // hanya urusan kapsul

// Nextion HMI
void nextionBegin();
void nextionPoll();
void nextionFlushInput();
void nextionSuspend();
void nextionResume();
void nextionHandleLine(char *line);
void nextionHandleConfig(char *body);
void nextionConfigError(const __FlashStringHelper *reason);
void nextionSendCurrentStatus();
void nextionSetState(NextionState state);
void nextionSetText(const char *obj, const char *text);
void nextionSetText(const char *obj, const __FlashStringHelper *text);
void nextionSetNumber(const char *obj, long value);
void nextionLog(const __FlashStringHelper *msg);
void nextionEndCommand();

// Kalkulasi & eksekusi
CalcStatus calculateConfiguration(float volume, float factor, long caps, long &steps);
const __FlashStringHelper *calcErrorMessage(CalcStatus status);
bool startDispenseCycle();

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(9600);
  nextionBegin();

  for (int i = 0; i < 4; i++) {
    pinMode(enablePins[i], OUTPUT);
    digitalWrite(enablePins[i], HIGH); // disable driver
    pinMode(limitPins[i], INPUT_PULLUP);
  }

  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);

  nextionSetState(NEX_HOMING);

  // Homing awal: 4 → 3 → 2 → 1
  long L4 = homingMotor(4); homingOffset[4] = L4; posAbs[4] = 0;
  long L3 = homingMotor(3); homingOffset[3] = L3; posAbs[3] = 0;
  long L2 = homingMotor(2); homingOffset[2] = L2; posAbs[2] = 0;
  long L1 = homingMotor(1); homingOffset[1] = L1; posAbs[1] = 0;

  nextionFlushInput();
  nextionSetState(NEX_READY);
  nextionSetText("tStep", "-");
  nextionLog(F("Arduino READY"));

  Serial.println("Sistem siap. Masukkan perintah...");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  nextionPoll();

  if (stringComplete) {
    processQueue(inputString);
    inputString = "";
    stringComplete = false;
  }
}

void serialEvent() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    inputString += inChar;
    // Akhiri perintah jika user mengirim newline atau tanda '>'
    if (inChar == '\n' || inChar == '>') {
      stringComplete = true;
    }
  }
}

// ============================================================
// NEXTION HMI LINK
// ============================================================
void nextionBegin() {
  nextionSerial.begin(NEXTION_BAUD);
  nextionSerial.listen();
  nextionLineLen = 0;
  nextionLineOverflow = false;
  nextionState = NEX_BOOT;
  nextionProgress = 0;
  configurationReady = false;
  configurationHasVolume = false;
}

void nextionFlushInput() {
  while (nextionSerial.available()) {
    nextionSerial.read();
  }
  nextionLineLen = 0;
  nextionLineOverflow = false;
}

void nextionSuspend() {
#if NEXTION_SUSPEND_DURING_MOVE
  nextionSerial.end();
#endif
}

void nextionResume() {
#if NEXTION_SUSPEND_DURING_MOVE
  nextionSerial.begin(NEXTION_BAUD);
  nextionSerial.listen();
  nextionFlushInput();
#endif
}

void nextionEndCommand() {
  nextionSerial.write(0xFF);
  nextionSerial.write(0xFF);
  nextionSerial.write(0xFF);
}

void nextionSetText(const char *obj, const char *text) {
  nextionSerial.print(obj);
  nextionSerial.print(F(".txt=\""));
  nextionSerial.print(text);
  nextionSerial.print('"');
  nextionEndCommand();
}

void nextionSetText(const char *obj, const __FlashStringHelper *text) {
  nextionSerial.print(obj);
  nextionSerial.print(F(".txt=\""));
  nextionSerial.print(text);
  nextionSerial.print('"');
  nextionEndCommand();
}

void nextionSetNumber(const char *obj, long value) {
  nextionSerial.print(obj);
  nextionSerial.print(F(".val="));
  nextionSerial.print(value);
  nextionEndCommand();
}

void nextionLog(const __FlashStringHelper *msg) {
  // tLog.txt+="pesan\r" (Nextion mengubah \r menjadi CRLF).
  nextionSerial.print(F("tLog.txt+=\""));
  nextionSerial.print(msg);
  nextionSerial.print(F("\\r\""));
  nextionEndCommand();
}

void nextionSendCurrentStatus() {
  const __FlashStringHelper *status = F("BOOT");

  switch (nextionState) {
    case NEX_BOOT:    status = F("BOOT");    break;
    case NEX_HOMING:  status = F("HOMING");  break;
    case NEX_READY:   status = F("READY");   break;
    case NEX_RUNNING: status = F("RUNNING"); break;
    case NEX_ERROR:   status = F("ERROR");   break;
  }

  nextionSetText("tStatus", status);
  nextionSetNumber("jProg", nextionProgress);
}

void nextionSetState(NextionState state) {
  nextionState = state;
  nextionSendCurrentStatus();
}

void nextionConfigError(const __FlashStringHelper *reason) {
  configurationReady = false;
  configurationHasVolume = false;
  nextionSetState(NEX_ERROR);
  nextionLog(reason);
}

CalcStatus calculateConfiguration(float volume, float factor, long caps, long &steps) {
  if (!(volume > 0.0f) || volume > (float)MAX_VOLUME_UL) {
    return CALC_VOLUME_RANGE;
  }
  if (!(factor > 0.0f)) {
    return CALC_FACTOR_RANGE;
  }
  if (caps < 1 || caps > 5) {
    return CALC_CAPSULE_RANGE;
  }

  double stepValue = (double)volume * (double)factor;
  if (!(stepValue >= 1.0) || stepValue > (double)MAX_STEP_PER_CAPSULE) {
    return CALC_STEP_RANGE;
  }

  // Samakan dengan round() Python: pecahan tepat 0,5 dibulatkan ke bilangan genap.
  long lower = (long)stepValue;
  double fraction = stepValue - (double)lower;
  long result;
  if (fraction > 0.5) {
    result = lower + 1;
  } else if (fraction < 0.5) {
    result = lower;
  } else {
    result = (lower % 2 == 0) ? lower : lower + 1;
  }

  if (result <= 0) {
    return CALC_STEP_RANGE;
  }

  steps = result;
  return CALC_OK;
}

const __FlashStringHelper *calcErrorMessage(CalcStatus status) {
  switch (status) {
    case CALC_VOLUME_RANGE:  return F("ERR: volume 0-10 uL");
    case CALC_FACTOR_RANGE:  return F("ERR: faktor > 0");
    case CALC_CAPSULE_RANGE: return F("ERR: kapsul 1-5");
    case CALC_STEP_RANGE:    return F("ERR: hasil step");
    default:                 return F("ERR: kalkulasi");
  }
}

bool startDispenseCycle() {
  if (!configurationReady) {
    return false;
  }

  if (configurationHasVolume) {
    long steps;
    CalcStatus status = calculateConfiguration(volumePerCapsule_uL, calibrationFactor, jumlahCapsule, steps);
    if (status != CALC_OK) {
      configurationReady = false;
      return false;
    }
    stepsPerCapsule = steps;
  } else if (stepsPerCapsule <= 0) {
    configurationReady = false;
    return false;
  }

  totalVolumeSteps = stepsPerCapsule * jumlahCapsule;
  nextionProgress = 0;
  nextionSetState(NEX_RUNNING);
  nextionLog(F("Mulai siklus"));

  char text[12];
  ltoa(stepsPerCapsule, text, 10);
  nextionSetText("tStep", text);

  Serial.println(F("=== SIKLUS DIMULAI ==="));
  Serial.print(F("Volume per kapsul (uL): ")); Serial.println(volumePerCapsule_uL);
  Serial.print(F("Faktor (step/uL): ")); Serial.println(calibrationFactor);
  Serial.print(F("Step per kapsul: ")); Serial.println(stepsPerCapsule);
  Serial.print(F("Jumlah kapsul: ")); Serial.println(jumlahCapsule);
  Serial.print(F("Total steps: ")); Serial.println(totalVolumeSteps);

  prepareIodium();
  runAutoDispense();

  nextionProgress = 100;
  nextionSetState(NEX_READY);
  nextionLog(F("Siklus selesai"));
  Serial.println(F("OK: SIKLUS SELESAI"));
  return true;
}

void nextionHandleConfig(char *body) {
  char *end;
  char *cursor = body;

  double volume = strtod(cursor, &end);
  if (end == cursor || *end != ';') {
    nextionConfigError(F("ERR: format volume"));
    return;
  }

  cursor = end + 1;
  double factor = strtod(cursor, &end);
  if (end == cursor || *end != ';') {
    nextionConfigError(F("ERR: format faktor"));
    return;
  }

  cursor = end + 1;
  long caps = strtol(cursor, &end, 10);
  if (end == cursor || *end != '\0') {
    nextionConfigError(F("ERR: format kapsul"));
    return;
  }

  long steps;
  CalcStatus status = calculateConfiguration((float)volume, (float)factor, caps, steps);
  if (status != CALC_OK) {
    nextionConfigError(calcErrorMessage(status));
    return;
  }

  volumePerCapsule_uL = (float)volume;
  calibrationFactor = (float)factor;
  stepsPerCapsule = steps;
  jumlahCapsule = caps;
  totalVolumeSteps = steps * caps;
  configurationReady = true;
  configurationHasVolume = true;

  char text[12];
  ltoa(steps, text, 10);
  nextionSetText("tStep", text);
  nextionSetState(NEX_READY);
  nextionLog(F("CFG OK; motor belum jalan"));
}

void nextionHandleLine(char *line) {
  if (strcmp(line, "PING") == 0) {
    nextionSendCurrentStatus();
    return;
  }

  if (strcmp(line, "START") == 0) {
    if (!startDispenseCycle()) {
      nextionConfigError(F("ERR: CFG belum valid"));
    }
    return;
  }

  if (strcmp(line, "STOP") == 0) {
    // Firmware masih blocking; STOP baru akan dicek pada tahap berikutnya.
    nextionLog(F("STOP diterima; stop belum aktif"));
    return;
  }

  if (strncmp(line, "CMD:", 4) == 0) {
    const char *cmd = line + 4;
    if (strncmp(cmd, "INIT:", 5) == 0) {
      nextionLog(F("ERR: pakai CFG lalu START"));
      return;
    }
    executeCommand(String(cmd));
    nextionSendCurrentStatus();
    return;
  }

  if (strncmp(line, "CFG:", 4) == 0) {
    nextionHandleConfig(line + 4);
    return;
  }

  nextionLog(F("ERR: perintah tidak dikenal"));
}

void nextionPoll() {
  while (nextionSerial.available()) {
    char c = (char)nextionSerial.read();

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      nextionLine[nextionLineLen] = '\0';

      if (nextionLineOverflow) {
        nextionLog(F("ERR: baris terlalu panjang"));
      } else if (nextionLineLen > 0) {
        nextionHandleLine(nextionLine);
      }

      nextionLineLen = 0;
      nextionLineOverflow = false;
      continue;
    }

    if (nextionLineLen < NEXTION_LINE_MAX - 1) {
      nextionLine[nextionLineLen++] = c;
    } else {
      nextionLineOverflow = true;
    }
  }
}

// ============================================================
// HOMING MOTOR
// ============================================================
long homingMotor(int id) {
  Serial.print("Homing motor "); Serial.println(id);
  long langkah = 0;

  // Cegah ISR SoftwareSerial mengganggu pulsa step saat motor bergerak.
  nextionSuspend();

  digitalWrite(enablePins[id-1], LOW);
  digitalWrite(dirPin, homingDir[id-1] ? HIGH : LOW);

  while (digitalRead(limitPins[id-1]) == HIGH) {
    stepPulse(homingDelayUs);
    langkah++;
  }

  digitalWrite(dirPin, (homingDir[id-1] == LOW) ? HIGH : LOW);
  for (int i = 0; i < 50; i++) { stepPulse(homingDelayUs); langkah++; }

  digitalWrite(dirPin, homingDir[id-1] ? HIGH : LOW);
  while (digitalRead(limitPins[id-1]) == HIGH) {
    stepPulse(homingDelayUs);
    langkah++;
  }

  digitalWrite(enablePins[id-1], HIGH);
  nextionResume();

  Serial.print("Motor "); Serial.print(id);
  Serial.print(" langkah menuju HOME = "); Serial.println(langkah);
  return langkah;
}

// ============================================================
// STEP PULSE
// ============================================================
void stepPulse(int delayUs) {
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(delayUs);
  digitalWrite(stepPin, LOW);
  delayMicroseconds(delayUs);
}

// ============================================================
// MOVE MOTOR WITH ACCEL/DECEL
// ============================================================
void moveMotorAccel(int id, long langkah, int arah) {
  // arah: 0 atau 1
  if (id < 1 || id > 4) return;

  // Cegah ISR SoftwareSerial mengganggu pulsa step saat motor bergerak.
  nextionSuspend();

  digitalWrite(enablePins[id-1], LOW);
  digitalWrite(dirPin, arah ? HIGH : LOW);

  int minDelay = motorMinDelayUs[id - 1];
  int maxDelay = motorStartDelayUs[id - 1];
  long accelSteps = langkah / 8;
  if (accelSteps < 1) accelSteps = 1;

  // Accel
  for (long i = 0; i < accelSteps; i++) {
    int d = map(i, 0, accelSteps, maxDelay, minDelay);
    stepPulse(d);
  }

  // Cruise (catatan: jika langkah sangat kecil, loop ini bisa nol)
  for (long i = 0; i < langkah/2; i++) stepPulse(minDelay);

  // Decel
  for (long i = 0; i < accelSteps; i++) {
    int d = map(i, 0, accelSteps, minDelay, maxDelay);
    stepPulse(d);
  }

  digitalWrite(enablePins[id-1], HIGH);

  // Update posisi absolut
  if (arah == 0) posAbs[id] += langkah;
  else posAbs[id] -= langkah;

  nextionResume();
}


// ============================================================
// FASE PREPARE IODIUM (dipanggil sekali saat INIT)
// Membuat sistem berada di posisi standar sebelum kapsul
// ============================================================
void prepareIodium() {

  Serial.println("=== FASE PREPARE IODIUM ===");

  // M2 ke posisi kosong (plunger turun maksimal 113000)
  moveMotorAccel(2, 113000, 0);

  // M3 masuk ke dalam botol iodium
  moveMotorAccel(3, 135000, 1);

  // Priming awal (ambil sedikit cairan agar tidak ada udara)
  moveMotorAccel(2, stepsPerCapsule, 1);

  // M3 keluar botol dan standby di atas
  moveMotorAccel(3, 110000, 0);

  Serial.println("=== PREPARE SELESAI, SIAP DISPENSE ===");
}


// ============================================================
// AUTO DISPENSE SYSTEM (SINGLE FILL - SINGLE DISPENSE)
// Setiap kapsul: sedot → pindah → dispense
// ============================================================
void runAutoDispense() {

  Serial.println("=== AUTO DISPENSE PIPELINE SYRINGE ===");

  const long POS_BOTOL    = 0;
  const long POS_KAPSUL1  = 31500; 
  const long JARAK_KAPSUL = 16000;
  const int  TOTAL_SLOT   = 5;  
  const long POS_KAPSUL5  = POS_KAPSUL1 + 4 * JARAK_KAPSUL; // 95500
  const long POS_PENUTUP  = POS_KAPSUL5 + 133000;           // 227500

  // =========================
  // 🧪 KAPSUL 1 (HASIL PREPARE) — TANPA KE BOTOL
  // =========================
  Serial.println("=== KAPSUL 1 (hasil prepare) ===");

  long targetPos = POS_KAPSUL1;
  moveMotorAccel(1, targetPos - posAbs[1], 0);

  moveMotorAccel(3, 55000, 1);
  moveMotorAccel(2, stepsPerCapsule, 0);   // dispense isi prepare
  moveMotorAccel(3, 55000, 0);

  // =========================
  // 🧪 KAPSUL 2 s.d jumlahCapsule (pola normal)
  // =========================
  for (int i = 2; i <= jumlahCapsule; i++) {

    Serial.print("=== PROSES KAPSUL "); Serial.println(i);

    // Ke botol
    moveMotorAccel(1, posAbs[1], 1);

    moveMotorAccel(3, 110000, 1);
    moveMotorAccel(2, stepsPerCapsule, 1);  // sedot
    moveMotorAccel(3, 110000, 0);

    // Ke kapsul i
    long targetPos = POS_KAPSUL1 + (i - 1) * JARAK_KAPSUL;
    moveMotorAccel(1, targetPos - posAbs[1], 0);

    // Dispense
    moveMotorAccel(3, 55000, 1);
    moveMotorAccel(2, stepsPerCapsule, 0);
    moveMotorAccel(3, 55000, 0);
  }

  // =========================
  // ⏭️ BYPASS SLOT SISA
  // =========================
  for (int i = jumlahCapsule + 1; i <= TOTAL_SLOT; i++) {
    Serial.print("=== BYPASS KAPSUL "); Serial.println(i);
    long targetPos = POS_KAPSUL1 + (i - 1) * JARAK_KAPSUL;
    moveMotorAccel(1, targetPos - posAbs[1], 0);
  }

  // =========================
  // 🔒 FASE PENUTUP
  // =========================
  Serial.println("=== FASE PENUTUP ===");
  
  moveMotorAccel(1, POS_PENUTUP - posAbs[1], 0);
  moveMotorAccel(4, 16000, 1);

  // =========================
  // 🏁 HOME OTOMATIS
  // =========================
  // Homing awal: 4 → 3 → 2 → 1
  Serial.println("HOME:ALL otomatis...");

  long L4 = homingMotor(4); homingOffset[4] = L4; posAbs[4] = 0;
  long L3 = homingMotor(3); homingOffset[3] = L3; posAbs[3] = 0;
  long L2 = homingMotor(2); homingOffset[2] = L2; posAbs[2] = 0;
  //long L1 = homingMotor(1); homingOffset[1] = L1; posAbs[1] = 0;

  Serial.println("=== AUTO SELESAI ===");
}

// ============================================================
// PARSER
// ============================================================
void processQueue(String data) {
  int start = 0;
  while ((start = data.indexOf('<')) != -1) {
    int end = data.indexOf('>', start);
    if (end == -1) break;
    String cmd = data.substring(start + 1, end);
    executeCommand(cmd);
    data = data.substring(end + 1);
  }
}

// ============================================================
// EXECUTE COMMAND
// - M#;steps;dir
// - INIT:volume;factor;caps  -> kalkulasi di Arduino lalu jalankan siklus
// - INIT:steps;caps          -> kompatibilitas lama (step sudah dihitung HMI)
// - START                    -> jalankan konfigurasi CFG yang tersimpan
// ============================================================
void executeCommand(String cmd) {

  if (cmd == "START") {
    if (!startDispenseCycle()) {
      nextionConfigError(F("ERR: CFG belum valid"));
      Serial.println(F("ERR: Konfigurasi belum valid"));
      return;
    }
    Serial.println(F("OK: START Accepted"));
    return;
  }

  // Perintah motor langsung
  if (cmd.startsWith("M")) {
    cmd.remove(0,1);
    int s1 = cmd.indexOf(';');
    int s2 = cmd.indexOf(';', s1 + 1);
    if (s1 == -1 || s2 == -1) { Serial.println("Format M invalid"); return; }

    long motor = cmd.substring(0, s1).toInt();
    long langkah = cmd.substring(s1 + 1, s2).toInt();
    long arah = cmd.substring(s2 + 1).toInt();

    if (motor >= 1 && motor <= 4) moveMotorAccel(motor, langkah, arah);
    else Serial.println("Motor invalid");
    return;
  }

  // Format baru: <INIT:volume;factor;capsule>
  // Format lama : <INIT:steps;capsule> (dipertahankan untuk kompatibilitas)
  if (cmd.startsWith("INIT:")) {
    int s1 = cmd.indexOf(';', 5);
    if (s1 == -1) {
      Serial.println(F("ERR: INIT Format Wrong"));
      return;
    }

    int s2 = cmd.indexOf(';', s1 + 1);

    if (s2 == -1) {
      long stepInput = cmd.substring(5, s1).toInt();
      long caps = cmd.substring(s1 + 1).toInt();

      if (stepInput <= 0 || caps < 1 || caps > 5) {
        nextionConfigError(F("ERR: INIT tidak valid"));
        Serial.println(F("ERR: INIT Value Out of Range"));
        return;
      }

      volumePerCapsule_uL = 0.0f;
      calibrationFactor = 0.0f;
      stepsPerCapsule = stepInput;
      jumlahCapsule = caps;
      totalVolumeSteps = stepsPerCapsule * caps;
      configurationReady = true;
      configurationHasVolume = false;

      Serial.println(F("WARN: LEGACY INIT (step dari HMI)"));
      if (!startDispenseCycle()) {
        Serial.println(F("ERR: START gagal"));
        return;
      }
      Serial.println(F("OK: INIT Accepted"));
      return;
    }

    float volume = cmd.substring(5, s1).toFloat();
    float factor = cmd.substring(s1 + 1, s2).toFloat();
    long caps = cmd.substring(s2 + 1).toInt();

    long steps;
    CalcStatus status = calculateConfiguration(volume, factor, caps, steps);
    if (status != CALC_OK) {
      nextionConfigError(calcErrorMessage(status));
      Serial.print(F("ERR: "));
      Serial.println(calcErrorMessage(status));
      return;
    }

    volumePerCapsule_uL = volume;
    calibrationFactor = factor;
    stepsPerCapsule = steps;
    jumlahCapsule = caps;
    totalVolumeSteps = steps * caps;
    configurationReady = true;
    configurationHasVolume = true;

    if (!startDispenseCycle()) {
      Serial.println(F("ERR: START gagal"));
      return;
    }
    Serial.println(F("OK: INIT Accepted"));
    return;
  }


  // HOME:ALL atau HOME:n dapat tetap dipanggil sebagai perintah manual
  if (cmd.startsWith("HOME:")) {
    if (cmd.indexOf("ALL") != -1) {
      long L4 = homingMotor(4); homingOffset[4] = L4; posAbs[4] = 0;
      long L3 = homingMotor(3); homingOffset[3] = L3; posAbs[3] = 0;
      long L2 = homingMotor(2); homingOffset[2] = L2; posAbs[2] = 0;
      long L1 = homingMotor(1); homingOffset[1] = L1; posAbs[1] = 0;

      Serial.println();
      Serial.println("--- Rekap posisi dari HOME (langkah) ---");
      Serial.print("Motor 1: "); Serial.println(homingOffset[1]);
      Serial.print("Motor 2: "); Serial.println(homingOffset[2]);
      Serial.print("Motor 3: "); Serial.println(homingOffset[3]);
      Serial.print("Motor 4: "); Serial.println(homingOffset[4]);
    } else {
      int motor = getValue(cmd, "HOME:");
      if (motor >= 1 && motor <= 4) {
        long L = homingMotor(motor);
        homingOffset[motor] = L;
        posAbs[motor] = 0;
        Serial.print("Posisi Motor ");
        Serial.print(motor);
        Serial.print(" dari HOME = ");
        Serial.println(L);
      } else Serial.println("Perintah HOME invalid.");
    }
    return;
  }

  // READPOS
  if (cmd == "READPOS") {
    Serial.println("--- Posisi Absolut Motor (langkah dari HOME) ---");
    for (int i = 1; i <= 4; i++) {
      Serial.print("Motor "); Serial.print(i); Serial.print(": "); Serial.println(posAbs[i]);
    }
    return;
  }

  Serial.println("Silakan masukkan perintah lagi : ");
}

// ============================================================
// Utility parsers (tidak terpakai tapi dipertahankan)
// ============================================================
long getValue(String data, String label) {
  int start = data.indexOf(label);
  if (start == -1) return -1;
  int end = data.indexOf(';', start);
  if (end == -1) end = data.length();
  String val = data.substring(start + label.length(), end);
  return val.toInt();
}

long getValueLong(String data, String label) {
  int start = data.indexOf(label);
  if (start == -1) return -1;
  int end = data.indexOf(';', start);
  if (end == -1) end = data.length();
  String val = data.substring(start + label.length(), end);
  return val.toInt();
}



/*
<INIT:20330;3>

<HOME:ALL><INIT:1600;5>

<M2;154000;0>
<M3;122000;1>
<M2;8000;1>
<M3;90000;0>

<M1;31500;0>

<M3;45000;1>
<M2;1600;0>
<M3;45000;0>
<M1;16000;0>

<M3;45000;1>
<M2;1600;0>
<M3;45000;0>
<M1;16000;0>

<M3;45000;1>
<M2;1600;0>
<M3;45000;0>
<M1;16000;0>

<M3;45000;1>
<M2;1600;0>
<M3;45000;0>
<M1;16000;0>

<M3;45000;1>
<M2;1600;0>
<M3;45000;0>

<M1;132000;0>
<M4;16000;1>
<HOME:ALL>

<M2;156000;0><M3;122000;1><M2;40000;1><M3;90000;0>
<M1;31500;0><M3;45000;1><M2;8000;0><M3;45000;0>
<M1;16000;0><M3;45000;1><M2;8000;0><M3;45000;0>
<M1;16000;0><M3;45000;1><M2;8000;0><M3;45000;0>
<M1;16000;0><M3;45000;1><M2;8000;0><M3;45000;0>
<M1;16000;0><M3;45000;1><M2;8000;0><M3;45000;0>
<M1;132000;0><M4;16000;1><HOME:ALL>

<M1;227500;0><M4;16000;1><HOME:4><HOME:1>
<M1;228500;0><M4;16000;1>
<HOME:ALL>
//31500+16000+16000+16000+16000+133000


Sumber -> Capsule 5 = <M1;95500;0>
Capsule 5 -> Tutup  = <M1;132000;0> 
Tutup Capsule       = <M4;16000;1>

<READPOS>
112000
<HOME:2><M2;113000;0>
<HOME:3><M3;135000;1><M3;110000;0>
<HOME:1><M1;31500;0>
<M3;25000;1>

FIX Command : 5 Capsule 8000step
<M2;154000;0><M3;122000;1><M2;40000;1><M3;90000;0><M1;31500;0><M3;45000;1><M2;8000;0><M3;45000;0><M1;16000;0><M3;45000;1><M2;8000;0><M3;45000;0><M1;16000;0><M3;45000;1><M2;8000;0><M3;45000;0><M1;16000;0><M3;45000;1><M2;8000;0><M3;45000;0><M1;16000;0><M3;45000;1><M2;8000;0><M3;45000;0><M1;132000;0><M4;16000;1><HOME:ALL>


<M2;156000;0><M3;122000;1><M2;65000;1><M3;90000;0>
<M1;140000;0><M3;90000;1><M2;65000;0><M3;90000;0>

<M1;140000;1><M3;90000;1><M2;101000;1><M3;90000;0>
<M1;140000;0><M3;90000;1><M2;101000;0><M3;90000;0>


*/