/*
 * DIGITAL LOG FOR LAB ENTRIES  -  lab attendance system (pure standard C)
 *
 * Input: a USB barcode scanner. The scanner acts like a keyboard: it "types" the
 * ID printed on the student's card and presses Enter.
 *
 * Files (created automatically in the current folder):
 *   students.dat    binary database of registered students (struct Student)
 *   attendance.csv  attendance ledger (opens directly in Excel / Sheets)
 *   <any>.csv       student list exported from Excel (File > Save As > CSV);
 *                   you give its path at run time
 *
 * Build:
 *   gcc -Wall -Wextra -O2 attendance.c -o attendance
 *
 * Run:
 *   attendance                                          menu
 *   attendance --import list.csv [roll_col name_col]    add students without the menu
 *   attendance --replace list.csv [roll_col name_col]   replace the student list
 */

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define ID_LEN        32
#define NAME_LEN      64
#define LAB_LEN       48
#define LINE_LEN      256
#define STUDENT_FILE  "students.dat"
#define TEMP_FILE     "students.tmp"
#define BACKUP_FILE   "students.bak"
#define WIPE_PASSWORD "AMRITA VISHWA VIDYAPEETHAM"
#define EXPORT_DEFAULT "attendance_export.csv"
#define LEDGER_TEMP   "attendance.tmp"
#define LEDGER_HEADER "Date,Time,ID,Name,Lab,Type,Table,Hours"
#define ATTEND_FILE   "attendance.csv"
#define IMPORT_FILE   "students.csv"
#define DEBOUNCE_SEC  5     /* ignore the same ID scanned again within N seconds */

typedef struct {
    char id[ID_LEN];
    char name[NAME_LEN];
} Student;

static int ci_equal(const char *a, const char *b);

/* ---------- input helpers ---------- */

/* Read one line, strip newline / carriage return. Returns 0 on EOF. */
static int read_line(char *buf, size_t size)
{
    if (!fgets(buf, (int)size, stdin))
        return 0;
    size_t n = strlen(buf);
    if (n > 0 && buf[n - 1] != '\n') {      /* line longer than buffer: discard rest */
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
    }
    buf[strcspn(buf, "\r\n")] = '\0';
    return 1;
}

/* IDs: keep only printable, non-space characters; drop '*' (Code 39 start/stop)
 * and ',' / '"' (CSV safety); convert to upper case. */
static void sanitize_id(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        unsigned char c = (unsigned char)*r;
        if (!isprint(c) || isspace(c) || c == '*' || c == ',' || c == '"')
            continue;
        *w++ = (char)toupper(c);
    }
    *w = '\0';
    if (strlen(s) >= ID_LEN) s[ID_LEN - 1] = '\0';
}

/* Free text (names, lab): trim, remove commas/quotes/control chars. */
static void clean_text(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        unsigned char c = (unsigned char)*r;
        if (c == ',' || c == '"') c = ' ';
        if (c < 32 || c == 127) continue;
        *w++ = (char)c;
    }
    *w = '\0';
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
}

static void now_strings(char *date, size_t dsz, char *tm_, size_t tsz)
{
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    strftime(date, dsz, "%Y-%m-%d", lt);
    strftime(tm_,  tsz, "%H:%M:%S", lt);
}

/* ---------- student database (binary) ---------- */

static int find_student(const char *id, Student *out)
{
    FILE *fp = fopen(STUDENT_FILE, "rb");
    if (!fp) return 0;
    Student s;
    int found = 0;
    while (fread(&s, sizeof s, 1, fp) == 1) {
        s.id[ID_LEN - 1] = '\0';
        s.name[NAME_LEN - 1] = '\0';
        if (strcmp(s.id, id) == 0) {
            if (out) *out = s;
            found = 1;
            break;
        }
    }
    fclose(fp);
    return found;
}

/* returns 1 added, 0 duplicate, -1 error */
static int add_student(const char *id, const char *name)
{
    if (find_student(id, NULL)) return 0;
    FILE *fp = fopen(STUDENT_FILE, "ab");
    if (!fp) return -1;
    Student s;
    memset(&s, 0, sizeof s);
    strncpy(s.id, id, ID_LEN - 1);
    strncpy(s.name, name, NAME_LEN - 1);
    int ok = fwrite(&s, sizeof s, 1, fp) == 1;
    fclose(fp);
    return ok ? 1 : -1;
}

