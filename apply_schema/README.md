# Apply Schema Tool

Applies a complete .df schema file to an OpenEdge database.

## Usage

```bash
./apply_schema <schema.df> <target_db>
```

## Examples

```bash
# Apply schema to database
./apply_schema /tmp/sports3.df /var/db/test5/sports

# Apply delta file (incremental changes)
./apply_schema /tmp/changes.df /var/db/prod/mydb
```

## Features

- Applies complete schema definitions to databases
- Works with both full schemas and delta files
- Uses `prodict/load_df.p` wrapper procedure
- Auto-detects single-user vs multi-user mode
- Clean output with success/failure reporting
- Displays all OpenEdge messages during application

## How It Works

Uses the working wrapper procedure for schema application:

```abl
RUN prodict/load_df.p (INPUT "/tmp/schema.df")
```

Note: Uses `load_df.p` wrapper (which DOES accept parameters), not the internal `_load_df.p` procedure.

## Compilation

```bash
gcc -o apply_schema apply_schema.c
```

## Files

- `apply_schema.c` - C source code
- `apply_schema` - Compiled executable

## Return Codes

- `0` - Success
- `1` - Error (invalid parameters, file not found, application failed)

## Notes

- Database must exist before applying schema
- For databases without running servers, uses single-user mode (-1)
- For databases with running servers, connects in multi-user mode
- Can apply both full schemas and incremental deltas
