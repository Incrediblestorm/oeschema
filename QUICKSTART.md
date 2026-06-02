# OpenEdge Schema Tools - Quick Start

## Overview

Four compiled C tools for OpenEdge database schema management, organized in subdirectories:

```
📁 schema_dump/        - Dump database schemas to .df files
📁 compare_schemas/    - Compare databases and generate deltas
📁 apply_schema/       - Apply .df files to databases
📁 schema_sync_final/  - Complete workflow automation
```

## Common Tasks

### 1. Dump a Database Schema

```bash
cd schema_dump
./schema_dump_amd64 /var/db/sports /tmp/sports_schema.df false
```

### 2. Compare Two Databases

```bash
cd compare_schemas
./compare_schemas /tmp/newdb /tmp/olddb /tmp/changes.df
```

### 3. Apply Schema to Database

```bash
cd apply_schema
./apply_schema /tmp/sports3.df /var/db/test5/sports
```

### 4. Complete Workflow (with limitations)

```bash
cd schema_sync_final
./schema_sync_final /tmp/desired_schema.df /var/db/target/db
```

**Note:** Step 2 of schema_sync_final fails in OpenEdge 12.2.13. Use `apply_schema` for direct .df application.

## Typical Workflows

### Scenario 1: Update Production Database

```bash
# 1. Dump development schema
cd schema_dump
./schema_dump_amd64 /dev/myapp /tmp/dev_schema.df false

# 2. Generate delta vs production
cd ../compare_schemas
./compare_schemas /dev/myapp /prod/myapp /tmp/prod_delta.df

# 3. Review delta file
cat /tmp/prod_delta.df

# 4. Apply delta to production
cd ../apply_schema
./apply_schema /tmp/prod_delta.df /prod/myapp
```

### Scenario 2: Apply Complete Schema

```bash
# Simply apply the .df file directly
cd apply_schema
./apply_schema /path/to/schema.df /path/to/database
```

### Scenario 3: Clone Database Schema

```bash
# 1. Dump source schema
cd schema_dump
./schema_dump_amd64 /source/db /tmp/schema.df false

# 2. Create empty target database
procopy /usr/dlc/empty /target/newdb

# 3. Apply schema
cd ../apply_schema
./apply_schema /tmp/schema.df /target/newdb
```

## Environment

All tools require:
- `DLC` environment variable set
- OpenEdge 12.2.13+ installed
- Database files accessible

Tools automatically:
- Read codepage from `$DLC/startup.pf`
- Detect if database server is running
- Use appropriate connection mode

## Documentation

Each directory contains:
- `README.md` - Detailed usage and examples
- Source code (`.c`)
- Compiled executable

## Compilation

To recompile any tool:

```bash
cd <tool_directory>
gcc -o <tool_name> <tool_name>.c
```

## Quick Reference

| Tool | Purpose | Input | Output |
|------|---------|-------|--------|
| schema_dump | Export schema | Database | .df file |
| compare_schemas | Find differences | 2 Databases | Delta .df |
| apply_schema | Update schema | .df file + DB | Updated DB |
| schema_sync_final | Auto workflow | .df file + DB | Updated DB |

## Support

- Full documentation: `TOOLS_SUMMARY.md`
- Technical details: `SCHEMA_TOOLS_README.md`
- Each tool: See `<directory>/README.md`