static void menu_add_student(void)
{
    char id[LINE_LEN], name[LINE_LEN];
    printf("Scan or type the student ID: ");
    if (!read_line(id, sizeof id)) return;
    sanitize_id(id);
    if (!id[0]) { printf("Empty ID, cancelled.\n"); return; }
    printf("Student name: ");
    if (!read_line(name, sizeof name)) return;
    clean_text(name);
    if (!name[0]) { printf("Empty name, cancelled.\n"); return; }

    switch (add_student(id, name)) {
        case 1:  printf("Registered: %s - %s\n", id, name); break;
        case 0:  printf("ID %s is already registered.\n", id); break;
        default: printf("Error: could not write %s\n", STUDENT_FILE);
    }
}

/* Remove one student: copy every other record to a temp file, then swap files.
 * returns 1 removed, 0 not found, -1 error */
static int remove_student(const char *id)
{
    FILE *in = fopen(STUDENT_FILE, "rb");
    if (!in) return 0;
    FILE *out = fopen(TEMP_FILE, "wb");
    if (!out) { fclose(in); return -1; }

    Student s;
    int removed = 0, ok = 1;
    while (fread(&s, sizeof s, 1, in) == 1) {
        s.id[ID_LEN - 1] = '\0';
        if (strcmp(s.id, id) == 0) { removed = 1; continue; }
        if (fwrite(&s, sizeof s, 1, out) != 1) { ok = 0; break; }
    }
    fclose(in);
    if (fclose(out) != 0) ok = 0;

    if (!ok)      { remove(TEMP_FILE); return -1; }
    if (!removed) { remove(TEMP_FILE); return 0; }
    if (remove(STUDENT_FILE) != 0 || rename(TEMP_FILE, STUDENT_FILE) != 0) return -1;
    return 1;
}

static void menu_remove_student(void)
{
    char id[LINE_LEN], ans[LINE_LEN];
    printf("Scan or type the ID of the student to remove: ");
    if (!read_line(id, sizeof id)) return;
    sanitize_id(id);
    if (!id[0]) { printf("Empty ID, cancelled.\n"); return; }

    Student s;
    if (!find_student(id, &s)) { printf("ID %s is not registered.\n", id); return; }

    printf("Remove %s - %s ? (y/N): ", s.id, s.name);
    if (!read_line(ans, sizeof ans) || (ans[0] != 'y' && ans[0] != 'Y')) {
        printf("Cancelled.\n");
        return;
    }
    switch (remove_student(id)) {
        case 1:  printf("Removed %s. (Past attendance entries are kept in %s.)\n", id, ATTEND_FILE); break;
        case 0:  printf("ID %s not found.\n", id); break;
        default: printf("Error: could not update %s\n", STUDENT_FILE);
    }
}

/* ---------- CSV import (exported from Excel) ---------- */

/* Split one CSV line IN PLACE into fields. Handles "quoted, fields", "" escapes,
 * and accepts ',' or ';' as the separator (some Excel regional settings use ';'). */
static int split_csv(char *line, char *fields[], int max)
{
    int n = 0;
    char *p = line;
    while (n < max) {
        if (*p == '"') {
            char *w = ++p;
            fields[n++] = w;
            while (*p) {
                if (*p == '"') {
                    if (p[1] == '"') { *w++ = '"'; p += 2; continue; }
                    p++;
                    break;
                }
                *w++ = *p++;
            }
            while (*p && *p != ',' && *p != ';') p++;
            int more = (*p != '\0');
            *w = '\0';
            if (!more) break;
            p++;
        } else {
            fields[n++] = p;
            while (*p && *p != ',' && *p != ';') p++;
            if (!*p) break;
            *p++ = '\0';
        }
    }
    return n;
}

static void strip_path(char *s)       /* remove spaces and the quotes added by drag-and-drop */
{
    clean_text(s);
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '\'' && s[n - 1] == '\'') { s[n - 1] = '\0'; memmove(s, s + 1, n - 1); }
    /* clean_text() already turned double quotes into spaces; trim again */
    clean_text(s);
}

