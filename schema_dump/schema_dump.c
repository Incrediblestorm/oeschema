/* 
 * C implementation of schema_dump.py for AMD64 Linux
 * Compiles with GCC
 * Usage: ./schema_dump [db_path] [df_path] [print_flag]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <strings.h>

// Read codepage from $DLC/startup.pf
char* get_codepage_from_startup_pf(const char *dlc) {
    char pf_path[512];
    snprintf(pf_path, sizeof(pf_path), "%s/startup.pf", dlc);
    
    FILE *fp = fopen(pf_path, "r");
    if (!fp) return NULL;
    
    static char codepage[64] = {0};
    char line[512];
    
    while (fgets(line, sizeof(line), fp)) {
        // Skip empty lines and comments
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '#') continue;
        
        // Look for -cpstream parameter
        if (strncmp(p, "-cpstream", 9) == 0) {
            p += 9;
            while (*p == ' ' || *p == '\t') p++;
            // Copy the codepage value
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

/* Return 1 if the database broker is running (lock file present), 0 otherwise */
static int db_is_running(const char *db_path) {
    char lk_path[512];
    snprintf(lk_path, sizeof(lk_path), "%s.lk", db_path);
    return (access(lk_path, F_OK) == 0);
}

#define ABL_CODE \
"DEFINE VARIABLE cTables AS CHARACTER NO-UNDO INITIAL \"ALL\".\n" \
"DEFINE VARIABLE cOutFile AS CHARACTER NO-UNDO.\n" \
"DEFINE VARIABLE cCodePage AS CHARACTER NO-UNDO INITIAL ?.\n" \
"ASSIGN cTables = ENTRY(1, SESSION:PARAMETER, \"|\")\n" \
"       cOutFile = ENTRY(2, SESSION:PARAMETER, \"|\")\n" \
"       cCodePage = ENTRY(3, SESSION:PARAMETER, \"|\") NO-ERROR.\n" \
"IF cTables = \"\" OR cTables = ? THEN cTables = \"ALL\".\n" \
"IF cCodePage = \"\" THEN cCodePage = ?.\n" \
"RUN prodict/dump_df.p (INPUT cTables, INPUT cOutFile, INPUT cCodePage).\n"

int main(int argc, char *argv[]) {
    const char *db_path = (argc > 1) ? argv[1] : "/var/db/test3/sports";
    const char *df_path = (argc > 2) ? argv[2] : "/tmp/sports1.df";
    const char *print_flag = (argc > 3) ? argv[3] : "false";
    
    // Get codepage from startup.pf
    const char *dlc = getenv("DLC");
    if (!dlc) dlc = "/usr/dlc";
    
    char *codepage = get_codepage_from_startup_pf(dlc);
    const char *cp_param = codepage ? codepage : "";

    /* Use single-user mode only when the broker is not running */
    const char *single_user = db_is_running(db_path) ? "" : "-1 ";
    
    // Create temporary file for ABL procedure
    char tmp_path[64];
    strcpy(tmp_path, "/tmp/dumpXXXXXX");
    int fd = mkstemp(tmp_path);
    if (fd < 0) {
        perror("mkstemp failed");
        return 1;
    }
    
    // Rename to add .p extension
    char final_path[128];
    snprintf(final_path, sizeof(final_path), "%s.p", tmp_path);
    rename(tmp_path, final_path);
    strcpy(tmp_path, final_path);
    
    // Write ABL code to temp file
    if (write(fd, ABL_CODE, strlen(ABL_CODE)) < 0) {
        perror("write failed");
        close(fd);
        unlink(tmp_path);
        return 1;
    }
    close(fd);
    
    // Build command to execute _progres
    char command[2048];
    snprintf(command, sizeof(command),
        "export DLC=%s PROPATH=%s/tty PROTERMCAP=%s/protermcap TERM=xterm; "
        "%s/bin/_progres -db %s %s-b -p %s -param 'ALL|%s|%s' 2>&1",
        dlc, dlc, dlc, dlc, db_path, single_user, tmp_path, df_path, cp_param);
    
    // Execute command
    int ret = system(command);
    
    // Clean up temp file
    unlink(tmp_path);
    
    // If command failed, exit
    if (ret != 0) {
        fprintf(stderr, "OpenEdge command failed with exit code %d\n", ret);
        return 1;
    }
    
    // Optionally print the output file
    if (strcasecmp(print_flag, "true") == 0) {
        FILE *fp = fopen(df_path, "r");
        if (fp) {
            char buffer[8192];
            size_t n;
            while ((n = fread(buffer, 1, sizeof(buffer) - 1, fp)) > 0) {
                buffer[n] = '\0';
                printf("%s", buffer);
            }
            fclose(fp);
        }
    }
    
    return 0;
}
