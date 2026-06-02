/* 
 * OpenEdge Complete Schema Sync Tool - V2
 * Synchronizes database schema to match a .df file
 * 
 * Process:
 * 1. Create temporary empty database
 * 2. Load .df schema into temp database
 * 3. Generate delta (temp vs target) using dump_inc.p
 * 4. Apply delta to target database
 * 
 * Usage: schema_sync_v2 schema.df target_db
 *
 * Compiles with: gcc -O2 -o schema_sync_v2 schema_sync_v2.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

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

// Check if database server is running
int is_server_running(const char *db_path) {
    char lk_path[1024];
    snprintf(lk_path, sizeof(lk_path), "%s.lk", db_path);
    return access(lk_path, F_OK) == 0;
}

// Start database server
int start_server(const char *dlc, const char *db_path) {
    printf("  Starting server for %s...\n", db_path);
    
    char command[2048];
    snprintf(command, sizeof(command),
        "export DLC=%s PROTERMCAP=%s/protermcap TERM=xterm; "
        "%s/bin/_mprosrv %s -S 8888 -N TCP -H localhost -minport 8889 -maxport 8900 >/dev/null 2>&1 &",
        dlc, dlc, dlc, db_path);
    
    int ret = system(command);
    if (ret != 0) {
        fprintf(stderr, "  ERROR: Failed to start server\n");
        return 0;
    }
    
    // Wait for server to start
    sleep(2);
    
    if (is_server_running(db_path)) {
        printf("  ✓ Server started\n");
        return 1;
    }
    
    fprintf(stderr, "  ERROR: Server did not start properly\n");
    return 0;
}

// Stop database server
void stop_server(const char *dlc, const char *db_path) {
    if (!is_server_running(db_path)) return;
    
    printf("  Stopping server for %s...\n", db_path);
    
    char command[2048];
    snprintf(command, sizeof(command),
        "export DLC=%s; %s/bin/proshut %s -by >/dev/null 2>&1",
        dlc, dlc, db_path);
    
    system(command);
    sleep(1);
    printf("  ✓ Server stopped\n");
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <schema.df> <target_db>\n", argv[0]);
        printf("\n");
        printf("Synchronize target database schema to match schema.df file.\n");
        printf("\n");
        printf("Process:\n");
        printf("  1. Create temporary empty database\n");
        printf("  2. Load schema.df into temp database\n");
        printf("  3. Generate delta (temp vs target)\n");
        printf("  4. Apply delta to target database\n");
        printf("\n");
        printf("Arguments:\n");
        printf("  schema.df  - Schema definition file\n");
        printf("  target_db  - Target database path (without .db)\n");
        printf("\n");
        printf("Example:\n");
        printf("  %s /tmp/sports3.df /var/db/test6/sports\n", argv[0]);
        printf("\n");
        return 1;
    }
    
    const char *schema_file = argv[1];
    const char *target_db = argv[2];
    
    // Get DLC
    const char *dlc = getenv("DLC");
    if (!dlc) dlc = "/usr/dlc";
    
    // Validate inputs
    if (access(schema_file, F_OK) != 0) {
        fprintf(stderr, "ERROR: Schema file does not exist: %s\n", schema_file);
        return 1;
    }
    
    if (!db_exists(target_db)) {
        fprintf(stderr, "ERROR: Target database does not exist: %s\n", target_db);
        return 1;
    }
    
    // Get codepage
    char *codepage = get_codepage_from_startup_pf(dlc);
    const char *cp = codepage ? codepage : "ISO8859-1";
    
    printf("OpenEdge Complete Schema Sync Tool (V2)\n");
    printf("========================================\n");
    printf("Schema file: %s\n", schema_file);
    printf("Target database: %s\n", target_db);
    printf("Codepage: %s\n", cp);
    printf("DLC: %s\n\n", dlc);
    
    // Create temp database path
    char temp_db[512];
    snprintf(temp_db, sizeof(temp_db), "/tmp/ss%d", (int)(time(NULL) % 100000));
    
    char delta_file[512];
    snprintf(delta_file, sizeof(delta_file), "%s.delta", temp_db);
    
    int target_server_was_running = is_server_running(target_db);
    int target_server_started = 0;
    int temp_server_started = 0;
    
    // Step 1: Create empty temp database
    printf("Step 1: Creating temporary database...\n");
    char command[2048];
    snprintf(command, sizeof(command),
        "export DLC=%s; %s/bin/prodb %s empty 2>&1",
        dlc, dlc, temp_db);
    
    printf("  Running: prodb %s empty\n", temp_db);
    int create_ret = system(command);
    if (create_ret != 0) {
        fprintf(stderr, "ERROR: Failed to create temp database (exit code %d)\n", create_ret >> 8);
        return 1;
    }
    
    // Truncate BI file
    snprintf(command, sizeof(command),
        "export DLC=%s; %s/bin/_proutil %s -C truncate bi >/dev/null 2>&1",
        dlc, dlc, temp_db);
    system(command);
    
    printf("  ✓ Temp database created: %s\n\n", temp_db);
    
    // Step 2: Load schema into temp database
    printf("Step 2: Loading schema into temp database...\n");
    
    snprintf(command, sizeof(command),
        "export DLC=%s "
        "PROPATH=%s/tty "
        "PROTERMCAP=%s/protermcap "
        "TERM=xterm; "
        "%s/bin/_progres -db %s -1 -b -p prodict/load_df.p -param \"%s\" 2>&1 | head -20",
        dlc, dlc, dlc, dlc, temp_db, schema_file);
    
    printf("  Loading %s...\n", schema_file);
    int ret = system(command);
    
    if (ret != 0) {
        fprintf(stderr, "  WARNING: load_df.p may have issues (exit code %d)\n", ret >> 8);
        fprintf(stderr, "  Attempting to continue...\n");
    } else {
        printf("  ✓ Schema loaded\n");
    }
    printf("\n");
    
    // Step 3: Generate delta using dump_inc.p
    printf("Step 3: Generating schema delta...\n");
    printf("  Note: dump_inc.p requires database servers running\n\n");
    
    // Start temp database server
    printf("  Starting temp database server...\n");
    if (!start_server(dlc, temp_db)) {
        fprintf(stderr, "ERROR: Cannot start temp database server\n");
        goto cleanup;
    }
    temp_server_started = 1;
    
    // Start target database server if not running
    if (!target_server_was_running) {
        printf("  Starting target database server...\n");
        if (!start_server(dlc, target_db)) {
            fprintf(stderr, "ERROR: Cannot start target database server\n");
            goto cleanup;
        }
        target_server_started = 1;
    } else {
        printf("  Target server already running\n");
    }
    
    // Run dump_inc.p with environment variables
    printf("\n  Comparing schemas (temp vs target)...\n");
    snprintf(command, sizeof(command),
        "export DLC=%s "
        "PROPATH=%s/tty "
        "PROTERMCAP=%s/protermcap "
        "TERM=xterm "
        "DUMP_INC_DFFILE=%s "
        "DUMP_INC_CODEPAGE=%s "
        "DUMP_INC_INDEXMODE=active "
        "DUMP_INC_DUMPSECTION=No "
        "DUMP_INC_DEBUG=1; "
        "%s/bin/_progres -b -db %s -db %s -p prodict/dump_inc.p 2>&1",
        dlc, dlc, dlc,
        delta_file,
        cp,
        dlc,
        temp_db,    // First DB = SOURCE (desired)
        target_db); // Second DB = TARGET (current)
    
    ret = system(command);
    
    if (ret != 0) {
        fprintf(stderr, "\n  ERROR: Delta generation failed (exit code %d)\n", ret >> 8);
        goto cleanup;
    }
    
    // Verify delta file was created
    if (access(delta_file, F_OK) != 0) {
        fprintf(stderr, "  ERROR: Delta file was not created\n");
        goto cleanup;
    }
    
    printf("  ✓ Delta file created: %s\n\n", delta_file);
    
    // Show delta preview
    printf("  Delta preview:\n");
    printf("  --------------\n");
    FILE *fp = fopen(delta_file, "r");
    if (fp) {
        char line[1024];
        int line_count = 0;
        while (fgets(line, sizeof(line), fp) && line_count < 20) {
            printf("  %s", line);
            line_count++;
        }
        if (line_count >= 20) printf("  ...\n");
        fclose(fp);
    }
    printf("  --------------\n\n");
    
    // Step 4: Apply delta to target database
    printf("Step 4: Applying delta to target database...\n");
    
    // We need to stop the target server to apply changes in single-user mode
    if (target_server_started) {
        stop_server(dlc, target_db);
        target_server_started = 0;
    }
    
    // Apply using our apply_schema_v2 tool
    snprintf(command, sizeof(command),
        "/workspace/apply_schema/apply_schema_v2 %s %s 2>&1 | grep -E '(✓|ERROR|Added)'",
        delta_file, target_db);
    
    ret = system(command);
    
    if (ret != 0) {
        fprintf(stderr, "\nERROR: Failed to apply delta\n");
        goto cleanup;
    }
    
    printf("\n✓ Schema synchronization complete!\n");
    printf("\nTarget database %s now matches schema from %s\n", target_db, schema_file);
    
cleanup:
    // Stop servers and clean up temp database
    printf("\nCleaning up...\n");
    
    if (temp_server_started) {
        stop_server(dlc, temp_db);
    }
    
    if (target_server_started && !target_server_was_running) {
        stop_server(dlc, target_db);
    }
    
    // Delete temp database files
    char temp_pattern[1024];
    snprintf(temp_pattern, sizeof(temp_pattern), "%s*", temp_db);
    snprintf(command, sizeof(command), "rm -f %s 2>/dev/null", temp_pattern);
    system(command);
    
    // Delete delta file
    unlink(delta_file);
    
    printf("✓ Cleanup complete\n");
    
    return 0;
}