static int has_digit(const char *s)
{
    for (; *s; s++) if (isdigit((unsigned char)*s)) return 1;
    return 0;
}

/* Import students from a CSV file. id_col / name_col are 1-based column numbers. */
static int ends_with_ci(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    if (ls < lx) return 0;
    for (size_t i = 0; i < lx; i++)
        if (tolower((unsigned char)s[ls - lx + i]) != tolower((unsigned char)suffix[i])) return 0;
    return 1;
}

static int import_csv(const char *path, int id_col, int name_col)
{
    if (ends_with_ci(path, ".xlsx") || ends_with_ci(path, ".xls")) {
        printf("\nThis is an Excel workbook (%s) - the program can only read CSV files.\n"
               "In Excel:  File > Save As > 'CSV UTF-8 (Comma delimited)',\n"
               "then give the path of the new .csv file.\n",
               ends_with_ci(path, ".xlsx") ? ".xlsx" : ".xls");
        return -1;
    }
    FILE *fp = fopen(path, "r");
    if (!fp) { printf("Cannot open file: %s\n(Is it open in Excel? Close it and try again.)\n", path); return -1; }

    /* a real .xlsx / .zip renamed to .csv starts with "PK" */
    int c0 = fgetc(fp), c1 = fgetc(fp);
    if (c0 == 'P' && c1 == 'K') {
        printf("\nThis file is not a text CSV (it looks like a zipped Excel workbook).\n"
               "Open it in Excel and use File > Save As > CSV UTF-8.\n");
        fclose(fp);
        return -1;
    }
    rewind(fp);

    char line[LINE_LEN * 4];
    char *fields[64];
    int added = 0, dup = 0, skipped = 0, first = 1;

    while (fgets(line, sizeof line, fp)) {
        char *p = line;
        if (first) {                                  /* UTF-8 BOM written by Excel */
            if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
                p += 3;
            first = 0;
        }
        p[strcspn(p, "\r\n")] = '\0';
        if (!*p) continue;

        int n = split_csv(p, fields, 64);
        if (id_col > n || name_col > n) { skipped++; continue; }

        char id[LINE_LEN], name[LINE_LEN];
        snprintf(id,   sizeof id,   "%s", fields[id_col - 1]);
        snprintf(name, sizeof name, "%s", fields[name_col - 1]);
        sanitize_id(id);
        clean_text(name);

        /* header rows ("Roll No", "Name") contain no digit in the ID column */
        if (!id[0] || !name[0] || !has_digit(id)) { skipped++; continue; }

        int r = add_student(id, name);
        if (r == 1) added++; else if (r == 0) dup++; else skipped++;
    }
    fclose(fp);
    printf("Import finished: %d added, %d already registered, %d rows skipped (header/blank/bad).\n",
           added, dup, skipped);
    return added + dup;
}

static int ask_csv_params(char *path, size_t psz, int *id_col, int *name_col)
{
    char buf[LINE_LEN];
    *id_col = 1; *name_col = 2;

    printf("Path of the CSV file exported from Excel\n");
    printf("(e.g. C:\\data\\ECE_A.csv, or just ECE_A.csv if it is in this folder)\n> ");
    if (!read_line(path, psz)) return 0;
    strip_path(path);
    if (!path[0]) snprintf(path, psz, "%s", IMPORT_FILE);

    printf("Column number of ROLL NUMBER [1]: ");
    if (read_line(buf, sizeof buf) && atoi(buf) > 0) *id_col = atoi(buf);
    printf("Column number of NAME [2]: ");
    if (read_line(buf, sizeof buf) && atoi(buf) > 0) *name_col = atoi(buf);
    return 1;
}

/* Option 3: ADD students from a CSV. Students already registered are skipped. */
static void menu_import(void)
{
    char path[LINE_LEN];
    int id_col, name_col;
    if (!ask_csv_params(path, sizeof path, &id_col, &name_col)) return;
    import_csv(path, id_col, name_col);
}

static int count_students(void)
{
    FILE *fp = fopen(STUDENT_FILE, "rb");
    if (!fp) return 0;
    Student s;
    int n = 0;
    while (fread(&s, sizeof s, 1, fp) == 1) n++;
    fclose(fp);
    return n;
}

