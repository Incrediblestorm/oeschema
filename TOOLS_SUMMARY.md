# OpenEdge Schema Management Tools

## Directory Structure

All tools are organized in subdirectories:

```
workspace/
├── schema_dump/          - Database schema dumper
├── compare_schemas/      - Schema comparison tool
├── apply_schema/         - Schema application tool
└── schema_sync_final/    - Complete workflow tool
```

Each directory contains:
- Source code (`.c`)
- Compiled executable
- README.md with usage instructions

## Compiled Executables (Production Ready)

### 1. **schema_dump_amd64** (23KB)
**Purpose:** Dump complete database schema to .df file

**Location:** `schema_dump/`

**Usage:**
```bash
cd schema_dump
./schema_dump_amd64 <db_path> <output.df> [print_flag]
./schema_dump_amd64 /var/db/sports /tmp/schema.df false
```

**Features:**
- Automatic codepage detection from `$DLC/startup.pf`
- Single-user mode operation
- Uses `prodict/dump_df.p`

**Files:** `schema_dump.c`, `schema_dump_amd64`, `README.md`

---

### 2. **compare_schemas** (23KB)
**Purpose:** Compare two databases and generate schema delta

**Location:** `compare_schemas/`

**Usage:**
```bash
cd compare_schemas
./compare_schemas <desired_db> <current_db> <output_delta.df>
./compare_schemas /tmp/newdb /tmp/olddb /tmp/changes.df
```

**Features:**
- Compares two existing databases
- Generates incremental delta with differences only
- Uses persistent handle API for `dump_inc.p`
- Works in single-user mode (no server required)

**Files:** `compare_schemas.c`, `compare_schemas`, `README.md`

---

### 3. **apply_schema** (26KB)
**Purpose:** Apply a complete .df schema file to a database

**Location:** `apply_schema/`

**Usage:**
```bash
cd apply_schema
./apply_schema <schema.df> <target_db>
./apply_schema /tmp/sports3.df /var/db/test5/sports
```

**Features:**
- Applies complete schema definitions to databases
- Uses `prodict/load_df.p`
- Auto-detects single-user vs multi-user mode
- Clean output with success/failure reporting

**Files:** `apply_schema.c`, `apply_schema`, `README.md`

---

### 4. **schema_sync_final** (27KB)
**Purpose:** Complete workflow - compare and sync schemas from .df to database

**Location:** `schema_sync_final/`

**Usage:**
```bash
cd schema_sync_final
./schema_sync_final <desired_schema.df> <target_db>
./schema_sync_final /tmp/sports3.df /var/db/test5/sports
```

**Features:**
- End-to-end workflow automation
- Creates temp database
- Attempts to load desired schema (currently blocked in OpenEdge 12.2.13)
- Generates delta using persistent handles
- Applies delta to target database

**Limitations:**
- Step 2 (loading .df into temp db) currently fails
- Works for delta application (Steps 3-4)
- For full workflow, use `apply_schema` to apply complete .df files directly

**Files:** `schema_sync_final.c`, `schema_sync_final`, `README.md`

---

## Deprecated/Old Names

The following tools have been renamed:
- ~~mycode~~ → **schema_dump**
- ~~mycode_amd64~~ → **schema_dump_amd64**

All documentation and source code updated to use new naming.

---

## Quick Reference

| Task | Command |
|------|---------|
| Dump schema | `cd schema_dump && ./schema_dump_amd64 /var/db/sports /tmp/schema.df false` |
| Compare DBs | `cd compare_schemas && ./compare_schemas /tmp/new /tmp/old /tmp/delta.df` |
| Apply full schema | `cd apply_schema && ./apply_schema /tmp/schema.df /var/db/target` |
| End-to-end sync | `cd schema_sync_final && ./schema_sync_final /tmp/schema.df /var/db/target` |

---

## Compilation Commands

```bash
# Schema dump tool
cd schema_dump && gcc -o schema_dump_amd64 schema_dump.c

# Schema comparison tool
cd compare_schemas && gcc -o compare_schemas compare_schemas.c

# Schema application tool
cd apply_schema && gcc -o apply_schema apply_schema.c

# Full sync tool
cd schema_sync_final && gcc -o schema_sync_final schema_sync_final.c
```

---

## Environment Requirements

- OpenEdge Release 12.2.13+
- DLC environment variable set
- PROPATH includes $DLC/tty
- PROTERMCAP=$DLC/protermcap
- TERM=xterm

---

## Notes

All tools automatically:
- Read codepage from `$DLC/startup.pf`
- Detect if database server is running (.lk file check)
- Use appropriate connection mode (single-user -1 or multi-user)
- Handle errors gracefully with clear messages
