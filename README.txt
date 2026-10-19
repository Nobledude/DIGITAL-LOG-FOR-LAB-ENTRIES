# Digital Log for Lab Entries

A lightweight, high-performance console application engineered in pure standard C designed to automate laboratory attendance tracking using generic USB barcode scanners. This system replaces slow, error-prone manual paper logs with instantaneous barcode lookups, local binary database indexing, and real-time spreadsheet generation.

---

## 🚀 Key Features

* **Hardware Integration Ready:** Works out-of-the-box with standard USB barcode scanners (which act as keyboard emulation inputs).
* **Low-Level Binary Storage:** Utilizes C file streams (`fopen`, `fwrite`, `fread`) to maintain a fast, indexed, serverless local database (`students.dat`).
* **Input Sanitization Layer:** Programmatically handles rogue scanner inputs by filtering control characters, whitespace variations, and standard barcode framing asterisks (`*`).
* **Hardware Debouncing Mechanism:** Actively drops duplicate scans occurring within a configurable window (default: 5 seconds) to eliminate accidental double check-ins.
* **Portable Spreadsheets:** Automatically interfaces with the OS time kernel (`<time.h>`) to append structured attendance entries into a standardized `.csv` file compatible with Microsoft Excel or Google Sheets.

---

## 🛠️ System Architecture & Workflow

The system relies on an efficient terminal console loop that coordinates raw hardware streams directly into local file storage operations:

```text
[ Barcode Scanner Input ] 
           │
           ▼
     [ sanitize_id() ] ────► [ find_student() ] ────► Record Match?
                                                       │
                           ┌───────────────────────────┴───────────────────────────┐
                           ▼                                                       ▼
                   [ YES: Log Entry ]                                      [ NO: Error/Menu ]
                           │                                                       │
                           ▼                                                       ▼
               Updates: "attendance.csv"                               Prompts: menu_add_student()
                                                                                   │
                                                                                   ▼
                                                                       Writes: "students.dat"
```

---

## 📦 Build & Execution Manual

### Compilation
Compile the project from your terminal using any standard standard compliance C compiler:

```bash
gcc -Wall -Wextra -O2 attendance.c -o attendance
```

### Modes of Operation

#### 1. Interactive Administration Menu Mode
Launches the full interactive management interface to add/remove records or open logging sequences manually:
```bash
./attendance
```

#### 2. Fast Bulk Import Mode
Appends student credentials directly into the database from an existing CSV sheet without booting up the visual console options:
```bash
./attendance --import list.csv [roll_column_index name_column_index]
```

#### 3. Master Database Replacement Mode
Wipes current local records and overwrites them clean with items from a fresh spreadsheet:
```bash
./attendance --replace list.csv [roll_column_index name_column_index]
```

---

## ⚠️ Architectural Drawbacks & Limitations

While this architecture is optimized for execution on low-spec systems without heavy relational software configurations, it possesses structural limitations:

1. **Sequential Search Bottleneck (O(N)):** Record matching is executed via a linear lookup across the binary file records. As database entries pass thousands of nodes, lookup efficiency scales down linearly.
2. **Concurrency Limitations:** Because database tracking depends on local file read/write locks, the application cannot run concurrently. Multiple scanner terminals writing to the same network share will corrupt files.
3. **Absence of Real-Time Check-Out Logs:** The system is explicitly tailored to handle check-in logging. It lacks structural features to calculate overall duration stay metrics or record exit times seamlessly.
4. **Hardcoded Administrative Credentials:** The security master passwords for performing destructive database wipes are written directly into macro code compilation definitions (`WIPE_PASSWORD`), creating exposure.

---

## 🤝 Contribution Guidelines

Contributions are what make the open-source community an amazing place to learn, inspire, and create. Any contributions you make are **greatly appreciated**.

We are actively seeking enhancements in the following spaces:

* **Algorithmic Optimizations:** Transitioning the flat database sequence from an O(N) sequential file sweep into an indexed **Hash Map** or **B-Tree** sorting schema to handle thousands of records in O(1) time.
* **Graphical or Web User Interfaces (GUI/WebUI):** Wrapping the standard C engine into a cross-platform visual interface framework (like Qt) or exposing an API surface to connect to a local intranet web dashboard.
* **Dual Logging Support:** Rewriting the entry workflow logic into a state machine that can detect if a student is checking *in* or checking *out*, enabling calculation of actual hours spent inside the lab.
* **Enhanced Security Layers:** Integrating modern hashing layers (like SHA-256) or config-driven authentication profiles to securely verify admin workflows instead of utilizing plain-text compilation macros.

### Steps to Contribute
1. Fork the Project Repository.
2. Create your Feature Branch (`git checkout -b feature/AmazingFeature`).
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`).
4. Push to the Branch (`git push origin feature/AmazingFeature`).
5. Open a well-documented **Pull Request**.

---

## 📄 License

Distributed under the MIT License. See `LICENSE` for more information.