/* REPLACE the whole student list with the CSV. attendance.csv is never touched.
 * The old list is kept as students.bak. If the CSV is unusable, the old list is restored. */
static void replace_students(const char *path, int id_col, int name_col)
{
    int old_count = count_students();
    int had_old = 0;
    FILE *t = fopen(STUDENT_FILE, "rb");
    if (t) { had_old = 1; fclose(t); }

    remove(BACKUP_FILE);
    if (had_old && rename(STUDENT_FILE, BACKUP_FILE) != 0) {
        printf("Error: could not back up %s - nothing was changed.\n", STUDENT_FILE);
        return;
    }

    int valid = import_csv(path, id_col, name_col);
    if (valid <= 0) {                               /* bad file or no usable rows: roll back */
        remove(STUDENT_FILE);
        if (had_old) rename(BACKUP_FILE, STUDENT_FILE);
        printf("No valid students found in the file - the old list was kept (%d students).\n", old_count);
        return;
    }
    printf("\nStudent list replaced: %d students before, %d now.\n", old_count, count_students());
    printf("Attendance records in %s were not changed.\n", ATTEND_FILE);
    if (had_old) printf("The previous list is saved as %s.\n", BACKUP_FILE);
}

static void menu_replace(void)
{
    char path[LINE_LEN], ans[LINE_LEN];
    int id_col, name_col;
    printf("This REPLACES the whole student list with the file you choose.\n");
    printf("Students missing from the file will be removed from the list.\n");
    printf("Attendance records are NOT touched.\n\n");
    if (!ask_csv_params(path, sizeof path, &id_col, &name_col)) return;
    printf("Replace the current student list with '%s'? (y/N): ", path);
    if (!read_line(ans, sizeof ans) || (ans[0] != 'y' && ans[0] != 'Y')) {
        printf("Cancelled.\n");
        return;
    }
    replace_students(path, id_col, name_col);
}

static void menu_list_students(void)
{
    FILE *fp = fopen(STUDENT_FILE, "rb");
    if (!fp) { printf("No students registered yet.\n"); return; }
    Student s;
    int n = 0;
    printf("\n%-20s %s\n", "ID", "NAME");
    printf("---------------------------------------------\n");
    while (fread(&s, sizeof s, 1, fp) == 1) {
        s.id[ID_LEN - 1] = '\0';
        s.name[NAME_LEN - 1] = '\0';
        printf("%-20s %s\n", s.id, s.name);
        n++;
    }
    fclose(fp);
    printf("---------------------------------------------\nTotal: %d\n", n);
}

/* ---------- attendance ledger (CSV) ---------- */

/* Count rows already logged today for this ID (used to alternate IN / OUT). */
static int rows_today(const char *id, const char *date)
{
    FILE *fp = fopen(ATTEND_FILE, "r");
    if (!fp) return 0;
    char line[LINE_LEN * 2];
    int count = 0;
    while (fgets(line, sizeof line, fp)) {
        char *f_date = strtok(line, ",");
        strtok(NULL, ",");                     /* time */
        char *f_id = strtok(NULL, ",");
        if (f_date && f_id && strcmp(f_date, date) == 0 && strcmp(f_id, id) == 0)
            count++;
    }
    fclose(fp);
    return count;
}

/* Old ledgers had no Table/Hours columns: add them so every row lines up in Excel. */
static void upgrade_ledger(void)
{
    FILE *in = fopen(ATTEND_FILE, "r");
    if (!in) return;
    char line[LINE_LEN * 2];
    if (!fgets(line, sizeof line, in) || strstr(line, ",Table")) { fclose(in); return; }

    FILE *out = fopen(LEDGER_TEMP, "w");
    if (!out) { fclose(in); return; }
    fprintf(out, "%s\n", LEDGER_HEADER);
    int ok = 1;
    while (fgets(line, sizeof line, in)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] && fprintf(out, "%s,,\n", line) < 0) ok = 0;
    }
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    if (!ok) { remove(LEDGER_TEMP); return; }
    if (remove(ATTEND_FILE) == 0) rename(LEDGER_TEMP, ATTEND_FILE);
}

