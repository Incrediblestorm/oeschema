/*
 * C implementation to apply a .df schema file to an OpenEdge database
 * 
 * Usage: ./apply_schema <schema.df> <target_db>
 * 
 * This program directly applies a .df file using prodict/load_df.p
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

// Check if file exists
int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

// Check if database exists (look for .db file)
int db_exists(const char *db_path) {
    char db_file[512];
    snprintf(db_file, sizeof(db_file), "%s.db", db_path);
    return file_exists(db_file);
}

// Check if database has a running server (.lk file exists)
int db_has_server(const char *db_path) {
    char lk_file[512];
    snprintf(lk_file, sizeof(lk_file), "%s.lk", db_path);
    return file_exists(lk_file);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <schema.df> <target_db>\n", argv[0]);
        fprintf(stderr, "\nApplies a .df schema file to an OpenEdge database\n");
        fprintf(stderr, "  schema.df  - Schema definition file to apply\n");
        fprintf(stderr, "  target_db  - Path to target database (without .db extension)\n");
        return 1;
    }

    const char *df_file = argv[1];
    const char *db_path = argv[2];
    const char *dlc = getenv("DLC");
    
    if (!dlc) {
        fprintf(stderr, "Error: DLC environment variable not set\n");
        return 1;
    }

    // Validate inputs
    if (!file_exists(df_file)) {
        fprintf(stderr, "Error: Schema file not found: %s\n", df_file);
        return 1;
    }

    if (!db_exists(db_path)) {
        fprintf(stderr, "Error: Database not found: %s\n", db_path);
        return 1;
    }

    // Get absolute path to .df file
    char abs_df_path[512];
    if (df_file[0] == '/') {
        strcpy(abs_df_path, df_file);
    } else {
        char cwd[512];
        getcwd(cwd, sizeof(cwd));
        snprintf(abs_df_path, sizeof(abs_df_path), "%s/%s", cwd, df_file);
    }

    printf("╔═══════════════════════════════════════════════════╗\n");
    printf("║   OpenEdge Schema Application Tool               ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n\n");
    printf("Schema file: %s\n", abs_df_path);
    printf("Target database: %s\n", db_path);

    // Check if database has running server
    int has_server = db_has_server(db_path);
    const char *mode_flag = has_server ? "" : "-1";
    
    if (!has_server) {
        printf("Database mode: Single-user\n");
    } else {
        printf("Database mode: Multi-user (server running)\n");
    }

    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Applying schema...\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

    // Build command to apply .df file
    char command[2048];
    snprintf(command, sizeof(command),
        "export DLC=%s PROPATH=%s/tty PROTERMCAP=%s/protermcap TERM=xterm; "
        "%s/bin/_progres -db %s %s -b -p %s/tty/abl/prodict/load_df.p -param '%s' 2>&1",
        dlc, dlc, dlc, dlc, db_path, mode_flag, dlc, abs_df_path);

    FILE *fp = popen(command, "r");
    if (!fp) {
        fprintf(stderr, "Error: Failed to execute command\n");
        return 1;
    }

    // Capture output
    char line[512];
    int has_error = 0;
    while (fgets(line, sizeof(line), fp)) {
        printf("%s", line);
        // Check for common error patterns
        if (strstr(line, "Error") || strstr(line, "ERROR") || 
            strstr(line, "error") || strstr(line, "** ")) {
            has_error = 1;
        }
    }

    int ret = pclose(fp);

    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    if (ret == 0 && !has_error) {
        printf("✓ Schema applied successfully!\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        return 0;
    } else {
        printf("✗ Schema application failed (exit code: %d)\n", ret);
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        return 1;
    }
}
