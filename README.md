# OpenEdge Schema Management Tools

**Production-ready compiled C tools for OpenEdge database schema management.**

## Tools

| Tool | Description | Location |
|------|-------------|----------|
| **schema_dump** | Dump a database schema to a `.df` file | `schema_dump/` |
| **schema_compare** | Compare a database schema against a `.df` file | `schema_compare/` |
| **schema_sync** | Sync a `.df` schema file into a target database | `schema_sync_final/` |

## Quick Start

```bash
# Dump a schema to a .df file
./schema_dump/schema_dump_amd64 /var/db/sports

# Compare a database against a .df file
./schema_compare/schema_compare /var/db/prod/sports desired.df
./schema_compare/schema_compare --method quiet /var/db/prod/sports desired.df && echo "in sync"

# Sync a .df schema into a target database (non-destructive)
./schema_sync_final/schema_sync_complete /path/to/desired.df /var/db/target/sports
```

## Directory Structure

```
workspace/
├── README.md
├── schema_dump/                ← Dumps database schema to .df file
│   ├── schema_dump.c
│   └── schema_dump_amd64       ← Compiled executable
│
├── schema_compare/             ← Compares database schema against a .df file
│   ├── schema_compare.c
│   ├── schema_compare          ← Compiled executable
│   └── README.md
│
└── schema_sync_final/          ← Syncs .df schema into target database
    ├── schema_sync_complete.c
    ├── schema_sync_complete    ← Compiled executable
    └── README.md
```

## Requirements

- OpenEdge 12.2.13+
- `DLC` environment variable set (defaults to `/usr/dlc`)
- GCC (only needed for recompilation)

## Compilation

```bash
# schema_dump
gcc -O2 -o schema_dump/schema_dump_amd64 schema_dump/schema_dump.c

# schema_compare
gcc -O2 -o schema_compare/schema_compare schema_compare/schema_compare.c

# schema_sync
gcc -O2 -o schema_sync_final/schema_sync_complete schema_sync_final/schema_sync_complete.c
```
