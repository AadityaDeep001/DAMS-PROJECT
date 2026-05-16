/*
 * ============================================================
 *   DAMS v2.0 -  Digital Attendance Management System
 *   Authors : Aaditya Deep, Aaryan Keshari,
 *             Bali Kumar Wad, Rejan Dhungana
 * ============================================================
 *  COMPILE:
 *    g++ -std=c++14 main.cpp -o DAMS.exe -mconsole
 *
 *  DATA FILES
 *  ----------
 *  admins.dat      : plain text  Name,Username,Password
 *  departments.dat : XOR+base64  DeptName,HOD,DeptCode,Intakes
 *  semesters.dat   : XOR+base64  SemName,Batch,SemCode,SemID,DeptCode,IntakeName
 *  sections.dat    : XOR+base64  SectionName,SectionID,SemID
 *  subjects.dat    : XOR+base64  SubjectName,SubjectCode,SectionID
 *  students.dat    : XOR+base64  Name,StudentID,SectionID
 *  attendance.dat  : XOR+base64  StudentID,Date,Status,SubjectCode,SectionID
 *
 *  HIERARCHY: Department -> Semester -> Section -> Classroom Features
 *
 *  IMPORTANT: Delete all .dat files (except admins.dat if
 *  you want to keep admin) and re-run when upgrading formats.
 * ============================================================
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <iomanip>
#include <ctime>
#include <cctype>
#include <limits>
#include <cstdio>
#include <conio.h>

#ifdef _WIN32
  #include <windows.h>
  #include <direct.h>
  #define SLEEP_MS(ms) Sleep(ms)
  #define MKDIR(d)     _mkdir(d)
#else
  #include <unistd.h>
  #include <sys/stat.h>
  #define SLEEP_MS(ms) usleep((ms)*1000)
  #define MKDIR(d)     mkdir(d,0755)
#endif

using namespace std;

// ---------------------------------------------------------------
//  ANSI
// ---------------------------------------------------------------
#define RESET       "\033[0m"
#define BOLD        "\033[1m"
#define DIM         "\033[2m"
#define FG_RED      "\033[31m"
#define FG_GREEN    "\033[32m"
#define FG_YELLOW   "\033[33m"
#define FG_CYAN     "\033[36m"
#define FG_WHITE    "\033[37m"
#define FG_BRED     "\033[91m"
#define FG_BGREEN   "\033[92m"
#define FG_BYELLOW  "\033[93m"
#define FG_BBLUE    "\033[94m"
#define FG_BMAGENTA "\033[95m"
#define FG_BCYAN    "\033[96m"
#define FG_BWHITE   "\033[97m"
#define CURSOR_HIDE "\033[?25l"
#define CURSOR_SHOW "\033[?25h"
#define CLS         "\033[2J\033[H"
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
  #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

void enableANSI() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD m = 0;
    if (h && h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &m))
        SetConsoleMode(h, m | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

// --------------------------------------------------------------
//  SESSION STATE
// --------------------------------------------------------------
static string gAdminName    = "";
static string gAdminPw      = "";   // XOR key for .dat files
static string gDeptCode     = "";
static string gDeptName     = "";
static string gSemName      = "";
static string gBatch        = "";
static string gSemID        = "";
static string gSectionName  = "";   // NEW: current section name
static string gSectionID    = "";   // NEW: current section ID (SemID-SectionName)

// --------------------------------------------------------------
//  UTILITY
// --------------------------------------------------------------
void cls() { cout << CLS; cout.flush(); }

void waitKey() {
    cout << "\n  Press any key to continue...";
    cout.flush();
    while (_kbhit()) _getch();
    _getch();
}

string trim(const string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

string toLower(string s) {
    transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

string sanitize(const string &s) {
    string o = s;
    for (char &c : o) if (string(" /\\:*?\"<>|,").find(c) != string::npos) c = '_';
    return o;
}

string monthName(int m) {
    static const char* N[] = {"","January","February","March","April","May","June",
                               "July","August","September","October","November","December"};
    return (m>=1&&m<=12) ? N[m] : "Unknown";
}

// --------------------------------------------------------------
//  BASE64
// --------------------------------------------------------------
static const string B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

string b64Enc(const string &in) {
    string o; unsigned v=0; int vb=-6;
    for (unsigned char c : in) {
        v=(v<<8)+c; vb+=8;
        while(vb>=0){o+=B64[(v>>vb)&63]; vb-=6;}
    }
    if(vb>-6) o+=B64[((v<<8)>>(vb+8))&63];
    while(o.size()%4) o+='=';
    return o;
}

string b64Dec(const string &in) {
    string o; int T[256]; fill(T,T+256,-1);
    for(int i=0;i<64;i++) T[(unsigned char)B64[i]]=i;
    unsigned v=0; int vb=-8;
    for(unsigned char c:in){
        if(T[c]==-1) continue;
        v=(v<<6)+T[c]; vb+=6;
        if(vb>=0){o+=(char)((v>>vb)&255); vb-=8;}
    }
    return o;
}

// --------------------------------------------------------------
//  XOR CIPHER  (position-based: every byte XOR'd by key[i % kl])
//  This is fully symmetric: xorCrypt(xorCrypt(x,k),k) == x
//  for ALL inputs, with zero risk of key-index desync.
// --------------------------------------------------------------
string xorCrypt(const string &data, const string &key) {
    if (key.empty()) return data;
    string o = data;
    size_t kl = key.size();
    for (size_t i = 0; i < o.size(); ++i)
        o[i] = (char)((unsigned char)o[i] ^ (unsigned char)key[i % kl]);
    return o;
}

// --------------------------------------------------------------
//  DAT I/O   -  write: normalize line endings -> XOR -> base64
//            read : base64 -> XOR-decrypt -> plain CSV text
// --------------------------------------------------------------
bool writeDat(const string &fn, const string &plain) {
    // 1. Normalize: strip ALL \r so we always store \n-only text
    string norm; norm.reserve(plain.size());
    for (char c : plain) if (c != '\r') norm += c;

    // 2. Encrypt: XOR then base64-encode the raw bytes
    string enc = b64Enc(xorCrypt(norm, gAdminPw));
    enc += "\n";   // single trailing newline as record separator

    // 3. Write in binary mode  -  prevents Windows from mangling \n->\r\n
    FILE *fp = fopen(fn.c_str(), "wb");
    if (!fp) {
        cerr << "  [FATAL] Cannot open for write: " << fn << "\n";
        return false;
    }
    size_t written = fwrite(enc.c_str(), 1, enc.size(), fp);
    fflush(fp);
    fclose(fp);
    if (written != enc.size()) {
        cerr << "  [FATAL] Write incomplete: " << fn << "\n";
        return false;
    }
    return true;
}

string readDat(const string &fn) {
    // Read raw bytes in binary mode
    FILE *fp = fopen(fn.c_str(), "rb");
    if (!fp) return "";
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); return ""; }
    string raw(sz, 0);
    fread(&raw[0], 1, (size_t)sz, fp);
    fclose(fp);
    // Strip ALL whitespace/control chars to get clean base64
    string b64;
    b64.reserve(sz);
    for (unsigned char c : raw)
        if (c > 32 && c < 128) b64 += (char)c;
    if (b64.empty()) return "";
    string decoded = b64Dec(b64);
    if (decoded.empty()) return "";
    string plain = xorCrypt(decoded, gAdminPw);
    // Sanity check: decrypted data must be printable ASCII / newlines
    size_t total = 0, bad = 0;
    for (unsigned char c : plain) {
        if (c == '\n') continue;
        total++;
        if (c < 32 || c > 126) bad++;
    }
    if (total > 0 && bad * 5 > total) return "";  // >20% garbage = wrong key
    return plain;
}

// --------------------------------------------------------------
//  CSV LAYER
// --------------------------------------------------------------
vector<string> splitCSV(const string &line) {
    vector<string> t; istringstream ss(line); string tok;
    while (getline(ss, tok, ',')) t.push_back(trim(tok));
    return t;
}

// Returns ALL data rows (header skipped).
vector<vector<string>> readRows(const string &fn) {
    vector<vector<string>> rows;
    string plain = readDat(fn);
    if (plain.empty()) return rows;
    istringstream ss(plain); string line;
    getline(ss, line); // skip header
    while (getline(ss, line)) {
        string t = trim(line);
        if (!t.empty()) rows.push_back(splitCSV(t));
    }
    return rows;
}

bool writeRows(const string &fn, const string &hdr, const vector<vector<string>> &rows) {
    string plain = hdr + "\n";
    for (const auto &row : rows) {
        for (size_t i = 0; i < row.size(); ++i) {
            if (i) plain += ',';
            string f = trim(row[i]);
            f.erase(remove(f.begin(), f.end(), '\r'), f.end());
            plain += f;
        }
        plain += "\n";
    }
    return writeDat(fn, plain);
}

// Create a .dat file with header ONLY if the file is missing, empty,
// or unreadable with the current encryption key (stale from old session).
void initDat(const string &fn, const string &hdr) {
    FILE *fp = fopen(fn.c_str(), "rb");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fclose(fp);
        if (sz > 10) {
            string plain = readDat(fn);
            if (!plain.empty()) return;  // readable: leave it alone
        }
    }
    writeDat(fn, hdr + "\n");
}

// --------------------------------------------------------------
//  ADMIN DAT  (plain text  -  must be readable before login)
// --------------------------------------------------------------
void initAdminDat() {
    ifstream f("admins.dat");
    bool exists = false;
    if (f.is_open()) { f.seekg(0, ios::end); exists = (f.tellg() > 0); f.close(); }
    if (!exists) { ofstream o("admins.dat"); o << "Name,Username,Password\n"; }
}

bool adminExists() {
    ifstream f("admins.dat"); if (!f.is_open()) return false;
    string line; getline(f, line); // skip header
    while (getline(f, line)) if (!trim(line).empty()) return true;
    return false;
}

vector<vector<string>> readAdminRows() {
    vector<vector<string>> rows;
    ifstream f("admins.dat"); if (!f.is_open()) return rows;
    string line; getline(f, line);
    while (getline(f, line)) { string t=trim(line); if(!t.empty()) rows.push_back(splitCSV(t)); }
    return rows;
}

void writeAdminRow(const string &name, const string &user, const string &pw) {
    ofstream f("admins.dat");
    f << "Name,Username,Password\n" << name << "," << user << "," << pw << "\n";
}

// --------------------------------------------------------------
//  INPUT HELPERS
// --------------------------------------------------------------
int menuChoice() {
    int v = -1;
    string raw;
    if (!getline(cin, raw)) { cin.clear(); return -1; }
    raw = trim(raw);
    if (raw.empty()) return -1;
    bool allDig = true;
    for (size_t i = 0; i < raw.size(); i++) {
        if (i==0 && raw[i]=='-') continue;
        if (!isdigit((unsigned char)raw[i])) { allDig=false; break; }
    }
    if (!allDig) return -1;
    try { v = stoi(raw); } catch(...) { v = -1; }
    return v;
}

bool readLine(const string &prompt, string &out,
              size_t maxLen=0, bool noComma=true, bool alphaSpace=false) {
    cout << prompt;
    string raw; if (!getline(cin, raw)) { cin.clear(); return false; }
    raw = trim(raw);
    if (raw.empty())                { cout<<"  [ERROR] Cannot be empty.\n";       return false; }
    if (noComma && raw.find(',')!=string::npos) { cout<<"  [ERROR] No commas.\n"; return false; }
    if (alphaSpace) {
        for (unsigned char c : raw)
            if (!isalpha(c) && c!=' ') { cout<<"  [ERROR] Letters and spaces only.\n"; return false; }
    }
    if (maxLen > 0 && raw.size() > maxLen) { cout<<"  [ERROR] Too long (max "<<maxLen<<").\n"; return false; }
    out = raw; return true;
}

bool readDigits(const string &prompt, string &out, size_t minLen=1, size_t maxLen=10) {
    cout << prompt;
    string raw; if (!getline(cin, raw)) { cin.clear(); return false; }
    raw = trim(raw);
    if (raw.empty()) { cout<<"  [ERROR] Cannot be empty.\n"; return false; }
    for (unsigned char c : raw) if (!isdigit(c)) { cout<<"  [ERROR] Only digits allowed.\n"; return false; }
    if (raw.size() < minLen) { cout<<"  [ERROR] Must be at least "<<minLen<<" digit(s).\n"; return false; }
    if (raw.size() > maxLen) { cout<<"  [ERROR] Too many digits.\n"; return false; }
    out = raw; return true;
}

string getMaskedPw() {
    string pw; int ch;
    while (true) {
        ch = _getch();
        if (ch==0 || ch==0xE0) { _getch(); continue; }
        if (ch=='\r'||ch=='\n') break;
        if (ch==27) { cout<<"\n"; return ""; }
        if (ch=='\b') { if (!pw.empty()) { pw.pop_back(); cout<<"\b \b"; } }
        else if (ch>=32&&ch<127) { pw+=(char)ch; cout<<'*'; }
    }
    cout<<"\n"; return pw;
}

bool validatePw(const string &pw) {
    if (pw.size()<8) return false;
    bool al=false, nu=false, sy=false;
    for (unsigned char c:pw) {
        if (isalpha(c)) al=true;
        else if (isdigit(c)) nu=true;
        else if (ispunct(c)) sy=true;
    }
    return al&&nu&&sy;
}

// --------------------------------------------------------------
//  DATE HELPERS
// --------------------------------------------------------------
bool isLeap(int y){ return (y%4==0&&y%100!=0)||(y%400==0); }

bool validateDate(const string &s) {
    if (s.size()!=10 || s[4]!='-' || s[7]!='-') {
        cout<<"  [ERROR] Use YYYY-MM-DD format.\n"; return false;
    }
    for (int i=0;i<10;i++) {
        if (i==4||i==7) continue;
        if (!isdigit((unsigned char)s[i])) { cout<<"  [ERROR] Digits only in date.\n"; return false; }
    }
    int yr=stoi(s.substr(0,4)), mo=stoi(s.substr(5,2)), dy=stoi(s.substr(8,2));
    if (yr<2000||yr>2100) { cout<<"  [ERROR] Year must be 2000-2100.\n"; return false; }
    if (mo<1||mo>12)      { cout<<"  [ERROR] Month 01-12.\n"; return false; }
    int dim[]={0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (isLeap(yr)) dim[2]=29;
    if (dy<1||dy>dim[mo]) { cout<<"  [ERROR] Invalid day for that month.\n"; return false; }
    // Block future dates
    time_t now = time(nullptr);
    if (now != (time_t)-1) {
        struct tm *t = localtime(&now);
        if (t) {
            int ty=t->tm_year+1900, tm2=t->tm_mon+1, td=t->tm_mday;
            if (yr>ty || (yr==ty&&mo>tm2) || (yr==ty&&mo==tm2&&dy>td)) {
                cout<<"  [ERROR] Cannot mark future date.\n"; return false;
            }
        }
    }
    return true;
}

bool readDate(string &out) {
    while (true) {
        cout << "Date (YYYY-MM-DD, Q to cancel): ";
        string r; if (!getline(cin, r)) { cin.clear(); return false; }
        r = trim(r);
        if (r=="Q"||r=="q") return false;
        if (validateDate(r)) { out=r; return true; }
    }
}

char readPA(const string &label) {
    while (true) {
        cout << "  " << label << "  [P/A]: ";
        string r; if (!getline(cin, r)) { cin.clear(); continue; }
        r = trim(r);
        if (r.size()==1) { char c=toupper((unsigned char)r[0]); if(c=='P'||c=='A') return c; }
        cout << "  [ERROR] Enter P or A only.\n";
    }
}

// --------------------------------------------------------------
//  REPORTS DIR
// --------------------------------------------------------------
void ensureReports() { MKDIR("reports"); }

// --------------------------------------------------------------
//  CASCADING DELETE HELPERS
// --------------------------------------------------------------

// Delete all sections + classroom data (subjects/students/attendance)
// that belong to a given SemID prefix.
void cascadeDeleteSem(const string &semID) {
    // Remove sections belonging to this semester
    {
        auto r=readRows("sections.dat"); vector<vector<string>> k;
        for(auto&row:r) if(!(row.size()>=3&&row[2]==semID)) k.push_back(row);
        writeRows("sections.dat","SectionName,SectionID,SemID",k);
    }
    // Remove subjects whose SectionID starts with semID
    {
        auto r=readRows("subjects.dat"); vector<vector<string>> k;
        for(auto&row:r){
            if(row.size()>=3 && row[2].size()>=semID.size() &&
               row[2].substr(0,semID.size())==semID) continue;
            k.push_back(row);
        }
        writeRows("subjects.dat","SubjectName,SubjectCode,SectionID",k);
    }
    // Remove students whose SectionID starts with semID
    {
        auto r=readRows("students.dat"); vector<vector<string>> k;
        for(auto&row:r){
            if(row.size()>=3 && row[2].size()>=semID.size() &&
               row[2].substr(0,semID.size())==semID) continue;
            k.push_back(row);
        }
        writeRows("students.dat","Name,StudentID,SectionID",k);
    }
    // Remove attendance whose SectionID starts with semID
    {
        auto r=readRows("attendance.dat"); vector<vector<string>> k;
        for(auto&row:r){
            if(row.size()>=5 && row[4].size()>=semID.size() &&
               row[4].substr(0,semID.size())==semID) continue;
            k.push_back(row);
        }
        writeRows("attendance.dat","StudentID,Date,Status,SubjectCode,SectionID",k);
    }
}

// Cascading delete for a single Section (subjects, students, attendance)
void cascadeDeleteSection(const string &sectionID) {
    {
        auto r=readRows("sections.dat"); vector<vector<string>> k;
        for(auto&row:r) if(!(row.size()>=2&&row[1]==sectionID)) k.push_back(row);
        writeRows("sections.dat","SectionName,SectionID,SemID",k);
    }
    {
        auto r=readRows("subjects.dat"); vector<vector<string>> k;
        for(auto&row:r) if(!(row.size()>=3&&row[2]==sectionID)) k.push_back(row);
        writeRows("subjects.dat","SubjectName,SubjectCode,SectionID",k);
    }
    {
        auto r=readRows("students.dat"); vector<vector<string>> k;
        for(auto&row:r) if(!(row.size()>=3&&row[2]==sectionID)) k.push_back(row);
        writeRows("students.dat","Name,StudentID,SectionID",k);
    }
    {
        auto r=readRows("attendance.dat"); vector<vector<string>> k;
        for(auto&row:r) if(!(row.size()>=5&&row[4]==sectionID)) k.push_back(row);
        writeRows("attendance.dat","StudentID,Date,Status,SubjectCode,SectionID",k);
    }
}

// Full department cascading delete (dept + all semesters + all sections + all data)
void cascadeDelete(const string &deptCode) {
    // Get all semIDs that belong to this dept
    {
        auto semRows=readRows("semesters.dat");
        for(auto&r:semRows){
            if(r.size()>=5&&r[4]==deptCode) cascadeDeleteSem(r[3]);
        }
    }
    {
        auto r=readRows("departments.dat"); vector<vector<string>> k;
        for(auto&row:r) if(!(row.size()>=3&&row[2]==deptCode)) k.push_back(row);
        writeRows("departments.dat","DeptName,HOD,DeptCode,Intakes",k);
    }
    {
        auto r=readRows("semesters.dat"); vector<vector<string>> k;
        for(auto&row:r) if(!(row.size()>=5&&row[4]==deptCode)) k.push_back(row);
        writeRows("semesters.dat","SemName,Batch,SemCode,SemID,DeptCode,IntakeName",k);
    }
}

// --------------------------------------------------------------
//  REPORT FUNCTIONS
//  NOTE: semID parameter now refers to SectionID in classroom context.
//  The reportOverall / reportMonthlySubject / reportAllSubjects /
//  reportStudent functions accept a generic "scopeID" that maps to
//  the SectionID column in students/subjects/attendance.
// --------------------------------------------------------------

// Overall attendance summary: shows per-student totals across ALL subjectsxdates
void reportOverall(const string &scopeID, const string &label, const string &csvPath="") {
    auto stu = readRows("students.dat");
    auto att = readRows("attendance.dat");

    cout << "\n" << string(97,'=') << "\n";
    cout << "Overall Attendance Report  -  " << label << "\n";
    cout << string(97,'-') << "\n";
    cout << left << setw(22)<<"Name" << setw(32)<<"Student ID"
         << setw(10)<<"Present" << setw(12)<<"Total Cls" << setw(12)<<"Attend %" << "Eligibility\n";
    cout << string(97,'-') << "\n";

    string csv = "Name,Student ID,Present,Total Classes,Attend %,Eligibility\n";
    int count=0; double sumPct=0.0;

    for (auto &s : stu) {
        if (s.size()<3 || s[2]!=scopeID) continue;
        int pres=0, tot=0;
        for (auto &a : att) {
            if (a.size()<5 || a[0]!=s[1] || a[4]!=scopeID) continue;
            tot++;
            if (!a[2].empty() && toupper((unsigned char)a[2][0])=='P') pres++;
        }
        double pct = (tot>0) ? (pres*100.0/tot) : 0.0;
        string elig = (pct>=75.0) ? "Eligible" : "Not Eligible";

        cout << left << setw(22)<<s[0] << setw(32)<<s[1]
             << setw(10)<<pres << setw(12)<<tot
             << fixed<<setprecision(2)<<setw(12)<<pct << elig << "\n";

        ostringstream ps; ps<<fixed<<setprecision(2)<<pct;
        csv += s[0]+","+s[1]+","+to_string(pres)+","+to_string(tot)+","+ps.str()+","+elig+"\n";
        count++; sumPct+=pct;
    }

    cout << string(97,'-') << "\n";
    if (count>0)
        cout << "Total Students: "<<count<<"  |  Class Average: "<<fixed<<setprecision(2)<<(sumPct/count)<<"%\n";
    else
        cout << "No students found.\n";
    cout << string(97,'=') << "\n";

    if (!csvPath.empty()) {
        ensureReports();
        ofstream f(csvPath.c_str(), ios::out | ios::trunc);
        if(!f.is_open()){cout<<"  [ERROR] Cannot create CSV.\n";return;}
        f<<csv; f.flush(); f.close();
        cout<<"  [CSV exported] "<<csvPath<<"\n";
    }
}

// Monthly report for a single subject  -  terminal table + CSV
void reportMonthlySubject(const string &scopeID, const string &batch, const string &semCode,
                           const string &subCode, const string &subName, int yr, int mo) {
    ensureReports();
    auto stu = readRows("students.dat");
    auto att = readRows("attendance.dat");

    // Collect distinct dates in this month for this subject
    set<string> dset;
    for (auto &a : att) {
        if (a.size()<5||a[3]!=subCode||a[4]!=scopeID) continue;
        if (a[1].size()>=7 && stoi(a[1].substr(0,4))==yr && stoi(a[1].substr(5,2))==mo)
            dset.insert(a[1]);
    }
    if (dset.empty()) { cout<<"  [INFO] No data for "<<subName<<" in "<<monthName(mo)<<" "<<yr<<".\n"; return; }
    vector<string> dates(dset.begin(), dset.end());

    string fname = "reports/monthly-"+sanitize(subName)+"-"+monthName(mo)+"-"+to_string(yr)
                   +"-"+sanitize(scopeID)+".csv";
    ofstream f(fname.c_str(), ios::out | ios::trunc);
    if (!f.is_open()) { cout<<"  [ERROR] Cannot create "<<fname<<"\n"; return; }

    f<<"Monthly Attendance - "<<subName<<" ("<<subCode<<")\n";
    f<<"Month:,"<<monthName(mo)<<" "<<yr<<"\n\n";
    f<<"Name,Student ID";
    for (auto &d:dates) f<<","<<d;
    f<<",Present,Absent,Total,P%\n";

    int cP=0,cA=0,cT=0;
    map<string,int> colP,colA;
    for(auto&d:dates){colP[d]=0;colA[d]=0;}

    for (auto &s : stu) {
        if (s.size()<3||s[2]!=scopeID) continue;
        map<string,char> sm;
        for (auto &a : att)
            if (a.size()>=5&&a[0]==s[1]&&a[3]==subCode&&a[4]==scopeID)
                sm[a[1]]=toupper((unsigned char)a[2][0]);
        int p=0,ab=0;
        f<<s[0]<<","<<s[1];
        for (auto &d:dates){
            char st=(sm.count(d)?sm[d]:'-');
            f<<","<<st;
            if(st=='P'){p++;colP[d]++;}
            if(st=='A'){ab++;colA[d]++;}
        }
        int t=p+ab;
        f<<","<<p<<","<<ab<<","<<t<<","<<fixed<<setprecision(1)<<(t>0?p*100.0/t:0.0)<<"\n";
        cP+=p;cA+=ab;cT+=t;
    }
    f<<"CLASS TOTAL,,";
    for(auto&d:dates) f<<"P:"<<colP[d]<<"/A:"<<colA[d]<<",";
    f<<cP<<","<<cA<<","<<cT<<","<<fixed<<setprecision(1)<<(cT>0?cP*100.0/cT:0.0)<<"\n";
    f.close();
    cout<<"  [CSV exported] "<<fname<<"\n";

    // -- Terminal display ------------------------------------------
    cout << "\n" << string(110,'=') << "\n";
    cout << BOLD << FG_BCYAN << "  Monthly Attendance  |  " << subName
         << " (" << subCode << ")  |  " << monthName(mo) << " " << yr << RESET << "\n";
    cout << string(110,'-') << "\n";

    // Header row: Name | StudentID | date1 date2 ... | P | A | Tot | P%
    cout << BOLD << left << setw(22) << "  Name" << setw(28) << "Student ID";
    for (auto &d : dates) cout << setw(12) << d.substr(5); // MM-DD only to save space
    cout << setw(7)<<"P" << setw(7)<<"A" << setw(7)<<"Tot" << "P%" << RESET << "\n";
    cout << string(110,'-') << "\n";

    // Re-iterate students for terminal output
    for (auto &s : stu) {
        if (s.size()<3||s[2]!=scopeID) continue;
        map<string,char> sm2;
        for (auto &a : att)
            if (a.size()>=5&&a[0]==s[1]&&a[3]==subCode&&a[4]==scopeID)
                sm2[a[1]]=toupper((unsigned char)a[2][0]);
        int p=0,ab=0;
        cout << "  " << left << setw(20) << s[0] << setw(28) << s[1];
        for (auto &d : dates) {
            char st=(sm2.count(d)?sm2[d]:'-');
            if      (st=='P') cout << BOLD << FG_BGREEN  << setw(12) << st << RESET;
            else if (st=='A') cout << BOLD << FG_BRED    << setw(12) << st << RESET;
            else              cout << DIM  << FG_WHITE   << setw(12) << st << RESET;
            if(st=='P') p++; else if(st=='A') ab++;
        }
        int t=p+ab;
        double pct=(t>0)?p*100.0/t:0.0;
        string eligColor=(pct>=75.0)?FG_BGREEN:FG_BRED;
        cout << setw(7)<<p << setw(7)<<ab << setw(7)<<t
             << BOLD << eligColor << fixed<<setprecision(1)<<pct<<"%" << RESET << "\n";
    }
    cout << string(110,'-') << "\n";
    // Class totals row
    cout << BOLD << "  " << left << setw(20) << "CLASS TOTAL" << setw(28) << "";
    for (auto &d : dates) {
        string cell = "P:"+to_string(colP[d])+"/A:"+to_string(colA[d]);
        cout << setw(12) << cell;
    }
    double cpct=(cT>0)?cP*100.0/cT:0.0;
    cout << setw(7)<<cP << setw(7)<<cA << setw(7)<<cT
         << fixed<<setprecision(1)<<cpct<<"%" << RESET << "\n";
    cout << string(110,'=') << "\n";
}

// Monthly report for ALL subjects in a section  -  terminal table + CSV
void reportAllSubjects(const string &scopeID, int yr, int mo) {
    ensureReports();
    auto subs = readRows("subjects.dat");
    auto stu  = readRows("students.dat");
    auto att  = readRows("attendance.dat");

    string fname="reports/all-subjects-"+sanitize(scopeID)+"-"+monthName(mo)+"-"+to_string(yr)+".csv";
    ofstream f(fname.c_str(), ios::out | ios::trunc);
    if (!f.is_open()) { cout<<"  [ERROR] Cannot create "<<fname<<"\n"; return; }
    f<<"All Subjects Report - "<<scopeID<<"\nMonth:,"<<monthName(mo)<<" "<<yr<<"\n\n";

    bool any=false;
    for (auto &sub : subs) {
        if (sub.size()<3||sub[2]!=scopeID) continue;
        set<string> dset;
        for (auto &a:att){
            if(a.size()<5||a[3]!=sub[1]||a[4]!=scopeID) continue;
            if(a[1].size()>=7&&stoi(a[1].substr(0,4))==yr&&stoi(a[1].substr(5,2))==mo)
                dset.insert(a[1]);
        }
        if(dset.empty()) continue;
        any=true;
        vector<string> dates(dset.begin(),dset.end());

        // -- CSV section --
        f<<"Subject: "<<sub[0]<<" ("<<sub[1]<<")\n";
        f<<"Name,Student ID";
        for(auto&d:dates) f<<","<<d;
        f<<",Present,Absent,Total,P%\n";
        int cP=0,cA=0,cT=0;
        map<string,int> colP,colA;
        for(auto&d:dates){colP[d]=0;colA[d]=0;}
        for(auto&s:stu){
            if(s.size()<3||s[2]!=scopeID) continue;
            map<string,char> sm;
            for(auto&a:att)
                if(a.size()>=5&&a[0]==s[1]&&a[3]==sub[1]&&a[4]==scopeID)
                    sm[a[1]]=toupper((unsigned char)a[2][0]);
            int p=0,ab=0;
            f<<s[0]<<","<<s[1];
            for(auto&d:dates){char st=(sm.count(d)?sm[d]:'-');f<<","<<st;
                if(st=='P'){p++;colP[d]++;}if(st=='A'){ab++;colA[d]++;}}
            int t=p+ab;
            f<<","<<p<<","<<ab<<","<<t<<","<<fixed<<setprecision(1)<<(t>0?p*100.0/t:0.0)<<"\n";
            cP+=p;cA+=ab;cT+=t;
        }
        f<<"CLASS TOTAL,,";
        for(auto&d:dates) f<<"P:"<<colP[d]<<"/A:"<<colA[d]<<",";
        f<<cP<<","<<cA<<","<<cT<<","<<fixed<<setprecision(1)<<(cT>0?cP*100.0/cT:0.0)<<"\n";
        f<<string(60,'-')<<"\n\n";

        // -- Terminal table for this subject --
        cout << "\n" << string(110,'=') << "\n";
        cout << BOLD << FG_BCYAN << "  Subject: " << sub[0]
             << " (" << sub[1] << ")  |  " << monthName(mo) << " " << yr << RESET << "\n";
        cout << string(110,'-') << "\n";
        cout << BOLD << left << setw(22) << "  Name" << setw(28) << "Student ID";
        for (auto &d : dates) cout << setw(12) << d.substr(5);
        cout << setw(7)<<"P" << setw(7)<<"A" << setw(7)<<"Tot" << "P%" << RESET << "\n";
        cout << string(110,'-') << "\n";
        for(auto&s:stu){
            if(s.size()<3||s[2]!=scopeID) continue;
            map<string,char> sm2;
            for(auto&a:att)
                if(a.size()>=5&&a[0]==s[1]&&a[3]==sub[1]&&a[4]==scopeID)
                    sm2[a[1]]=toupper((unsigned char)a[2][0]);
            int p=0,ab=0;
            cout << "  " << left << setw(20) << s[0] << setw(28) << s[1];
            for(auto&d:dates){
                char st=(sm2.count(d)?sm2[d]:'-');
                if(st=='P') cout<<BOLD<<FG_BGREEN<<setw(12)<<st<<RESET;
                else if(st=='A') cout<<BOLD<<FG_BRED<<setw(12)<<st<<RESET;
                else cout<<DIM<<FG_WHITE<<setw(12)<<st<<RESET;
                if(st=='P')p++; else if(st=='A')ab++;
            }
            int t=p+ab; double pct=(t>0)?p*100.0/t:0.0;
            cout<<setw(7)<<p<<setw(7)<<ab<<setw(7)<<t
                <<BOLD<<(pct>=75.0?FG_BGREEN:FG_BRED)
                <<fixed<<setprecision(1)<<pct<<"%"<<RESET<<"\n";
        }
        cout << string(110,'-') << "\n";
        cout << BOLD << "  " << left << setw(20) << "CLASS TOTAL" << setw(28) << "";
        for(auto&d:dates){string cell="P:"+to_string(colP[d])+"/A:"+to_string(colA[d]);cout<<setw(12)<<cell;}
        double cp=(cT>0)?cP*100.0/cT:0.0;
        cout<<setw(7)<<cP<<setw(7)<<cA<<setw(7)<<cT<<fixed<<setprecision(1)<<cp<<"%"<<RESET<<"\n";
        cout << string(110,'=') << "\n";
    }
    if(!any) { f<<"No data found for "<<monthName(mo)<<" "<<yr<<".\n"; cout<<"  [INFO] No subject data found.\n"; }
    f.close();
    cout<<"  [CSV exported] "<<fname<<"\n";
}

// Individual student monthly report
void reportStudent(const string &stuID, const string &stuName,
                   const string &scopeID, int yr, int mo) {
    ensureReports();
    auto subs=readRows("subjects.dat");
    auto att=readRows("attendance.dat");

    vector<vector<string>> mySubs;
    for(auto&r:subs) if(r.size()>=3&&r[2]==scopeID) mySubs.push_back(r);
    if(mySubs.empty()){cout<<"  [INFO] No subjects in section.\n";return;}

    set<string> dset;
    for(auto&a:att){
        if(a.size()<5||a[0]!=stuID||a[4]!=scopeID) continue;
        if(a[1].size()>=7&&stoi(a[1].substr(0,4))==yr&&stoi(a[1].substr(5,2))==mo)
            dset.insert(a[1]);
    }
    if(dset.empty()){cout<<"  [INFO] No data for "<<stuName<<" in "<<monthName(mo)<<" "<<yr<<".\n";return;}
    vector<string> dates(dset.begin(),dset.end());

    map<string,map<string,char>> lk;
    for(auto&a:att){
        if(a.size()<5||a[0]!=stuID||a[4]!=scopeID) continue;
        if(a[1].size()>=7&&stoi(a[1].substr(0,4))==yr&&stoi(a[1].substr(5,2))==mo)
            lk[a[3]][a[1]]=toupper((unsigned char)a[2][0]);
    }

    string fname="reports/student-"+sanitize(stuID)+"-"+monthName(mo)+"-"+to_string(yr)+".csv";
    ofstream f(fname.c_str(), ios::out | ios::trunc);
    if(!f.is_open()){cout<<"  [ERROR] Cannot create "<<fname<<"\n";return;}
    f<<"Student: "<<stuName<<","<<stuID<<"\nMonth:,"<<monthName(mo)<<" "<<yr<<"\n\n";
    f<<"Date";
    for(auto&s:mySubs) f<<","<<s[0];
    f<<"\n";
    for(auto&d:dates){
        f<<d;
        for(auto&s:mySubs){char st='-';if(lk.count(s[1])&&lk[s[1]].count(d))st=lk[s[1]][d];f<<","<<st;}
        f<<"\n";
    }
    f<<"\nSummary";
    for(auto&s:mySubs)f<<","<<s[0];
    f<<"\n";
    auto cntSt=[&](const string&sc,char t)->int{
        int n=0; if(!lk.count(sc))return 0;
        for(auto&kv:lk.at(sc)) if(kv.second==t) n++;
        return n;
    };
    auto cntTot=[&](const string&sc)->int{
        int n=0; if(!lk.count(sc))return 0;
        for(auto&kv:lk.at(sc)) if(kv.second=='P'||kv.second=='A') n++;
        return n;
    };
    f<<"Present"; for(auto&s:mySubs)f<<","<<cntSt(s[1],'P'); f<<"\n";
    f<<"Absent";  for(auto&s:mySubs)f<<","<<cntSt(s[1],'A'); f<<"\n";
    f<<"Total";   for(auto&s:mySubs)f<<","<<cntTot(s[1]);    f<<"\n";
    f<<"P%";
    for(auto&s:mySubs){int p=cntSt(s[1],'P'),t=cntTot(s[1]);
        f<<","<<fixed<<setprecision(1)<<(t>0?p*100.0/t:0.0);}
    f<<"\n";
    f.close();
    cout<<"  [CSV exported] "<<fname<<"\n";

    // Terminal preview
    cout<<"\n  Student: "<<stuName<<"  |  ID: "<<stuID<<"\n";
    cout<<"  Section: "<<scopeID<<"\n";
    cout<<"  Month: "<<monthName(mo)<<" "<<yr<<"\n";
    cout<<"  "<<string(62,'-')<<"\n";
    cout<<"  "<<left<<setw(30)<<"Subject"<<setw(8)<<"P"<<setw(8)<<"A"<<setw(8)<<"Total"<<"P%\n";
    cout<<"  "<<string(62,'-')<<"\n";
    for(auto&s:mySubs){
        int p=cntSt(s[1],'P'),a=cntSt(s[1],'A'),t=cntTot(s[1]);
        cout<<"  "<<left<<setw(30)<<s[0]<<setw(8)<<p<<setw(8)<<a<<setw(8)<<t
            <<fixed<<setprecision(1)<<(t>0?p*100.0/t:0.0)<<"%\n";
    }
    cout<<"  "<<string(62,'-')<<"\n";
}

// --------------------------------------------------------------
//  FORWARD DECLARATIONS
// --------------------------------------------------------------
void phase1(); void phase2(); void phase3(); void phase3b(); void phase4();

// --------------------------------------------------------------
//  BULK SEMESTER PROMOTION
//  Copies all students from source semester (all its sections) into
//  target semester sections, auto-generating new IDs.
//  attendance.dat is NEVER touched.
// --------------------------------------------------------------
void bulkPromote() {
    cls();
    cout << "=== Bulk Semester Promotion ===\n\n";
    cout << "  Department: " << gDeptName << "  (" << gDeptCode << ")\n\n";

    // List semesters in this department
    auto semRows = readRows("semesters.dat");
    vector<vector<string>> deptSems;
    for (auto &r : semRows)
        if (r.size() >= 5 && r[4] == gDeptCode) deptSems.push_back(r);

    if (deptSems.size() < 2) {
        cout << "  [ERROR] Need at least 2 semesters in this department to promote.\n";
        waitKey(); return;
    }

    cout << "  Available semesters:\n";
    for (size_t i = 0; i < deptSems.size(); i++) {
        cout << "    [" << i+1 << "] " << deptSems[i][3]
             << "  (" << deptSems[i][0];
        if (deptSems[i].size() >= 6 && !deptSems[i][5].empty())
            cout << " | Intake: " << deptSems[i][5];
        cout << " | Batch: " << deptSems[i][1] << ")\n";
    }

    cout << "\n  Select SOURCE semester number: ";
    int srcIdx = menuChoice() - 1;
    if (srcIdx < 0 || srcIdx >= (int)deptSems.size()) {
        cout << "  [ERROR] Invalid selection.\n"; waitKey(); return;
    }

    cout << "  Select TARGET semester number: ";
    int tgtIdx = menuChoice() - 1;
    if (tgtIdx < 0 || tgtIdx >= (int)deptSems.size()) {
        cout << "  [ERROR] Invalid selection.\n"; waitKey(); return;
    }
    if (srcIdx == tgtIdx) {
        cout << "  [ERROR] Source and target must be different.\n"; waitKey(); return;
    }

    string srcSemID = deptSems[srcIdx][3];
    string tgtSemID = deptSems[tgtIdx][3];

    cout << "\n  Source : " << srcSemID << "\n";
    cout << "  Target : " << tgtSemID << "\n";

    // Gather source sections
    auto secRows = readRows("sections.dat");
    vector<vector<string>> srcSections;
    for (auto &r : secRows)
        if (r.size() >= 3 && r[2] == srcSemID) srcSections.push_back(r);

    if (srcSections.empty()) {
        cout << "\n  [ERROR] No sections found in source semester '" << srcSemID << "'.\n";
        waitKey(); return;
    }

    // Gather target sections
    vector<vector<string>> tgtSections;
    for (auto &r : secRows)
        if (r.size() >= 3 && r[2] == tgtSemID) tgtSections.push_back(r);

    if (tgtSections.empty()) {
        cout << "\n  [ERROR] No sections found in target semester '" << tgtSemID << "'.\n";
        cout << "  Please create at least one section in the target semester first.\n";
        waitKey(); return;
    }

    // Load all students once
    auto allStudents = readRows("students.dat");

    // Count source students
    int srcCount = 0;
    for (auto &s : allStudents) {
        if (s.size() < 3) continue;
        // Find if this student's section belongs to srcSemID
        for (auto &sec : srcSections)
            if (sec.size() >= 2 && sec[1] == s[2]) { srcCount++; break; }
    }

    if (srcCount == 0) {
        cout << "\n  [ERROR] No students found in any section of '" << srcSemID << "'.\n";
        waitKey(); return;
    }

    // Map each source section -> target section by name matching or ask user
    // Strategy: for each source section, try to find target section with same name;
    // if ambiguous or no match, ask the admin to map manually.
    cout << "\n  Mapping source sections to target sections:\n";
    cout << "  (Enter target Section ID for each source section)\n\n";

    // Build map: srcSectionID -> tgtSectionID
    map<string, string> sectionMap; // srcSecID -> tgtSecID

    for (auto &srcSec : srcSections) {
        cout << "  Source section: " << srcSec[1] << "  (" << srcSec[0] << ")\n";
        cout << "  Target sections available:\n";
        for (auto &ts : tgtSections)
            cout << "    " << ts[1] << "  (" << ts[0] << ")\n";

        string tgtSecID; bool ok = false;
        while (!ok) {
            ok = readLine("  Map to target Section ID (or SKIP to skip this section): ",
                          tgtSecID, 80, true);
            if (!ok) continue;
            tgtSecID = trim(tgtSecID);
            if (toLower(tgtSecID) == "skip") { ok = true; tgtSecID = ""; break; }
            bool found = false;
            for (auto &ts : tgtSections)
                if (ts[1] == tgtSecID) { found = true; break; }
            if (!found) { cout << "  [ERROR] Section ID not found in target semester.\n"; ok = false; }
        }
        if (!tgtSecID.empty())
            sectionMap[srcSec[1]] = tgtSecID;
        cout << "\n";
    }

    if (sectionMap.empty()) {
        cout << "  No section mappings selected. Promotion cancelled.\n";
        waitKey(); return;
    }

    // Preview
    cout << "  Promotion preview:\n";
    cout << "  " << string(60, '-') << "\n";
    int totalToMove = 0;
    for (auto &kv : sectionMap) {
        int cnt = 0;
        for (auto &s : allStudents)
            if (s.size() >= 3 && s[2] == kv.first) cnt++;
        cout << "  " << kv.first << " -> " << kv.second
             << "  (" << cnt << " student(s))\n";
        totalToMove += cnt;
    }
    cout << "  " << string(60, '-') << "\n";
    cout << "  Total students to promote: " << totalToMove << "\n\n";

    char conf = 0;
    while (conf != 'Y' && conf != 'N') {
        cout << "  Confirm promotion? (Y/N): ";
        string ans; getline(cin, ans); ans = trim(ans);
        if (ans.size() == 1) conf = toupper((unsigned char)ans[0]);
    }
    if (conf != 'Y') { cout << "  Cancelled.\n"; waitKey(); return; }

    // Perform promotion: for each mapped section, copy students with new IDs
    // Reload students fresh before writing
    auto stuRows = readRows("students.dat");
    vector<vector<string>> newEntries;

    for (auto &kv : sectionMap) {
        const string &srcSecID = kv.first;
        const string &tgtSecID = kv.second;

        // Build set of already-used serials in tgtSecID (including any we're adding this pass)
        set<int> tgtUsed;
        auto collectUsed = [&]() {
            tgtUsed.clear();
            for (auto &s : stuRows) {
                if (s.size() < 3 || trim(s[2]) != tgtSecID) continue;
                const string &sid = trim(s[1]);
                size_t sp = sid.rfind("-S");
                if (sp == string::npos) continue;
                string num = sid.substr(sp + 2);
                bool allD = !num.empty();
                for (char c : num) if (!isdigit((unsigned char)c)) { allD = false; break; }
                if (allD) tgtUsed.insert(stoi(num));
            }
            // Also count from newEntries already queued for this tgtSecID
            for (auto &ne : newEntries) {
                if (ne.size() < 3 || trim(ne[2]) != tgtSecID) continue;
                const string &sid = trim(ne[1]);
                size_t sp = sid.rfind("-S");
                if (sp == string::npos) continue;
                string num = sid.substr(sp + 2);
                bool allD = !num.empty();
                for (char c : num) if (!isdigit((unsigned char)c)) { allD = false; break; }
                if (allD) tgtUsed.insert(stoi(num));
            }
        };

        for (auto &s : stuRows) {
            if (s.size() < 3 || trim(s[2]) != srcSecID) continue;
            collectUsed();
            int nextSerial = 1;
            while (tgtUsed.count(nextSerial)) ++nextSerial;
            string newID = tgtSecID + "-S" + to_string(nextSerial);
            tgtUsed.insert(nextSerial);
            newEntries.push_back({trim(s[0]), newID, tgtSecID});
        }
    }

    // Append new entries
    for (auto &ne : newEntries) stuRows.push_back(ne);
    bool writeOK = writeRows("students.dat", "Name,StudentID,SectionID", stuRows);
    if (!writeOK) {
        cout << "\n  [ERROR] Failed to write promoted students. Please try again.\n";
        waitKey(); return;
    }

    cout << "\n  Promotion complete! " << (int)newEntries.size()
         << " student record(s) created in target semester.\n";
    cout << "  Attendance history in '" << srcSemID
         << "' is preserved and untouched.\n\n";

    // Archive option: delete source semester students?
    char delOld = 0;
    while (delOld != 'Y' && delOld != 'N') {
        cout << "  Delete student records from the old semester '" << srcSemID
             << "'? (Y/N): ";
        string ans; getline(cin, ans); ans = trim(ans);
        if (ans.size() == 1) delOld = toupper((unsigned char)ans[0]);
    }
    if (delOld == 'Y') {
        // Build set of all source section IDs in this promotion
        set<string> srcSecIDs;
        for (auto &kv : sectionMap) srcSecIDs.insert(kv.first);

        auto fresh = readRows("students.dat");
        vector<vector<string>> kept;
        int removed = 0;
        for (auto &s : fresh) {
            if (s.size() >= 3 && srcSecIDs.count(trim(s[2]))) {
                removed++; // skip = delete
            } else {
                kept.push_back(s);
            }
        }
        writeRows("students.dat", "Name,StudentID,SectionID", kept);
        cout << "  " << removed << " old student record(s) deleted from source semester.\n";
        cout << "  NOTE: Attendance records in '" << srcSemID
             << "' are PRESERVED for audit purposes.\n";
    } else {
        cout << "  Old records kept. Students now exist in both semesters.\n";
    }

    waitKey();
}

// --------------------------------------------------------------
//  PHASE 4   -  Classroom Features (inside a Section)
//  gSectionID is the active scope key for all data operations.
// --------------------------------------------------------------
void phase4() {
    while (true) {
        cls();
        cout << "\n  " << BOLD << FG_BCYAN << "+" << string(72,'-') << "+" << RESET << "\n";
        cout << "  " << BOLD << FG_BCYAN << "| " << RESET
             << "Welcome " << BOLD << FG_BWHITE << gAdminName << RESET
             << " | " << FG_BYELLOW << gDeptName
             << " | " << gSemName << " | Batch " << gBatch
             << " | " << FG_BMAGENTA << gSectionName << RESET << "\n";
        cout << "  " << BOLD << FG_BCYAN << "+" << string(72,'-') << "+" << RESET << "\n\n";
        cout<<"  "<<FG_BGREEN  <<"1."<<RESET<<"  Create Student\n";
        cout<<"  "<<FG_BYELLOW <<"2."<<RESET<<"  Manage Subjects\n";
        cout<<"  "<<FG_BCYAN   <<"3."<<RESET<<"  Mark Attendance\n";
        cout<<"  "<<FG_BMAGENTA<<"4."<<RESET<<"  View / Export Report\n";
        cout<<"  "<<FG_BBLUE   <<"5."<<RESET<<"  Individual Student Report\n";
        cout<<"  "<<FG_BYELLOW <<"6."<<RESET<<"  Edit Attendance\n";
        cout<<"  "<<FG_BYELLOW <<"7."<<RESET<<"  Edit Student Name\n";
        cout<<"  "<<FG_BRED    <<"8."<<RESET<<"  Delete Student\n";
        cout<<"  "<<FG_BYELLOW <<"9."<<RESET<<"  Transfer Student to Another Section\n";
        cout<<"  "<<FG_BRED    <<"0."<<RESET<<"  Back\n\n";
        cout<<"  "<<BOLD<<FG_BWHITE<<"Choice: "<<RESET;
        int ch = menuChoice();

        // -- 1. CREATE STUDENT --------------------------------------
        if (ch == 1) {
            cls();
            cout << "=== Create Student ===\n\n";

            // Require at least one subject first
            {
                auto sr = readRows("subjects.dat");
                bool hasSub = false;
                for (auto &r : sr) if (r.size()>=3&&r[2]==gSectionID) { hasSub=true; break; }
                if (!hasSub) {
                    cout<<"  [ERROR] No subjects yet. Add subjects (option 2) first.\n";
                    waitKey(); continue;
                }
            }

            string name;
            bool ok=false;
            while (!ok) ok=readLine("Student Name (letters & spaces): ",name,60,true,true);

            auto rows = readRows("students.dat");

            // Check for duplicate / similar names in THIS section
            string lname = toLower(trim(name));
            vector<vector<string>> sameNames;
            for (auto &r : rows) {
                if (r.size()<3||r[2]!=gSectionID) continue;
                string rl = toLower(trim(r[0]));
                bool match = (rl == lname);
                if (!match && rl.size() > lname.size() && rl.substr(0,lname.size())==lname
                    && rl[lname.size()]==' ')
                    match = true;
                if (match) sameNames.push_back(r);
            }

            static const char* ROMANS[] = {"","II","III","IV","V","VI","VII","VIII","IX","X"};
            string finalName = trim(name);

            if (!sameNames.empty()) {
                cout<<"\n  WARNING: Similar name(s) already exist in this section:\n";
                for (auto &e : sameNames)
                    cout<<"    "<<e[0]<<"  |  ID: "<<e[1]<<"\n";
                char c=0;
                while (c!='Y'&&c!='N') {
                    cout<<"  Different person with same name? (Y/N): ";
                    string ans; getline(cin,ans); ans=trim(ans);
                    if (ans.size()==1) c=toupper((unsigned char)ans[0]);
                }
                if (c=='N') { cout<<"  Cancelled.\n"; waitKey(); continue; }
                int cnt=(int)sameNames.size();
                string suffix=(cnt<10)?ROMANS[cnt]:to_string(cnt+1);
                finalName = trim(name)+" "+suffix;
                cout<<"  Adding as: \""<<finalName<<"\"\n";
            }

            // Re-read AGAIN after user interaction
            rows = readRows("students.dat");

            // Use a SET of used serials  -  guarantees no duplicates even with gaps
            set<int> usedSerials;
            for (auto &r : rows) {
                if (r.size()<3 || trim(r[2])!=trim(gSectionID)) continue;
                const string &sid = trim(r[1]);
                size_t sp = sid.rfind("-S");
                if (sp==string::npos) continue;
                string num = sid.substr(sp+2);
                bool allDig = !num.empty();
                for (char c:num) if(!isdigit((unsigned char)c)){allDig=false;break;}
                if (allDig) usedSerials.insert(stoi(num));
            }
            int nextSerial = 1;
            while (usedSerials.count(nextSerial)) ++nextSerial;
            string stuID = gSectionID + "-S" + to_string(nextSerial);

            rows.push_back({finalName, stuID, gSectionID});
            bool writeOK = writeRows("students.dat","Name,StudentID,SectionID",rows);
            if (!writeOK) {
                cout<<"\n  [ERROR] Failed to save student data. Please try again.\n";
                waitKey(); continue;
            }

            // Verify the save by re-reading
            {
                auto verify = readRows("students.dat");
                bool saved = false;
                for (auto &v : verify)
                    if (v.size()>=2 && trim(v[1])==stuID) { saved=true; break; }
                if (!saved) {
                    cout<<"\n  [ERROR] Failed to save student data. Please try again.\n";
                    waitKey(); continue;
                }
            }

            cout<<"\n  Student Created!\n"
                <<"  Name : "<<finalName<<"\n"
                <<"  ID   : "<<stuID<<"\n";
            waitKey();
        }

        // -- 2. MANAGE SUBJECTS -------------------------------------
        else if (ch == 2) {
            while (true) {
                cls();
                cout<<"=== Subjects for "<<gSectionID<<" ===\n\n";
                auto sr=readRows("subjects.dat");
                bool any=false;
                for(auto&r:sr) if(r.size()>=3&&r[2]==gSectionID){
                    cout<<"  "<<r[0]<<"  ->  "<<r[1]<<"\n"; any=true;
                }
                if(!any) cout<<"  (No subjects yet)\n";
                cout<<"\n  1. Add Subject\n  2. Delete Subject\n  3. Back\n  Choice: ";
                int sc=menuChoice();
                if(sc==3) break;

                if(sc==1){
                    string sName,sCode; bool ok=false;
                    while(!ok) ok=readLine("Subject Full Name: ",sName,60,true);
                    ok=false;
                    while(!ok){
                        ok=readLine("Short Code (no spaces, e.g. DSA): ",sCode,10,true);
                        if(ok&&sCode.find(' ')!=string::npos){cout<<"  [ERROR] No spaces.\n";ok=false;}
                    }
                    transform(sCode.begin(),sCode.end(),sCode.begin(),::toupper);
                    string subCode=gSectionID+"-"+sCode;
                    sr=readRows("subjects.dat");
                    bool exists=false;
                    for(auto&r:sr) if(r.size()>=2&&r[1]==subCode){exists=true;break;}
                    if(exists){cout<<"  [ERROR] Code already exists.\n";waitKey();continue;}
                    sr.push_back({sName,subCode,gSectionID});
                    writeRows("subjects.dat","SubjectName,SubjectCode,SectionID",sr);
                    cout<<"  Subject Added: "<<sName<<" ("<<subCode<<")\n";
                    char more=0;
                    while(more!='Y'&&more!='N'){
                        cout<<"  Add another? (Y/N): ";
                        string ans;getline(cin,ans);ans=trim(ans);
                        if(ans.size()==1)more=toupper((unsigned char)ans[0]);
                    }
                    if(more=='N') break;
                }
                else if(sc==2){
                    if(!any){cout<<"  No subjects to delete.\n";waitKey();continue;}
                    string delCode; bool ok=false;
                    while(!ok) ok=readLine("Enter Subject Code to DELETE: ",delCode,60,true);
                    delCode=trim(delCode);
                    bool found=false;
                    for(auto&r:sr) if(r.size()>=2&&trim(r[1])==delCode&&r[2]==gSectionID){found=true;break;}
                    if(!found){cout<<"  [ERROR] Subject code not found in this section.\n";waitKey();continue;}
                    char conf=0;
                    while(conf!='Y'&&conf!='N'){
                        cout<<"  Delete '"<<delCode<<"' and ALL its attendance records? (Y/N): ";
                        string ans;getline(cin,ans);ans=trim(ans);
                        if(ans.size()==1)conf=toupper((unsigned char)ans[0]);
                    }
                    if(conf!='Y'){cout<<"  Cancelled.\n";waitKey();continue;}
                    vector<vector<string>> newSubs;
                    for(auto&r:sr) if(!(r.size()>=2&&trim(r[1])==delCode&&r[2]==gSectionID)) newSubs.push_back(r);
                    writeRows("subjects.dat","SubjectName,SubjectCode,SectionID",newSubs);
                    auto att=readRows("attendance.dat");
                    vector<vector<string>> newAtt;
                    for(auto&a:att) if(!(a.size()>=5&&trim(a[3])==delCode&&a[4]==gSectionID)) newAtt.push_back(a);
                    writeRows("attendance.dat","StudentID,Date,Status,SubjectCode,SectionID",newAtt);
                    cout<<"  Subject and its attendance records deleted.\n";
                    waitKey(); break;
                }
                else{cout<<"  Invalid.\n";waitKey();}
            }
        }

        // -- 3. MARK ATTENDANCE ------------------------------------
        else if (ch == 3) {
            cls();
            cout<<"=== Mark Attendance ===\n\n";

            auto subs=readRows("subjects.dat");
            vector<vector<string>> semSubs;
            for(auto&r:subs) if(r.size()>=3&&r[2]==gSectionID) semSubs.push_back(r);
            if(semSubs.empty()){cout<<"  No subjects. Add subjects first.\n";waitKey();continue;}

            string date;
            if(!readDate(date)){cout<<"  Cancelled.\n";waitKey();continue;}

            auto students=readRows("students.dat");
            vector<vector<string>> semStudents;
            for(auto&s:students) if(s.size()>=3&&s[2]==gSectionID) semStudents.push_back(s);
            if(semStudents.empty()){cout<<"  No students found. Create students first.\n";waitKey();continue;}

            auto attDisk = readRows("attendance.dat");
            vector<vector<string>> attSession = attDisk;
            vector<vector<string>> newRecs;

            while (true) {
                vector<vector<string>> pending;
                for (auto &sub : semSubs) {
                    bool marked=false;
                    for (auto &a:attSession)
                        if(a.size()>=5&&a[1]==date&&a[3]==sub[1]&&a[4]==gSectionID){marked=true;break;}
                    if(!marked) pending.push_back(sub);
                }

                if(pending.empty()){
                    cout<<"\n  All subjects marked for "<<date<<". Session complete!\n";
                    break;
                }

                cout<<"\n  Subjects not yet marked for "<<date<<":\n";
                for(size_t i=0;i<pending.size();i++)
                    cout<<"    ["<<i+1<<"] "<<pending[i][0]<<"  ("<<pending[i][1]<<")\n";
                cout<<"    [0] Stop session\n\n  Enter subject number: ";
                int idx=menuChoice()-1;

                if(idx==-1){
                    if(!pending.empty()){
                        cout<<"\n  WARNING: These subjects not yet marked:\n";
                        for(auto&p:pending) cout<<"    - "<<p[0]<<" ("<<p[1]<<")\n";
                        char conf=0;
                        while(conf!='Y'&&conf!='N'){
                            cout<<"  End session anyway? (Y/N): ";
                            string ans;getline(cin,ans);ans=trim(ans);
                            if(ans.size()==1)conf=toupper((unsigned char)ans[0]);
                        }
                        if(conf!='Y') continue;
                    }
                    break;
                }

                if(idx<0||idx>=(int)pending.size()){cout<<"  [ERROR] Invalid number.\n";continue;}

                auto &sub=pending[idx];
                cout<<"\n  Marking: "<<sub[0]<<" on "<<date<<"\n";
                cout<<"  (P = Present, A = Absent)\n\n";

                for(auto&s:semStudents){
                    string lbl=s[0]+" ("+s[1]+")";
                    char st=readPA(lbl);
                    vector<string> rec={s[1],date,string(1,st),sub[1],gSectionID};
                    attSession.push_back(rec);
                    newRecs.push_back(rec);
                }
                cout<<"\n  Attendance for "<<sub[0]<<" recorded.\n";

                int rem=0;
                for(auto&s2:semSubs){
                    bool done=false;
                    for(auto&a:attSession)
                        if(a.size()>=5&&a[1]==date&&a[3]==s2[1]&&a[4]==gSectionID){done=true;break;}
                    if(!done)rem++;
                }
                if(rem==0){cout<<"  All subjects for "<<date<<" are now marked!\n";break;}

                char more=0;
                while(more!='Y'&&more!='N'){
                    cout<<"  Mark another subject? (Y/N): ";
                    string ans;getline(cin,ans);ans=trim(ans);
                    if(ans.size()==1)more=toupper((unsigned char)ans[0]);
                }
                if(more=='N'){
                    cout<<"  WARNING: "<<rem<<" subject(s) not yet marked.\n";
                    char conf=0;
                    while(conf!='Y'&&conf!='N'){
                        cout<<"  End session anyway? (Y/N): ";
                        string ans;getline(cin,ans);ans=trim(ans);
                        if(ans.size()==1)conf=toupper((unsigned char)ans[0]);
                    }
                    if(conf=='Y') break;
                }
            }

            // -- SAVE --
            if(!newRecs.empty()){
                auto disk2=readRows("attendance.dat");
                set<string> existing;
                for(auto&a:disk2)
                    if(a.size()>=5)
                        existing.insert(trim(a[0])+"|"+trim(a[1])+"|"+trim(a[3])+"|"+trim(a[4]));
                int added=0;
                for(auto&r:newRecs){
                    if(r.size()<5) continue;
                    string key=trim(r[0])+"|"+trim(r[1])+"|"+trim(r[3])+"|"+trim(r[4]);
                    if(!existing.count(key)){
                        disk2.push_back(r);
                        existing.insert(key);
                        added++;
                    }
                }
                if(added>0){
                    writeRows("attendance.dat","StudentID,Date,Status,SubjectCode,SectionID",disk2);
                    cout<<"\n  Attendance saved! ("<<added<<" record(s) written)\n";
                } else {
                    cout<<"\n  No new records to save (already exist).\n";
                }
            } else {
                cout<<"\n  No attendance recorded.\n";
            }
            waitKey();
        }

        // -- 4. VIEW / EXPORT REPORT -------------------------------
        else if (ch == 4) {
            cls();
            cout<<"=== Reports for "<<gSectionID<<" ===\n\n";
            cout<<"  1. Overall Summary (terminal + CSV)\n";
            cout<<"  2. Monthly Report - Single Subject (terminal + CSV)\n";
            cout<<"  3. Monthly Report - All Subjects (terminal + CSV)\n";
            cout<<"  4. Back\n  Choice: ";
            int rc=menuChoice();

            if(rc==1){
                string path="reports/overall-"+sanitize(gSectionID)+".csv";
                reportOverall(gSectionID,
                    gDeptName+" | "+gSemName+" | Batch "+gBatch+" | "+gSectionName, path);
                waitKey();
            }
            else if(rc==2||rc==3){
                string yrStr,moStr;
                bool ok=false;
                while(!ok) ok=readDigits("Year (e.g. 2024): ",yrStr,4,4);
                ok=false;
                while(!ok){
                    ok=readDigits("Month (1-12): ",moStr,1,2);
                    if(ok){int m=stoi(moStr);if(m<1||m>12){cout<<"  [ERROR] 1-12 only.\n";ok=false;}}
                }
                int yr=stoi(yrStr), mo=stoi(moStr);

                if(rc==3){
                    reportAllSubjects(gSectionID,yr,mo);
                    waitKey();
                } else {
                    auto sr=readRows("subjects.dat");
                    vector<vector<string>> semSubs;
                    for(auto&r:sr) if(r.size()>=3&&r[2]==gSectionID) semSubs.push_back(r);
                    if(semSubs.empty()){cout<<"  No subjects.\n";waitKey();continue;}
                    cout<<"\n  Subjects:\n";
                    for(size_t i=0;i<semSubs.size();i++)
                        cout<<"    ["<<i+1<<"] "<<semSubs[i][0]<<" ("<<semSubs[i][1]<<")\n";
                    cout<<"  Select: ";
                    int si=menuChoice()-1;
                    if(si<0||si>=(int)semSubs.size()){cout<<"  Invalid.\n";waitKey();continue;}
                    reportMonthlySubject(gSectionID,gBatch,gSectionID,
                                         semSubs[si][1],semSubs[si][0],yr,mo);
                    waitKey();
                }
            }
            else if(rc==4) continue;
            else{cout<<"  Invalid.\n";waitKey();}
        }

        // -- 5. INDIVIDUAL STUDENT REPORT --------------------------
        else if (ch == 5) {
            cls();
            auto stu=readRows("students.dat");
            bool any=false;
            cout<<"  Students:\n";
            for(auto&s:stu) if(s.size()>=3&&s[2]==gSectionID){cout<<"    "<<s[1]<<"  -  "<<s[0]<<"\n";any=true;}
            if(!any){cout<<"  No students.\n";waitKey();continue;}

            string stuID; bool ok=false;
            while(!ok) ok=readLine("\nEnter Student ID: ",stuID,80,true);
            string stuName="";
            for(auto&s:stu) if(s.size()>=2&&s[1]==stuID){stuName=s[0];break;}
            if(stuName.empty()){cout<<"  [ERROR] Student not found.\n";waitKey();continue;}

            string yrStr,moStr; ok=false;
            while(!ok) ok=readDigits("Year: ",yrStr,4,4);
            ok=false;
            while(!ok){
                ok=readDigits("Month (1-12): ",moStr,1,2);
                if(ok){int m=stoi(moStr);if(m<1||m>12){cout<<"  [ERROR] 1-12.\n";ok=false;}}
            }
            reportStudent(stuID,stuName,gSectionID,stoi(yrStr),stoi(moStr));
            waitKey();
        }

        // -- 6. EDIT ATTENDANCE ------------------------------------
        else if (ch == 6) {
            cls();
            cout<<"=== Edit Attendance ===\n\n";
            auto subs=readRows("subjects.dat");
            vector<vector<string>> semSubs;
            for(auto&r:subs) if(r.size()>=3&&r[2]==gSectionID) semSubs.push_back(r);
            if(semSubs.empty()){cout<<"  No subjects.\n";waitKey();continue;}

            cout<<"  Subjects:\n";
            for(size_t i=0;i<semSubs.size();i++)
                cout<<"    ["<<i+1<<"] "<<semSubs[i][0]<<" ("<<semSubs[i][1]<<")\n";
            cout<<"  Select subject: ";
            int si=menuChoice()-1;
            if(si<0||si>=(int)semSubs.size()){cout<<"  Invalid.\n";waitKey();continue;}
            string subCode=semSubs[si][1], subName=semSubs[si][0];

            auto attRows=readRows("attendance.dat");
            set<string> dateSet;
            for(auto&a:attRows) if(a.size()>=5&&a[3]==subCode&&a[4]==gSectionID) dateSet.insert(a[1]);
            if(dateSet.empty()){cout<<"  No records for "<<subName<<".\n";waitKey();continue;}

            cout<<"\n  Dates with records for "<<subName<<":\n";
            for(auto&d:dateSet) cout<<"    "<<d<<"\n";
            string editDate; bool ok=false;
            while(!ok) ok=readLine("\nEnter date (YYYY-MM-DD): ",editDate,10,true);
            if(!dateSet.count(editDate)){cout<<"  [ERROR] No records for that date.\n";waitKey();continue;}

            auto stuRows=readRows("students.dat");
            cout<<"\n  Current attendance for "<<subName<<" on "<<editDate<<":\n";
            for(auto&s:stuRows){
                if(s.size()<3||s[2]!=gSectionID) continue;
                char cur='-';
                for(auto&a:attRows)
                    if(a.size()>=5&&a[0]==s[1]&&a[1]==editDate&&a[3]==subCode&&a[4]==gSectionID)
                        cur=toupper((unsigned char)a[2][0]);
                cout<<"    "<<s[1]<<"  -  "<<s[0]<<"  ->  "<<cur<<"\n";
            }

            bool changed=false;
            while(true){
                string editID; ok=false;
                while(!ok) ok=readLine("\nEnter Student ID to edit (or DONE): ",editID,80,true);
                if(toLower(editID)=="done") break;
                bool found=false;
                for(auto&a:attRows){
                    if(a.size()>=5&&a[0]==editID&&a[1]==editDate&&a[3]==subCode&&a[4]==gSectionID){
                        char cur=toupper((unsigned char)a[2][0]);
                        char nw=(cur=='P'?'A':'P');
                        cout<<"  Current: "<<cur<<"  -> Change to "<<nw<<"? (Y/N): ";
                        string ans;getline(cin,ans);ans=trim(ans);
                        if(!ans.empty()&&toupper((unsigned char)ans[0])=='Y'){
                            a[2]=string(1,nw);
                            cout<<"  Changed to "<<nw<<".\n";
                            changed=true;
                        } else cout<<"  No change.\n";
                        found=true; break;
                    }
                }
                if(!found) cout<<"  [ERROR] No record for that student on this date/subject.\n";
            }

            if(changed){
                auto fresh=readRows("attendance.dat");
                for(auto&fr:fresh){
                    if(fr.size()<5) continue;
                    if(trim(fr[3])!=trim(subCode)||trim(fr[4])!=trim(gSectionID)||trim(fr[1])!=trim(editDate)) continue;
                    for(auto&ar:attRows)
                        if(ar.size()>=5&&trim(ar[0])==trim(fr[0])&&trim(ar[1])==trim(fr[1])
                           &&trim(ar[3])==trim(fr[3])&&trim(ar[4])==trim(fr[4]))
                            {fr[2]=ar[2];break;}
                }
                writeRows("attendance.dat","StudentID,Date,Status,SubjectCode,SectionID",fresh);
                cout<<"  Attendance updated.\n";
            } else cout<<"  No changes.\n";
            waitKey();
        }

        // -- 7. EDIT STUDENT NAME ----------------------------------
        else if (ch == 7) {
            cls();
            cout<<"=== Edit Student Name ===\n\n";
            auto stuRows = readRows("students.dat");
            bool any = false;
            cout<<"  Students in this section:\n";
            for (auto &s : stuRows)
                if (s.size()>=3 && trim(s[2])==gSectionID) {
                    cout<<"    "<<trim(s[1])<<"  -  "<<trim(s[0])<<"\n";
                    any = true;
                }
            if (!any) { cout<<"  No students.\n"; waitKey(); continue; }

            string editID; bool ok=false;
            while (!ok) ok=readLine("\nEnter Student ID to edit: ", editID, 80, true);
            editID = trim(editID);

            int foundIdx = -1;
            for (int i=0; i<(int)stuRows.size(); i++) {
                if (stuRows[i].size()>=2 && trim(stuRows[i][1])==editID) {
                    foundIdx = i; break;
                }
            }
            if (foundIdx < 0) {
                cout<<"  [ERROR] Student ID '"<<editID<<"' not found.\n";
                waitKey(); continue;
            }

            cout<<"  Current name: "<<trim(stuRows[foundIdx][0])<<"\n";
            string newName; ok=false;
            while (!ok) {
                cout<<"New name: ";
                string raw; if (!getline(cin, raw)) { cin.clear(); continue; }
                raw = trim(raw);
                if (raw.empty()) { cout<<"  [ERROR] Name cannot be empty.\n"; continue; }
                if (raw.find(',') != string::npos) { cout<<"  [ERROR] No commas.\n"; continue; }
                bool valid2 = true;
                for (unsigned char c : raw)
                    if (!isalpha(c) && c!=' ' && c!='.' && c!='-') { valid2=false; break; }
                if (!valid2) { cout<<"  [ERROR] Only letters, spaces, dots, hyphens.\n"; continue; }
                newName = raw; ok = true;
            }

            stuRows[foundIdx][0] = newName;
            writeRows("students.dat","Name,StudentID,SectionID", stuRows);
            cout<<"  Name updated: "<<newName<<"\n";
            waitKey();
        }

        // -- 8. DELETE STUDENT -------------------------------------
        else if (ch == 8) {
            cls();
            cout<<"=== Delete Student ===\n\n";
            auto stuRows = readRows("students.dat");
            bool any=false;
            cout<<"  Students:\n";
            for(auto&s:stuRows)
                if(s.size()>=3&&trim(s[2])==gSectionID){cout<<"    "<<trim(s[1])<<"  -  "<<trim(s[0])<<"\n";any=true;}
            if(!any){cout<<"  No students.\n";waitKey();continue;}

            string delID; bool ok=false;
            while(!ok) ok=readLine("\nStudent ID to DELETE: ",delID,80,true);
            delID=trim(delID);

            bool found=false;
            string delName="";
            for(auto&s:stuRows) if(trim(s[1])==delID&&trim(s[2])==gSectionID){found=true;delName=s[0];break;}
            if(!found){cout<<"  [ERROR] Not found.\n";waitKey();continue;}

            char conf=0;
            while(conf!='Y'&&conf!='N'){
                cout<<"  Delete '"<<delName<<"' ("<<delID<<") and ALL attendance records? (Y/N): ";
                string ans;getline(cin,ans);ans=trim(ans);
                if(ans.size()==1)conf=toupper((unsigned char)ans[0]);
            }
            if(conf!='Y'){cout<<"  Cancelled.\n";waitKey();continue;}

            vector<vector<string>> newStu;
            for(auto&s:stuRows) if(!(trim(s[1])==delID&&trim(s[2])==gSectionID)) newStu.push_back(s);
            writeRows("students.dat","Name,StudentID,SectionID",newStu);

            auto att=readRows("attendance.dat");
            vector<vector<string>> newAtt;
            for(auto&a:att) if(!(a.size()>=5&&trim(a[0])==delID)) newAtt.push_back(a);
            writeRows("attendance.dat","StudentID,Date,Status,SubjectCode,SectionID",newAtt);

            cout<<"  Student deleted.\n";
            waitKey();
        }

        // -- 9. TRANSFER STUDENT TO ANOTHER SECTION -----------------
        else if (ch == 9) {
            cls();
            cout<<"=== Transfer Student to Another Section ===\n\n";
            auto stuRows=readRows("students.dat");
            bool any=false;
            cout<<"  Students in current section ("<<gSectionID<<"):\n";
            for(auto&s:stuRows)
                if(s.size()>=3&&trim(s[2])==gSectionID){cout<<"    "<<trim(s[1])<<"  -  "<<trim(s[0])<<"\n";any=true;}
            if(!any){cout<<"  No students.\n";waitKey();continue;}

            string srcID; bool ok=false;
            while(!ok) ok=readLine("\nStudent ID to transfer: ",srcID,80,true);
            srcID=trim(srcID);
            string srcName="";
            for(auto&s:stuRows) if(trim(s[1])==srcID&&trim(s[2])==gSectionID){srcName=trim(s[0]);break;}
            if(srcName.empty()){cout<<"  [ERROR] Student not found in current section.\n";waitKey();continue;}

            // Show available sections in the same semester
            auto secRows=readRows("sections.dat");
            cout<<"\n  Available target sections in semester "<<gSemID<<":\n";
            bool anySec=false;
            for(auto&r:secRows)
                if(r.size()>=3&&r[2]==gSemID&&r[1]!=gSectionID){
                    cout<<"    "<<r[1]<<"  ("<<r[0]<<")\n";anySec=true;
                }
            if(!anySec){cout<<"  No other sections available in this semester.\n";waitKey();continue;}

            string tgtSectionID; ok=false;
            while(!ok) ok=readLine("Enter target Section ID: ",tgtSectionID,80,true);
            tgtSectionID=trim(tgtSectionID);
            bool secValid=false;
            for(auto&r:secRows) if(r.size()>=3&&r[1]==tgtSectionID&&r[2]==gSemID){secValid=true;break;}
            if(!secValid){cout<<"  [ERROR] Invalid section ID or section not in this semester.\n";waitKey();continue;}

            // Generate new serial for target section
            set<int> tgtUsed;
            for(auto&s:stuRows){
                if(s.size()<3||trim(s[2])!=tgtSectionID) continue;
                const string&sid=trim(s[1]);
                size_t sp=sid.rfind("-S");
                if(sp==string::npos) continue;
                string num=sid.substr(sp+2);
                bool allD=!num.empty();
                for(char c:num) if(!isdigit((unsigned char)c)){allD=false;break;}
                if(allD) tgtUsed.insert(stoi(num));
            }
            int nextTgt=1;
            while(tgtUsed.count(nextTgt)) ++nextTgt;
            string newStuID=tgtSectionID+"-S"+to_string(nextTgt);

            cout<<"\n  Transfer summary:\n";
            cout<<"  Student  : "<<srcName<<"\n";
            cout<<"  From     : "<<gSectionID<<"  ("<<srcID<<")\n";
            cout<<"  To       : "<<tgtSectionID<<"  (new ID: "<<newStuID<<")\n";
            cout<<"\n  Previous section record will be DELETED after transfer.\n";
            cout<<"  Attendance history stays in old section for archiving.\n\n";

            char conf=0;
            while(conf!='Y'&&conf!='N'){
                cout<<"  Confirm transfer? (Y/N): ";
                string ans;getline(cin,ans);ans=trim(ans);
                if(ans.size()==1)conf=toupper((unsigned char)ans[0]);
            }
            if(conf!='Y'){cout<<"  Cancelled.\n";waitKey();continue;}

            stuRows.push_back({srcName,newStuID,tgtSectionID});
            vector<vector<string>> updated;
            for(auto&s:stuRows)
                if(!(trim(s[1])==srcID&&trim(s[2])==gSectionID)) updated.push_back(s);
            bool twOK = writeRows("students.dat","Name,StudentID,SectionID",updated);
            if(!twOK){cout<<"  [ERROR] Transfer failed. Could not write.\n";waitKey();continue;}
            {
                auto vfy=readRows("students.dat");
                bool ok2=false;
                for(auto&v:vfy) if(v.size()>=2&&trim(v[1])==newStuID){ok2=true;break;}
                if(!ok2){cout<<"  [ERROR] Transfer verification failed.\n";waitKey();continue;}
            }
            cout<<"  Transfer complete!\n";
            cout<<"  "<<srcName<<" is now in "<<tgtSectionID<<" with ID "<<newStuID<<"\n";
            waitKey();
        }

        else if (ch==0) {
            gSectionName=""; gSectionID="";
            return;
        }
        else { cout<<"  [ERROR] Invalid choice.\n"; waitKey(); }
    }
}

// --------------------------------------------------------------
//  PHASE 3b   -  Section Portal (inside a Semester)
// --------------------------------------------------------------
void phase3b() {
    while (true) {
        cls();
        cout<<"\n  "<<BOLD<<FG_BCYAN<<"+"<<string(62,'-')<<"+"<<RESET<<"\n";
        cout<<"  "<<BOLD<<FG_BCYAN<<"| "<<RESET
            <<"Welcome "<<BOLD<<FG_BWHITE<<gAdminName<<RESET
            <<" | "<<FG_BYELLOW<<gDeptName
            <<" | "<<gSemName<<" | Batch "<<gBatch<<RESET<<"\n";
        cout<<"  "<<BOLD<<FG_BCYAN<<"+"<<string(62,'-')<<"+"<<RESET<<"\n\n";

        // List existing sections
        auto secRows=readRows("sections.dat");
        bool anyS=false;
        cout<<"  "<<BOLD<<FG_BCYAN<<"Sections in "<<gSemID<<":"<<RESET<<"\n";
        for(auto&r:secRows){
            if(r.size()>=3&&r[2]==gSemID){
                cout<<"    "<<FG_BMAGENTA<<r[1]<<RESET<<"  ("<<r[0]<<")\n";
                anyS=true;
            }
        }
        if(!anyS) cout<<"  "<<DIM<<"  (No sections yet)\n"<<RESET;

        cout<<"\n";
        cout<<"  "<<FG_BGREEN  <<"1."<<RESET<<"  Create Section\n";
        cout<<"  "<<FG_BYELLOW <<"2."<<RESET<<"  Enter Section\n";
        cout<<"  "<<FG_BRED    <<"3."<<RESET<<"  Delete Section (Cascading)\n";
        cout<<"  "<<FG_BCYAN   <<"4."<<RESET<<"  Overall Report - All Sections\n";
        cout<<"  "<<FG_BRED    <<"0."<<RESET<<"  Back\n\n";
        cout<<"  "<<BOLD<<FG_BWHITE<<"Choice: "<<RESET;
        int ch=menuChoice();

        // -- 1. CREATE SECTION --------------------------------------
        if(ch==1){
            cls();
            cout<<"=== Create Section ===\n\n";
            cout<<"  Semester: "<<gSemID<<"\n\n";
            cout<<"  Examples: Section A, Section B, Morning, Evening\n\n";
            string sName; bool ok=false;
            while(!ok) ok=readLine("Section Name: ",sName,40,true);
            sName=trim(sName);

            // SectionID = SemID + "-" + sanitized SectionName (no spaces)
            string sanitizedName=sName;
            for(char&c:sanitizedName) if(c==' ') c='_';
            // Remove commas just in case
            sanitizedName.erase(remove(sanitizedName.begin(),sanitizedName.end(),','),sanitizedName.end());
            string sectionID=gSemID+"-"+sanitizedName;

            // Check duplicate
            auto secR=readRows("sections.dat");
            bool exists=false;
            for(auto&r:secR) if(r.size()>=2&&r[1]==sectionID){exists=true;break;}
            if(exists){
                cout<<"  [ERROR] Section '"<<sectionID<<"' already exists in this semester.\n";
                waitKey(); continue;
            }

            secR.push_back({sName,sectionID,gSemID});
            writeRows("sections.dat","SectionName,SectionID,SemID",secR);
            cout<<"\n  Section Created!\n";
            cout<<"  Name      : "<<sName<<"\n";
            cout<<"  Section ID: "<<sectionID<<"\n";
            cout<<"  Semester  : "<<gSemID<<"\n";
            waitKey();
        }

        // -- 2. ENTER SECTION --------------------------------------
        else if(ch==2){
            auto secR=readRows("sections.dat");
            bool any=false;
            cout<<"\n  Available Sections in "<<gSemID<<":\n";
            for(auto&r:secR) if(r.size()>=3&&r[2]==gSemID){
                cout<<"    "<<r[1]<<"  ("<<r[0]<<")\n";any=true;
            }
            if(!any){cout<<"\n  No sections. Create one first.\n";waitKey();continue;}

            string sectionID; bool ok=false;
            while(!ok) ok=readLine("\nEnter Section ID: ",sectionID,80,true);
            sectionID=trim(sectionID);

            bool valid=false;
            for(auto&r:secR){
                if(r.size()>=3&&r[1]==sectionID&&r[2]==gSemID){
                    gSectionID=sectionID;
                    gSectionName=r[0];
                    valid=true;
                    break;
                }
            }
            if(valid) phase4();
            else{cout<<"  [ERROR] Invalid Section ID or section not in this semester.\n";waitKey();}
        }

        // -- 3. DELETE SECTION (CASCADING) -------------------------
        else if(ch==3){
            auto secR=readRows("sections.dat");
            bool any=false;
            cout<<"\n  Sections in "<<gSemID<<":\n";
            for(auto&r:secR) if(r.size()>=3&&r[2]==gSemID){
                cout<<"    "<<r[1]<<"  ("<<r[0]<<")\n";any=true;
            }
            if(!any){cout<<"\n  No sections to delete.\n";waitKey();continue;}

            string sectionID; bool ok=false;
            while(!ok) ok=readLine("\nSection ID to DELETE: ",sectionID,80,true);
            sectionID=trim(sectionID);

            bool found=false; string foundName="";
            for(auto&r:secR) if(r.size()>=2&&r[1]==sectionID&&r[2]==gSemID){found=true;foundName=r[0];break;}
            if(!found){cout<<"  [ERROR] Section not found in this semester.\n";waitKey();continue;}

            char conf=0;
            while(conf!='Y'&&conf!='N'){
                cout<<"  Delete section '"<<foundName<<"' ("<<sectionID<<")\n";
                cout<<"  This will PERMANENTLY delete all students, subjects, and attendance in this section.\n";
                cout<<"  Confirm? (Y/N): ";
                string ans;getline(cin,ans);ans=trim(ans);
                if(ans.size()==1)conf=toupper((unsigned char)ans[0]);
            }
            if(conf!='Y'){cout<<"  Cancelled.\n";waitKey();continue;}

            cascadeDeleteSection(sectionID);
            cout<<"\n  Section '"<<foundName<<"' and all its data deleted.\n";
            waitKey();
        }

        // -- 4. OVERALL REPORT - ALL SECTIONS ----------------------
        else if(ch==4){
            auto secR=readRows("sections.dat");
            bool any=false;
            for(auto&r:secR) if(r.size()>=3&&r[2]==gSemID){
                cls();
                string lbl=gDeptName+" | "+gSemName+" | Batch "+gBatch+" | "+r[0];
                reportOverall(r[1],lbl,"");
                any=true;
                waitKey();
            }
            if(!any){cout<<"\n  No sections in this semester.\n";waitKey();}
        }

        else if(ch==0){
            gSemID=""; gSemName=""; gBatch="";
            return;
        }
        else{cout<<"  [ERROR] Invalid choice.\n";waitKey();}
    }
}

// --------------------------------------------------------------
//  PHASE 3   -  Semester Portal
// --------------------------------------------------------------
void phase3() {
    while (true) {
        cls();
        cout<<"\n  Welcome "<<BOLD<<FG_BWHITE<<gAdminName<<RESET
            <<" in "<<FG_BYELLOW<<gDeptName<<RESET<<" Department\n\n";
        cout<<"  "<<FG_BGREEN <<" 1."<<RESET<<"  Create Semester\n";
        cout<<"  "<<FG_BYELLOW<<" 2."<<RESET<<"  Enter Semester\n";
        cout<<"  "<<FG_BCYAN  <<" 3."<<RESET<<"  View Overall Report by Semester\n";
        cout<<"  "<<FG_BBLUE  <<" 4."<<RESET<<"  Individual Student Report (Semester View)\n";
        cout<<"  "<<FG_BMAGENTA<<" 5."<<RESET<<"  Bulk Semester Promotion (Transfer All Students)\n";
        cout<<"  "<<FG_BRED   <<" 6."<<RESET<<"  Back\n\n";
        cout<<"  "<<BOLD<<FG_BWHITE<<"Choice: "<<RESET;
        int ch=menuChoice();

        if (ch==1) {
            cls();
            cout<<"=== Create Semester ===\n\n";
            string semName,batch,semCode,intake; bool ok=false;

            while(!ok) ok=readLine("Semester Name (e.g. First): ",semName,30,true,true);
            ok=false;
            while(!ok){
                ok=readDigits("Batch Year (e.g. 2024): ",batch,4,4);
                if(ok){int y=stoi(batch);if(y<2000||y>2100){cout<<"  [ERROR] Year 2000-2100.\n";ok=false;}}
            }
            ok=false;
            while(!ok) ok=readDigits("Semester Number (e.g. 1): ",semCode,1,2);

            cout<<"  Intake name (e.g. July, Fall - no spaces, press Enter to skip): ";
            string intakeRaw; getline(cin,intakeRaw); intakeRaw=trim(intakeRaw);
            if(!intakeRaw.empty()) {
                string cleaned;
                for(char c:intakeRaw) if(c!=','&&c!=' ') cleaned+=c;
                intake = cleaned;
            }

            string semID;
            if(intake.empty())
                semID = gDeptCode+"-"+batch+"-"+semCode;
            else
                semID = gDeptCode+"-"+intake+"-"+batch+"-"+semCode;

            auto rows=readRows("semesters.dat");
            bool exists=false;
            for(auto&r:rows) if(r.size()>=4&&r[3]==semID){exists=true;break;}
            if(exists){cout<<"\n  [ERROR] '"<<semID<<"' already exists.\n";waitKey();continue;}

            rows.push_back({semName,batch,semCode,semID,gDeptCode,intake});
            writeRows("semesters.dat","SemName,Batch,SemCode,SemID,DeptCode,IntakeName",rows);
            cout<<"\n  Semester Created!\n";
            cout<<"  Name  : "<<semName<<"\n";
            cout<<"  Batch : "<<batch<<"\n";
            if(!intake.empty()) cout<<"  Intake: "<<intake<<"\n";
            cout<<"  ID    : "<<semID<<"\n";
            cout<<"\n  NOTE: You can now enter this semester and create Sections.\n";
            waitKey();
        }
        else if (ch==2) {
            auto rows=readRows("semesters.dat");
            bool any=false;
            cout<<"\n  Available Semesters in "<<gDeptName<<":\n";
            for(auto&r:rows) if(r.size()>=5&&r[4]==gDeptCode){
                cout<<"    "<<r[3]<<"  (";
                cout<<r[0]<<" | Batch: "<<r[1];
                if(r.size()>=6&&!r[5].empty()) cout<<" | Intake: "<<r[5];
                cout<<")\n"; any=true;
            }
            if(!any){cout<<"\n  No semesters. Create one first.\n";waitKey();continue;}

            string semID; bool ok=false;
            while(!ok) ok=readLine("\nEnter Semester ID: ",semID,60,true);

            bool valid=false;
            for(auto&r:rows)
                if(r.size()>=5&&r[3]==semID&&r[4]==gDeptCode){
                    gSemID=semID; gSemName=r[0]; gBatch=r[1]; valid=true; break;
                }
            if(valid) phase3b();
            else{cout<<"  [ERROR] Invalid Semester ID.\n";waitKey();}
        }
        else if (ch==3) {
            // Overall report aggregated across all sections of a semester
            auto semRows=readRows("semesters.dat");
            bool any=false;
            cout<<"\n  Semesters in "<<gDeptName<<":\n";
            for(auto&r:semRows) if(r.size()>=5&&r[4]==gDeptCode){
                cout<<"    "<<r[0]<<" | Batch: "<<r[1]<<" | ID: "<<r[3]<<"\n";any=true;
            }
            if(!any){cout<<"\n  No semesters.\n";waitKey();continue;}
            string qID; bool ok=false;
            while(!ok) ok=readLine("\nEnter Semester ID: ",qID,60,true);
            bool valid=false;
            string qName="",qBatch="";
            for(auto&r:semRows) if(r.size()>=5&&r[3]==qID&&r[4]==gDeptCode){qName=r[0];qBatch=r[1];valid=true;break;}
            if(!valid){cout<<"  [ERROR] Invalid.\n";waitKey();continue;}

            // Show report per section
            auto secRows=readRows("sections.dat");
            bool anyS=false;
            for(auto&r:secRows){
                if(r.size()>=3&&r[2]==qID){
                    cls();
                    string lbl=gDeptName+" | "+qName+" | Batch "+qBatch+" | "+r[0];
                    reportOverall(r[1],lbl,"");
                    anyS=true;
                    waitKey();
                }
            }
            if(!anyS){
                cout<<"\n  No sections in this semester.\n";waitKey();
            }
        }
        else if (ch==4) {
            auto semRows=readRows("semesters.dat");
            bool any=false;
            cout<<"\n  Semesters:\n";
            for(auto&r:semRows) if(r.size()>=5&&r[4]==gDeptCode){
                cout<<"    "<<r[0]<<" | Batch: "<<r[1]<<" | ID: "<<r[3]<<"\n";any=true;
            }
            if(!any){cout<<"\n  No semesters.\n";waitKey();continue;}
            string qSemID; bool ok=false;
            while(!ok) ok=readLine("\nEnter Semester ID: ",qSemID,60,true);
            bool valid=false;
            for(auto&r:semRows) if(r.size()>=5&&r[3]==qSemID&&r[4]==gDeptCode){valid=true;break;}
            if(!valid){cout<<"  [ERROR] Invalid.\n";waitKey();continue;}

            // Show sections, pick section then student
            auto secRows=readRows("sections.dat");
            bool anyS=false;
            cout<<"\n  Sections in "<<qSemID<<":\n";
            for(auto&r:secRows) if(r.size()>=3&&r[2]==qSemID){
                cout<<"    "<<r[1]<<"  ("<<r[0]<<")\n";anyS=true;
            }
            if(!anyS){cout<<"  No sections.\n";waitKey();continue;}

            string qSecID; ok=false;
            while(!ok) ok=readLine("\nEnter Section ID: ",qSecID,80,true);
            bool secValid=false;
            for(auto&r:secRows) if(r.size()>=2&&r[1]==qSecID&&r[2]==qSemID){secValid=true;break;}
            if(!secValid){cout<<"  [ERROR] Section not found in this semester.\n";waitKey();continue;}

            auto stuRows=readRows("students.dat");
            bool anyStu=false;
            cout<<"\n  Students in "<<qSecID<<":\n";
            for(auto&s:stuRows) if(s.size()>=3&&s[2]==qSecID){cout<<"    "<<s[1]<<"  -  "<<s[0]<<"\n";anyStu=true;}
            if(!anyStu){cout<<"  No students.\n";waitKey();continue;}

            string stuID; ok=false;
            while(!ok) ok=readLine("\nEnter Student ID: ",stuID,80,true);
            string stuName="";
            for(auto&s:stuRows) if(s.size()>=2&&s[1]==stuID){stuName=s[0];break;}
            if(stuName.empty()){cout<<"  [ERROR] Not found.\n";waitKey();continue;}

            string yrStr,moStr; ok=false;
            while(!ok) ok=readDigits("Year: ",yrStr,4,4);
            ok=false;
            while(!ok){
                ok=readDigits("Month (1-12): ",moStr,1,2);
                if(ok){int m=stoi(moStr);if(m<1||m>12){cout<<"  [ERROR] 1-12.\n";ok=false;}}
            }
            reportStudent(stuID,stuName,qSecID,stoi(yrStr),stoi(moStr));
            waitKey();
        }
        else if(ch==5) { bulkPromote(); }
        else if(ch==6) return;
        else{cout<<"  [ERROR] Invalid.\n";waitKey();}
    }
}

// --------------------------------------------------------------
//  PHASE 2   -  Department Portal
// --------------------------------------------------------------
void phase2() {
    while (true) {
        cls();
        cout<<"\n  Welcome "<<BOLD<<FG_BWHITE<<gAdminName<<RESET<<" to the "<<BOLD<<FG_BCYAN<<"DAMS"<<RESET<<"\n\n";
        cout<<"  "<<FG_BGREEN   <<"1."<<RESET<<"  Create Department\n";
        cout<<"  "<<FG_BYELLOW  <<"2."<<RESET<<"  Enter Department\n";
        cout<<"  "<<FG_BCYAN    <<"3."<<RESET<<"  View Report by Dept + Batch\n";
        cout<<"  "<<FG_BRED     <<"4."<<RESET<<"  Delete Department (Cascading)\n";
        cout<<"  "<<FG_BMAGENTA <<"5."<<RESET<<"  Logout\n\n";
        cout<<"  "<<BOLD<<FG_BWHITE<<"Choice: "<<RESET;
        int ch=menuChoice();

        if (ch==1) {
            cls();
            cout<<"=== Create Department ===\n\n";
            string dn,hod,intakesStr; bool ok=false;
            while(!ok) ok=readLine("Dept Name (letters & spaces): ",dn,40,true,true);
            ok=false;
            while(!ok) ok=readLine("HOD Name (letters & spaces): ",hod,60,true,true);
            ok=false;
            while(!ok){
                ok=readDigits("Number of intakes per year (e.g. 1 or 2): ",intakesStr,1,1);
                if(ok){int n=stoi(intakesStr);if(n<1||n>9){cout<<"  [ERROR] Must be 1-9.\n";ok=false;}}
            }
            string dc="D-"+dn;
            auto rows=readRows("departments.dat");
            bool exists=false;
            for(auto&r:rows) if(r.size()>=3&&r[2]==dc){exists=true;break;}
            if(exists){cout<<"\n  [ERROR] '"<<dc<<"' already exists.\n";waitKey();continue;}
            rows.push_back({dn,hod,dc,intakesStr});
            writeRows("departments.dat","DeptName,HOD,DeptCode,Intakes",rows);
            cout<<"\n  Dept Saved!\n  Name   : "<<dn<<"\n  HOD    : "<<hod<<"\n  Code   : "<<dc<<"\n  Intakes: "<<intakesStr<<" per year\n";
            waitKey();
        }
        else if (ch==2) {
            auto rows=readRows("departments.dat");
            bool any=false;
            cout<<"\n  Available Departments:\n";
            for(auto&r:rows) if(r.size()>=3){
                cout<<"    "<<r[0]<<"  |  Code: "<<r[2];
                if(r.size()>=4&&!r[3].empty()) cout<<"  |  Intakes/yr: "<<r[3];
                cout<<"\n"; any=true;
            }
            if(!any){cout<<"\n  No departments. Create one first.\n";waitKey();continue;}
            string target; bool ok=false;
            while(!ok) ok=readLine("\nEnter Dept Code (e.g. D-Computer): ",target,50,true);
            bool valid=false;
            for(auto&r:rows) if(r.size()>=3&&r[2]==target){gDeptName=r[0];gDeptCode=r[2];valid=true;break;}
            if(valid) phase3();
            else{cout<<"  [ERROR] Invalid Dept Code.\n";waitKey();}
        }
        else if (ch==3) {
            auto dr=readRows("departments.dat");
            bool anyD=false;
            cout<<"\n  Departments:\n";
            for(auto&r:dr) if(r.size()>=3){cout<<"    "<<r[0]<<"  |  Code: "<<r[2]<<"\n";anyD=true;}
            if(!anyD){cout<<"\n  None.\n";waitKey();continue;}
            string qDept,qBatch; bool ok=false;
            while(!ok) ok=readLine("\nEnter Dept Code: ",qDept,50,true);
            ok=false;
            while(!ok) ok=readDigits("Enter Batch Year: ",qBatch,4,4);
            auto sr=readRows("semesters.dat");
            vector<vector<string>> matched;
            for(auto&r:sr) if(r.size()>=5&&r[4]==qDept&&r[1]==qBatch) matched.push_back(r);
            if(matched.empty()){cout<<"\n  No semesters for "<<qDept<<" batch "<<qBatch<<".\n";waitKey();continue;}
            // Show per-section reports across all matching semesters
            auto secRows=readRows("sections.dat");
            for(auto&sem:matched){
                for(auto&sec:secRows){
                    if(sec.size()>=3&&sec[2]==sem[3]){
                        cls();
                        string lbl="Sem: "+sem[0]+" | "+sec[0]+" | ID: "+sec[1];
                        reportOverall(sec[1],lbl);
                        waitKey();
                    }
                }
            }
        }
        else if (ch==4) {
            auto rows=readRows("departments.dat");
            bool any=false;
            cout<<"\n  Departments:\n";
            for(auto&r:rows) if(r.size()>=3){cout<<"    "<<r[0]<<"  |  Code: "<<r[2]<<"\n";any=true;}
            if(!any){cout<<"\n  None to delete.\n";waitKey();continue;}
            string target; bool ok=false;
            while(!ok) ok=readLine("\nDept Code to DELETE: ",target,50,true);
            bool found=false;
            for(auto&r:rows) if(r.size()>=3&&r[2]==target){found=true;break;}
            if(!found){cout<<"  [ERROR] Not found.\n";waitKey();continue;}
            char conf=0;
            while(conf!='Y'&&conf!='N'){
                cout<<"  Delete '"<<target<<"' and ALL its data (semesters, sections, students, subjects, attendance)? (Y/N): ";
                string ans;getline(cin,ans);ans=trim(ans);
                if(ans.size()==1)conf=toupper((unsigned char)ans[0]);
            }
            if(conf!='Y'){cout<<"  Cancelled.\n";waitKey();continue;}
            cascadeDelete(target);
            cout<<"\n  Deleted '"<<target<<"' and all its data.\n";
            waitKey();
        }
        else if (ch==5) {
            gAdminName=gAdminPw=gDeptCode=gDeptName=gSemName=gBatch=gSemID=gSectionName=gSectionID="";
            return;
        }
        else{cout<<"  [ERROR] Invalid.\n";waitKey();}
    }
}

// --------------------------------------------------------------
//  ANIMATIONS
// --------------------------------------------------------------
void typewrite(const string &txt, int ms=25) {
    for(char c:txt){cout<<c;cout.flush();SLEEP_MS(ms);}
}
void pline(int w,const char*col,int ms=5){
    cout<<col;for(int i=0;i<w;i++){cout<<'-';cout.flush();SLEEP_MS(ms);}cout<<RESET<<"\n";
}

void showIntro(){
    cout<<CURSOR_HIDE<<CLS;
    pline(56,FG_CYAN,5);SLEEP_MS(80);
    const char* logo[]={"  ____    _    __  __  ____  ",
                        "  |  _ \\  / \\  |  \\/  |/ ___| ",
                        "  | | | |/ _ \\ | |\\/| |\\___ \\ ",
                        "  |____/_/ \\_\\|_|  |_||____/ "};
    const char* lc[]={FG_BCYAN,FG_BMAGENTA,FG_BYELLOW,FG_BGREEN};
    for(int i=0;i<4;i++){cout<<BOLD<<lc[i]<<logo[i]<<RESET<<"\n";cout.flush();SLEEP_MS(90);}
    SLEEP_MS(100);
    cout<<"\n  "<<BOLD<<FG_BWHITE;typewrite("Digital Attendance Management System",20);cout<<RESET<<"\n";
    cout<<"  "<<DIM<<FG_WHITE<<"v2.2 "<<RESET<<"\n\n";
    pline(56,FG_CYAN,5);SLEEP_MS(100);
    cout<<"\n  "<<BOLD<<FG_BYELLOW<<"Developed by:"<<RESET<<"\n\n";
    struct{const char*col,*name;}devs[]={{FG_BCYAN,"Aaditya Deep"},{FG_BMAGENTA,"Aaryan Keshari"},
                                         {FG_BYELLOW,"Bali Kumar Wad"},{FG_BGREEN,"Rejan Dhungana"}};
    for(auto&d:devs){SLEEP_MS(130);cout<<"    "<<BOLD<<d.col<<"> "<<d.name<<RESET<<"\n";cout.flush();}
    cout<<"\n";pline(56,FG_CYAN,5);SLEEP_MS(200);
    for(int b=0;b<3;b++){
        cout<<"\r  "<<BOLD<<FG_BWHITE<<"  Press any key to continue...  "<<RESET;cout.flush();SLEEP_MS(380);
        cout<<"\r  "<<string(34,' ');cout.flush();SLEEP_MS(220);
    }
    cout<<"\r  "<<BOLD<<FG_BWHITE<<"  Press any key to continue...  "<<RESET<<"\n";cout.flush();
    cout<<CURSOR_SHOW;while(_kbhit())_getch();_getch();
}

void showOutro(){
    cout<<CURSOR_HIDE<<CLS<<"\n\n";
    pline(56,FG_CYAN,5);
    cout<<"\n  "<<BOLD<<FG_BWHITE;typewrite("Thank you for using DAMS",30);cout<<RESET<<"\n\n";
    cout<<"  "<<BOLD<<FG_BYELLOW;typewrite("Goodbye! See you next time.",35);cout<<RESET<<"\n\n  ";
    const char*bc[]={FG_BCYAN,FG_BMAGENTA,FG_BYELLOW,FG_BGREEN};
    for(int i=0;i<52;i++){cout<<BOLD<<bc[i%4]<<"#"<<RESET;cout.flush();SLEEP_MS(18);}
    cout<<"\n\n  "<<DIM<<FG_WHITE<<"DAMS v2.2  |  Aaditya . Aaryan . Bali . Rejan"<<RESET<<"\n\n";
    cout.flush();SLEEP_MS(1600);cout<<CURSOR_SHOW<<CLS;
}

// --------------------------------------------------------------
//  PHASE 1   -  Admin Portal
// --------------------------------------------------------------
void phase1() {
    initAdminDat();
    while (true) {
        cls();
        cout<<"\n  "<<BOLD<<FG_BCYAN<<"+"<<string(30,'-')<<"+"<<RESET<<"\n";
        cout<<"  "<<BOLD<<FG_BCYAN<<"|"<<RESET<<BOLD<<FG_BWHITE<<"      DAMS  --  Admin Portal   "<<RESET<<BOLD<<FG_BCYAN<<"|"<<RESET<<"\n";
        cout<<"  "<<BOLD<<FG_BCYAN<<"+"<<string(30,'-')<<"+"<<RESET<<"\n\n";
        cout<<"  "<<FG_BGREEN <<"1."<<RESET<<"  Create New Admin\n";
        cout<<"  "<<FG_BYELLOW<<"2."<<RESET<<"  Login\n";
        cout<<"  "<<FG_BRED   <<"3."<<RESET<<"  Exit\n\n";
        cout<<"  "<<BOLD<<FG_BWHITE<<"Choice: "<<RESET;
        int ch=menuChoice();

        if (ch==1) {
            if(adminExists()){cout<<"\n  [ERROR] Admin already exists. Only 1 admin allowed.\n";waitKey();continue;}
            cls(); cout<<"=== Create Admin ===\n\n";
            string name,user,pw; bool ok=false;
            while(!ok) ok=readLine("Full Name (letters & spaces): ",name,60,true,true);
            ok=false;
            while(!ok){
                ok=readLine("Username (no spaces): ",user,30,true);
                if(ok&&user.find(' ')!=string::npos){cout<<"  [ERROR] No spaces.\n";ok=false;}
            }
            bool pwOk=false;
            while(!pwOk){
                cout<<"Password (8+ chars, letter+digit+symbol): ";
                pw=getMaskedPw();
                if(!validatePw(pw)){cout<<"  [ERROR] Weak password. Need 8+ chars with letter+digit+symbol.\n";continue;}
                cout<<"Confirm Password: ";
                string pw2=getMaskedPw();
                if(pw2!=pw){cout<<"  [ERROR] Passwords do not match.\n";continue;}
                pwOk=true;
            }
            writeAdminRow(name,user,pw);
            cout<<"\n  Admin created! Use option 2 to login.\n";
            waitKey();
        }
        else if (ch==2) {
            cls(); cout<<"=== Admin Login ===\n\n";
            string u; bool ok=false;
            while(!ok) ok=readLine("Username: ",u,30,true);
            cout<<"Password: "; string p=getMaskedPw();
            auto rows=readAdminRows();
            bool found=false;
            for(auto&r:rows){
                if(r.size()>=3&&r[1]==u&&r[2]==p){
                    gAdminName=r[0]; gAdminPw=p; found=true; break;
                }
            }
            if(found){
                // Init all DAT files now that we have the encryption key
                initDat("departments.dat","DeptName,HOD,DeptCode,Intakes");
                initDat("semesters.dat","SemName,Batch,SemCode,SemID,DeptCode,IntakeName");
                initDat("sections.dat","SectionName,SectionID,SemID");    // NEW
                initDat("subjects.dat","SubjectName,SubjectCode,SectionID");
                initDat("students.dat","Name,StudentID,SectionID");
                initDat("attendance.dat","StudentID,Date,Status,SubjectCode,SectionID");
                phase2();
                gAdminPw="";
            } else {
                cout<<"  [ERROR] Wrong username or password.\n"; waitKey();
            }
        }
        else if (ch==3) { showOutro(); exit(0); }
        else { cout<<"  [ERROR] Invalid choice.\n"; waitKey(); }
    }
}

// --------------------------------------------------------------
//  MAIN
// --------------------------------------------------------------
int main() {
    enableANSI();
    showIntro();
    phase1();
    return 0;
}