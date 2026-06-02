# Schema Sync - Compiled C Implementation

## Overview
This is a compiled C implementation of the pyoe schema synchronization workflow. It applies a desired schema (from a .df file) to a target OpenEdge database by creating a delta and applying it.

## Files Generated
- **schema_sync.c** - C source code (11KB)
- **schema_sync.s** - AMD64 assembly source (20KB)
- **schema_sync_amd64** - Compiled executable (28KB)
- **schema_sync_c** - C compiled version for verification (31KB)

## Usage

```bash
./schema_sync_amd64 <desired_schema.df> <target_db>
```

### Parameters
1. **desired_schema.df** - Path to .df file containing the desired schema
2. **target_db** - Path to the database to update (without .db extension)

### Example

```bash
# Apply a schema from a .df file to a database
./schema_sync_amd64 /tmp/desired_schema.df /var/db/test3/mydb
```

## How It Works

The program implements the full schema synchronization workflow from `pyoe.schema.applier.sync_schema`:

1. **Create Temporary Database**
   - Uses `procopy` to create a temporary empty database from `$DLC/empty`

2. **Load Desired Schema**
   - Loads the desired .df file into the temporary database using `prodict/load_df_silent.p`

3. **Generate Schema Delta**
   - Connects both databases (target as DICTDB, temp as DICTDB2)
   - Runs `prodict/dump_inc.p` to compare schemas
   - Generates a delta .df with ADD/UPDATE/DELETE/RENAME statements

4. **Apply Delta**
   - If delta is not empty, applies it to the target database
   - Uses `prodict/load_df_silent.p` to apply changes

5. **Cleanup**
   - Removes temporary database files
   - Saves final delta to `<target_db>_schema_delta.df` for review

## Features

- **Automatic Codepage Detection**: Reads `-cpstream` from `$DLC/startup.pf`
- **Minimal Changes**: Only applies differences, not full schema rebuild
- **Safe**: Creates delta file first, shows what would change
- **Clean**: Automatically cleans up temporary files
- **DLC Support**: Respects `$DLC` environment variable

## Output

The program saves the delta file to `<target_db>_schema_delta.df` so you can:
- Review what changes were made
- Apply the same changes to other databases
- Keep a record of schema modifications

### Example Output

```
Syncing schema from /tmp/desired_schema.df to /var/db/test3/mydb
Creating temporary database...
Loading desired schema into temporary database...
Generating schema delta...
Applying delta to target database (1523 bytes)...
Schema sync complete. Delta applied.
Delta file saved to: /var/db/test3/mydb_schema_delta.df
```

If schemas are identical:
```
Syncing schema from /tmp/desired_schema.df to /var/db/test3/mydb
Creating temporary database...
Loading desired schema into temporary database...
Generating schema delta...
Schemas are identical. No changes needed.
Delta file saved to: /var/db/test3/mydb_schema_delta.df
```

## Error Handling

The program checks for:
- Target database existence
- Schema .df file existence
- OpenEdge installation (`$DLC/empty` template)
- All OpenEdge batch operations return codes

## Dependencies

- OpenEdge installation (uses `$DLC` or `/usr/dlc`)
- Required OpenEdge binaries:
  - `procopy` - for creating empty database
  - `_progres` - for running ABL procedures
- Required ABL procedures (in `prodict.pl`):
  - `prodict/load_df_silent.p`
  - `prodict/dump_inc.p`

## Technical Details

### ABL Procedures Used

**prodict/load_df_silent.p**
```abl
DEFINE VARIABLE cInFile AS CHARACTER NO-UNDO.
cInFile = SESSION:PARAMETER.
RUN prodict/load_df_silent.p (INPUT cInFile).
```

**prodict/dump_inc.p**
```abl
DEFINE VARIABLE cTables AS CHARACTER NO-UNDO INITIAL "ALL".
DEFINE VARIABLE cOutFile AS CHARACTER NO-UNDO.
DEFINE VARIABLE cCodePage AS CHARACTER NO-UNDO INITIAL ?.
ASSIGN cTables = ENTRY(1, SESSION:PARAMETER, "|")
       cOutFile = ENTRY(2, SESSION:PARAMETER, "|")
       cCodePage = ENTRY(3, SESSION:PARAMETER, "|") NO-ERROR.
IF cTables = "" OR cTables = ? THEN cTables = "ALL".
IF cCodePage = "" THEN cCodePage = ?.
RUN prodict/dump_inc.p (INPUT cTables, INPUT cOutFile, INPUT cCodePage).
```

### Connection Convention
- **DICTDB** (first -db): The current/target database to be altered
- **DICTDB2** (second -db): The desired schema database

### Compilation

```bash
# Generate assembly
gcc -S -O2 schema_sync.c -o schema_sync.s

# Compile assembly to executable
gcc schema_sync.s -o schema_sync_amd64

# Or compile C directly
gcc -O2 schema_sync.c -o schema_sync_c
```

## Relationship to Python Implementation

This C implementation mirrors the Python workflow from:
- `pyoe.schema.applier.sync_schema()` - Main workflow
- `pyoe.schema.applier.apply_df()` - Apply .df to database
- `pyoe.schema.comparator.make_delta()` - Generate delta
- `pyoe.db.creator.create_empty_db()` - Create empty database
- `pyoe._oe.OERunner` - OpenEdge batch execution

## Checksums

```
a6085c0d4a24d535ae94b26a2f6d7248  schema_sync.c
05df9f06925b5b79d7ada14beddb07c8  schema_sync.s
12121171e8e53209d2782ed5636ef0fc  schema_sync_amd64
```
