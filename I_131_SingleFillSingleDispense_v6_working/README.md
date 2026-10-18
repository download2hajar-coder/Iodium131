# I-131 Single Fill / Single Dispense — v6 Working

## 1. Judul & Deskripsi Proyek

Sketch Arduino ini mengendalikan sistem empat motor stepper untuk proses persiapan cairan, pemindahan posisi kapsul, pengisian satu kali per kapsul, mekanisme penutup, dan homing. Arduino menerima perintah teks melalui Serial pada **9600 baud**.

Perintah utama `<INIT:stepsPerCapsule;jumlahCapsule>` menerima jumlah langkah syringe pump per kapsul langsung dari HMI, menjalankan persiapan (`prepareIodium()`), lalu pipeline pengisian otomatis (`runAutoDispense()`). Perintah manual untuk menggerakkan motor, homing, dan membaca estimasi posisi juga tersedia.

> **Peringatan keselamatan:** dokumentasi ini menjelaskan implementasi kode dan bukan validasi dosis, proses farmasi, atau persetujuan penggunaan klinis/radiologis. Pastikan desain mekanis, interlock, limit, penghentian darurat, dan prosedur operasi telah divalidasi oleh personel yang berwenang. Sketch ini melakukan gerakan blocking dan tidak dapat memproses perintah stop selama rangkaian gerak otomatis berlangsung.

## 2. Komponen Perangkat Keras (Hardware)

Komponen yang tersirat atau diperlukan agar fungsi sketch dapat berjalan:

- Board Arduino yang menyediakan GPIO digital, `micros()`, `millis()`, `delayMicroseconds()`, dan serial hardware. Board dan level logikanya harus sesuai dengan driver stepper.
- Empat motor stepper: M1, M2 (syringe pump), M3, dan M4.
- Empat driver stepper yang menerima sinyal STEP, DIR, dan ENABLE. Pin STEP dan DIR dibagi bersama; pin ENABLE memilih motor aktif.
- Empat limit switch, satu per motor, dipasang dengan wiring yang membuat input terbaca **LOW saat switch aktif**. Sketch mengaktifkan pull-up internal.
- Catu daya motor/driver yang sesuai arus dan tegangan spesifikasi masing-masing. Jangan memasok motor dari pin daya GPIO Arduino.
- Kabel USB/serial untuk unggah firmware dan/atau komunikasi dengan HMI/Serial Monitor.
- HMI atau komputer opsional untuk mengirim perintah serial sesuai protokol di bawah.

**Library:** sketch tidak mengimpor library eksternal. Ia menggunakan Arduino Core (`Arduino.h` secara implisit), termasuk `String`, Serial, fungsi GPIO, timer, `map()`, serta delay mikrodetik. Dukungan `String` dan kapasitas RAM berbeda antar-board; uji pada board yang dipakai.

## 3. Konfigurasi & Pemetaan Pin

| Sinyal/fungsi | Pin Arduino | Koneksi/perilaku kode |
|---|---:|---|
| STEP (bersama) | 7 | Ke input STEP keempat driver. Hanya motor dengan ENABLE aktif yang seharusnya bergerak. |
| DIR (bersama) | 6 | Ke input DIR keempat driver. Semua motor yang aktif menerima level arah yang sama. |
| ENABLE M1 | 8 | Ke ENABLE driver M1. Logika diasumsikan aktif-LOW: LOW mengaktifkan, HIGH menonaktifkan. |
| ENABLE M2 | 9 | Ke ENABLE driver M2; diasumsikan aktif-LOW. M2 digunakan sebagai syringe pump berdasarkan urutan program. |
| ENABLE M3 | 10 | Ke ENABLE driver M3; diasumsikan aktif-LOW. |
| ENABLE M4 | 11 | Ke ENABLE driver M4; diasumsikan aktif-LOW. |
| Limit M1 | 3 | Ke limit switch M1; `INPUT_PULLUP`, LOW berarti limit aktif. |
| Limit M2 | 4 | Ke limit switch M2; `INPUT_PULLUP`, LOW berarti limit aktif. |
| Limit M3 | 5 | Ke limit switch M3; `INPUT_PULLUP`, LOW berarti limit aktif. |
| Limit M4 | 2 | Ke limit switch M4; `INPUT_PULLUP`, LOW berarti limit aktif. |
| Serial RX/TX | Pin serial bawaan board | USB serial/HMI, 9600 baud. Pin fisiknya bergantung board. |

> **Penting:** pastikan input ENABLE driver memang aktif-LOW. Bila driver memiliki polaritas atau koneksi berbeda, sesuaikan wiring/kode. Tentukan sambungan ground bersama sesuai persyaratan driver dan board. Tidak ada LCD, sensor analog, atau tombol pengguna lain yang dikonfigurasi di sketch.

### Arah homing

