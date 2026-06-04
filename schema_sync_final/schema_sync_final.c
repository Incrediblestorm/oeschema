/* 
 * OpenEdge Schema Sync Tool - Final Complete Implementation
 * Takes a schema .df file and applies it to a target database
 * 
 * Usage: schema_sync source.df target_db
 *
 * Process:
 * 1. Create temporary database
 * 2. Load desired schema from .df file into temp db
 * 3. Generate delta comparing temp db (desired) vs target db (current)
 * 3b. Filter any DROP operations (temp-tables with "tt" prefix are exempt)
 * 4. Apply filtered delta to target database
 * 5. Cleanup
 *
 * Compiles with: gcc -O2 -o schema_sync_final schema_sync_final.c
 *
 * Drop protection:
 *   DROP operations targeting permanent schema objects are blocked. By
 *   convention, temp-tables are identified by a "tt" prefix (case-insensitive)
 *   and are allowed to be dropped. Sequences are always protected.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>

// Read codepage from $DLC/startup.pf
char* get_codepage_from_startup_pf(const char *dlc) {
    char pf_path[512];
    snprintf(pf_path, sizeof(pf_path), "%s/startup.pf", dlc);
    
    FILE *fp = fopen(pf_path, "r");
    if (!fp) return NULL;
    
    static char codepage[64] = {0};
    char line[512];
    
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '#') continue;
        
        if (strncmp(p, "-cpstream", 9) == 0) {
            p += 9;
            while (*p == ' ' || *p == '\t') p++;
            char *end = p;
            while (*end && *end != ' ' && *end != '\t' && *end != '\n' && *end != '\r') end++;
            size_t len = end - p;
            if (len > 0 && len < sizeof(codepage)) {
                strncpy(codepage, p, len);
                codepage[len] = '\0';
                fclose(fp);
                return codepage;
            }
        }
    }
    
    fclose(fp);
    return NULL;
}

// Check if database exists
int db_exists(const char *db_path) {
    char check_path[1024];
    snprintf(check_path, sizeof(check_path), "%s.db", db_path);
    return access(check_path, F_OK) == 0;
}

// Check if database has running server
int db_has_server(const char *db_path) {
    char lk_path[1024];
    snprintf(lk_path, sizeof(lk_path), "%s.lk", db_path);
    return access(lk_path, F_OK) == 0;
}

/*
 * Shut down a running database broker using proshut -by.
 * Waits up to 30 seconds for the broker to stop.
 * Returns 1 on success, 0 on failure.
 */
static int shutdown_db(const char *dlc, const char *db_path) {
    char command[4096];
    snprintf(command, sizeof(command),
        "export DLC=%s; %s/bin/proshut %s -by 2>&1",
        dlc, dlc, db_path);
    system(command);

    /* Wait for .lk file to disappear (up to 30 seconds) */
    for (int i = 0; i < 30; i++) {
        if (!db_has_server(db_path))
            return 1;
        sleep(1);
    }
    fprintf(stderr, "✗ Database did not shut down within 30 seconds: %s\n", db_path);
    return 0;
}

/*
 * Restart a database broker using proserve.
 * Returns 1 on success, 0 on failure.
 */
static int restart_db(const char *dlc, const char *db_path) {
    char command[4096];
    snprintf(command, sizeof(command),
        "export DLC=%s; %s/bin/proserve %s 2>&1",
        dlc, dlc, db_path);
    int ret = system(command);

    /* Wait up to 10 seconds for .lk to appear */
    for (int i = 0; i < 10; i++) {
        if (db_has_server(db_path))
            return 1;
        sleep(1);
    }
    (void)ret;
    fprintf(stderr, "✗ Database did not come back online within 10 seconds: %s\n", db_path);
    return 0;
}

// Returns 1 if the table name is a temp-table by convention (tt prefix).
static int is_temp_table(const char *name) {
    return (strncasecmp(name, "tt", 2) == 0);
}

