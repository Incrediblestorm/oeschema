/*
 * schema_compare - Compare an OpenEdge database schema against a .df file
 *
 * Usage:
 *   schema_compare [--method <method>] <target_db> <schema.df>
 *
 * Methods:
 *   1 | diff   - Dump current DB schema and diff against .df, piped to less
 *   2 | delta  - Generate incremental delta via dump_inc.p, print to stdout
 *   3 | quiet  - Exit 0 if schemas match, exit 1 if they differ (no output)
 *
 * Default method: delta
 *
 * Compiles with: gcc -O2 -o schema_compare schema_compare.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

#define METHOD_DIFF  1
#define METHOD_DELTA 2
#define METHOD_QUIET 3

/* -------------------------------------------------------------------------- */
/* Helpers                                                                     */
/* -------------------------------------------------------------------------- */

static const char *get_codepage(const char *dlc) {
    char pf[512];
    snprintf(pf, sizeof(pf), "%s/startup.pf", dlc);
    FILE *fp = fopen(pf, "r");
    if (!fp) return "ISO8859-1";

    static char cp[64] = {0};
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "-cpstream", 9) == 0) {
            p += 9;
            while (*p == ' ' || *p == '\t') p++;
            char *end = p;
            while (*end && *end != ' ' && *end != '\t' && *end != '\n') end++;
            size_t n = end - p;
            if (n > 0 && n < sizeof(cp)) {
                strncpy(cp, p, n);
                cp[n] = '\0';
                fclose(fp);
                return cp;
            }
        }
    }
    fclose(fp);
    return "ISO8859-1";
}

static int db_exists(const char *db) {
    char path[1024];
    snprintf(path, sizeof(path), "%s.db", db);
    return access(path, F_OK) == 0;
}

static int server_running(const char *db) {
    char path[1024];
    snprintf(path, sizeof(path), "%s.lk", db);
    return access(path, F_OK) == 0;
}

/*
 * Returns 1 if delta file contains actual schema changes, 0 if empty/noise-only.
 * Filters "UPDATE INDEX ... DESCRIPTION ?" blocks which are noise caused by
 * NULL vs empty-string description differences between the temp database
 * (created from empty) and target databases.
 */
static int delta_has_changes(const char *delta_file) {
    FILE *fp = fopen(delta_file, "r");
    if (!fp) return 0;

    int has_changes = 0;
    char line[512];
    int in_update_index = 0;   /* are we inside an UPDATE INDEX block? */
    int block_has_real = 0;    /* does current UPDATE INDEX block have real content? */

    while (fgets(line, sizeof(line), fp)) {
        /* Hard structural changes - always real */
        if (strncmp(line, "ADD ",    4) == 0 ||
            strncmp(line, "DROP ",   5) == 0 ||
            strncmp(line, "RENAME ", 7) == 0) {
            has_changes = 1;
            break;
        }

        /* UPDATE FIELD/TABLE/SEQUENCE - real changes */
        if (strncmp(line, "UPDATE TABLE",    12) == 0 ||
            strncmp(line, "UPDATE FIELD",    12) == 0 ||
            strncmp(line, "UPDATE SEQUENCE", 15) == 0) {
            has_changes = 1;
            break;
        }

        /* Track UPDATE INDEX blocks - may be description-only noise */
        if (strncmp(line, "UPDATE INDEX", 12) == 0) {
            if (in_update_index && block_has_real) { has_changes = 1; break; }
            in_update_index = 1;
            block_has_real = 0;
            continue;
        }

        if (in_update_index) {
            /* Empty line or new block starts - close current block */
            if (line[0] == '\n' || line[0] == '\r' || line[0] == '.') {
                if (block_has_real) { has_changes = 1; break; }
                in_update_index = 0;
                continue;
            }
            /* DESCRIPTION ? is noise; anything else is real */
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (strncmp(p, "DESCRIPTION ?", 13) != 0) {
                block_has_real = 1;
            }
        }
    }

    /* Flush last block */
    if (!has_changes && in_update_index && block_has_real)
        has_changes = 1;

    fclose(fp);
    return has_changes;
}