/* First scan of the day = IN, second = OUT, third = IN again, ... */
static int is_out_scan(const char *id, const char *date)
{
    return rows_today(id, date) % 2 == 1;
}

static int log_attendance(const Student *s, const char *lab, const char *date, const char *tm_,
                          const char *type, const char *table, const char *hours)
{
    int new_file = 0;
    FILE *chk = fopen(ATTEND_FILE, "r");
    if (!chk) new_file = 1; else fclose(chk);

    FILE *fp = fopen(ATTEND_FILE, "a");
    if (!fp) return 0;
    if (new_file) fprintf(fp, "%s\n", LEDGER_HEADER);
    fprintf(fp, "%s,%s,%s,%s,%s,%s,%s,%s\n", date, tm_, s->id, s->name, lab, type, table, hours);
    fflush(fp);
    int ok = !ferror(fp);
    fclose(fp);
    return ok;
}

static void menu_today_report(void)
{
    FILE *fp = fopen(ATTEND_FILE, "r");
    if (!fp) { printf("No attendance recorded yet.\n"); return; }
    char date[16], tm_[16], line[LINE_LEN * 2];
    now_strings(date, sizeof date, tm_, sizeof tm_);
    int n = 0;
    printf("\nAttendance for %s\n", date);
    printf("---------------------------------------------------------------\n");
    while (fgets(line, sizeof line, fp)) {
        if (strncmp(line, date, strlen(date)) == 0) { fputs(line, stdout); n++; }
    }
    fclose(fp);
    printf("---------------------------------------------------------------\n");
    printf("%d entries today (full ledger: %s)\n", n, ATTEND_FILE);
}

/* ---------- scan session ---------- */

typedef struct {
    char   lab[LAB_LEN];
    char   last_id[ID_LEN];
    time_t last_time;
} Session;

static Session g_session;

static void session_start(const char *lab_arg)
{
    char lab[LINE_LEN];
    if (lab_arg && *lab_arg) {
        snprintf(lab, sizeof lab, "%s", lab_arg);
    } else {
        printf("Lab name / session (e.g. DBMS Lab): ");
        if (!read_line(lab, sizeof lab)) lab[0] = '\0';
    }
    clean_text(lab);
    if (!lab[0]) snprintf(lab, sizeof lab, "Lab");
    snprintf(g_session.lab, sizeof g_session.lab, "%.*s", LAB_LEN - 1, lab);
    g_session.last_id[0] = '\0';
    g_session.last_time = 0;
}

/* Ask for the table number (asked only when the student scans OUT).
 * Returns 1 if valid, 0 if cancelled / input ended. */
static int ask_table(char *out, size_t size)
{
    char buf[LINE_LEN];
    for (;;) {
        printf("      Table number used (or CANCEL): ");
        if (!read_line(buf, sizeof buf)) return 0;
        clean_text(buf);
        if (ci_equal(buf, "CANCEL")) return 0;

        size_t n = strlen(buf);
        int ok = (n >= 1 && n <= 6 && n < size);
        for (size_t i = 0; ok && i < n; i++)
            if (!isalnum((unsigned char)buf[i])) ok = 0;
        if (ok) {
            for (size_t i = 0; i <= n; i++) out[i] = (char)toupper((unsigned char)buf[i]);
            return 1;
        }
        printf("      Table must be 1-6 letters/digits, e.g. 40.\n");
    }
}

/* Ask for the hours attended (2, 3, 1.5 ...). Returns 1 if valid, 0 if cancelled. */
static int ask_hours(char *out, size_t size)
{
    char buf[LINE_LEN];
    for (;;) {
        printf("      Hours attended (e.g. 2 or 3, or CANCEL): ");
        if (!read_line(buf, sizeof buf)) return 0;
        clean_text(buf);
        if (ci_equal(buf, "CANCEL")) return 0;

        char *end;
        double v = strtod(buf, &end);
        if (buf[0] && *end == '\0' && v > 0 && v <= 12) {
            snprintf(out, size, "%g", v);
            return 1;
        }
        printf("      Enter the hours as a number up to 12, e.g. 2 or 3.\n");
    }
}

/* One scan: clean -> verify -> debounce -> IN or OUT.
 *   IN  : just logged.
 *   OUT : the student scans again; the program asks the table number and hours. */
