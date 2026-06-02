# OpenEdge Schema Management Tools - Compiled C Executables

This repository contains compiled C implementations for OpenEdge database schema management operations.

## Overview

These tools provide command-line utilities for dumping and comparing OpenEdge database schemas, compiled to native AMD64 executables for maximum performance and portability.

## Tools

### 1. **schema_dump** - Schema Dumper
Dumps the complete schema definition from an OpenEdge database to a .df file.

**Usage:**
```bash
./schema_dump [db_path] [df_path] [print_flag]
./schema_dump /var/db/test3/sports /tmp/schema.df false
```

**Features:**
- Automatically reads codepage from `$DLC/startup.pf`
- Uses single-user mode (-1 flag)
- Dumps complete schema using `prodict/dump_df.p`

**Files:**
- `schema_dump.c` - C source code (4.8KB)
- `schema_dump.s` - AMD64 assembly
- `schema_dump_amd64` - Compiled executable (23KB)

---

### 2. **compare_schemas** - Schema Comparator
Compares two existing OpenEdge databases and generates a schema delta file showing differences.

**Usage:**
```bash
./compare_schemas <desired_db> <current_db> <output.delta>
./compare_schemas /tmp/newdb /tmp/olddb /tmp/changes.delta
```

**Features:**
- Compares two existing databases
- Generates incremental delta showing only differences
- Uses persistent handle API for `prodict/dump_inc.p`
- Automatically reads codepage from `$DLC/startup.pf`
- Works in single-user mode (no server required)

**Output:**
```
ADD FIELD "TestField" OF "Customer" AS character
  DESCRIPTION "Test field for schema sync demonstration"
  FORMAT "x(30)"
  POSITION 18
  INITIAL ""
  ORDER 181
  MAX-WIDTH 60
```

**Files:**
- `compare_schemas.c` - C source code (7KB)
- `compare_schemas.s` - AMD64 assembly  
- `compare_schemas` - Compiled executable (23KB)

**Example:**
```bash
# Compare newdb (desired) with olddb (current)
$ ./compare_schemas /tmp/newdb /tmp/olddb /tmp/delta.df

OpenEdge Schema Comparator
==========================
Desired schema: /tmp/newdb
Current schema: /tmp/olddb
Delta output: /tmp/delta.df
Codepage: ISO8859-1

Comparing schemas...
✓ Delta generated successfully

========== SCHEMA DELTA ==========
ADD FIELD "TestField" OF "Customer" AS character
  ...
==================================

✓ Delta file created: /tmp/delta.df
```

---

### 3. **schema_sync_final** - Complete Schema Sync Tool
Comprehensive tool that attempts to apply a schema definition file to a target database through the complete workflow.

**Usage:**
```bash
./schema_sync_final <schema.df> <target_db>
./schema_sync_final /tmp/sports3.df /var/db/test2/sports
```

**Process:**
1. Creates temporary database
2. Attempts to load desired schema from .df file into temp db
3. Generates delta comparing temp db (desired) vs target db (current)
4. Displays delta for review
5. Attempts to apply delta to target database
6. Cleans up temporary database

**Features:**
- Complete end-to-end workflow
- Detailed progress reporting with visual separators
- Automatic cleanup of temporary databases
- Handles both single-user and server modes
- Comprehensive error reporting

**Files:**
- `schema_sync_final.c` - C source code (12.7KB)
- `schema_sync_final.s` - AMD64 assembly
- `schema_sync_final` - Compiled executable (27KB)

---

## OpenEdge 12.2.13 Compatibility Notes

### What Works ✅

1. **Schema Dumping** (`dump_df.p`, `dump_inc.p`)
   - Full schema dumps work perfectly
   - Incremental delta generation works via persistent handles
   - Codepage detection from `startup.pf` works

2. **Delta Generation** (`dump_inc.p` with persistent handles)
   - Comparing two existing databases works flawlessly
   - Uses internal procedures: `setFileName`, `setCodePage`, `setIndexMode`, `doDumpIncr`
   - This is the CORRECT API for OpenEdge 12.2.13

### What Doesn't Work ❌

1. **Schema Loading** (`load_df.p`, `_load_df.p`)
   - All parameter-based approaches fail with error 3234
   - `INPUT FROM` redirection fails with parameter mismatch
   - `-param` command-line flag fails
   - `SESSION:PARAMETER` is read-only and cannot be set

2. **Delta Application** (`load_df.p`)
   - Same issues as schema loading
   - No working programmatic method found in 12.2.13

### Root Cause

OpenEdge 12.2.13 has changed the procedure signatures for `prodict/dump/_load_df.p` and `prodict/load_df.p`. They now reject all parameter-passing methods that previously worked in earlier versions and that the Python `pyoe` library expects.

### Workarounds

For delta application, use manual methods:
1. **Data Dictionary GUI**: `_progres -p _dict` → "Load .df" option
2. **Interactive _dict tool**
3. **ProTop or other admin tools**
4. **SQL DDL statements** (if applicable)

## Key Discovery: Persistent Handle API

The breakthrough was examining the r-code structure of `dump_inc.p`:

