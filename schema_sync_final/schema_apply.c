/*
 * OpenEdge Schema Application Tool
 * Applies a .df schema file to a target database using delta approach
 * 
 * Usage: ./schema_apply <source_df> <target_db>
 * 
 * Process:
 * 1. Create temporary database
 * 2. Load desired schema from .df into temp
 * 3. Generate incremental delta comparing target->temp
 * 4. Display delta
 * 5. Apply delta to target database
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

// Read codepage from $DLC/startup.pf
char* get_codepage(const char *dlc) {
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

// Check if database has a server running
int db_has_server(const char *db_path) {
    char lk_path[1024];
    snprintf(lk_path, sizeof(lk_path), "%s.lk", db_path);
    struct stat st;
    return (stat(lk_path, &st) == 0);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <source_df> <target_db>\n", argv[0]);
        fprintf(stderr, "  source_df: Path to desired schema .df file\n");
        fprintf(stderr, "  target_db: Path to target database (without .db extension)\n");
        return 1;
    }
    
    const char *source_df = argv[1];
    const char *target_db = argv[2];
    
    // Get DLC
    const char *dlc = getenv("DLC");
    if (!dlc) dlc = "/usr/dlc";
    
    char *codepage = get_codepage(dlc);
    const char *cp = codepage ? codepage : "ISO8859-1";
    
    printf("=== OpenEdge Schema Application ===\n");
    printf("Source .df:  %s\n", source_df);
    printf("Target DB:   %s\n", target_db);
    printf("Codepage:    %s\n", cp);
    printf("\n");
    
    // Check if target database has server
    int target_has_server = db_has_server(target_db);
    printf("Target database server: %s\n", target_has_server ? "RUNNING" : "NOT RUNNING");
    
    // Shutdown target server if running (required for schema changes)
    if (target_has_server) {
        printf("Shutting down target database server...\n");
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "%s/bin/proshut %s -by 2>&1", dlc, target_db);
        system(cmd);
        sleep(2);
    }
    
    // Step 1: Create temporary database for desired schema
    printf("\n=== Step 1: Creating temporary database ===\n");
    char temp_db[] = "/tmp/schema_temp";
    char cmd[4096];
    
    // Remove old temp files
    snprintf(cmd, sizeof(cmd), "rm -f %s.* 2>/dev/null", temp_db);
    system(cmd);
    
    // Create from empty template
    snprintf(cmd, sizeof(cmd), "%s/bin/prodb %s empty 2>&1 | grep -E '(Database|copied)'", 
             dlc, temp_db);
    if (system(cmd) != 0) {
        fprintf(stderr, "Failed to create temporary database\n");
        return 1;
    }
    
    // Step 2: Load desired schema into temp database
    printf("\n=== Step 2: Loading schema from %s ===\n", source_df);
    
    // Create ABL loader
    char loader_path[64];
    strcpy(loader_path, "/tmp/load_XXXXXX");
    int fd = mkstemp(loader_path);
    
    // Rename to add .p extension
    char final_path[128];
    snprintf(final_path, sizeof(final_path), "%s.p", loader_path);
    rename(loader_path, final_path);
    strcpy(loader_path, final_path);
    
    // Reopen for writing
    fd = open(loader_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        perror("mkstemp failed");
        return 1;
    }
    
    const char *load_abl = 
        "DEFINE VARIABLE cInFile AS CHARACTER NO-UNDO.\n"
        "cInFile = SESSION:PARAMETER.\n"
        "OUTPUT TO /tmp/load_result.txt.\n"
        "MESSAGE \"Loading\" cInFile.\n"
        "RUN prodict/load_df_silent.p (INPUT cInFile).\n"
        "MESSAGE \"Loaded\".\n"
        "OUTPUT CLOSE.\n";
    
    write(fd, load_abl, strlen(load_abl));
    close(fd);
    
    snprintf(cmd, sizeof(cmd),
        "export DLC=%s PROPATH=%s/tty PROTERMCAP=%s/protermcap TERM=xterm; "
        "%s/bin/_progres -db %s -1 -b -p %s -param '%s' 2>&1",
        dlc, dlc, dlc, dlc, temp_db, loader_path, source_df);
    
    int load_result = system(cmd);
    unlink(loader_path);
    
    if (load_result != 0) {
        fprintf(stderr, "Warning: Schema load may have failed (exit %d)\n", load_result);
        // Continue anyway - may still work
    }
    
    // Step 3: Generate delta file
    printf("\n=== Step 3: Generating schema delta ===\n");
    printf("Comparing: %s (current) -> %s (desired)\n", target_db, temp_db);
    
    char delta_path[] = "/tmp/schema.delta";
    
    // Create dump_inc.in control file
    FILE *control = fopen("/tmp/dump_inc.in", "w");
    if (!control) {
        perror("Failed to create dump_inc.in");
        return 1;
    }
    fprintf(control, "%s\n%s\nyes\nno\n", temp_db, delta_path);
    fclose(control);
    
    // Run dump_inc
    snprintf(cmd, sizeof(cmd),
        "cd /tmp && export DLC=%s PROPATH=%s/tty; "
        "%s/bin/_progres -b -db %s -1 -p prodict/dump_inc.p 2>&1",
        dlc, dlc, dlc, target_db);
    
    system(cmd);
    
    // Step 4: Display delta
    printf("\n=== Step 4: Schema Delta File ===\n");
    FILE *delta_fp = fopen(delta_path, "r");
    if (!delta_fp) {
        fprintf(stderr, "ERROR: Delta file was not generated\n");
        fprintf(stderr, "This may indicate:\n");
        fprintf(stderr, "  - Schema load failed\n");
        fprintf(stderr, "  - dump_inc.p not compatible with this OpenEdge version\n");
        fprintf(stderr, "  - No schema differences detected\n");
        
        // Clean up
        snprintf(cmd, sizeof(cmd), "rm -f %s.*", temp_db);
        system(cmd);
        return 1;
    }
    
    printf("--- BEGIN DELTA ---\n");
    char buffer[8192];
    while (fgets(buffer, sizeof(buffer), delta_fp)) {
        printf("%s", buffer);
    }
    printf("--- END DELTA ---\n");
    fclose(delta_fp);
    
    // Step 5: Apply delta
    printf("\n=== Step 5: Apply delta to target database? ===\n");
    printf("This will modify %s\n", target_db);
    printf("Apply changes? (yes/no): ");
    
    char response[10];
    if (fgets(response, sizeof(response), stdin)) {
        if (strncmp(response, "yes", 3) == 0) {
            printf("Applying delta...\n");
            
            // Create loader for delta
            char delta_loader[64];
            strcpy(delta_loader, "/tmp/apply_XXXXXX");
            fd = mkstemp(delta_loader);
            
            // Rename to add .p extension
            char delta_final[128];
            snprintf(delta_final, sizeof(delta_final), "%s.p", delta_loader);
            rename(delta_loader, delta_final);
            strcpy(delta_loader, delta_final);
            
            // Reopen for writing
            fd = open(delta_loader, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd < 0) {
                perror("mkstemp failed");
                return 1;
            }
            
            const char *apply_abl =
                "DEFINE VARIABLE cDelta AS CHARACTER NO-UNDO.\n"
                "cDelta = SESSION:PARAMETER.\n"
                "OUTPUT TO /tmp/apply_result.txt.\n"
                "MESSAGE \"Applying\" cDelta.\n"
                "RUN prodict/load_df_silent.p (INPUT cDelta).\n"
                "MESSAGE \"Applied\".\n"
                "OUTPUT CLOSE.\n";
            
            write(fd, apply_abl, strlen(apply_abl));
            close(fd);
            
            snprintf(cmd, sizeof(cmd),
                "export DLC=%s PROPATH=%s/tty PROTERMCAP=%s/protermcap TERM=xterm; "
                "%s/bin/_progres -db %s -1 -b -p %s -param '%s' 2>&1",
                dlc, dlc, dlc, dlc, target_db, delta_loader, delta_path);
            
            int apply_result = system(cmd);
            unlink(delta_loader);
            
            if (apply_result == 0) {
                printf("✓ Schema changes applied successfully\n");
            } else {
                fprintf(stderr, "✗ Failed to apply schema changes (exit %d)\n", apply_result);
            }
        } else {
            printf("Skipped - no changes applied\n");
        }
    }
    
    // Clean up temp database
    printf("\n=== Cleanup ===\n");
    snprintf(cmd, sizeof(cmd), "rm -f %s.* /tmp/dump_inc.in %s 2>/dev/null", 
             temp_db, delta_path);
    system(cmd);
    printf("Temporary files removed\n");
    
    // Restart target server if it was running
    if (target_has_server) {
        printf("Restarting target database server...\n");
        snprintf(cmd, sizeof(cmd), "%s/bin/proserve %s -S 10301 2>&1 | grep -E 'Started|BROKER'",
                 dlc, target_db);
        system(cmd);
    }
    
    printf("\nDone.\n");
    return 0;
}
