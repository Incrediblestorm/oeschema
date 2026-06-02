; x86-64 Assembly standalone implementation of schema_dump.py
; Assembles with NASM for macOS (Mach-O format)
; Usage: ./schema_dump [db_path] [df_path] [print_flag]
;
; NOTE: This is a standalone version that mimics the file I/O behavior.
; The original dump_schema() calls OpenEdge Progress database tools which
; cannot be replicated in pure assembly without reimplementing the entire
; OpenEdge database engine. This version creates a stub output file instead.

default rel

section .data
    default_db_path: db "/var/db/test3/sports", 0
    default_df_path: db "/tmp/sports1.df", 0
    default_print_flag: db "false", 0
    true_str: db "true", 0
    read_mode: db "r", 0
    write_mode: db "w", 0
    
    ; Stub schema content (simulates what dump_schema would write)
    stub_schema: db "ADD TABLE ", 34, "sports", 34, 10
                db "  AREA ", 34, "Schema Area", 34, 10
                db "  DESCRIPTION ", 34, "Sports database schema", 34, 10
                db 10
                db "ADD FIELD ", 34, "id", 34, " OF ", 34, "sports", 34, " AS integer", 10
                db "  FORMAT ", 34, ">>>>>>9", 34, 10
                db "  INITIAL ", 34, "0", 34, 10
                db "  LABEL ", 34, "ID", 34, 10
                db "  POSITION 2", 10
                db 10, 0
    stub_schema_end:
    stub_schema_len equ (stub_schema_end - stub_schema - 1)
    
    ; Messages
    err_open_read: db "Error opening file for reading", 10, 0
    err_open_write: db "Error creating output file", 10, 0
    
section .bss
    db_path_ptr: resq 1
    df_path_ptr: resq 1
    print_flag_ptr: resq 1
    file_buffer: resb 8192
    file_descriptor: resq 1
    bytes_read: resq 1
    
section .text
    global _main
    extern _fopen
    extern _fwrite
    extern _fread
    extern _fclose
    extern _printf
    extern _strcasecmp
    extern _exit
    
_main:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    
    ; Save argc and argv
    mov [rbp-8], rdi        ; argc
    mov [rbp-16], rsi       ; argv
    
    ; Parse command line arguments
    ; db_path = sys.argv[1] if len(sys.argv) > 1 else "/var/db/test3/sports"
    cmp rdi, 1
    jle .use_default_db
    mov rax, [rsi+8]        ; argv[1]
    mov [db_path_ptr], rax
    jmp .check_df_path
    
.use_default_db:
    lea rax, [default_db_path]
    mov [db_path_ptr], rax
    
.check_df_path:
    ; df_path = sys.argv[2] if len(sys.argv) > 2 else "/tmp/sports1.df"
    mov rdi, [rbp-8]        ; argc
    cmp rdi, 2
    jle .use_default_df
    mov rsi, [rbp-16]       ; argv
    mov rax, [rsi+16]       ; argv[2]
    mov [df_path_ptr], rax
    jmp .check_print_flag
    
.use_default_df:
    lea rax, [default_df_path]
    mov [df_path_ptr], rax
    
.check_print_flag:
    ; print_flag = sys.argv[3] if len(sys.argv) > 3 else "false"
    mov rdi, [rbp-8]        ; argc
    cmp rdi, 3
    jle .use_default_print
    mov rsi, [rbp-16]       ; argv
    mov rax, [rsi+24]       ; argv[3]
    mov [print_flag_ptr], rax
    jmp .dump_schema_stub
    
.use_default_print:
    lea rax, [default_print_flag]
    mov [print_flag_ptr], rax
    
.dump_schema_stub:
    ; Simulate dump_schema(db_path, df_path) by writing stub content
    ; Open output file for writing
    mov rdi, [df_path_ptr]
    lea rsi, [write_mode]
    call _fopen
    
    test rax, rax
    jz .file_write_error
    mov [file_descriptor], rax
    
    ; Write stub schema to file
    lea rdi, [stub_schema]
    mov rsi, 1
    mov rdx, stub_schema_len
    mov rcx, [file_descriptor]
    call _fwrite
    
    ; Close output file
    mov rdi, [file_descriptor]
    call _fclose
    
.open_file:
    ; Open file for reading: fopen(df_path, "r")
    mov rdi, [df_path_ptr]
    lea rsi, [read_mode]
    call _fopen
    
    ; Check if file opened successfully
    test rax, rax
    jz .file_read_error
    mov [file_descriptor], rax
    
.check_print_flag_value:
    ; Check if print_flag.lower() == "true"
    mov rdi, [print_flag_ptr]
    lea rsi, [true_str]
    call _strcasecmp
    
    test eax, eax
    jnz .close_file         ; If not equal, skip printing
    
.read_and_print:
    ; Read file contents in a loop
    mov qword [bytes_read], 0
    
.read_loop:
    lea rdi, [file_buffer]
    mov rsi, 1
    mov rdx, 8191           ; Leave room for null terminator
    mov rcx, [file_descriptor]
    call _fread
    
    ; Check if we read anything
    test rax, rax
    jz .close_file
    
    ; Save bytes read
    mov [bytes_read], rax
    
    ; Null-terminate the buffer
    lea rdi, [file_buffer]
    add rdi, rax
    mov byte [rdi], 0
    
    ; Print the contents
    lea rdi, [file_buffer]
    xor rax, rax            ; printf needs AL=0 for varargs
    call _printf
    
    ; Continue reading if buffer was full
    cmp qword [bytes_read], 8191
    je .read_loop
    
.close_file:
    ; Close the file
    mov rdi, [file_descriptor]
    call _fclose
    
.exit:
    ; Exit with status 0
    xor rdi, rdi
    call _exit
    
.file_read_error:
    ; Print error and exit
    lea rdi, [err_open_read]
    xor rax, rax
    call _printf
    mov rdi, 1
    call _exit
    
.file_write_error:
    ; Print error and exit
    lea rdi, [err_open_write]
    xor rax, rax
    call _printf
    mov rdi, 1
    call _exit
