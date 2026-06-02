#!/bin/python3
"""
Standalone assembler for OpenEdge database schema dumping.
This implementation directly calls OpenEdge tools without requiring the pyoe library.
"""
import os
import sys
import subprocess
import tempfile
from pathlib import Path


def dump_schema(db_path, output_file, tables="ALL", codepage="", dlc="/usr/dlc", timeout=120):
    """
    Dump the schema of an OpenEdge database to a .df file.
    
    This is a standalone implementation that directly invokes OpenEdge's
    _progres binary to run the prodict/dump_df.p procedure.
    """
    # Normalize database path (remove .db extension if present)
    db_path = Path(db_path).with_suffix("")
    
    # Check if database exists
    if not db_path.with_suffix(".db").exists():
        raise FileNotFoundError(f"Database not found: {db_path}.db")
    
    # Create output directory if needed
    output_file = Path(output_file)
    output_file.parent.mkdir(parents=True, exist_ok=True)
    
    # Get codepage from startup.pf if not specified
    if not codepage:
        pf = Path(dlc) / "startup.pf"
        try:
            for line in pf.read_text().splitlines():
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split()
                if len(parts) >= 2 and parts[0] == "-cpstream":
                    codepage = parts[1]
                    break
        except OSError:
            pass
    
    # ABL code to dump schema
    abl_code = f"""\
/* Standalone schema dump */
/* SESSION:PARAMETER = tables|outfile|codepage */
DEFINE VARIABLE cTables   AS CHARACTER NO-UNDO INITIAL "ALL".
DEFINE VARIABLE cOutFile  AS CHARACTER NO-UNDO.
DEFINE VARIABLE cCodePage AS CHARACTER NO-UNDO INITIAL ?.
ASSIGN
    cTables   = ENTRY(1, SESSION:PARAMETER, "|")
    cOutFile  = ENTRY(2, SESSION:PARAMETER, "|")
    cCodePage = ENTRY(3, SESSION:PARAMETER, "|") NO-ERROR.
IF cTables   = "" OR cTables   = ? THEN cTables   = "ALL".
IF cCodePage = "" THEN cCodePage = ?.
RUN prodict/dump_df.p (INPUT cTables, INPUT cOutFile, INPUT cCodePage).
"""
    
    # Write ABL code to temporary file
    with tempfile.NamedTemporaryFile(suffix=".p", mode="w", delete=False) as fh:
        fh.write(abl_code)
        proc_path = fh.name
    
    try:
        # Build command line
        progres = str(Path(dlc) / "bin" / "_progres")
        param = f"{tables}|{output_file}|{codepage}"
        cmd = [
            progres,
            "-db", str(db_path),
            "-1",      # Single-user mode
            "-b",      # Batch mode
            "-p", proc_path,
            "-param", param
        ]
        
        # Set up environment
        env = dict(os.environ)
        env["DLC"] = dlc
        
        # Add tty to PROPATH so prodict/*.r can be found
        tty = str(Path(dlc) / "tty")
        existing_propath = env.get("PROPATH", "")
        if tty not in existing_propath.split(":"):
            env["PROPATH"] = f"{tty}:{existing_propath}" if existing_propath else tty
        
        # Set terminal type
        env.setdefault("PROTERMCAP", str(Path(dlc) / "protermcap"))
        env.setdefault("TERM", "xterm")
        
        # Run OpenEdge batch process
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            env=env,
            timeout=timeout,
        )
        
        if result.returncode != 0:
            raise RuntimeError(
                f"OpenEdge batch process failed (rc={result.returncode}):\n"
                f"{result.stderr.strip()}"
            )
        
        return output_file
        
    finally:
        # Clean up temporary file
        try:
            os.unlink(proc_path)
        except OSError:
            pass


# Main execution
if __name__ == "__main__":
    db_path = sys.argv[1] if len(sys.argv) > 1 else "/var/db/test3/sports"
    df_path = sys.argv[2] if len(sys.argv) > 2 else "/tmp/sports1.df"
    print_flag = sys.argv[3] if len(sys.argv) > 3 else "false"
    
    # Dump the schema
    dump_schema(db_path, df_path)
    
    # Optionally print the output
    with open(df_path, "r") as outputfile:
        if print_flag.lower() == "true":
            print(outputfile.read())