static void usage(const char *prog) {
    printf("Usage: %s [--method <method>] <target_db> <schema.df>\n\n", prog);
    printf("Compare an OpenEdge database schema against a .df file.\n\n");
    printf("Methods:\n");
    printf("  1 | diff   Dump current DB schema and diff against .df file, piped to less\n");
    printf("  2 | delta  Generate incremental delta via dump_inc.p and print it  [default]\n");
    printf("  3 | quiet  Exit 0 if schemas match, exit 1 if they differ (no output)\n\n");
    printf("Examples:\n");
    printf("  %s /var/db/prod/sports desired.df\n", prog);
    printf("  %s --method diff /var/db/prod/sports desired.df\n", prog);
    printf("  %s --method quiet /var/db/prod/sports desired.df && echo 'in sync'\n", prog);
}

/* -------------------------------------------------------------------------- */
/* Method 1: diff                                                              */
/* -------------------------------------------------------------------------- */

static int do_diff(const char *target_db, const char *schema_file,
                   const char *dlc, int pid) {
    char current_df[256];
    snprintf(current_df, sizeof(current_df), "/tmp/cmp_cur_%d.df", pid);

    /* Dump current database schema */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "%s/bin/_progres -db %s -1 -b -p prodict/dump_df.p "
        "-param \"ALL,%s,\" "
        "PROPATH=%s 2>/dev/null",
        dlc, target_db, current_df, dlc);

    /* Use schema_dump_amd64 which we know works */
    snprintf(cmd, sizeof(cmd),
        "/workspace/schema_dump/schema_dump_amd64 %s %s 2>/dev/null",
        target_db, current_df);
    system(cmd);

    if (access(current_df, F_OK) != 0) {
        fprintf(stderr, "ERROR: Failed to dump current database schema\n");
        return 1;
    }

    /* Diff and pipe to less */
    snprintf(cmd, sizeof(cmd),
        "diff --color=always -u %s %s | less -R",
        current_df, schema_file);
    int ret = system(cmd);

    unlink(current_df);

    /* diff exits 0=same, 1=different, 2=error */
    return (ret == 0 || ret == 1) ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Method 2/3: delta via dump_inc.p                                           */
/* -------------------------------------------------------------------------- */