```bash
$ strings prodict/dump_inc.r | grep PROC
PROC doDumpIncr 1,,
PROC setDebugMode 1,,1 inc_debug 4 0
PROC setRenameFilename 1,,1 inc_renamefile 1 0
PROC setIndexMode 1,,1 inc_indexmode 1 0
PROC setCodePage 1,,1 inc_codepage 1 0
PROC setFileName 1,,1 inc_dffile 1 0
PROC setSilent 1,,1 setsilent 3 0
```

This revealed that `dump_inc.p` has internal procedures that must be called via persistent handles, not parameters.

**Correct Usage:**
```abl
/* Run dump_inc persistent */
RUN prodict/dump_inc.p PERSISTENT SET hdump.

/* Set parameters via internal procedures */
RUN setFileName IN hdump (INPUT "/tmp/delta.df").
RUN setCodePage IN hdump (INPUT "ISO8859-1").
RUN setIndexMode IN hdump (INPUT TRUE).

/* Generate the delta */
RUN doDumpIncr IN hdump.

DELETE OBJECT hdump.
```

## Compilation

All tools compile with GCC:

```bash
# Compile executables
gcc -O2 -o schema_dump_amd64 schema_dump.c
gcc -O2 -o compare_schemas compare_schemas.c
gcc -O2 -o schema_sync_final schema_sync_final.c

# Generate assembly (optional)
gcc -S -O2 -o schema_dump.s schema_dump.c
gcc -S -O2 -o compare_schemas.s compare_schemas.c
gcc -S -O2 -o schema_sync_final.s schema_sync_final.c
```

## Environment Requirements

- **DLC**: OpenEdge installation directory (default: `/usr/dlc`)
- **PROPATH**: Must include `/usr/dlc/tty` for procedure libraries
- **PROTERMCAP**: Terminal capabilities file at `/usr/dlc/protermcap`
- **TERM**: Terminal type (default: `xterm`)

All tools automatically set these environment variables internally.

## Complete Workflow Example

```bash
# 1. Dump current schema
./schema_dump /var/db/production/sports /tmp/current.df false

# 2. Edit schema file or use modified version
vim /tmp/current.df  # Add new fields, tables, etc.

# 3. Create test database with modified schema
procopy /usr/dlc/sports /tmp/desired
# (manually apply changes to /tmp/desired)

# 4. Generate delta showing what changed
./compare_schemas /tmp/desired /var/db/production/sports /tmp/changes.delta

# 5. Review the delta
cat /tmp/changes.delta

# 6. Apply delta manually using Data Dictionary
_progres -db /var/db/production/sports -p _dict
# Then use GUI to load /tmp/changes.delta
```

## Test Results

Successfully tested with:
- OpenEdge Release 12.2.13 (Thu Nov 2 13:30:51 EDT 2023)
- Sports demonstration database
- Custom schema modifications (added TestField to Customer table)

**Delta Generation Test:**
```bash
$ ./compare_schemas /tmp/newdb /tmp/olddb /tmp/test.delta
✓ Delta file created: /tmp/test.delta

$ cat /tmp/test.delta
ADD FIELD "TestField" OF "Customer" AS character
  DESCRIPTION "Test field for schema sync demonstration"
  FORMAT "x(30)"
  POSITION 18
  INITIAL ""
  ORDER 181
  MAX-WIDTH 60
```

## Files Summary

| File | Size | Description |
|------|------|-------------|
| `schema_dump.c` | 4.8KB | Schema dumper source |
| `schema_dump.s` | 11KB | Schema dumper assembly |
| `schema_dump_amd64` | 23KB | Schema dumper executable |
| `compare_schemas.c` | 7KB | Schema comparator source |
| `compare_schemas.s` | 12KB | Schema comparator assembly |
| `compare_schemas` | 23KB | Schema comparator executable |
| `schema_sync_final.c` | 12.7KB | Complete sync tool source |
| `schema_sync_final.s` | 27KB | Complete sync tool assembly |
| `schema_sync_final` | 27KB | Complete sync tool executable |

## Known Limitations

1. **Schema Loading**: Cannot programmatically load .df files in OpenEdge 12.2.13 due to procedure signature changes
2. **Delta Application**: Cannot programmatically apply delta files due to same issues
3. **Template Database**: Tools use sports database as template for temp databases
4. **Database Names**: Must be ≤11 characters for procopy operations
5. **Multi-Database**: Dump_inc requires both databases connected (desired as DICTDB, current as DICTDB2)

## Future Improvements

Potential enhancements:
1. Find working method for programmatic delta application in 12.2.13
2. Add support for custom template databases
3. Implement schema validation before generation
4. Add diff viewer for delta files
5. Support for partial schema operations (tables-only, indexes-only, etc.)

## License

These tools were created for OpenEdge database administration. The compiled executables contain no proprietary Progress OpenEdge code - they only invoke the standard OpenEdge utilities via command-line execution.

## Author

Generated as part of OpenEdge schema management automation project.

## See Also

- OpenEdge Data Management documentation
- `prodict/dump_df.p` - Full schema dump utility
- `prodict/dump_inc.p` - Incremental delta generation
- `prodict/dump/_load_df.p` - Schema/delta loader
- Python `pyoe` library - Original Python implementation (has compatibility issues with 12.2.13)
