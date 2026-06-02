/* 
 * OpenEdge Schema Applicator - C Implementation (v2)
 * Applies schema changes by directly manipulating DICTDB tables
 * 
 * Usage: apply_schema_v2 schema.df target_db
 *
 * Compiles with: gcc -O2 -o apply_schema_v2 apply_schema_v2.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

// Check if database server is running
int is_server_running(const char *db_path) {
    char lk_path[1024];
    snprintf(lk_path, sizeof(lk_path), "%s.lk", db_path);
    return access(lk_path, F_OK) == 0;
}

// Parse .df file and generate ABL script to apply changes
int generate_apply_script(const char *df_file, const char *script_path, const char *log_path) {
    FILE *df = fopen(df_file, "r");
    if (!df) {
        fprintf(stderr, "ERROR: Cannot open schema file: %s\n", df_file);
        return 0;
    }
    
    FILE *script = fopen(script_path, "w");
    if (!script) {
        fprintf(stderr, "ERROR: Cannot create script file: %s\n", script_path);
        fclose(df);
        return 0;
    }
    
    // Write ABL script header
    fprintf(script, "/* Auto-generated schema application script */\n");
    fprintf(script, "OUTPUT TO %s.\n", log_path);
    fprintf(script, "MESSAGE \"Starting schema application...\".\n");
    fprintf(script, "MESSAGE \"\".\n\n");
    
    char line[4096];
    char current_table[256] = {0};
    char field_name[256] = {0};
    char field_type[256] = {0};
    char field_format[256] = {0};
    char field_label[256] = {0};
    char field_initial[256] = {0};
    char field_desc[512] = {0};
    int field_order = 0;
    int field_position = 0;
    int field_mandatory = 0;
    int in_field = 0;
    
    while (fgets(line, sizeof(line), df)) {
        // Trim leading/trailing whitespace
        char *p = line;
        while (*p && isspace(*p)) p++;
        if (*p == '\0' || *p == '\n') continue;
        
        // Check for ADD FIELD
        if (strncmp(p, "ADD FIELD", 9) == 0) {
            // Save previous field if any
            if (in_field && current_table[0] && field_name[0]) {
                fprintf(script, "/* Add field %s to table %s */\n", field_name, current_table);
                fprintf(script, "FOR EACH DICTDB._File WHERE DICTDB._File._File-name = \"%s\":\n", current_table);
                fprintf(script, "    CREATE DICTDB._Field.\n");
                fprintf(script, "    ASSIGN\n");
                fprintf(script, "        DICTDB._Field._File-recid = RECID(DICTDB._File)\n");
                fprintf(script, "        DICTDB._Field._Field-name = \"%s\"\n", field_name);
                fprintf(script, "        DICTDB._Field._Data-type = \"%s\"\n", field_type);
                if (field_format[0])
                    fprintf(script, "        DICTDB._Field._Format = \"%s\"\n", field_format);
                if (field_label[0])
                    fprintf(script, "        DICTDB._Field._Label = \"%s\"\n", field_label);
                if (field_initial[0])
                    fprintf(script, "        DICTDB._Field._Initial = \"%s\"\n", field_initial);
                if (field_desc[0])
                    fprintf(script, "        DICTDB._Field._Desc = \"%s\"\n", field_desc);
                if (field_order > 0)
                    fprintf(script, "        DICTDB._Field._Order = %d\n", field_order);
                if (field_position > 0)
                    fprintf(script, "        DICTDB._Field._Field-rpos = %d\n", field_position);
                fprintf(script, "        DICTDB._Field._Mandatory = %s\n", field_mandatory ? "YES" : "NO");
                fprintf(script, "        DICTDB._Field._Extent = 0.\n");
                fprintf(script, "    MESSAGE \"  ✓ Added field %s\".\n", field_name);
                fprintf(script, "END.\n\n");
            }
            
            // Reset for new field
            in_field = 1;
            field_name[0] = field_type[0] = field_format[0] = 0;
            field_label[0] = field_initial[0] = field_desc[0] = 0;
            field_order = field_position = field_mandatory = 0;
            
            // Parse: ADD FIELD "FieldName" OF "TableName" AS type
            char *field_start = strchr(p, '"');
            if (field_start) {
                field_start++;
                char *field_end = strchr(field_start, '"');
                if (field_end) {
                    int len = field_end - field_start;
                    if (len < sizeof(field_name)) {
                        strncpy(field_name, field_start, len);
                        field_name[len] = '\0';
                    }
                    
                    // Find table name
                    char *of_pos = strstr(field_end, "OF");
                    if (of_pos) {
                        char *table_start = strchr(of_pos, '"');
                        if (table_start) {
                            table_start++;
                            char *table_end = strchr(table_start, '"');
                            if (table_end) {
                                len = table_end - table_start;
                                if (len < sizeof(current_table)) {
                                    strncpy(current_table, table_start, len);
                                    current_table[len] = '\0';
                                }
                                
                                // Find type
                                char *as_pos = strstr(table_end, "AS");
                                if (as_pos) {
                                    as_pos += 2;
                                    while (*as_pos && isspace(*as_pos)) as_pos++;
                                    char *type_end = as_pos;
                                    while (*type_end && !isspace(*type_end) && *type_end != '\n') type_end++;
                                    len = type_end - as_pos;
                                    if (len < sizeof(field_type)) {
                                        strncpy(field_type, as_pos, len);
                                        field_type[len] = '\0';
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        // Parse field attributes
        else if (in_field) {
            if (strstr(p, "FORMAT")) {
                char *fmt_start = strchr(p, '"');
                if (fmt_start) {
                    fmt_start++;
                    char *fmt_end = strchr(fmt_start, '"');
                    if (fmt_end) {
                        int len = fmt_end - fmt_start;
                        if (len < sizeof(field_format)) {
                            strncpy(field_format, fmt_start, len);
                            field_format[len] = '\0';
                        }
                    }
                }
            }
            else if (strstr(p, "LABEL")) {
                char *lbl_start = strchr(p, '"');
                if (lbl_start) {
                    lbl_start++;
                    char *lbl_end = strchr(lbl_start, '"');
                    if (lbl_end) {
                        int len = lbl_end - lbl_start;
                        if (len < sizeof(field_label)) {
                            strncpy(field_label, lbl_start, len);
                            field_label[len] = '\0';
                        }
                    }
                }
            }
            else if (strstr(p, "INITIAL")) {
                char *init_start = strchr(p, '"');
                if (init_start) {
                    init_start++;
                    char *init_end = strchr(init_start, '"');
                    if (init_end) {
                        int len = init_end - init_start;
                        if (len < sizeof(field_initial)) {
                            strncpy(field_initial, init_start, len);
                            field_initial[len] = '\0';
                        }
                    }
                }
            }
            else if (strstr(p, "DESCRIPTION")) {
                char *desc_start = strchr(p, '"');
                if (desc_start) {
                    desc_start++;
                    char *desc_end = strchr(desc_start, '"');
                    if (desc_end) {
                        int len = desc_end - desc_start;
                        if (len < sizeof(field_desc)) {
                            strncpy(field_desc, desc_start, len);
                            field_desc[len] = '\0';
                        }
                    }
                }
            }
            else if (strstr(p, "ORDER")) {
                sscanf(p, " ORDER %d", &field_order);
            }
            else if (strstr(p, "POSITION")) {
                sscanf(p, " POSITION %d", &field_position);
            }
            else if (strstr(p, "MANDATORY")) {
                field_mandatory = 1;
            }
        }
    }
    
    // Save last field
    if (in_field && current_table[0] && field_name[0]) {
        fprintf(script, "/* Add field %s to table %s */\n", field_name, current_table);
        fprintf(script, "FOR EACH DICTDB._File WHERE DICTDB._File._File-name = \"%s\":\n", current_table);
        fprintf(script, "    CREATE DICTDB._Field.\n");
        fprintf(script, "    ASSIGN\n");
        fprintf(script, "        DICTDB._Field._File-recid = RECID(DICTDB._File)\n");
        fprintf(script, "        DICTDB._Field._Field-name = \"%s\"\n", field_name);
        fprintf(script, "        DICTDB._Field._Data-type = \"%s\"\n", field_type);
        if (field_format[0])
            fprintf(script, "        DICTDB._Field._Format = \"%s\"\n", field_format);
        if (field_label[0])
            fprintf(script, "        DICTDB._Field._Label = \"%s\"\n", field_label);
        if (field_initial[0])
            fprintf(script, "        DICTDB._Field._Initial = \"%s\"\n", field_initial);
        if (field_desc[0])
            fprintf(script, "        DICTDB._Field._Desc = \"%s\"\n", field_desc);
        if (field_order > 0)
            fprintf(script, "        DICTDB._Field._Order = %d\n", field_order);
        if (field_position > 0)
            fprintf(script, "        DICTDB._Field._Field-rpos = %d\n", field_position);
        fprintf(script, "        DICTDB._Field._Mandatory = %s\n", field_mandatory ? "YES" : "NO");
        fprintf(script, "        DICTDB._Field._Extent = 0.\n");
        fprintf(script, "    MESSAGE \"  ✓ Added field %s\".\n", field_name);
        fprintf(script, "END.\n\n");
    }
    
    fprintf(script, "MESSAGE \"\".\n");
    fprintf(script, "MESSAGE \"Schema application complete.\".\n");
    fprintf(script, "OUTPUT CLOSE.\n");
    fprintf(script, "QUIT.\n");
    
    fclose(df);
    fclose(script);
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <schema.df> <target_db>\n", argv[0]);
        printf("\n");
        printf("Apply schema changes from .df file to target database.\n");
        printf("Works by directly manipulating DICTDB._Field tables.\n");
        printf("\n");
        printf("Arguments:\n");
        printf("  schema.df  - Schema definition file (can be delta or full)\n");
        printf("  target_db  - Target database path (without .db)\n");
        printf("\n");
        printf("Example:\n");
        printf("  %s /tmp/schema.df /var/db/test6/sports\n", argv[0]);
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
    
    // Detect single-user vs multi-user
    int server_running = is_server_running(target_db);
    const char *mode_flag = server_running ? "" : "-1";
    
    printf("OpenEdge Schema Applicator (v2 - Direct Dictionary Manipulation)\n");
    printf("==================================================================\n");
    printf("Schema file: %s\n", schema_file);
    printf("Target database: %s\n", target_db);
    printf("Server status: %s\n", server_running ? "RUNNING (multi-user)" : "STOPPED (single-user)");
    printf("Codepage: %s\n\n", get_codepage_from_startup_pf(dlc) ?: "ISO8859-1");
    
    // Generate ABL script
    char script_path[256], log_path[256];
    snprintf(script_path, sizeof(script_path), "/tmp/apply_%d.p", getpid());
    snprintf(log_path, sizeof(log_path), "/tmp/apply_%d.log", getpid());
    
    printf("Parsing schema file...\n");
    if (!generate_apply_script(schema_file, script_path, log_path)) {
        return 1;
    }
    printf("✓ ABL script generated\n\n");
    
    // Apply schema changes
    printf("Applying schema changes...\n");
    
    char command[2048];
    snprintf(command, sizeof(command),
        "export DLC=%s PROPATH=%s/tty PROTERMCAP=%s/protermcap TERM=xterm; "
        "%s/bin/_progres -db %s %s -b -p %s 2>&1",
        dlc, dlc, dlc, dlc, target_db, mode_flag, script_path);
    
    int ret = system(command);
    
    // Show log
    FILE *log = fopen(log_path, "r");
    if (log) {
        char line[1024];
        while (fgets(line, sizeof(line), log)) {
            printf("%s", line);
        }
        fclose(log);
    }
    
    // Clean up
    unlink(script_path);
    unlink(log_path);
    
    if (ret != 0) {
        fprintf(stderr, "\nERROR: Schema application failed\n");
        return 1;
    }
    
    printf("\n✓ Schema changes applied successfully\n");
    
    return 0;
}