static int do_delta(const char *target_db, const char *schema_file,
                    const char *dlc, const char *cp, int pid,
                    int quiet, int *has_changes_out) {
    char cmd[4096];
    int ret = 0;

    char temp_dir[256];
    char temp_db_name[] = "tempdb";
    char temp_db[512];   /* full path: temp_dir/tempdb */
    char delta_file[256];

    snprintf(temp_dir,   sizeof(temp_dir),   "/tmp/cmp%d",      pid);
    snprintf(temp_db,    sizeof(temp_db),    "%s/%s", temp_dir, temp_db_name);
    snprintf(delta_file, sizeof(delta_file), "/tmp/cmp_dlt%d.df", pid);

    int target_was_running = server_running(target_db);

    /* ---- Step 1: create temp dir ---- */
    if (!quiet) printf("Step 1: Creating temporary database with proper structure...\n");

    snprintf(cmd, sizeof(cmd), "mkdir -p %s", temp_dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "ERROR: Failed to create temp directory\n");
        return 1;
    }

    /* Extract structure file from target */
    snprintf(cmd, sizeof(cmd),
        "export DLC=%s; %s/bin/prostrct list %s %s/%s.st >/dev/null 2>&1",
        dlc, dlc, target_db, temp_dir, temp_db_name);
    if (system(cmd) != 0) {
        fprintf(stderr, "ERROR: prostrct list failed\n");
        ret = 1; goto cleanup;
    }

    /* Fix path (last token on each line) to relative "." */
    snprintf(cmd, sizeof(cmd),
        "sed -ri 's/ \\S+$/ ./' %s/%s.st", temp_dir, temp_db_name);
    system(cmd);

    /* Create empty database - MUST run from temp_dir */
    snprintf(cmd, sizeof(cmd),
        "cd %s && export DLC=%s; %s/bin/prodb %s empty >/dev/null 2>&1",
        temp_dir, dlc, dlc, temp_db_name);
    if (system(cmd) != 0) {
        fprintf(stderr, "ERROR: prodb failed to create temp database\n");
        ret = 1; goto cleanup;
    }
    if (!quiet) printf("  ✓ Temp database created\n\n");

    /* ---- Step 2: load schema into temp database ---- */
    if (!quiet) printf("Step 2: Loading schema into temp database...\n");

    {
        char loader[512];
        snprintf(loader, sizeof(loader), "%s/load.p", temp_dir);
        FILE *fp = fopen(loader, "w");
        if (!fp) { fprintf(stderr, "ERROR: cannot write loader script\n"); ret = 1; goto cleanup; }
        fprintf(fp, "CREATE ALIAS DICTDB FOR DATABASE %s.\n", temp_db_name);
        fprintf(fp, "RUN prodict/load_df.p (\"%s\").\n", schema_file);
        fprintf(fp, "DELETE ALIAS DICTDB.\n");
        fclose(fp);
    }

    snprintf(cmd, sizeof(cmd),
        "cd %s && export DLC=%s PROPATH=%s; "
        "%s/bin/_progres -db %s -1 -b -p %s/load.p 2>&1 | grep -v 'Attempt to write'",
        temp_dir, dlc, dlc, dlc, temp_db_name, temp_dir);
    system(cmd);
    if (!quiet) printf("  ✓ Schema loaded\n\n");

    /* ---- Step 3: start servers ---- */
    if (!quiet) printf("Step 3: Starting database servers...\n");

    if (target_was_running) {
        if (!quiet) printf("  Stopping target server (was running)...\n");
        snprintf(cmd, sizeof(cmd),
            "export DLC=%s; %s/bin/proshut %s -by >/dev/null 2>&1",
            dlc, dlc, target_db);
        system(cmd);
        sleep(2);
    }

    /* Start temp server */
    snprintf(cmd, sizeof(cmd),
        "cd %s && export DLC=%s PROTERMCAP=%s/protermcap TERM=xterm; "
        "%s/bin/_mprosrv %s -S 14000 -N TCP -H localhost >/dev/null 2>&1 &",
        temp_dir, dlc, dlc, dlc, temp_db_name);
    system(cmd);
    sleep(3);

    if (!server_running(temp_db)) {
        fprintf(stderr, "ERROR: Failed to start temp server\n");
        ret = 1; goto cleanup;
    }
    if (!quiet) printf("  ✓ Temp server started\n");

    /* Start target server */
    char target_dir[1024];
    char target_name[256];
    strncpy(target_dir, target_db, sizeof(target_dir) - 1);
    char *slash = strrchr(target_dir, '/');
    if (slash) {
        strncpy(target_name, slash + 1, sizeof(target_name) - 1);
        *slash = '\0';
    } else {
        strncpy(target_name, target_db, sizeof(target_name) - 1);
        strcpy(target_dir, ".");
    }

    snprintf(cmd, sizeof(cmd),
        "cd %s && export DLC=%s PROTERMCAP=%s/protermcap TERM=xterm; "
        "%s/bin/_mprosrv %s -S 15000 -N TCP -H localhost >/dev/null 2>&1 &",
        target_dir, dlc, dlc, dlc, target_name);
    system(cmd);
    sleep(3);

    if (!server_running(target_db)) {
        fprintf(stderr, "ERROR: Failed to start target server\n");
        ret = 1; goto cleanup;
    }
    if (!quiet) printf("  ✓ Target server started\n\n");

    /* ---- Step 4: generate delta ---- */
    if (!quiet) {
        printf("Step 4: Generating delta (desired vs current)...\n");
        printf("  SOURCE: %s\n", temp_db);
        printf("  TARGET: %s\n", target_db);
    }

    snprintf(cmd, sizeof(cmd),
        "export DLC=%s "
        "PROPATH=%s/tty "
        "PROTERMCAP=%s/protermcap "
        "TERM=xterm "
        "DUMP_INC_DFFILE=%s "
        "DUMP_INC_CODEPAGE=%s "
        "DUMP_INC_INDEXMODE=active "
        "DUMP_INC_DUMPSECTION=No "
        "DUMP_INC_DEBUG=0; "
        "%s/bin/_progres -b "
        "-db %s "   /* SOURCE: desired schema */
        "-db %s "   /* TARGET: current schema */
        "-p prodict/dump_inc.p >/dev/null 2>&1",
        dlc, dlc, dlc,
        delta_file, cp, dlc,
        temp_db, target_db);
    system(cmd);

    if (access(delta_file, F_OK) != 0) {
        fprintf(stderr, "ERROR: Delta file not generated\n");
        ret = 1; goto cleanup;
    }

    *has_changes_out = delta_has_changes(delta_file);
    if (!quiet) printf("  ✓ Delta generated\n\n");

    /* ---- Step 5: shutdown servers ---- */
    if (!quiet) printf("Step 5: Shutting down servers...\n");
    snprintf(cmd, sizeof(cmd),
        "export DLC=%s; %s/bin/proshut %s -by 2>&1 | grep -E '(Shutdown|complete)'",
        dlc, dlc, temp_db);
    system(cmd);
    snprintf(cmd, sizeof(cmd),
        "export DLC=%s; %s/bin/proshut %s -by 2>&1 | grep -E '(Shutdown|complete)'",
        dlc, dlc, target_db);
    system(cmd);
    sleep(2);
    if (!quiet) printf("  ✓ Servers stopped\n\n");

    /* Restart target if it was running before we stopped it */
    if (target_was_running) {
        snprintf(cmd, sizeof(cmd),
            "cd %s && export DLC=%s PROTERMCAP=%s/protermcap TERM=xterm; "
            "%s/bin/_mprosrv %s -S 15000 -N TCP -H localhost >/dev/null 2>&1 &",
            target_dir, dlc, dlc, dlc, target_name);
        system(cmd);
        sleep(2);
        if (!quiet) printf("  ✓ Target server restarted\n\n");
    }

    /* ---- Output delta (method 2 only) ---- */
    if (!quiet) {
        if (*has_changes_out) {
            printf("=== Schema Delta ===\n");
            snprintf(cmd, sizeof(cmd), "cat %s", delta_file);
            system(cmd);
            printf("=== End Delta ===\n");
        } else {
            printf("✓ Schemas are identical — no delta\n");
        }
    }

