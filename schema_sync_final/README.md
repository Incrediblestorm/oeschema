# schema_sync

Production-ready tool to synchronize an OpenEdge database schema from a `.df` file.
Safely handles existing databases with live data — never drops or recreates the target.

## Usage

```bash
./schema_sync_complete <schema.df> <target_db_path>
```

## Example

```bash
./schema_sync_complete /path/to/desired.df /var/db/prod/sports
```

## How It Works

1. **Extract structure** — Runs `prostrct list` against the target database to get its area layout
2. **Create temp database** — Creates a fresh empty database in a temp directory using the extracted structure file (so all areas match), then loads the desired `.df` schema into it via `prodict/load_df.p`
3. **Generate delta** — Starts both databases as servers, runs `prodict/dump_inc.p` to produce an incremental `.df` delta (desired vs. current)
4. **Apply delta** — Shuts down both servers, applies the delta to the target using `prodict/load_df.p` in single-user mode
5. **Cleanup** — Removes the temp directory and all generated files

If the target is already in sync with the schema, the delta will be empty and no changes are made.

## Key Technical Details

- Uses `prostrct list` + `sed` to create a relative-path structure file, then `prodb` from within the temp directory so relative paths resolve correctly
- `dump_inc.p` on OpenEdge 12.2.13 requires **environment variables** (not parameters):
  ```bash
  DUMP_INC_DFFILE, DUMP_INC_CODEPAGE, DUMP_INC_INDEXMODE, DUMP_INC_DUMPSECTION
  ```
- First DB connected = SOURCE (desired schema), Second DB = TARGET (current) — order matters
- Both databases must be running as servers for `dump_inc.p` (multi-user mode required)

## Compilation

```bash
gcc -O2 -o schema_sync_complete schema_sync_complete.c
```

## Files

- `schema_sync_complete.c` — Source code
- `schema_sync_complete` — Compiled executable

## Requirements

- OpenEdge 12.2.13+
- `DLC` environment variable set (defaults to `/usr/dlc`)