static void handle_scan(const char *raw)
{
    char id[LINE_LEN];
    snprintf(id, sizeof id, "%s", raw);
    sanitize_id(id);
    if (!id[0]) return;

    time_t now = time(NULL);
    if (strcmp(id, g_session.last_id) == 0 && difftime(now, g_session.last_time) < DEBOUNCE_SEC)
        return;                                   /* same card still being held */
    snprintf(g_session.last_id, sizeof g_session.last_id, "%.*s", ID_LEN - 1, id);
    g_session.last_time = now;

    Student s;
    if (!find_student(id, &s)) {
        printf("\a[X] %-16s NOT REGISTERED\n", id);
        return;
    }

    char date[16], when[16], table[16] = "", hours[16] = "";
    now_strings(date, sizeof date, when, sizeof when);   /* time of the scan itself */
    int out_scan = is_out_scan(s.id, date);
    const char *type = out_scan ? "OUT" : "IN";

    printf("\a[%s] %s  %-16s %s\n", type, when, s.id, s.name);

    if (out_scan && (!ask_table(table, sizeof table) || !ask_hours(hours, sizeof hours))) {
        printf("      OUT cancelled - nothing saved. Scan the card again to retry.\n");
        g_session.last_id[0] = '\0';
        return;
    }

    if (!log_attendance(&s, g_session.lab, date, when, type, table, hours)) {
        printf("[!] ERROR writing %s - is it open in Excel?\n", ATTEND_FILE);
        g_session.last_id[0] = '\0';
        return;
    }
    if (out_scan) printf("      Saved: table %s, %s hour(s).\n", table, hours);
    g_session.last_time = time(NULL);
}

/* ---------- USB barcode scanner (keyboard input) ---------- */

static void scanner_mode(void)
{
    session_start(NULL);
    printf("\n=== SCANNING (USB scanner): %s ===\n", g_session.lab);
    printf("1st scan of a student = IN.  2nd scan (when leaving) = OUT: it then asks the\n");
    printf("table number and the hours attended.  Type EXIT to stop.\n\n");

    char line[LINE_LEN];
    while (read_line(line, sizeof line)) {
        sanitize_id(line);
        if (strcmp(line, "EXIT") == 0) break;
        handle_scan(line);
    }
    printf("\nScan session ended.\n");
}

/* ---------- delete all student data (password protected) ---------- */

static void menu_delete_all(void)
{
    char buf[LINE_LEN];
    int n = count_students();
    if (n == 0) { printf("There is no student data to delete.\n"); return; }

    printf("\n*** WARNING ***\n");
    printf("This permanently deletes ALL %d registered students (%s and %s).\n",
           n, STUDENT_FILE, BACKUP_FILE);
    printf("Attendance records in %s are NOT deleted.\n", ATTEND_FILE);
    printf("To continue, type the password exactly, or press Enter to cancel:\n> ");
    if (!read_line(buf, sizeof buf)) return;
    clean_text(buf);
    if (!buf[0]) { printf("Cancelled.\n"); return; }
    if (strcmp(buf, WIPE_PASSWORD) != 0) {
        printf("Wrong password. Nothing was deleted.\n");
        return;
    }
    if (remove(STUDENT_FILE) == 0) {
        remove(BACKUP_FILE);
        remove(TEMP_FILE);
        printf("All student data deleted. Register or import students to start again.\n");
    } else {
        printf("Error: could not delete %s\n", STUDENT_FILE);
    }
}

/* ---------- export attendance for Excel ---------- */

static int ci_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* Copy rows of attendance.csv into a new CSV that Excel opens directly.
 * date_filter: "" = all, "2026-10-14" = one day, "2026-10" = one month.
 * lab_filter : "" = all labs. */
