/* 
 * OpenEdge Schema Sync - Working Version
 * Takes .df file and database, makes them match
 * 
 * Usage: schema_sync_working schema.df target_db
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <schema.df> <target_db>\n\n", argv[0]);
        printf("Synchronize target database to match schema.df\n\n");
        return 1;
    }
    
    const char *schema_file = argv[1];
    const char *target_db = argv[2];
    
    // Validate
    if (access(schema_file, F_OK) != 0) {
        fprintf(stderr, "ERROR: Schema file not found: %s\n", schema_file);
        return 1;
    }
    
    printf("OpenEdge Schema Sync\n");
    printf("====================\n");
    printf("Schema: %s\n", schema_file);
    printf("Target: %s\n\n", target_db);
    
    // Just call apply_schema_v2 - it works!
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "/workspace/apply_schema/apply_schema_v2 %s %s",
        schema_file, target_db);
    
    return system(cmd);
}
