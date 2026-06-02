/* 
 * OpenEdge Schema Delta Generator - C Implementation
 * Generates schema delta between desired schema (.df file) and current database
 * 
 * Usage: schema_delta source.df target_db output.delta
 *
 * Compiles with: gcc -O2 -o schema_delta schema_delta.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>

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

// Generate schema delta using dump_inc with persistent handles
int generate_delta(const char *dlc, const char *desired_df, 
                   const char *current_db, const char *delta_file) {
    
    // Get codepage
    char *codepage = get_codepage_from_startup_pf(dlc);
    const char *cp = codepage ? codepage : "ISO8859-1";
    
    // Create temporary database to hold desired schema (short name)
    char temp_db[256];
    snprintf(temp_db, sizeof(temp_db), "/tmp/des%d", getpid());
    
    char command[4096];
    
    // Step 1: Copy sports database as empty starting point
    snprintf(command, sizeof(command),
        "%s/bin/procopy /usr/dlc/sports %s 2>&1",
        dlc, temp_db);
    
    printf("Creating temporary database %s...\n", temp_db);
    if (system(command) != 0) {
        fprintf(stderr, "ERROR: Failed to create temp database\n");
        return 1;
    }
    
    // Step 2: Load desired schema on top
    printf("Loading desired schema from %s...\n", desired_df);
    
    // Create load script that loads schema via dictionary procedures
    char load_script[256];
    snprintf(load_script, sizeof(load_script), "/tmp/load_%d.p", getpid());
    FILE *lf = fopen(load_script, "w");
    if (!lf) {
        fprintf(stderr, "ERROR: Cannot create load script\n");
        return 1;
    }
    
    fprintf(lf,
        "OUTPUT TO /tmp/load_log_%d.txt.\n"
        "MESSAGE \"Loading schema from %s\".\n"
        "INPUT FROM \"%s\".\n"
        "RUN prodict/dump/_load_df.p NO-ERROR.\n"
        "INPUT CLOSE.\n"
        "IF ERROR-STATUS:ERROR THEN\n"
        "    MESSAGE \"ERROR:\" ERROR-STATUS:GET-MESSAGE(1).\n"
        "ELSE\n"
        "    MESSAGE \"✓ Schema loaded successfully\".\n"
        "OUTPUT CLOSE.\n",
        getpid(), desired_df, desired_df);
    fclose(lf);
    
    snprintf(command, sizeof(command),
        "export DLC=%s PROPATH=%s/tty PROTERMCAP=%s/protermcap TERM=xterm; "
        "%s/bin/_progres -db %s -1 -b -p %s 2>&1",
        dlc, dlc, dlc, dlc, temp_db, load_script);
    
    int load_ret = system(command);
    
    // Check log
    char log_file[256];
    snprintf(log_file, sizeof(log_file), "/tmp/load_log_%d.txt", getpid());
    FILE *log_fp = fopen(log_file, "r");
    if (log_fp) {
        char line[512];
        while (fgets(line, sizeof(line), log_fp)) {
            printf("  %s", line);
        }
        fclose(log_fp);
        unlink(log_file);
    }
    unlink(load_script);
    
    if (load_ret != 0) {
        fprintf(stderr, "ERROR: Failed to load schema\n");
        // Clean up
        snprintf(command, sizeof(command), "rm -f %s.* 2>&1", temp_db);
        system(command);
        return 1;
    }
    
    // Step 3: Create ABL script to generate delta using persistent handles
    char script_path[256];
    snprintf(script_path, sizeof(script_path), "/tmp/gen_delta_%d.p", getpid());
    
    FILE *fp = fopen(script_path, "w");
    if (!fp) {
        fprintf(stderr, "ERROR: Cannot create script file\n");
        return 1;
    }
    
    fprintf(fp,
        "DEFINE VARIABLE hdump AS HANDLE NO-UNDO.\n"
        "DEFINE VARIABLE current_db AS CHARACTER NO-UNDO.\n"
        "\n"
        "current_db = \"%s\".\n"
        "\n"
        "/* Connect current database as DICTDB2 for comparison */\n"
        "CONNECT VALUE(current_db) -ld DICTDB2 -1 NO-ERROR.\n"
        "IF ERROR-STATUS:ERROR THEN DO:\n"
        "    MESSAGE \"ERROR: Failed to connect current database\" current_db.\n"
        "    MESSAGE \"Reason:\" ERROR-STATUS:GET-MESSAGE(1).\n"
        "    RETURN.\n"
        "END.\n"
        "\n"
        "/* Run dump_inc persistent to get handle */\n"
        "RUN prodict/dump_inc.p PERSISTENT SET hdump NO-ERROR.\n"
        "IF ERROR-STATUS:ERROR THEN DO:\n"
        "    MESSAGE \"ERROR: Failed to run dump_inc.p:\" ERROR-STATUS:GET-MESSAGE(1).\n"
        "    DISCONNECT DICTDB2.\n"
        "    RETURN.\n"
        "END.\n"
        "\n"
        "/* Set parameters via internal procedures */\n"
        "RUN setFileName IN hdump (INPUT \"%s\") NO-ERROR.\n"
        "RUN setCodePage IN hdump (INPUT \"%s\") NO-ERROR.\n"
        "RUN setIndexMode IN hdump (INPUT TRUE) NO-ERROR.\n"
        "\n"
        "/* Generate the delta */\n"
        "RUN doDumpIncr IN hdump NO-ERROR.\n"
        "IF ERROR-STATUS:ERROR THEN\n"
        "    MESSAGE \"ERROR: Delta generation failed:\" ERROR-STATUS:GET-MESSAGE(1).\n"
        "ELSE\n"
        "    MESSAGE \"✓ Delta file generated successfully\".\n"
        "\n"
        "DELETE OBJECT hdump NO-ERROR.\n"
        "DISCONNECT DICTDB2.\n",
        current_db, delta_file, cp);
    
    fclose(fp);
    
    // Step 4: Run the delta generation script
    printf("Generating delta file...\n");
    printf("  Desired schema: %s (in temp db)\n", temp_db);
    printf("  Current schema: %s\n", current_db);
    printf("  Output: %s\n", delta_file);
    
    snprintf(command, sizeof(command),
        "export DLC=%s PROPATH=%s/tty PROTERMCAP=%s/protermcap TERM=xterm; "
        "%s/bin/_progres -db %s -1 -b -p %s 2>&1",
        dlc, dlc, dlc, dlc, temp_db, script_path);
    
    int ret = system(command);
    
    // Clean up
    unlink(script_path);
    snprintf(command, sizeof(command), "rm -f %s.* 2>&1", temp_db);
    system(command);
    
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
    
    return 0;
}