// Extract the first double-quoted string from a line into buf.
// Returns 1 on success, 0 if no quoted string found.
static int extract_first_quoted(const char *line, char *buf, size_t bufsz) {
    const char *start = strchr(line, '"');
    if (!start) return 0;
    start++;
    const char *end = strchr(start, '"');
    if (!end) return 0;
    size_t len = (size_t)(end - start);
    if (len >= bufsz) len = bufsz - 1;
    strncpy(buf, start, len);
    buf[len] = '\0';
    return 1;
}

/*
 * Filter DROP statements from delta_file, writing a safe copy to filtered_file.
 * Rules:
 *   DROP TABLE "X"         - blocked unless X has "tt" prefix (temp-table)
 *   DROP FIELD "X" OF "T"  - blocked unless T has "tt" prefix
 *   DROP INDEX "X" ON "T"  - blocked unless T has "tt" prefix
 *   DROP SEQUENCE "X"      - always blocked
 *   DROP CONSTRAINT "X" ON "T" - always blocked
 *   Any other DROP form    - blocked (default-deny)
 * Blocked lines are printed with a warning prefix.
 * Returns the number of blocked lines.
 */
static int filter_delta_drops(const char *delta_file, const char *filtered_file) {
    FILE *in = fopen(delta_file, "r");
    if (!in) {
        fprintf(stderr, "✗ Cannot open delta file for filtering: %s\n", delta_file);
        return 0;
    }
    FILE *out = fopen(filtered_file, "w");
    if (!out) {
        fprintf(stderr, "✗ Cannot create filtered delta file: %s\n", filtered_file);
        fclose(in);
        return 0;
    }

    int blocked = 0;
    char line[2048];

    while (fgets(line, sizeof(line), in)) {
        int is_drop = (strncasecmp(line, "DROP ", 5) == 0);
        int is_delete = (strncasecmp(line, "DELETE ", 7) == 0);

        if (!is_drop && !is_delete) {
            fputs(line, out);
            continue;
        }

        const char *rest = is_drop ? line + 5 : line + 7;
        char table_name[256] = "";
        int allow = 0;

        if (strncasecmp(rest, "TABLE ", 6) == 0) {
            if (extract_first_quoted(rest, table_name, sizeof(table_name)) &&
                is_temp_table(table_name))
                allow = 1;
        } else if (strncasecmp(rest, "FIELD ", 6) == 0) {
            /* Format: DROP FIELD "name" OF "tablename" */
            const char *of_pos = strstr(line, " OF \"");
            if (of_pos &&
                extract_first_quoted(of_pos + 4, table_name, sizeof(table_name)) &&
                is_temp_table(table_name))
                allow = 1;
        } else if (strncasecmp(rest, "INDEX ", 6) == 0) {
            /* Format: DROP INDEX "name" ON "tablename" */
            const char *on_pos = strstr(line, " ON \"");
            if (on_pos &&
                extract_first_quoted(on_pos + 4, table_name, sizeof(table_name)) &&
                is_temp_table(table_name))
                allow = 1;
        }
        /* SEQUENCE, CONSTRAINT, and any other DROP forms: allow = 0 (blocked) */

        if (allow) {
            fputs(line, out);
        } else {
            /* Trim trailing newline for display */
            char display[2048];
            strncpy(display, line, sizeof(display) - 1);
            display[sizeof(display) - 1] = '\0';
            size_t len = strlen(display);
            while (len > 0 && (display[len - 1] == '\n' || display[len - 1] == '\r'))
                display[--len] = '\0';
            printf("  ⚠ BLOCKED: %s\n", display);
            blocked++;
        }
    }

    fclose(in);
    fclose(out);
    return blocked;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("OpenEdge Schema Sync Tool\n");
        printf("=========================\n\n");
        printf("Usage: %s <schema.df> <target_db>\n\n", argv[0]);
        printf("Applies a schema definition to a target database:\n");
        printf("  1. Creates temporary database\n");
        printf("  2. Loads desired schema from .df file\n");
        printf("  3. Generates delta (desired vs current)\n");
        printf("  4. Displays delta for review\n");
        printf("  5. Applies delta to target database\n\n");
        printf("Arguments:\n");
        printf("  schema.df  - Schema definition file to apply\n");
        printf("  target_db  - Target database path (without .db)\n\n");
        printf("Example:\n");
        printf("  %s /tmp/sports3.df /var/db/test2/sports\n\n", argv[0]);
        return 1;
    }
    
    const char *schema_file = argv[1];
    const char *target_db = argv[2];
    
    // Get DLC
    const char *dlc = getenv("DLC");
    if (!dlc) dlc = "/usr/dlc";
    
    // Get codepage
    char *codepage = get_codepage_from_startup_pf(dlc);
    const char *cp = codepage ? codepage : "ISO8859-1";
    
    // Validate inputs
    if (access(schema_file, R_OK) != 0) {
        fprintf(stderr, "ERROR: Cannot read schema file: %s\n", schema_file);
        return 1;
    }
    
    if (!db_exists(target_db)) {
        fprintf(stderr, "ERROR: Target database does not exist: %s\n", target_db);
        return 1;
    }

    int was_running = db_has_server(target_db);

    printf("\n");
    printf("╔═══════════════════════════════════════════════════╗\n");
    printf("║   OpenEdge Schema Synchronization Tool           ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n\n");
    printf("Schema file:     %s\n", schema_file);
    printf("Target database: %s\n", target_db);
    printf("Codepage:        %s\n", cp);
    printf("Server running:  %s\n\n", was_running ? "YES" : "NO");
    
    char command[4096];
    char temp_db[256];
    char delta_file[512];
    char script_file[256];
    
    snprintf(temp_db, sizeof(temp_db), "/tmp/tmp%d", getpid());
    snprintf(delta_file, sizeof(delta_file), "/tmp/delta_%d.df", getpid());
    
    // ===========================================
    // STEP 1: Create temporary database
    // ===========================================
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("STEP 1: Creating temporary database\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    snprintf(command, sizeof(command),
        "%s/bin/_dbutil procopy %s/empty %s 2>&1 | tail -5",
        dlc, dlc, temp_db);
    
    if (system(command) != 0) {
        fprintf(stderr, "✗ Failed to create temporary database\n");
        return 1;
    }
    printf("✓ Temporary database created: %s\n\n", temp_db);
    
    // ===========================================
    // STEP 2: Load schema into temp database
    // ===========================================
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("STEP 2: Loading desired schema\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    snprintf(script_file, sizeof(script_file), "/tmp/load_%d.p", getpid());
    FILE *fp = fopen(script_file, "w");
    if (fp) {
        fprintf(fp,
            "OUTPUT TO /tmp/load_log_%d.txt.\n"
            "RUN prodict/load_df.p (INPUT \"%s\") NO-ERROR.\n"
            "IF ERROR-STATUS:ERROR THEN\n"
            "    MESSAGE \"Schema load FAILED: \" + ERROR-STATUS:GET-MESSAGE(1).\n"
            "ELSE\n"
            "    MESSAGE \"✓ Schema loaded successfully\".\n"
            "OUTPUT CLOSE.\n",
            getpid(), schema_file);
        fclose(fp);
        
        snprintf(command, sizeof(command),
            "export DLC=%s PROPATH=%s/tty PROTERMCAP=%s/protermcap TERM=xterm; "
            "%s/bin/_progres -db %s -1 -b -p %s 2>&1 | grep -E '(✓|FAILED)'",
            dlc, dlc, dlc, dlc, temp_db, script_file);
        
        system(command);
        
        // Show full log
        char log_path[256];
        snprintf(log_path, sizeof(log_path), "/tmp/load_log_%d.txt", getpid());
        fp = fopen(log_path, "r");
        if (fp) {
            char line[512];
            while (fgets(line, sizeof(line), fp)) {
                printf("%s", line);
            }
            fclose(fp);
            unlink(log_path);
        }
        unlink(script_file);
    }
    printf("\n");
    
    // ===========================================
    // STEP 3: Generate schema delta
    // ===========================================
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("STEP 3: Generating schema delta\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Comparing: %s (desired) vs %s (current)\n\n", temp_db, target_db);

    /* Connect to target without -1 if broker is running, with -1 if offline */
    const char *dictdb2_flags = db_has_server(target_db) ? "" : "-1";

    snprintf(script_file, sizeof(script_file), "/tmp/delta_%d.p", getpid());
    fp = fopen(script_file, "w");
    if (fp) {
        fprintf(fp,
            "DEFINE VARIABLE hdump AS HANDLE NO-UNDO.\n"
            "OUTPUT TO /tmp/delta_log_%d.txt.\n"
            "CONNECT VALUE(\"%s\") -ld DICTDB2 %s NO-ERROR.\n"
            "IF ERROR-STATUS:ERROR THEN DO:\n"
            "    MESSAGE \"✗ Failed to connect to current database\".\n"
            "    OUTPUT CLOSE.\n"
            "    RETURN.\n"
            "END.\n"
            "RUN prodict/dump_inc.p PERSISTENT SET hdump NO-ERROR.\n"
            "IF ERROR-STATUS:ERROR THEN DO:\n"
            "    MESSAGE \"✗ Failed to run dump_inc.p\".\n"
            "    OUTPUT CLOSE.\n"
            "    DISCONNECT DICTDB2.\n"
            "    RETURN.\n"
            "END.\n"
            "RUN setFileName IN hdump (INPUT \"%s\").\n"
            "RUN setCodePage IN hdump (INPUT \"%s\").\n"
            "RUN setIndexMode IN hdump (INPUT TRUE).\n"
            "RUN doDumpIncr IN hdump NO-ERROR.\n"
            "IF ERROR-STATUS:ERROR THEN\n"
            "    MESSAGE \"✗ Delta generation failed\".\n"
            "ELSE\n"
            "    MESSAGE \"✓ Delta generated successfully\".\n"
            "DELETE OBJECT hdump NO-ERROR.\n"
            "DISCONNECT DICTDB2.\n"
            "OUTPUT CLOSE.\n",
            getpid(), target_db, dictdb2_flags, delta_file, cp);
        fclose(fp);

        snprintf(command, sizeof(command),
            "export DLC=%s PROPATH=%s/tty PROTERMCAP=%s/protermcap TERM=xterm; "
            "%s/bin/_progres -db %s -1 -b -p %s 2>&1 | grep -v '^$'",
            dlc, dlc, dlc, dlc, temp_db, script_file);

        system(command);

        // Show log
        char log_path[256];
        snprintf(log_path, sizeof(log_path), "/tmp/delta_log_%d.txt", getpid());
        fp = fopen(log_path, "r");
        if (fp) {
            char line[512];
            while (fgets(line, sizeof(line), fp)) {
                printf("%s", line);
            }
            fclose(fp);
            unlink(log_path);
        }
        unlink(script_file);
    }
    
    if (access(delta_file, F_OK) != 0) {
        fprintf(stderr, "\n✗ Delta file was not created\n");
        snprintf(command, sizeof(command), "rm -f %s.* 2>&1", temp_db);
        system(command);
        return 1;
    }
    
    // Display delta
    printf("\n");
    printf("════════════════ SCHEMA DELTA ════════════════════\n");
    fp = fopen(delta_file, "r");
    if (fp) {
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            printf("%s", line);
        }
        fclose(fp);
    }
    printf("══════════════════════════════════════════════════\n\n");

    // ===========================================
    // STEP 3b: Drop protection scan
    // ===========================================
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("STEP 3b: Drop protection scan\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Permanent schema objects are protected from DROP.\n");
    printf("Temp-tables (\"tt\" prefix by convention) may be dropped.\n\n");

    char filtered_file[512];
    snprintf(filtered_file, sizeof(filtered_file), "/tmp/delta_%d.filtered.df", getpid());

    int blocked_count = filter_delta_drops(delta_file, filtered_file);

    if (blocked_count > 0) {
        printf("\n⚠  %d DROP operation(s) blocked to protect permanent schema objects.\n", blocked_count);
        printf("   Original delta preserved: %s\n", delta_file);
        printf("   Filtered delta to apply:  %s\n\n", filtered_file);
    } else {
        printf("✓ No DROP operations found. Delta is safe to apply as-is.\n\n");
    }

    /* Step 4 applies the filtered delta (safe even when blocked_count == 0) */
    const char *apply_file = filtered_file;

    // ===========================================
    // STEP 3c: Shut down broker if running
    // ===========================================
    if (db_has_server(target_db)) {
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("STEP 3c: Shutting down database broker\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("Running: proshut %s -by\n\n", target_db);
        if (!shutdown_db(dlc, target_db)) {
            fprintf(stderr, "✗ Cannot proceed: database broker is still running.\n");
            snprintf(command, sizeof(command), "rm -f %s.* 2>&1", temp_db);
            system(command);
            unlink(filtered_file);
            return 1;
        }
        printf("✓ Database broker stopped\n\n");
    }

    // ===========================================
    // STEP 4: Apply delta to target database
    // ===========================================
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("STEP 4: Applying schema delta\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    snprintf(script_file, sizeof(script_file), "/tmp/apply_%d.p", getpid());
    fp = fopen(script_file, "w");
    if (fp) {
        fprintf(fp,
            "OUTPUT TO /tmp/apply_log_%d.txt.\n"
            "MESSAGE \"Applying delta file: %s\".\n"
            "\n"
            "/* Use load_df.p wrapper which accepts parameters */\n"
            "RUN prodict/load_df.p (INPUT \"%s\") NO-ERROR.\n"
            "\n"
            "IF ERROR-STATUS:ERROR THEN DO:\n"
            "    MESSAGE \"✗ Delta application failed:\" ERROR-STATUS:GET-MESSAGE(1).\n"
            "    MESSAGE \"\".\n"
            "    MESSAGE \"NOTE: The filtered delta file: %s\".\n"
            "    MESSAGE \"You may need to apply it manually via Data Dictionary GUI.\".\n"
            "END.\n"
            "ELSE MESSAGE \"✓ Delta applied successfully!\".\n"
            "OUTPUT CLOSE.\n",
                getpid(), apply_file, apply_file, apply_file);
        fclose(fp);

        snprintf(command, sizeof(command),
            "export DLC=%s PROPATH=%s/tty PROTERMCAP=%s/protermcap TERM=xterm; "
            "%s/bin/_progres -db %s -1 -b -p %s 2>&1 | grep -v '^$'",
            dlc, dlc, dlc, dlc, target_db, script_file);

        system(command);

        // Show log
        char log_path[256];
        snprintf(log_path, sizeof(log_path), "/tmp/apply_log_%d.txt", getpid());
        fp = fopen(log_path, "r");
        if (fp) {
            char line[512];
            while (fgets(line, sizeof(line), fp)) {
                printf("%s", line);
            }
            fclose(fp);
            unlink(log_path);
        }
        unlink(script_file);
    }
    printf("\n");

    // ===========================================
    // STEP 5: Cleanup
    // ===========================================
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("STEP 5: Cleanup\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    snprintf(command, sizeof(command), "rm -f %s.* 2>&1 | grep -v '^$'", temp_db);
    system(command);
    unlink(filtered_file);
    printf("✓ Temporary database removed\n\n");

    // ===========================================
    // STEP 6: Restart broker if it was running
    // ===========================================
    if (was_running) {
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("STEP 6: Restarting database broker\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("Running: proserve %s\n\n", target_db);
        if (restart_db(dlc, target_db))
            printf("✓ Database broker restarted\n\n");
        else
            printf("⚠ Database broker did not confirm online — check manually\n\n");
    }
    
    printf("╔═══════════════════════════════════════════════════╗\n");
    printf("║   Schema Synchronization Process Complete        ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n\n");
    printf("Delta file: %s\n", delta_file);
    printf("\n");
    
    return 0;
}