Urutan nilai `homingDir` adalah `{1, 1, 0, 0}` untuk M1 sampai M4. Dalam implementasi, `1` menghasilkan DIR HIGH dan `0` menghasilkan DIR LOW. Arah mekanis yang dihasilkan bergantung pada wiring coil motor dan driver.

### Profil timing motor

Gerakan biasa melalui `moveMotorAccel()` menggunakan tabel berikut:

| Motor | Jeda minimum gerak | Jeda awal/akhir | Catatan |
|---|---:|---:|---|
| M1 | 80 µs | 800 µs | Setelan jeda minimum sketch. |
| M2 | 300 µs | 800 µs | Profil lebih lambat sebagai titik awal untuk syringe pump; perlu kalibrasi. |
| M3 | 80 µs | 800 µs | Setelan jeda minimum sketch. |
| M4 | 80 µs | 800 µs | Setelan jeda minimum sketch. |
| Homing semua motor | 80 µs | Tidak ada ramp | Dipakai saat mendekati limit, mundur 50 pulsa, dan mendekati ulang. |

Jeda yang lebih kecil meningkatkan laju pulsa, tetapi nilai tersebut **bukan** bukti kecepatan maksimum aman motor. Beban, arus, tegangan, microstepping, karakteristik driver, dan mekanik menentukan batas sebenarnya.

## 4. Alur Kerja Program (Logic Flow)

### 4.1 Startup

1. `setup()` membuka Serial pada 9600 baud.
2. Untuk M1–M4, pin ENABLE diatur sebagai OUTPUT lalu HIGH (driver nonaktif); pin limit diatur `INPUT_PULLUP`.
3. STEP (pin 7) dan DIR (pin 6) diatur sebagai OUTPUT.
4. Homing awal berjalan berurutan **M4 → M3 → M2 → M1**. Setiap pemanggilan mengaktifkan driver motor terkait, mencari limit, mundur 50 pulsa, melakukan pencarian ulang, lalu menonaktifkan driver. Posisi motor diinisialisasi menjadi nol.
5. Arduino mencetak `Sistem siap. Masukkan perintah...` dan menunggu data serial.

Saat homing, loop pencarian berhenti hanya ketika input limit menjadi LOW. **Tidak ada timeout** bila switch, wiring, atau arah gerak bermasalah.

### 4.2 Penerimaan dan parsing perintah

- `serialEvent()` menambahkan karakter yang diterima ke `inputString`.
- Buffer ditandai lengkap saat newline atau karakter `>` diterima.
- `loop()` meneruskan buffer ke `processQueue()`, yang mencari setiap perintah berbentuk `<...>`.
- Setiap isi perintah dikirim ke `executeCommand()`.
- Gerakan motor dan siklus otomatis bersifat blocking; selama fungsi gerak berjalan, firmware tidak memproses perintah baru.

### 4.3 Siklus INIT otomatis

Untuk `<INIT:20330;4>`:

1. Parser membaca `20330` sebagai **step per kapsul**, bukan nilai µL, dan `4` sebagai jumlah kapsul.
2. Validasi menerima jumlah kapsul 1–5 serta nilai step lebih dari 0.
3. `prepareIodium()` menjalankan M2 113.000 step arah 0; M3 135.000 arah 1; M2 sebanyak `stepsPerCapsule` arah 1 untuk priming; lalu M3 110.000 arah 0.
4. `runAutoDispense()` mengisi kapsul 1 dari hasil persiapan tanpa kembali ke botol terlebih dahulu.
5. Untuk kapsul 2 sampai jumlah yang diminta, sistem bergerak ke botol, menjalankan sedot dengan M2, berpindah ke posisi kapsul, lalu dispense.
6. Slot tersisa dilewati hingga total lima slot.
7. M1 berpindah ke posisi penutup; M4 bergerak 16.000 step arah 1.
8. Homing akhir dilakukan untuk M4, M3, dan M2. Homing M1 dikomentari/nonaktif pada bagian ini.
9. Program mencetak `=== AUTO SELESAI ===` dan kemudian `OK: INIT Accepted`.

Target indexer yang tertulis di kode: kapsul 1 = 31.500 step, jarak antarkapsul = 16.000 step, kapsul 5 = 95.500 step, dan posisi penutup = 227.500 step.

### 4.4 Perintah lain

- `<M2;154000;0>`: menggerakkan motor 2 sebanyak langkah yang diberikan dengan arah 0 (DIR LOW).
- `<HOME:ALL>`: homing M4 → M3 → M2 → M1 dan mencetak rekap.
- `<HOME:3>`: homing motor 3 saja.
- `<READPOS>`: mencetak estimasi `posAbs` M1–M4.

Untuk perintah motor, arah 0 memperbesar estimasi posisi dan arah selain 0 akan memilih DIR HIGH serta mengurangi estimasi posisi. Sebaiknya kirim arah hanya sebagai 0 atau 1.

## 5. Cara Penggunaan (How to Use)

