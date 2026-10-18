// Tes perangkat keras: empat motor stepper berjalan serentak.
// Setiap limit switch yang baru ditekan membalik arah SEMUA motor.
//
// CATATAN PENTING:
// - STEP dan DIR dipakai bersama. Keempat motor menerima pulsa dan arah yang sama.
// - Motor berjalan terus pada konfigurasi jeda 80 us hingga dihentikan.
// - Amankan mekanik/area gerak sebelum menyalakan perangkat.
// - Gunakan Serial Monitor: kirim 'S' untuk stop, 'R' untuk lanjut.
// - 80 us adalah setelan tercepat dalam sketch, bukan jaminan batas aman maksimum.

const int enablePins[4] = {8, 9, 10, 11};
const int stepPin = 7;
const int dirPin = 6;
const int limitPins[4] = {3, 4, 5, 2};

const unsigned long STEP_HALF_PERIOD_US = 80;
const unsigned long LIMIT_DEBOUNCE_MS = 25;

bool directionState = LOW;
bool stepState = LOW;
bool motorsRunning = true;
bool directionChangePending = false;
unsigned long lastStepEdgeUs = 0;

bool lastRawLimitState[4];
bool stableLimitState[4];
unsigned long lastLimitChangeMs[4];

void setup() {
  Serial.begin(9600);

  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  digitalWrite(stepPin, LOW);
  digitalWrite(dirPin, directionState);

  for (int i = 0; i < 4; i++) {
    pinMode(enablePins[i], OUTPUT);
    digitalWrite(enablePins[i], LOW); // Enable driver aktif-low

    pinMode(limitPins[i], INPUT_PULLUP);
    bool initialState = digitalRead(limitPins[i]);
    lastRawLimitState[i] = initialState;
    stableLimitState[i] = initialState;
    lastLimitChangeMs[i] = millis();
  }

  lastStepEdgeUs = micros();
  Serial.println(F("Tes serentak dimulai: M1-M4 berjalan pada jeda 80 us."));
  Serial.println(F("Tekan limit mana pun untuk membalik arah semua motor."));
  Serial.println(F("Kirim S=stop, R=run."));
}

void loop() {
  processSerialControl();
  scanLimitSwitches();
  updateStepperPulse();
}

void processSerialControl() {
  while (Serial.available() > 0) {
    char command = Serial.read();

    if (command == 'S' || command == 's') {
      stopMotors();
      Serial.println(F("STOP: semua driver dinonaktifkan."));
    } else if (command == 'R' || command == 'r') {
      startMotors();
      Serial.println(F("RUN: semua motor berjalan."));
    }
  }
}

void scanLimitSwitches() {
  unsigned long nowMs = millis();
  bool anyNewPress = false;

  for (int i = 0; i < 4; i++) {
    bool rawState = digitalRead(limitPins[i]);

    if (rawState != lastRawLimitState[i]) {
      lastRawLimitState[i] = rawState;
      lastLimitChangeMs[i] = nowMs;
    }

    if (rawState != stableLimitState[i] &&
        nowMs - lastLimitChangeMs[i] >= LIMIT_DEBOUNCE_MS) {
      stableLimitState[i] = rawState;

      // INPUT_PULLUP: LOW berarti switch ditekan.
      if (stableLimitState[i] == LOW) {
        Serial.print(F("Limit M"));
        Serial.print(i + 1);
        Serial.println(F(" ditekan; membalik arah semua motor."));
        anyNewPress = true;
      }
    }
  }

  // Satu pembalikan untuk satu atau lebih switch yang terdeteksi ditekan
  // pada iterasi yang sama. Switch harus dilepas sebelum tekan berikutnya.
  if (anyNewPress && motorsRunning) {
    directionChangePending = true;
  }
}

void updateStepperPulse() {
  if (!motorsRunning) return;

  unsigned long nowUs = micros();

  // Ubah DIR hanya saat STEP LOW, lalu beri waktu setup sebelum pulsa berikutnya.
  if (directionChangePending && stepState == LOW) {
    directionState = !directionState;
    digitalWrite(dirPin, directionState);
    directionChangePending = false;
    lastStepEdgeUs = nowUs;
    Serial.print(F("Arah bersama sekarang: "));
    Serial.println(directionState == HIGH ? F("DIR HIGH") : F("DIR LOW"));
    return;
  }

  if (nowUs - lastStepEdgeUs >= STEP_HALF_PERIOD_US) {
    stepState = !stepState;
    digitalWrite(stepPin, stepState);
    lastStepEdgeUs = nowUs;
  }
}

void stopMotors() {
  motorsRunning = false;
  directionChangePending = false;
  stepState = LOW;
  digitalWrite(stepPin, LOW);

  for (int i = 0; i < 4; i++) {
    digitalWrite(enablePins[i], HIGH); // Disable driver aktif-low
  }
}

void startMotors() {
  digitalWrite(stepPin, LOW);
  stepState = LOW;
  lastStepEdgeUs = micros();

  for (int i = 0; i < 4; i++) {
    digitalWrite(enablePins[i], LOW); // Enable driver aktif-low
  }

  motorsRunning = true;
}
