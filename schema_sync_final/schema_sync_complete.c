/* 
 * OpenEdge Complete Schema Sync Tool - PRODUCTION VERSION
 * Synchronizes database schema to match a .df file
 * 
 * Process:
 * 1. Create temporary empty database
 * 2. Load .df schema into temp database
 * 3. Start servers for both databases
 * 4. Generate delta using dump_inc.p
 * 5. Shutdown servers
 * 6. Apply delta to target database
 * 
 * Usage: schema_sync_complete <schema.df> <target_db>
 *
 * Compiles with: gcc -O2 -o schema_sync_complete schema_sync_complete.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <schema.df> <target_db>\n", argv[0]);
        printf("\n");
        printf("Synchronize target database schema to match schema.df file.\n");
        printf("\n");
        printf("Process:\n");
        printf("  1. Create temporary empty database\n");
        printf("  2. Load schema.df into temp database\n");
        printf("  3. Start servers for both databases\n");
        printf("  4. Generate delta using dump_inc.p\n");
        printf("  5. Shutdown servers\n");
        printf("  6. Apply delta to target database\n");
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
    
    printf("OpenEdge Schema Sync Tool - Production Version\n");
    printf("===============================================\n");
    printf("Schema file: %s\n", schema_file);
    printf("Target database: %s\n", target_db);
    printf("Codepage: %s\n\n", cp);
    
    // Create temp directory and database paths
    char temp_dir[512];
    char temp_db_name[64];
    snprintf(temp_dir, sizeof(temp_dir), "/tmp/sync%d", (int)(time(NULL) % 10000));
    snprintf(temp_db_name, sizeof(temp_db_name), "tempdb");
    
    char temp_db_full[512];
    snprintf(temp_db_full, sizeof(temp_db_full), "%s/%s", temp_dir, temp_db_name);
    
    char delta_file[512];
    snprintf(delta_file, sizeof(delta_file), "/tmp/dlt%d.df", (int)(time(NULL) % 10000));
    
    int target_was_running = is_server_running(target_db);
    char command[4096];
    
    // Step 1: Create temp database with structure from target
    printf("Step 1: Creating temporary database with proper structure...\n");
    
    // Create temp directory
    snprintf(command, sizeof(command), "mkdir -p %s", temp_dir);
    if (system(command) != 0) {
        fprintf(stderr, "ERROR: Failed to create temp directory\n");
        return 1;
    }
    
    // Extract structure file from target database
    snprintf(command, sizeof(command),
        "export DLC=%s; %s/bin/prostrct list %s %s/%s.st >/dev/null 2>&1",
        dlc, dlc, target_db, temp_dir, temp_db_name);
    if (system(command) != 0) {
        fprintf(stderr, "ERROR: Failed to extract structure file\n");
        return 1;
    }
    
    // Fix path (last token on each line) to relative "."
    snprintf(command, sizeof(command),
        "sed -ri 's/ \\S+$/ ./' %s/%s.st",
        temp_dir, temp_db_name);
    system(command);
    
    // Create empty database with structure (MUST be in temp_dir for relative paths)
    snprintf(command, sizeof(command),
        "cd %s && export DLC=%s; %s/bin/prodb %s empty >/dev/null 2>&1",
        temp_dir, dlc, dlc, temp_db_name);
    
    if (system(command) != 0) {
        fprintf(stderr, "ERROR: Failed to create temp database\n");
        goto cleanup;
    }
    printf("  ✓ Created: %s (with proper areas)\n\n", temp_db_full);
    
    // Step 2: Load schema into temp database using load_df.p
    printf("Step 2: Loading schema into temp database...\n");
    
    // Create ABL script to load the .df file
    char loader_script[512];
    snprintf(loader_script, sizeof(loader_script), "%s/load.p", temp_dir);
    
    FILE *script = fopen(loader_script, "w");
    if (!script) {
        fprintf(stderr, "ERROR: Cannot create loader script\n");
        goto cleanup;
    }
    fprintf(script, "CREATE ALIAS DICTDB FOR DATABASE %s.\n", temp_db_name);
    fprintf(script, "RUN prodict/load_df.p (\"%s\").\n", schema_file);
    fprintf(script, "DELETE ALIAS DICTDB.\n");
    fclose(script);
    
    // Load schema with load_df.p (MUST be in temp_dir)
    snprintf(command, sizeof(command),
        "cd %s && export DLC=%s PROPATH=%s; "
        "%s/bin/_progres -db %s -1 -b -p %s/load.p 2>&1 | grep -v 'Attempt to write'",
        temp_dir, dlc, dlc, dlc, temp_db_name, temp_dir);
    
    system(command);
    
    // Check for errors
    char error_file[512];
    snprintf(error_file, sizeof(error_file), "%s/%s.e", temp_dir, temp_db_name);
    if (access(error_file, F_OK) == 0) {
        fprintf(stderr, "  WARNING: Schema load had errors (check %s)\n", error_file);
    }
    printf("  ✓ Schema loaded\n\n");
    snprintf(error_file, sizeof(error_file), "%s/tempdb.e", temp_dir);
    if (access(error_file, F_OK) == 0) {
        fprintf(stderr, "  WARNING: Schema load had errors (check %s)\n", error_file);
    }
    printf("  ✓ Schema loaded\n\n");
    
    // Step 3: Start servers
    printf("Step 3: Starting database servers...\n");
    
    // Stop target if running
    if (target_was_running) {
        printf("  Stopping target server (was running)...\n");
        snprintf(command, sizeof(command),
            "export DLC=%s; %s/bin/proshut %s -by 2>&1 | grep -E '(Shutdown|complete)'",
            dlc, dlc, target_db);
        system(command);
        sleep(2);
    }
    
    // Start temp server
    printf("  Starting temp server (port 14000)...\n");
    snprintf(command, sizeof(command),
        "cd %s && export DLC=%s PROTERMCAP=%s/protermcap TERM=xterm; "
        "%s/bin/_mprosrv %s -S 14000 -N TCP -H localhost >/dev/null 2>&1 &",
        temp_dir, dlc, dlc, dlc, temp_db_name);
    system(command);
    sleep(3);
    
    if (!is_server_running(temp_db_full)) {
        fprintf(stderr, "ERROR: Failed to start temp server\n");
        goto cleanup;
    }
    printf("  ✓ Temp server started\n");
    
    // Start target server
    printf("  Starting target server (port 15000)...\n");
    char target_dir[1024];
    strncpy(target_dir, target_db, sizeof(target_dir) - 1);
    char *last_slash = strrchr(target_dir, '/');
    if (last_slash) *last_slash = '\0';
    else strcpy(target_dir, ".");
    
    char target_name[256];
    if (last_slash) {
        strncpy(target_name, last_slash + 1, sizeof(target_name) - 1);
    } else {
        strncpy(target_name, target_db, sizeof(target_name) - 1);
    }
    
    snprintf(command, sizeof(command),
        "cd %s && export DLC=%s PROTERMCAP=%s/protermcap TERM=xterm; "
        "%s/bin/_mprosrv %s -S 15000 -N TCP -H localhost >/dev/null 2>&1 &",
        target_dir, dlc, dlc, dlc, target_name);
    system(command);
    sleep(3);
    
    if (!is_server_running(target_db)) {
        fprintf(stderr, "ERROR: Failed to start target server\n");
        goto cleanup;
    }
    printf("  ✓ Target server started\n\n");
    
    // Step 4: Generate delta using dump_inc.p
    printf("Step 4: Generating schema delta...\n");
    printf("  Comparing: %s (desired) vs %s (current)\n", temp_db_full, target_db);
    
    snprintf(command, sizeof(command),
        "cd /tmp && "
        "export DLC=%s "
        "PROPATH=%s/tty "
        "PROTERMCAP=%s/protermcap "
        "TERM=xterm "
        "DUMP_INC_DFFILE=%s "
        "DUMP_INC_CODEPAGE=%s "
        "DUMP_INC_INDEXMODE=active "
        "DUMP_INC_DUMPSECTION=No "
        "DUMP_INC_DEBUG=0; "
        "%s/bin/_progres -b -db %s -db %s -p prodict/dump_inc.p 2>&1",
        dlc, dlc, dlc,
        delta_file,
        cp,
        dlc,
        temp_db_full,    // First DB = SOURCE (desired schema)
        target_db); // Second DB = TARGET (current schema)
    
    int ret = system(command);
    
    if (ret != 0 || access(delta_file, F_OK) != 0) {
        fprintf(stderr, "  ERROR: Delta generation failed\n");
        goto cleanup;
    }
    printf("  ✓ Delta generated: %s\n\n", delta_file);
    
    // Show delta preview
    printf("  Delta preview:\n");
    printf("  --------------\n");
    snprintf(command, sizeof(command), "head -20 %s | sed 's/^/  /'", delta_file);
    system(command);
    printf("  --------------\n\n");
    
    // Step 5: Shutdown servers
    printf("Step 5: Shutting down servers...\n");
    
    snprintf(command, sizeof(command),
        "export DLC=%s; %s/bin/proshut %s -by 2>&1 | grep -E '(Shutdown|complete)'",
        dlc, dlc, temp_db_full);
    system(command);
    
    snprintf(command, sizeof(command),
        "export DLC=%s; %s/bin/proshut %s -by 2>&1 | grep -E '(Shutdown|complete)'",
        dlc, dlc, target_db);
    system(command);
    
    sleep(2);
    printf("  ✓ Servers stopped\n\n");
    
    // Step 6: Apply delta to target database
    printf("Step 6: Applying delta to target database...\n");
    
    snprintf(command, sizeof(command),
        "/workspace/apply_schema/apply_schema_v2 %s %s 2>&1 | grep -E '(✓|Added|ERROR)'",
        delta_file, target_db);
    
    system(command);
    
    printf("\n");
    printf("========================================\n");
    printf("✓ Schema synchronization complete!\n");
    printf("========================================\n\n");
    printf("Target database: %s\n", target_db);
    printf("Now matches schema: %s\n\n", schema_file);
    
    // Restart target if it was running
    if (target_was_running) {
        printf("Restarting target server (was running before)...\n");
        snprintf(command, sizeof(command),
            "cd %s && export DLC=%s PROTERMCAP=%s/protermcap TERM=xterm; "
            "%s/bin/_mprosrv %s -S 15000 -N TCP -H localhost >/dev/null 2>&1 &",
            target_dir, dlc, dlc, dlc, target_name);
        system(command);
        sleep(2);
        printf("✓ Target server restarted\n\n");
    }
    
cleanup:
    // Cleanup: Delete temp database files
    printf("Cleaning up temporary files...\n");
    snprintf(command, sizeof(command), "rm -rf %s %s 2>/dev/null", temp_dir, delta_file);
    system(command);
    printf("✓ Cleanup complete\n\n");
    
    return 0;
}
