# Schema Dump Tool

Dumps complete OpenEdge database schema to .df files.

## Usage

```bash
./schema_dump_amd64 <db_path> <output.df> [print_flag]
```

## Examples

```bash
# Dump sports database schema
./schema_dump_amd64 /var/db/sports /tmp/schema.df false

# Dump with output printing
./schema_dump_amd64 /var/db/test/mydb /tmp/myschema.df true
```

## Features

- Automatic codepage detection from `$DLC/startup.pf`
- Single-user mode operation (uses -1 flag)
- Uses OpenEdge's `prodict/dump_df.p` procedure
- Handles databases with or without running servers

## Compilation

```bash
gcc -o schema_dump_amd64 schema_dump.c
```

## Files

- `schema_dump.c` - C source code
- `schema_dump_amd64` - Compiled AMD64 executable (recommended)
- `schema_dump.s` - Assembly source
- `schema_dump.py` - Original Python implementation
