/* 
 * OpenEdge Schema Comparator - C Implementation
 * Compares two existing databases and generates schema delta
 * 
 * Usage: compare_schemas desired_db current_db output.delta
 *
 * Compiles with: gcc -O2 -o compare_schemas compare_schemas.c
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
    
    printf("OpenEdge Schema Comparator\n");
    printf("==========================\n");
    printf("Desired schema: %s\n", desired_db);
    printf("Current schema: %s\n", current_db);
    printf("Delta output: %s\n", delta_file);
    printf("Codepage: %s\n\n", cp);
    
    // Create ABL script to generate delta
    char script_path[256];
    snprintf(script_path, sizeof(script_path), "/tmp/compare_%d.p", getpid());
    
    FILE *fp = fopen(script_path, "w");
    if (!fp) {
        fprintf(stderr, "ERROR: Cannot create script file\n");
        return 1;
    }
    
    fprintf(fp,
        "DEFINE VARIABLE hdump AS HANDLE NO-UNDO.\n"
        "DEFINE VARIABLE current_db AS CHARACTER NO-UNDO.\n"
        "\n"
        "OUTPUT TO /tmp/compare_log_%d.txt.\n"
        "current_db = \"%s\".\n"
        "\n"
        "/* Connect current database as DICTDB2 for comparison */\n"
        "MESSAGE \"Connecting to\" current_db \"as DICTDB2...\".\n"
        "CONNECT VALUE(current_db) -ld DICTDB2 -1 NO-ERROR.\n"
        "IF ERROR-STATUS:ERROR THEN DO:\n"
        "    MESSAGE \"ERROR: Failed to connect current database\" current_db.\n"
        "    MESSAGE \"Reason:\" ERROR-STATUS:GET-MESSAGE(1).\n"
        "    OUTPUT CLOSE.\n"
        "    RETURN.\n"
        "END.\n"
        "MESSAGE \"✓ Connected\".\n"
        "\n"
        "/* Run dump_inc persistent to get handle */\n"
        "MESSAGE \"Running dump_inc.p persistent...\".\n"
        "RUN prodict/dump_inc.p PERSISTENT SET hdump NO-ERROR.\n"
        "IF ERROR-STATUS:ERROR THEN DO:\n"
        "    MESSAGE \"ERROR: Failed to run dump_inc.p:\" ERROR-STATUS:GET-MESSAGE(1).\n"
        "    OUTPUT CLOSE.\n"
        "    DISCONNECT DICTDB2.\n"
        "    RETURN.\n"
        "END.\n"
        "MESSAGE \"✓ Got handle:\" hdump.\n"
        "\n"
        "/* Set parameters via internal procedures */\n"
        "MESSAGE \"Setting parameters...\".\n"
        "RUN setFileName IN hdump (INPUT \"%s\") NO-ERROR.\n"
        "RUN setCodePage IN hdump (INPUT \"%s\") NO-ERROR.\n"
        "RUN setIndexMode IN hdump (INPUT TRUE) NO-ERROR.\n"
        "MESSAGE \"✓ Parameters set\".\n"
        "\n"
        "/* Generate the delta */\n"
        "MESSAGE \"Generating schema delta...\".\n"
        "RUN doDumpIncr IN hdump NO-ERROR.\n"
        "IF ERROR-STATUS:ERROR THEN\n"
        "    MESSAGE \"ERROR: Delta generation failed:\" ERROR-STATUS:GET-MESSAGE(1).\n"
        "ELSE\n"
        "    MESSAGE \"✓ Delta file generated successfully\".\n"
        "\n"
        "DELETE OBJECT hdump NO-ERROR.\n"
        "DISCONNECT DICTDB2.\n"
        "OUTPUT CLOSE.\n",
        getpid(), current_db, delta_file, cp);
    
    fclose(fp);
    
    // Run the delta generation script
    printf("Comparing schemas...\n");
    
    char command[2048];
    snprintf(command, sizeof(command),
        "export DLC=%s PROPATH=%s/tty PROTERMCAP=%s/protermcap TERM=xterm; "
        "%s/bin/_progres -db %s -1 -b -p %s 2>&1",
        dlc, dlc, dlc, dlc, desired_db, script_path);
    
    int ret = system(command);
    
    // Clean up script
    unlink(script_path);
    
    if (ret != 0) {
        fprintf(stderr, "ERROR: Delta generation script failed\n");
        return 1;
    }
    
    // Verify delta file was created
    if (access(delta_file, F_OK) != 0) {
        fprintf(stderr, "ERROR: Delta file was not created\n");
        return 1;
    }
    
    // Show delta file
    printf("\n========== SCHEMA DELTA ==========\n");
    fp = fopen(delta_file, "r");
    if (fp) {
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            printf("%s", line);
        }
        fclose(fp);
    }
    printf("==================================\n\n");
    
    printf("✓ Delta file created: %s\n", delta_file);
    printf("\nTo apply this delta to %s:\n", current_db);
    printf("  # This step is currently blocked by OpenEdge 12.2.13 parameter issues\n");
    printf("  # Manual application via Data Dictionary is required\n");
    printf("\n");
    
    return 0;
}
