# Final HMI Auto Dispensing Iodium Capsule
# Full code will be generated here...

import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import threading
import time
import os
import csv
from datetime import datetime


# -----------------------
# CONFIG
# -----------------------
BAUDRATE = 9600
RECONNECT_INTERVAL_MS = 2000   # interval pengecekan auto-reconnect
SERIAL_READ_SLEEP = 0.1
MAX_VOLUME_UL = 10.0   # HARD LIMIT volume maksimum (µL)
LOG_DIR = "logs"
LOG_FILE = os.path.join(LOG_DIR, "dispensing_log.csv")

# -----------------------
# GLOBALS
# -----------------------
ser = None
serial_lock = threading.Lock()
running = True

# -----------------------
# HELPERS
# -----------------------

def log_to_csv(volume="", faktor="", step="", jumlah="", status="", note=""):
    try:
        os.makedirs(LOG_DIR, exist_ok=True)
        file_exists = os.path.isfile(LOG_FILE)

        with open(LOG_FILE, mode="a", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            if not file_exists:
                writer.writerow([
                    "timestamp",
                    "volume_ul",
                    "faktor_step_per_ul",
                    "step_per_capsule",
                    "jumlah_kapsul",
                    "status",
                    "keterangan"
                ])

            writer.writerow([
                datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                volume,
                faktor,
                step,
                jumlah,
                status,
                note
            ])
    except Exception as e:
        append_monitor(f"[LOG ERROR] {e}")


def detect_arduino_port():
    """Return device name if an Arduino/USB-serial is found, else None."""
    ports = serial.tools.list_ports.comports()
    for p in ports:
        desc = (p.description or "").lower()
        manufacturer = (p.manufacturer or "").lower()
        if "arduino" in desc or "ch340" in desc or "usb-serial" in desc or "ft232" in desc or "usb serial" in desc:
            return p.device
        # fallback heuristics
        if "usb" in desc and ("serial" in desc or "com" in desc):
            return p.device
    return None


def safe_write(s: str):
    """Write to serial in thread-safe manner."""
    global ser
    try:
        with serial_lock:
            if ser and ser.is_open:
                ser.write(s.encode())
                append_monitor(f"TX → {s.strip()}")
                return True
    except Exception as e:
        append_monitor(f"[ERROR TX] {e}")
    return False


def append_monitor(text: str):
    monitor.configure(state="normal")
    monitor.insert(tk.END, text + "\n")
    monitor.see(tk.END)
    monitor.configure(state="disabled")


def try_parse_float(s):
    try:
        return float(s)
    except:
        return None


def close_serial():
    global ser
    try:
        with serial_lock:
            if ser:
                if ser.is_open:
                    ser.close()
                ser = None
    except:
        pass

# -----------------------
# UI ACTIONS
# -----------------------
def send_init_command():
    vol_txt = entry_volume.get().strip()
    jumlah_txt = jumlah_var.get().strip()
    faktor_txt = kal_var.get().strip()

    # --- Validasi input ---
    volume = try_parse_float(vol_txt)
    faktor = try_parse_float(faktor_txt)

    if volume is None or volume <= 0:
        messagebox.showwarning("Input Error", "Volume (µL) harus > 0.")
        return

    if faktor is None or faktor <= 0:
        messagebox.showwarning("Input Error", "Faktor kalibrasi harus > 0.")
        return

    try:
        jumlah = int(jumlah_txt)
    except:
        messagebox.showwarning("Input Error", "Jumlah kapsul tidak valid.")
        return

    if jumlah < 1 or jumlah > 5:
        messagebox.showwarning("Input Error", "Jumlah kapsul maksimal 5.")
        return
    
    # --- Lock volume maksimum ---
    if volume > MAX_VOLUME_UL:
        messagebox.showerror("Volume Locked", f"Volume maksimum dibatasi {MAX_VOLUME_UL} µL.\n" f"Input Anda: {volume} µL")
        append_monitor(f"[LOCK] Volume {volume} µL ditolak (maks {MAX_VOLUME_UL} µL)")
        log_to_csv(
            volume=volume,
            faktor=faktor,
            jumlah=jumlah,
            status="LOCK",
            note="Volume melebihi batas maksimum"
        )
        return

    # --- Konversi µL → STEP ---
    step_per_capsule = int(round(volume * faktor))

    if step_per_capsule <= 0:
        messagebox.showerror("Conversion Error", "Hasil konversi STEP tidak valid.")
        return
    
    log_to_csv(
    volume=volume,
    faktor=faktor,
    step=step_per_capsule,
    jumlah=jumlah,
    status="SENT",
    note="INIT sent to Arduino")

    


    # --- Command baru (STEP based) ---
    cmd = f"<INIT:{step_per_capsule};{jumlah}>"

    append_monitor(
        f"[CALC] {volume} µL × {faktor} = {step_per_capsule} step"
    )

    progress.start(10)
    status_var.set("Sending INIT...")
    sent = safe_write(cmd + "\n")

    def watch_timeout(timeout=6):
        waited = 0.0
        while waited < timeout:
            time.sleep(0.2)
            waited += 0.2
            if "DONE" in status_var.get() or "ERROR" in status_var.get():
                break
        progress.stop()

    threading.Thread(target=watch_timeout, daemon=True).start()

    if not sent:
        messagebox.showerror("Serial Error", "Gagal mengirim ke Arduino.")
        progress.stop()
        status_var.set("Serial Disconnected")


def send_manual_command():
    cmd = entry_command.get().strip()
    if not cmd:
        messagebox.showwarning("Warning", "Command tidak boleh kosong.")
        return
    if not cmd.endswith("\n"):
        cmd_line = cmd + "\n"
    else:
        cmd_line = cmd
    ok = safe_write(cmd_line)
    if ok:
        entry_command.delete(0, tk.END)
    else:
        messagebox.showerror("Serial Error", "Serial tidak terhubung.")

# -----------------------
# SERIAL READ THREAD
# -----------------------
def serial_reader_thread():
    global ser, running
    while running:
        if ser and ser.is_open:
            try:
                with serial_lock:
                    data = ser.readline()
                if not data:
                    time.sleep(SERIAL_READ_SLEEP)
                    continue
                try:
                    text = data.decode(errors="ignore").strip()
                except:
                    text = str(data)
                if text:
                    append_monitor(f"RX ← {text}")
                    handle_incoming_status(text)
            except Exception as e:
                append_monitor(f"[RX ERROR] {e}")
                time.sleep(0.5)
        else:
            time.sleep(0.2)


def handle_incoming_status(msg: str):
    """Update status indicator based on incoming serial text."""
    mu = msg.upper()
    # Common keywords: READY, BUSY, FILLING, DONE, ERROR
    if "READY" in mu:
        status_label.config(text="Status: READY")
        status_label.config(foreground="lime")
        status_var.set("READY")
    elif "BUSY" in mu:
        status_label.config(text="Status: BUSY")
        status_label.config(foreground="orange")
        status_var.set("BUSY")
    elif "FILLING" in mu or "FILL" in mu:
        status_label.config(text="Status: FILLING")
        status_label.config(foreground="yellow")
        status_var.set("FILLING")
    elif "DONE" in mu or "FINISHED" in mu or "COMPLETE" in mu:
        status_label.config(text="Status: DONE")
        status_label.config(foreground="cyan")
        status_var.set("DONE")
    elif "ERROR" in mu or "FAIL" in mu:
        status_label.config(text="Status: ERROR")
        status_label.config(foreground="red")
        status_var.set("ERROR")
    elif "DONE" in mu or "FINISHED" in mu or "COMPLETE" in mu:
        status_label.config(text="Status: DONE")
        status_label.config(foreground="cyan")
        status_var.set("DONE")
    elif "ERROR" in mu or "FAIL" in mu:
        status_label.config(text="Status: ERROR")
        status_label.config(foreground="red")
        status_var.set("ERROR")
        log_to_csv(status="ERROR", note=msg)

    log_to_csv(status="DONE", note="Dispensing complete")

    # else: keep previous status

# -----------------------
# AUTO RECONNECT
# -----------------------
def auto_reconnect_loop():
    global ser
    try:
        if ser is None or not (ser.is_open if ser else False):
            port = detect_arduino_port()
            if port:
                try:
                    with serial_lock:
                        ser = serial.Serial(port, BAUDRATE, timeout=1)
                    time.sleep(1.5)  # give device a moment to reset
                    append_monitor(f"[Auto-Reconnect] Connected to {port}")
                    status_label.config(text="Status: READY")
                    status_label.config(foreground="lime")
                    status_var.set("READY")
                except Exception as e:
                    append_monitor(f"[Auto-Reconnect ERR] {e}")
                    ser = None
            else:
                # no port found
                pass
    except Exception as e:
        append_monitor(f"[Auto-Reconnect Exception] {e}")
    finally:
        root.after(RECONNECT_INTERVAL_MS, auto_reconnect_loop)

# -----------------------
# THEME TOGGLE
# -----------------------
theme_dark = True
def apply_theme_dark():
    root.configure(bg="#1e1e2f")
    style.configure("TLabel", background="#1e1e2f", foreground="white")
    style.configure("TButton", background="#4c8cff", foreground="white")
    style.configure("TFrame", background="#25253a")
    monitor.configure(bg="#0b1020", fg="white")
    status_bar.configure(background="#1e1e2f", foreground="white")

def apply_theme_light():
    root.configure(bg="#f0f0f0")
    style.configure("TLabel", background="#f0f0f0", foreground="black")
    style.configure("TButton", background="#e0e0e0", foreground="black")
    style.configure("TFrame", background="#ffffff")
    monitor.configure(bg="white", fg="black")
    status_bar.configure(background="#f0f0f0", foreground="black")

def toggle_theme():
    global theme_dark
    theme_dark = not theme_dark
    if theme_dark:
        apply_theme_dark()
        theme_btn.config(text="Light Mode")
    else:
        apply_theme_light()
        theme_btn.config(text="Dark Mode")

# -----------------------
# BUILD UI
# -----------------------
root = tk.Tk()
root.title("HMI - Auto Dispensing Iodium Capsule (FINAL)")
root.geometry("720x640")
root.minsize(720, 640)

style = ttk.Style()
style.theme_use("clam")

# Title
title = ttk.Label(root, text="AUTO DISPENSING IODIUM", font=("Segoe UI", 16, "bold"))
title.pack(pady=(12,6))

main_frame = ttk.Frame(root, padding=12)
main_frame.pack(fill="both", expand=True)

# --- Left: Controls ---
left = ttk.Frame(main_frame)
left.pack(side="top", fill="x", padx=8, pady=4)

# Faktor Kalibrasi
lbl_kal = ttk.Label(left, text="Faktor Kalibrasi (step / µL)")
lbl_kal.grid(row=0, column=2, sticky="w", pady=6)

kal_var = tk.StringVar(value="2033")
entry_kal = ttk.Entry(left, textvariable=kal_var, width=12)
entry_kal.grid(row=0, column=3, sticky="w", pady=6, padx=(6,0))

# Volume
lbl_vol = ttk.Label(left, text="Volume Iodium/Capsule (mikro liter)")
lbl_vol.grid(row=0, column=0, sticky="w", pady=6)
entry_volume = ttk.Entry(left, width=18)
entry_volume.grid(row=0, column=1, sticky="w", pady=6, padx=(6,20))

# Jumlah kapsul (Combobox 1..5)
lbl_jml = ttk.Label(left, text="Jumlah Kapsul (1–5)")
lbl_jml.grid(row=1, column=0, sticky="w", pady=6)
jumlah_var = tk.StringVar(value="1")
jumlah_combo = ttk.Combobox(left, textvariable=jumlah_var, values=["1","2","3","4","5"], state="readonly", width=16)
jumlah_combo.grid(row=1, column=1, sticky="w", pady=6, padx=(6,20))
jumlah_combo.current(0)

# Buttons row
btn_frame = ttk.Frame(left)
btn_frame.grid(row=2, column=0, columnspan=2, pady=(10,2))

btn_send = ttk.Button(btn_frame, text="KIRIM KE ARDUINO", command=send_init_command)
btn_send.grid(row=0, column=0, padx=6)

progress = ttk.Progressbar(btn_frame, mode="indeterminate", length=220)
progress.grid(row=0, column=1, padx=6)

# status label
status_label = ttk.Label(left, text="Status: UNKNOWN", font=("Segoe UI", 11, "bold"))
status_label.grid(row=3, column=0, columnspan=2, pady=(10,2), sticky="w")
status_var = tk.StringVar(value="UNKNOWN")

# --- Middle: Serial Monitor ---
mon_frame = ttk.LabelFrame(main_frame, text="Serial Monitor", padding=8)
mon_frame.pack(fill="both", expand=True, padx=8, pady=8)

monitor = tk.Text(mon_frame, height=18, wrap="none", state="disabled", font=("Consolas", 10))
monitor.pack(fill="both", expand=True, padx=4, pady=4)

# --- Bottom: Manual Command & Theme/Connect Controls ---
bottom_frame = ttk.Frame(main_frame)
bottom_frame.pack(fill="x", padx=8, pady=(2,12))

# Manual command
cmd_lbl = ttk.Label(bottom_frame, text="Manual Command:")
cmd_lbl.grid(row=0, column=0, sticky="w", pady=6)
entry_command = ttk.Entry(bottom_frame, width=40)
entry_command.grid(row=0, column=1, sticky="w", padx=6)
btn_send_cmd = ttk.Button(bottom_frame, text="Send Command", command=send_manual_command)
btn_send_cmd.grid(row=0, column=2, sticky="w", padx=6)

# Theme toggle
theme_btn = ttk.Button(bottom_frame, text="Light Mode", command=toggle_theme)
theme_btn.grid(row=1, column=0, pady=(8,0))

# Manual connect/refresh button (optional)
def manual_refresh():
    global ser   # deklarasi global berada di awal fungsi

    append_monitor("[Manual] Scanning ports...")

    try:
        if ser is None or not (ser.is_open if ser else False):
            port = detect_arduino_port()
            if port:
                try:
                    with serial_lock:
                        ser = serial.Serial(port, BAUDRATE, timeout=1)
                    time.sleep(1.2)
                    append_monitor(f"[Manual] Connected to {port}")
                    status_label.config(text="Status: READY")
                    status_label.config(foreground="lime")
                    status_var.set("READY")
                except Exception as e:
                    append_monitor(f"[Manual] Connect error: {e}")
            else:
                append_monitor("[Manual] No suitable COM port found.")
        else:
            append_monitor("[Manual] Already connected.")
    except Exception as e:
        append_monitor(f"[Manual] Exception: {e}")

btn_refresh = ttk.Button(bottom_frame, text="Refresh COM", command=manual_refresh)
btn_refresh.grid(row=1, column=1, pady=(8,0), sticky="w", padx=6)

# Status bar at very bottom
status_bar = tk.Label(root, text="Ready.", anchor="w")
status_bar.pack(fill="x", side="bottom")

# initial theme
apply_theme_dark()

# -----------------------
# START SERIAL THREADS & AUTO-RECONNECT
# -----------------------
reader_thread = threading.Thread(target=serial_reader_thread, daemon=True)
reader_thread.start()

root.after(500, auto_reconnect_loop)

# -----------------------
# CLEANUP ON EXIT
# -----------------------
def on_closing():
    global running
    if messagebox.askokcancel("Quit", "Keluar dari aplikasi HMI?"):
        running = False
        close_serial()
        root.destroy()

root.protocol("WM_DELETE_WINDOW", on_closing)

# -----------------------
# RUN APP
# -----------------------
if __name__ == "__main__":
    # create logs folder (optional)
    try:
        os.makedirs("logs", exist_ok=True)
    except:
        pass
    append_monitor("HMI started.")
    root.mainloop()