cleanup:
    snprintf(cmd, sizeof(cmd), "rm -rf %s %s 2>/dev/null", temp_dir, delta_file);
    system(cmd);
    return ret;
}

/* -------------------------------------------------------------------------- */
/* main                                                                        */
/* -------------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    int method = METHOD_DELTA;
    const char *target_db  = NULL;
    const char *schema_file = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--method") == 0 || strcmp(argv[i], "-m") == 0) {
            if (++i >= argc) { fprintf(stderr, "ERROR: --method requires a value\n"); return 1; }
            if      (strcmp(argv[i], "1") == 0 || strcmp(argv[i], "diff")  == 0) method = METHOD_DIFF;
            else if (strcmp(argv[i], "2") == 0 || strcmp(argv[i], "delta") == 0) method = METHOD_DELTA;
            else if (strcmp(argv[i], "3") == 0 || strcmp(argv[i], "quiet") == 0) method = METHOD_QUIET;
            else { fprintf(stderr, "ERROR: Unknown method '%s'. Use 1/diff, 2/delta, or 3/quiet\n", argv[i]); return 1; }
        } else if (!target_db) {
            target_db = argv[i];
        } else if (!schema_file) {
            schema_file = argv[i];
        } else {
            fprintf(stderr, "ERROR: Unexpected argument: %s\n", argv[i]);
            return 1;
        }
    }

    if (!target_db || !schema_file) { usage(argv[0]); return 1; }

    /* Validate */
    if (!db_exists(target_db)) {
        fprintf(stderr, "ERROR: Database does not exist: %s\n", target_db);
        return 1;
    }
    if (access(schema_file, F_OK) != 0) {
        fprintf(stderr, "ERROR: Schema file does not exist: %s\n", schema_file);
        return 1;
    }

    const char *dlc = getenv("DLC");
    if (!dlc) dlc = "/usr/dlc";
    const char *cp = get_codepage(dlc);
    int pid = (int)getpid();

    if (method == METHOD_DIFF) {
        return do_diff(target_db, schema_file, dlc, pid);
    }

    int has_changes = 0;
    int quiet = (method == METHOD_QUIET);

    if (!quiet) {
        printf("schema_compare\n");
        printf("==============\n");
        printf("Database:    %s\n", target_db);
        printf("Schema file: %s\n", schema_file);
        printf("Codepage:    %s\n\n", cp);
    }

    int ret = do_delta(target_db, schema_file, dlc, cp, pid, quiet, &has_changes);
    if (ret != 0) return ret;

    if (method == METHOD_QUIET) {
        return has_changes ? 1 : 0;
    }

    return 0;
}
