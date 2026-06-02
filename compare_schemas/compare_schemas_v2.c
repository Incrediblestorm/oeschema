/* 
 * OpenEdge Schema Comparator - C Implementation (v2)
 * Uses environment variable method for dump_inc.p (OpenEdge 12.2.13+)
 * 
 * Usage: compare_schemas_v2 desired_db current_db output.delta
 *
 * Compiles with: gcc -O2 -o compare_schemas_v2 compare_schemas_v2.c
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

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <desired_db> <current_db> <output.delta>\n", argv[0]);
        printf("\n");
        printf("Compare two OpenEdge databases and generate schema delta.\n");
        printf("Uses environment variable method for dump_inc.p\n");
        printf("\n");
        printf("Arguments:\n");
        printf("  desired_db    - Database with desired schema (without .db)\n");
        printf("  current_db    - Database with current schema (without .db)\n");
        printf("  output.delta  - Output delta file path\n");
        printf("\n");
        printf("Example:\n");
        printf("  %s /tmp/newdb /tmp/olddb /tmp/schema.delta\n", argv[0]);
        printf("\n");
        printf("The delta will show changes needed to transform current_db into desired_db.\n");
        printf("\n");
        return 1;
    }
    
    const char *desired_db = argv[1];
    const char *current_db = argv[2];
    const char *delta_file = argv[3];
    
    // Get DLC
    const char *dlc = getenv("DLC");
    if (!dlc) dlc = "/usr/dlc";
    
    // Validate databases exist
    if (!db_exists(desired_db)) {
        fprintf(stderr, "ERROR: Desired database does not exist: %s\n", desired_db);
        return 1;
    }
    
    if (!db_exists(current_db)) {
        fprintf(stderr, "ERROR: Current database does not exist: %s\n", current_db);
        return 1;
    }
    
    // Get codepage
    char *codepage = get_codepage_from_startup_pf(dlc);
    const char *cp = codepage ? codepage : "ISO8859-1";
    
    printf("OpenEdge Schema Comparator (v2 - Environment Variables)\n");
    printf("========================================================\n");
    printf("Desired schema: %s\n", desired_db);
    printf("Current schema: %s\n", current_db);
    printf("Delta output: %s\n", delta_file);
    printf("Codepage: %s\n\n", cp);
    
    // Generate delta using dump_inc.p with environment variables
    printf("Comparing schemas...\n");
    
    char command[2048];
    snprintf(command, sizeof(command),
        "export DLC=%s "
        "PROPATH=%s/tty "
        "PROTERMCAP=%s/protermcap "
        "TERM=xterm "
        "DUMP_INC_DFFILE=%s "
        "DUMP_INC_CODEPAGE=%s "
        "DUMP_INC_INDEXMODE=active "
        "DUMP_INC_DUMPSECTION=No "
        "DUMP_INC_DEBUG=2; "
        "%s/bin/_progres -b -db %s -db %s -p prodict/dump_inc.p 2>&1",
        dlc, dlc, dlc,
        delta_file,    // DUMP_INC_DFFILE
        cp,            // DUMP_INC_CODEPAGE
        dlc,           // bin path
        desired_db,    // First DB = SOURCE (desired)
        current_db);   // Second DB = TARGET (current)
    
    int ret = system(command);
    
    if (ret != 0) {
        fprintf(stderr, "ERROR: Delta generation failed\n");
        return 1;
    }
    
    // Verify delta file was created
    if (access(delta_file, F_OK) != 0) {
        fprintf(stderr, "ERROR: Delta file was not created\n");
        return 1;
    }
    
    // Show delta file
    printf("\n========== SCHEMA DELTA ==========\n");
    FILE *fp = fopen(delta_file, "r");
    if (fp) {
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            printf("%s", line);
        }
        fclose(fp);
    }
    printf("==================================\n\n");
    
    printf("✓ Delta file created: %s\n", delta_file);
    printf("\nTo apply this delta run:\n");
    printf("  apply_schema %s %s\n\n", delta_file, current_db);
    
    return 0;
}
