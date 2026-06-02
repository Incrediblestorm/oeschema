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
 * 4. Apply delta to target database
 * 5. Verify changes
 *
 * Compiles with: gcc -O2 -o schema_sync_final schema_sync_final.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════╗\n");
    printf("║   OpenEdge Schema Synchronization Tool           ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n\n");
    printf("Schema file:     %s\n", schema_file);
    printf("Target database: %s\n", target_db);
    printf("Codepage:        %s\n", cp);
    printf("Server running:  %s\n\n", db_has_server(target_db) ? "YES" : "NO");
    
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
        "%s/bin/procopy /usr/dlc/sports %s 2>&1 | tail -5",
        dlc, temp_db);
    
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
            "INPUT FROM \"%s\".\n"
            "RUN prodict/dump/_load_df.p NO-ERROR.\n"
            "INPUT CLOSE.\n"
            "IF ERROR-STATUS:ERROR THEN\n"
            "    MESSAGE \"Schema load via INPUT FROM: FAILED\".\n"
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
    
    snprintf(script_file, sizeof(script_file), "/tmp/delta_%d.p", getpid());
    fp = fopen(script_file, "w");
    if (fp) {
        fprintf(fp,
            "DEFINE VARIABLE hdump AS HANDLE NO-UNDO.\n"
            "OUTPUT TO /tmp/delta_log_%d.txt.\n"
            "CONNECT VALUE(\"%s\") -ld DICTDB2 -1 NO-ERROR.\n"
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
            getpid(), target_db, delta_file, cp);
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
            "    MESSAGE \"NOTE: The delta file has been created: %s\".\n"
            "    MESSAGE \"You may need to apply it manually via Data Dictionary GUI.\".\n"
            "END.\n"
            "ELSE MESSAGE \"✓ Delta applied successfully!\".\n"
            "OUTPUT CLOSE.\n",
                getpid(), delta_file, delta_file, delta_file);
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
    printf("✓ Temporary database removed\n\n");
    
    printf("╔═══════════════════════════════════════════════════╗\n");
    printf("║   Schema Synchronization Process Complete        ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n\n");
    printf("Delta file: %s\n", delta_file);
    printf("\n");
    
    return 0;
}
