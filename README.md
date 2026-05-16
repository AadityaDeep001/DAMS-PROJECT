# DAMS v2.2 - Digital Attendance Management System

DAMS (Digital Attendance Management System) is a high-performance, secure, console-based academic management utility engineered in C++14. Designed for institutional scaling, it supports hierarchical data structures (Departments ➔ Semesters ➔ Sections ➔ Classrooms), robust custom data persistence using an inline position-based XOR-Base64 hybrid encryption system, cascading record mutations, and exhaustive reporting features with automated CSV generation.

---

## 🛠 Features

* **Secure Storage Architecture:** Protects student data and attendance records from manual text-editor manipulation by applying a symmetric, position-based `XOR` streaming cipher stacked over a `Base64` encoding layer using the logged-in administrator's password as the encryption key.
* **Hierarchical Structural Modeling:** Data isolation is maintained uniformly through a parent-to-child data tree mapping:
    $$\text{Department} \longrightarrow \text{Semester} \longrightarrow \text{Section} \longrightarrow \text{Classroom (Students, Subjects, Attendance)}$$
* **Automated Bulk Semester Promotion:** Seamlessly migrates an entire student demographic from a source semester across relative sections into target sections, auto-generating continuous, non-conflicting structural serial IDs while maintaining isolated, read-only historical attendance logs.
* **Interactive Terminal Graphics:** Implements responsive cross-platform ANSI escape sequences, dynamic typewriter effects, styled matrix charts, and operational loading banners.
* **Cascading Relational Integrity:** Enforces absolute integrity across data constraints; deleting a section or structural hierarchy triggers a recursive clean-up across child students, localized courses, and corresponding multi-point attendance histories.

---

## 📂 File System Structure & Data Schemas

The application handles metadata using human-readable flat CSV format files, whereas transaction logs and dynamic child boundaries are kept isolated in structural `.dat` binaries processed via customized `wb` and `rb` file streams.

### Core Data File Mapping

| Filename | Storage Mode | Schema Columns |
| :--- | :--- | :--- |
| **`admins.dat`** | Plain Text CSV | `Name,Username,Password` |
| **`departments.dat`** | XOR + Base64 | `DeptName,HOD,DeptCode,Intakes` |
| **`semesters.dat`** | XOR + Base64 | `SemName,Batch,SemCode,SemID,DeptCode,IntakeName` |
| **`sections.dat`** | XOR + Base64 | `SectionName,SectionID,SemID` |
| **`subjects.dat`** | XOR + Base64 | `SubjectName,SubjectCode,SectionID` |
| **`students.dat`** | XOR + Base64 | `Name,StudentID,SectionID` |
| **`attendance.dat`** | XOR + Base64 | `StudentID,Date,Status,SubjectCode,SectionID` |

> ⚠️ **Important Data Notice:** If upgrading structurally across variations, purge all internal `.dat` storage containers (excluding `admins.dat`) to let the runtime compile clean, freshly formatted schemas matching the updated block bounds.

---

## 💻 Build & Operational Deployment

The system incorporates cross-platform headers supporting conditional execution loops for both POSIX standard environments (`unistd.h`, `sys/stat.h`) and standard Windows environments (`windows.h`, `direct.h`, `conio.h`).

### Prerequisites
* A C++ compiler supporting the **C++14 language standard** or higher (such as `GCC / MinGW`).

### Compiling and Running
Run the following commands in your terminal from the root directory of the project workspace to build and execute the system using your designated path structure:

```bash
# Compile the application from the workspace root folder
g++ -std=c++14 src/main.cpp -o bin/DAMS_System.exe -mconsole

# Launch the compiled binary application executable
.\bin\DAMS_System.exe