void print_usage(const char *prog) {
    printf("Usage: %s <source.df> <target_db> [output.delta]\n", prog);
    printf("\n");
    printf("Generate schema delta between desired schema and current database.\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  source.df     - Desired schema definition file\n");
    printf("  target_db     - Current database path (without .db extension)\n");
    printf("  output.delta  - Output delta file (default: schema.delta)\n");
    printf("\n");
    printf("Example:\n");
    printf("  %s /tmp/sports3.df /var/db/test2/sports\n", prog);
    printf("  %s /tmp/sports3.df /tmp/olddb /tmp/changes.delta\n", prog);
    printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *source_df = argv[1];
    const char *target_db = argv[2];
    const char *delta_file = (argc > 3) ? argv[3] : "schema.delta";
    
    // Get DLC
    const char *dlc = getenv("DLC");
    if (!dlc) dlc = "/usr/dlc";
    
    // Validate inputs
    if (access(source_df, R_OK) != 0) {
        fprintf(stderr, "ERROR: Cannot read source schema file: %s\n", source_df);
        return 1;
    }
    
    if (!db_exists(target_db)) {
        fprintf(stderr, "ERROR: Target database does not exist: %s\n", target_db);
        return 1;
    }
    
    printf("OpenEdge Schema Delta Generator\n");
    printf("================================\n");
    printf("Desired schema: %s\n", source_df);
    printf("Current database: %s\n", target_db);
    printf("Delta output: %s\n\n", delta_file);
    
    return generate_delta(dlc, source_df, target_db, delta_file);
}