static void export_attendance(const char *out_path, const char *date_filter, const char *lab_filter)
{
    FILE *in = fopen(ATTEND_FILE, "r");
    if (!in) { printf("No attendance recorded yet (%s not found).\n", ATTEND_FILE); return; }
    FILE *out = fopen(out_path, "w");
    if (!out) {
        printf("Cannot create %s (is it open in Excel? use a different name).\n", out_path);
        fclose(in);
        return;
    }
    fputs("\xEF\xBB\xBF", out);                    /* UTF-8 marker so Excel shows names correctly */
    fprintf(out, "%s\n", LEDGER_HEADER);

    char line[LINE_LEN * 2], copy[LINE_LEN * 2];
    int rows = 0;
    size_t dlen = strlen(date_filter);
    while (fgets(line, sizeof line, in)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || strncmp(line, "Date,", 5) == 0) continue;     /* blank / header */
        snprintf(copy, sizeof copy, "%s", line);
        char *f_date = strtok(copy, ",");
        strtok(NULL, ","); strtok(NULL, ","); strtok(NULL, ",");      /* time, id, name */
        char *f_lab = strtok(NULL, ",");
        if (!f_date || !f_lab) continue;
        if (dlen && strncmp(f_date, date_filter, dlen) != 0) continue;
        if (lab_filter[0] && !ci_equal(f_lab, lab_filter)) continue;
        fprintf(out, "%s\n", line);
        rows++;
    }
    fclose(in);
    fclose(out);
    printf("Exported %d attendance rows to %s\n", rows, out_path);
    if (rows == 0) printf("(No rows matched the filters - the file only contains the header.)\n");
    else printf("Open it in Excel, or copy it to a USB drive / email it.\n");
}

static void menu_export(void)
{
    char out[LINE_LEN], date[LINE_LEN], lab[LINE_LEN];

    printf("Export file name [%s]: ", EXPORT_DEFAULT);
    if (!read_line(out, sizeof out)) return;
    strip_path(out);
    if (!out[0]) snprintf(out, sizeof out, "%s", EXPORT_DEFAULT);
    if (ends_with_ci(out, ".xlsx") || ends_with_ci(out, ".xls")) {
        printf("Plain C cannot write .xlsx. Use a .csv name - Excel opens it directly.\n");
        return;
    }
    if (!ends_with_ci(out, ".csv") && strlen(out) + 4 < sizeof out) strcat(out, ".csv");
    if (ci_equal(out, ATTEND_FILE)) {
        printf("That is the live ledger file. Choose a different name.\n");
        return;
    }

    printf("Only one date or month? (e.g. 2026-10-14 or 2026-10; Enter = all dates): ");
    if (!read_line(date, sizeof date)) return;
    clean_text(date);
    printf("Only one lab? (type the lab name; Enter = all labs): ");
    if (!read_line(lab, sizeof lab)) return;
    clean_text(lab);

    export_attendance(out, date, lab);
}

/* ---------- main ---------- */

static void main_menu(void)
{
    char choice[LINE_LEN];
    for (;;) {
        printf("\n===== DIGITAL LOG FOR LAB ENTRIES =====\n");
        printf(" 1. Start scanning (barcode scanner)\n");
        printf(" 2. Register a student\n");
        printf(" 3. Add students from Excel/CSV file (existing ones skipped)\n");
        printf(" 4. List students\n");
        printf(" 5. Today's attendance\n");
        printf(" 6. Remove a student\n");
        printf(" 7. Replace whole student list from CSV\n");
        printf(" 8. Export attendance to CSV (for Excel)\n");
        printf(" 9. Delete ALL student data (password)\n");
        printf("10. Exit\n");
        printf("Choice: ");
        if (!read_line(choice, sizeof choice)) return;
        switch (atoi(choice)) {
            case 1: scanner_mode();        break;
            case 2: menu_add_student();    break;
            case 3: menu_import();         break;
            case 4: menu_list_students();  break;
            case 5: menu_today_report();   break;
            case 6: menu_remove_student(); break;
            case 7: menu_replace();        break;
            case 8: menu_export();         break;
            case 9: menu_delete_all();     break;
            case 10: return;
            default: printf("Invalid choice.\n");
        }
    }
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);          /* output appears instantly */
    upgrade_ledger();

    if (argc >= 3 && strcmp(argv[1], "--import") == 0) {
        import_csv(argv[2], argc >= 4 ? atoi(argv[3]) : 1, argc >= 5 ? atoi(argv[4]) : 2);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "--replace") == 0) {
        replace_students(argv[2], argc >= 4 ? atoi(argv[3]) : 1, argc >= 5 ? atoi(argv[4]) : 2);
        return 0;
    }
    main_menu();
    return 0;
}
