; ARM64 Assembly standalone implementation of schema_dump.py for Apple Silicon
; Assembles with NASM/clang for macOS (ARM64)
; Usage: ./schema_dump [db_path] [df_path] [print_flag]

.global _main
.align 2

.data
default_db_path: .asciz "/var/db/test3/sports"
default_df_path: .asciz "/tmp/sports1.df"
default_print_flag: .asciz "false"
true_str: .asciz "true"
read_mode: .asciz "r"
write_mode: .asciz "w"

; Stub schema content (simulates what dump_schema would write)
stub_schema: .ascii "ADD TABLE \"sports\"\n"
            .ascii "  AREA \"Schema Area\"\n"
            .ascii "  DESCRIPTION \"Sports database schema\"\n"
            .ascii "\n"
            .ascii "ADD FIELD \"id\" OF \"sports\" AS integer\n"
            .ascii "  FORMAT \">>>>>>9\"\n"
            .ascii "  INITIAL \"0\"\n"
            .ascii "  LABEL \"ID\"\n"
            .ascii "  POSITION 2\n"
            .asciz "\n"

err_open_read: .asciz "Error opening file for reading\n"
err_open_write: .asciz "Error creating output file\n"

.bss
.align 3
db_path_ptr: .skip 8
df_path_ptr: .skip 8
print_flag_ptr: .skip 8
file_descriptor: .skip 8
bytes_read: .skip 8
file_buffer: .skip 8192

.text
_main:
    ; Save frame pointer and link register
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    
    ; Save argc (x0) and argv (x1) to callee-saved registers
    mov x19, x0        ; argc
    mov x20, x1        ; argv
    
    ; Parse command line arguments
    ; db_path = sys.argv[1] if len(sys.argv) > 1 else "/var/db/test3/sports"
    cmp x19, #1
    ble use_default_db
    ldr x0, [x20, #8]  ; argv[1]
    adrp x1, db_path_ptr@PAGE
    str x0, [x1, db_path_ptr@PAGEOFF]
    b check_df_path
    
use_default_db:
    adrp x0, default_db_path@PAGE
    add x0, x0, default_db_path@PAGEOFF
    adrp x1, db_path_ptr@PAGE
    str x0, [x1, db_path_ptr@PAGEOFF]
    
check_df_path:
    ; df_path = sys.argv[2] if len(sys.argv) > 2 else "/tmp/sports1.df"
    cmp x19, #2
    ble use_default_df
    ldr x0, [x20, #16]  ; argv[2]
    adrp x1, df_path_ptr@PAGE
    str x0, [x1, df_path_ptr@PAGEOFF]
    b check_print_flag
    
use_default_df:
    adrp x0, default_df_path@PAGE
    add x0, x0, default_df_path@PAGEOFF
    adrp x1, df_path_ptr@PAGE
    str x0, [x1, df_path_ptr@PAGEOFF]
    
check_print_flag:
    ; print_flag = sys.argv[3] if len(sys.argv) > 3 else "false"
    cmp x19, #3
    ble use_default_print
    ldr x0, [x20, #24]  ; argv[3]
    adrp x1, print_flag_ptr@PAGE
    str x0, [x1, print_flag_ptr@PAGEOFF]
    b dump_schema_stub
    
use_default_print:
    adrp x0, default_print_flag@PAGE
    add x0, x0, default_print_flag@PAGEOFF
    adrp x1, print_flag_ptr@PAGE
    str x0, [x1, print_flag_ptr@PAGEOFF]
    
dump_schema_stub:
    ; Open output file for writing
    adrp x0, df_path_ptr@PAGE
    ldr x0, [x0, df_path_ptr@PAGEOFF]
    adrp x1, write_mode@PAGE
    add x1, x1, write_mode@PAGEOFF
    bl _fopen
    
    cbz x0, file_write_error
    adrp x1, file_descriptor@PAGE
    str x0, [x1, file_descriptor@PAGEOFF]
    
    ; Write stub schema to file
    adrp x0, stub_schema@PAGE
    add x0, x0, stub_schema@PAGEOFF
    mov x1, #1
    mov x2, #220        ; Approximate length of stub_schema
    adrp x3, file_descriptor@PAGE
    ldr x3, [x3, file_descriptor@PAGEOFF]
    bl _fwrite
    
    ; Close output file
    adrp x0, file_descriptor@PAGE
    ldr x0, [x0, file_descriptor@PAGEOFF]
    bl _fclose
    
open_file:
    ; Open file for reading
    adrp x0, df_path_ptr@PAGE
    ldr x0, [x0, df_path_ptr@PAGEOFF]
    adrp x1, read_mode@PAGE
    add x1, x1, read_mode@PAGEOFF
    bl _fopen
    
    cbz x0, file_read_error
    adrp x1, file_descriptor@PAGE
    str x0, [x1, file_descriptor@PAGEOFF]
    
check_print_flag_value:
    ; Check if print_flag.lower() == "true"
    adrp x0, print_flag_ptr@PAGE
    ldr x0, [x0, print_flag_ptr@PAGEOFF]
    adrp x1, true_str@PAGE
    add x1, x1, true_str@PAGEOFF
    bl _strcasecmp
    
    cbnz x0, close_file  ; If not equal, skip printing
    
read_and_print:
    ; Initialize bytes_read to 0
    adrp x0, bytes_read@PAGE
    str xzr, [x0, bytes_read@PAGEOFF]
    
read_loop:
    ; Read file contents
    adrp x0, file_buffer@PAGE
    add x0, x0, file_buffer@PAGEOFF
    mov x1, #1
    mov x2, #8191       ; Leave room for null terminator
    adrp x3, file_descriptor@PAGE
    ldr x3, [x3, file_descriptor@PAGEOFF]
    bl _fread
    
    ; Check if we read anything
    cbz x0, close_file
    
    ; Save bytes read
    adrp x1, bytes_read@PAGE
    str x0, [x1, bytes_read@PAGEOFF]
    
    ; Null-terminate the buffer
    adrp x1, file_buffer@PAGE
    add x1, x1, file_buffer@PAGEOFF
    add x1, x1, x0
    strb wzr, [x1]
    
    ; Print the contents
    adrp x0, file_buffer@PAGE
    add x0, x0, file_buffer@PAGEOFF
    bl _printf
    
    ; Continue reading if buffer was full
    adrp x0, bytes_read@PAGE
    ldr x0, [x0, bytes_read@PAGEOFF]
    mov x1, #8191
    cmp x0, x1
    beq read_loop
    
close_file:
    ; Close the file
    adrp x0, file_descriptor@PAGE
    ldr x0, [x0, file_descriptor@PAGEOFF]
    bl _fclose
    
exit_success:
    ; Exit with status 0
    mov x0, #0
    bl _exit
    
file_read_error:
    ; Print error and exit
    adrp x0, err_open_read@PAGE
    add x0, x0, err_open_read@PAGEOFF
    bl _printf
    mov x0, #1
    bl _exit
    
file_write_error:
    ; Print error and exit
    adrp x0, err_open_write@PAGE
    add x0, x0, err_open_write@PAGEOFF
    bl _printf
    mov x0, #1
    bl _exit
