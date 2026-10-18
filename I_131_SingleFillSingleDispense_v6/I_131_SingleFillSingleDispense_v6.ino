//Berikan langkah apa saja disertai dengan komen ketika saya memberikan input berupa : <INIT:20330;4>
// === FULL ARDUINO PROGRAM (CLEANED) ===
// Menggabungkan: sistem HOMING, parser, gerakan motor, dan AUTO DISPENSE
// ----------------------------------------------------------------------

// --- Konfigurasi pin ---
const int enablePins[4] = {8, 9, 10, 11};
const int stepPin = 7;
const int dirPin = 6;
const int limitPins[4] = {3, 4, 5, 2};

// --- Arah homing masing-masing motor ---
const int homingDir[4] = {1, 1, 0, 0};

// --- Data mekanik lead screw ---
const float leadMotor1_mmPerRev = 2.0;
const float leadMotor2_mmPerRev = 1.0;
const float leadMotor3_mmPerRev = 1.0;
const float leadMotor4_mmPerRev = 8.0;
const long stepsPerRev = 1600;

// --- Variabel operasional ---
long jumlahCapsule = 0;

// Volume dalam mikroliter (input dari HMI)
float volumePerCapsule_uL = 0;

// ✅ BARU: step per kapsul dikirim langsung dari HMI
long stepsPerCapsule = 0;
long totalVolumeSteps = 0;

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

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(9600);

  for (int i = 0; i < 4; i++) {
    pinMode(enablePins[i], OUTPUT);
    digitalWrite(enablePins[i], HIGH); // disable driver
    pinMode(limitPins[i], INPUT_PULLUP);
  }

  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);

  // Homing awal: 4 → 3 → 2 → 1
  long L4 = homingMotor(4); homingOffset[4] = L4; posAbs[4] = 0;
  long L3 = homingMotor(3); homingOffset[3] = L3; posAbs[3] = 0;
  long L2 = homingMotor(2); homingOffset[2] = L2; posAbs[2] = 0;
  long L1 = homingMotor(1); homingOffset[1] = L1; posAbs[1] = 0;

  Serial.println("Sistem siap. Masukkan perintah...");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
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
// HOMING MOTOR
// ============================================================
long homingMotor(int id) {
  Serial.print("Homing motor "); Serial.println(id);
  long langkah = 0;

  digitalWrite(enablePins[id-1], LOW);
  digitalWrite(dirPin, homingDir[id-1] ? HIGH : LOW);

  while (digitalRead(limitPins[id-1]) == HIGH) {
    stepPulse(80);
    langkah++;
  }

  digitalWrite(dirPin, (homingDir[id-1] == LOW) ? HIGH : LOW);
  for (int i = 0; i < 50; i++) { stepPulse(250); langkah++; }

  digitalWrite(dirPin, homingDir[id-1] ? HIGH : LOW);
  while (digitalRead(limitPins[id-1]) == HIGH) {
    stepPulse(300);
    langkah++;
  }

  digitalWrite(enablePins[id-1], HIGH);

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

  digitalWrite(enablePins[id-1], LOW);
  digitalWrite(dirPin, arah ? HIGH : LOW);

  int minDelay = 80;
  int maxDelay = 800;
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
// - INIT:volume;caps   -> set globals, hit runAutoDispense()
// ============================================================
void executeCommand(String cmd) {

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

// Format: <INIT:volume;capsule>
if (cmd.startsWith("INIT:")) {
    int sep = cmd.indexOf(';');
    if (sep == -1) { 
      Serial.println("ERR: INIT Format Wrong"); 
      return; 
    }

    long stepInput = cmd.substring(5, sep).toInt();  // ✅ SUDAH STEP dari HMI
    int caps = cmd.substring(sep + 1).toInt();

    if (caps > 0 && caps <= 5 && stepInput > 0) {
      jumlahCapsule = caps;
      stepsPerCapsule = stepInput;          // ✅ langsung dari HMI
      totalVolumeSteps = stepsPerCapsule * jumlahCapsule;

      Serial.print("Volume per kapsul (uL): ");
      Serial.println(volumePerCapsule_uL);

      Serial.print("Step per kapsul: ");
      Serial.println(stepsPerCapsule);

      Serial.print("Total steps: ");
      Serial.println(totalVolumeSteps);
      
      prepareIodium();
      runAutoDispense();
      Serial.println("OK: INIT Accepted");

    } else {
      Serial.println("ERR: INIT Value Out of Range");
    }
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