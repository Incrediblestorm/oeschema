# schema_compare

Compare an OpenEdge database schema against a `.df` file.

## Usage

```bash
schema_compare [--method <method>] <target_db> <schema.df>
```

## Methods

| Flag | Method | Description |
|------|--------|-------------|
| `--method 1` or `--method diff` | Diff | Dump current DB schema and diff against `.df`, piped to `less` |
| `--method 2` or `--method delta` | Delta | Generate incremental delta via `dump_inc.p` and print it *(default)* |
| `--method 3` or `--method quiet` | Quiet | Exit `0` if schemas match, exit `1` if they differ — no output |

Short flag: `-m` is also accepted.

## Examples

```bash
# Show what would change (full delta)
schema_compare /var/db/prod/sports desired.df

# Side-by-side diff in less
schema_compare --method diff /var/db/prod/sports desired.df

# Script-friendly check
schema_compare --method quiet /var/db/prod/sports desired.df && echo "in sync" || echo "out of sync"
```

## How It Works (methods 2 and 3)

1. **Extract structure** — `prostrct list` on the target DB to get its area layout
2. **Create temp database** — Fresh empty DB in `/tmp` using the extracted structure, then loads the desired `.df` via `prodict/load_df.p`
3. **Generate delta** — Starts both DBs as servers, runs `prodict/dump_inc.p` (SOURCE = desired, TARGET = current)
4. **Shutdown** — Stops both servers; restarts target if it was running before
5. **Report** — Prints delta (method 2) or returns exit code (method 3)
6. **Cleanup** — Removes temp directory and all generated files

## Notes

- `UPDATE INDEX ... DESCRIPTION ?` entries are filtered from change detection — these are harmless NULL vs empty-string differences caused by how the temp database is initialized and do not represent real schema differences
- If the target DB was running as a server before comparison, it is restarted afterward
- Requires `DLC` environment variable (defaults to `/usr/dlc`)

## Compilation

```bash
gcc -O2 -o schema_compare schema_compare.c
```
