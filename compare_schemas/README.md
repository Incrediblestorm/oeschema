# Compare Schemas Tool

Compares two OpenEdge databases and generates a schema delta file showing differences.

## Usage

```bash
./compare_schemas <desired_db> <current_db> <output_delta.df>
```

## Examples

```bash
# Compare two databases
./compare_schemas /tmp/newdb /tmp/olddb /tmp/changes.df

# Generate delta for production update
./compare_schemas /dev/schema /prod/db /tmp/prod_update.df
```

## Features

- Compares two existing databases
- Generates incremental delta showing only differences
- Uses persistent handle API for `prodict/dump_inc.p` (works with OpenEdge 12.2.13)
- Automatic codepage detection from `$DLC/startup.pf`
- Works in single-user mode (no server required)
- Auto-starts servers if multi-database comparison requires them

## Output Example

```
ADD FIELD "TestField" OF "Customer" AS character
  DESCRIPTION "Test field for demonstration"
  FORMAT "x(30)"
  POSITION 18
  INITIAL ""
  MAX-WIDTH 60
```

## Compilation

```bash
gcc -o compare_schemas compare_schemas.c
```

## Files

- `compare_schemas.c` - C source code
- `compare_schemas` - Compiled executable
- `compare_schemas.s` - Assembly source

## Technical Notes

This tool uses the **persistent handle API** discovered for OpenEdge 12.2.13:

```abl
RUN prodict/dump_inc.p PERSISTENT SET hdump.
RUN setFileName IN hdump (INPUT "/tmp/delta.df").
RUN setCodePage IN hdump (INPUT "ISO8859-1").
RUN doDumpIncr IN hdump.
DELETE OBJECT hdump.
```

This is the ONLY method that works in OpenEdge 12.2.13 for programmatic delta generation.