### Prasyarat VS Code

1. Pasang VS Code.
2. Pasang ekstensi Arduino yang sesuai dengan workflow board yang digunakan (misalnya Arduino Community Edition), atau pasang Arduino CLI/toolchain vendor board.
3. Pastikan board core/toolchain telah terpasang dan board/port serial dikenali sistem operasi.
4. Hubungkan board lewat USB. Pastikan rangkaian driver dan limit switch sudah diperiksa sebelum motor diberi daya.

### Unggah sketch

1. Buka folder sketch `I_131_SingleFillSingleDispense_v6_working` di VS Code; file utama di dalamnya bernama `I_131_SingleFillSingleDispense_v6_working.ino`.
2. Pilih board/target yang tepat dan port serial yang sesuai melalui ekstensi Arduino atau Arduino CLI.
3. Jalankan **Verify/Compile** terlebih dahulu dan periksa pesan build.
4. Jika kompilasi berhasil, jalankan **Upload** melalui perintah ekstensi (Command Palette) atau workflow Arduino CLI yang dikonfigurasi untuk board tersebut.
5. Buka Serial Monitor dengan baud **9600**. Saat reset, amati pesan homing; setelah homing berhasil, monitor menampilkan `Sistem siap. Masukkan perintah...`.
6. Kirim satu perintah uji dengan format `<...>` dan akhiri dengan `>` atau newline. Pastikan area mekanis aman sebelum mengirim perintah yang menggerakkan motor.

Nama menu dan perintah unggah dapat berbeda sesuai ekstensi, model board, serta versi toolchain. Pilih target spesifik perangkat; jangan menganggap pin mapping atau voltage level sama untuk semua model Arduino.

## 6. Protokol Perintah Serial

| Perintah | Fungsi |
|---|---|
| `<INIT:stepPerCapsule;jumlah>` | Jalankan prepare dan pengisian otomatis; jumlah valid 1–5, step harus > 0. |
| `<Mnomor;langkah;arah>` | Gerak manual satu motor, misalnya `<M2;1600;1>`. Motor 1–4. |
| `<HOME:ALL>` | Homing semua motor dalam urutan M4, M3, M2, M1. |
| `<HOME:n>` | Homing motor `n` (1–4). |
| `<READPOS>` | Tampilkan estimasi posisi internal dari home. |

Contoh `INIT` memakai angka step langsung. Meskipun output serial mencetak label `Volume per kapsul (uL)`, variabel `volumePerCapsule_uL` tidak diisi oleh parser ini dan tetap 0. Konversi volume ke step harus disediakan oleh HMI atau ditambahkan dan divalidasi di firmware.

## 7. Catatan Teknis dan Batasan yang Perlu Diperhatikan

1. **Jumlah pulsa gerak biasa:** `moveMotorAccel()` membuat `langkah/8` pulsa pada fase percepatan, `langkah/2` pada cruise, dan `langkah/8` pada perlambatan (secara kasar total sekitar 3/4 dari nilai `langkah`, bergantung pembulatan dan `map()`). Namun `posAbs` diperbarui sebesar seluruh nilai `langkah`. Estimasi posisi karena itu tidak sama dengan jumlah pulsa yang benar-benar dikeluarkan; verifikasi/fix sebelum mengandalkan target posisi atau volume.
2. **Gerak homing tanpa timeout:** limit yang tidak pernah aktif menyebabkan pencarian homing berjalan tanpa batas. Pertimbangkan timeout, fault state, dan penghentian perangkat keras.
3. **Profil homing cepat:** semua fase homing menggunakan jeda 80 µs, termasuk pendekatan ulang. Validasi risiko overshoot/benturan mekanis pada perangkat nyata.
4. **Tidak ada kontrol darurat saat auto berjalan:** parser tidak bisa memproses perintah lain saat fungsi gerak blocking.
5. **Posisi open-loop:** `posAbs` bukan pembacaan encoder; kehilangan langkah atau gerakan mekanis manual tidak terdeteksi.
6. **Parameter mekanis:** `leadMotor*_mmPerRev`, `stepsPerRev`, `volumePerCapsule_uL`, dan `POS_BOTOL` dideklarasikan tetapi tidak digunakan untuk menghitung gerak pada alur ini.
7. **Tidak ada validasi menyeluruh untuk nilai manual:** jumlah langkah negatif atau arah yang bukan 0/1 tidak secara eksplisit ditolak.
8. **M2 syringe pump:** nilai 300 µs adalah pengaturan awal firmware, bukan karakterisasi debit atau validasi akurasi volume.
9. **Serial dan memori:** penggunaan `String` dapat memakai heap/RAM; uji stabilitas sesuai kapasitas board.

Dokumentasi ini mengikuti kode yang tersedia saat ini. Perubahan wiring, board, mekanika, timing, atau urutan proses perlu disertai pembaruan dokumentasi dan pengujian ulang.
