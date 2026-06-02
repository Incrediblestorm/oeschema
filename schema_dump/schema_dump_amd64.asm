; x86-64 Assembly standalone implementation of schema_dump.py for Linux (ELF format)
; Assembles with NASM for Linux AMD64
; Usage: ./schema_dump [db_path] [df_path] [print_flag]
;
; This implements the core functionality from schema_dump.py:
; - Parse command line arguments (db_path, df_path, print_flag)
; - Call dump_schema() which invokes OpenEdge _progres binary
; - Optionally print the output file

section .data
    default_db_path: db "/var/db/test3/sports", 0
    default_df_path: db "/tmp/sports1.df", 0
    default_print_flag: db "false", 0
    true_str: db "true", 0
    
    ; OpenEdge paths and commands
    dlc_path: db "/usr/dlc", 0
    progres_bin: db "/usr/dlc/bin/_progres", 0
    tty_path: db "/usr/dlc/tty", 0
    protermcap_path: db "/usr/dlc/protermcap", 0
    
    ; ABL procedure code for schema dump
    abl_template: db "DEFINE VARIABLE cTables AS CHARACTER NO-UNDO INITIAL ", 34, "ALL", 34, ".", 10
                 db "DEFINE VARIABLE cOutFile AS CHARACTER NO-UNDO.", 10
                 db "DEFINE VARIABLE cCodePage AS CHARACTER NO-UNDO INITIAL ?.", 10
                 db "ASSIGN cTables = ENTRY(1, SESSION:PARAMETER, ", 34, "|", 34, ")", 10
                 db "       cOutFile = ENTRY(2, SESSION:PARAMETER, ", 34, "|", 34, ")", 10
                 db "       cCodePage = ENTRY(3, SESSION:PARAMETER, ", 34, "|", 34, ") NO-ERROR.", 10
                 db "IF cTables = ", 34, 34, " OR cTables = ? THEN cTables = ", 34, "ALL", 34, ".", 10
                 db "IF cCodePage = ", 34, 34, " THEN cCodePage = ?.", 10
                 db "RUN prodict/dump_df.p (INPUT cTables, INPUT cOutFile, INPUT cCodePage).", 10, 0
    
    ; Command line argument strings
    arg_db: db "-db", 0
    arg_single_user: db "-1", 0
    arg_batch: db "-b", 0
    arg_procedure: db "-p", 0
    arg_param: db "-param", 0
    
    ; Environment variable names
    env_dlc: db "DLC=/usr/dlc", 0
    env_propath: db "PROPATH=/usr/dlc/tty", 0
    env_protermcap: db "PROTERMCAP=/usr/dlc/protermcap", 0
    env_term: db "TERM=xterm", 0
    
    ; Temp file template
    tmp_template: db "/tmp/dumpXXXXXX.p", 0
    tmp_path: times 64 db 0
    
    ; Parameter string buffer
    param_buffer: times 512 db 0
    
    ; Messages
    msg_db_not_found: db "Database not found: ", 0
    msg_error: db "Error: ", 0
    msg_newline: db 10, 0
    read_mode: db "r", 0
    write_mode: db "w", 0
    
section .bss
    db_path_ptr: resq 1
    df_path_ptr: resq 1
    print_flag_ptr: resq 1
    file_buffer: resb 8192
    file_descriptor: resq 1
    bytes_read: resq 1
    proc_fd: resd 1
    
section .text
    global _start
    extern fopen
    extern fwrite
    extern fread
    extern fclose
    extern printf
    extern strcasecmp
    extern exit
    extern fork
    extern execve
    extern wait
    extern unlink
    extern strlen
    extern strcpy
    extern strcat
    extern mkstemp
    extern access
    
_start:
    ; Save argc and argv
    pop rdi                     ; argc
    mov rsi, rsp                ; argv
    
    ; Parse command line arguments
    ; db_path = argv[1] if argc > 1 else "/var/db/test3/sports"
    cmp rdi, 1
    jle .use_default_db
    mov rax, [rsi+8]            ; argv[1]
    mov [db_path_ptr], rax
    jmp .check_df_path
    
.use_default_db:
    lea rax, [rel default_db_path]
    mov [db_path_ptr], rax
    
.check_df_path:
    ; df_path = argv[2] if argc > 2 else "/tmp/sports1.df"
    cmp rdi, 2
    jle .use_default_df
    mov rax, [rsi+16]           ; argv[2]
    mov [df_path_ptr], rax
    jmp .check_print_flag
    
.use_default_df:
    lea rax, [rel default_df_path]
    mov [df_path_ptr], rax
    
.check_print_flag:
    ; print_flag = argv[3] if argc > 3 else "false"
    cmp rdi, 3
    jle .use_default_print
    mov rax, [rsi+24]           ; argv[3]
    mov [print_flag_ptr], rax
    jmp .create_temp_proc
    
.use_default_print:
    lea rax, [rel default_print_flag]
    mov [print_flag_ptr], rax
    
.create_temp_proc:
    ; Create temporary .p file for ABL code
    lea rdi, [rel tmp_template]
    lea rsi, [rel tmp_path]
    call strcpy
    
    ; Create temp file using mkstemp
    lea rdi, [rel tmp_path]
    call mkstemp
    cmp rax, 0
    jl .exit_error
    mov [proc_fd], eax
    
    ; Write ABL code to temp file
    mov edi, [proc_fd]
    lea rsi, [rel abl_template]
    call strlen
    mov rdx, rax                ; length
    mov rax, 1                  ; sys_write
    mov edi, [proc_fd]
    lea rsi, [rel abl_template]
    syscall
    
    ; Close temp file
    mov rax, 3                  ; sys_close
    mov edi, [proc_fd]
    syscall
    
.build_param:
    ; Build parameter string: "ALL|{df_path}|"
    lea rdi, [rel param_buffer]
    mov byte [rdi], 'A'
    mov byte [rdi+1], 'L'
    mov byte [rdi+2], 'L'
    mov byte [rdi+3], '|'
    add rdi, 4
    
    ; Append df_path
    mov rsi, [df_path_ptr]
    call strcpy
    
    ; Append final "|"
    lea rdi, [rel param_buffer]
    call strlen
    lea rdi, [rel param_buffer]
    add rdi, rax
    mov byte [rdi], '|'
    mov byte [rdi+1], 0
    
.fork_and_exec:
    ; Fork to run OpenEdge _progres
    call fork
    cmp rax, 0
    je .child_process
    
    ; Parent: wait for child
    mov rdi, rax                ; child PID
    xor rsi, rsi
    mov rax, 61                 ; sys_wait4
    syscall
    
    jmp .cleanup_temp
    
.child_process:
    ; Build argv for execve
    ; argv[] = {progres_bin, "-db", db_path, "-1", "-b", "-p", tmp_path, "-param", param_buffer, NULL}
    sub rsp, 80                 ; space for 10 pointers
    
    lea rax, [rel progres_bin]
    mov [rsp], rax
    
    lea rax, [rel arg_db]
    mov [rsp+8], rax
    
    mov rax, [db_path_ptr]
    mov [rsp+16], rax
    
    lea rax, [rel arg_single_user]
    mov [rsp+24], rax
    
    lea rax, [rel arg_batch]
    mov [rsp+32], rax
    
    lea rax, [rel arg_procedure]
    mov [rsp+40], rax
    
    lea rax, [rel tmp_path]
    mov [rsp+48], rax
    
    lea rax, [rel arg_param]
    mov [rsp+56], rax
    
    lea rax, [rel param_buffer]
    mov [rsp+64], rax
    
    mov qword [rsp+72], 0       ; NULL terminator
    
    ; Build envp
    sub rsp, 40                 ; space for 5 env vars
    lea rax, [rel env_dlc]
    mov [rsp], rax
    lea rax, [rel env_propath]
    mov [rsp+8], rax
    lea rax, [rel env_protermcap]
    mov [rsp+16], rax
    lea rax, [rel env_term]
    mov [rsp+24], rax
    mov qword [rsp+32], 0       ; NULL terminator
    
    ; execve
    lea rdi, [rel progres_bin]
    lea rsi, [rsp+40]           ; argv
    mov rdx, rsp                ; envp
    mov rax, 59                 ; sys_execve
    syscall
    
    ; If execve returns, exit with error
    mov rdi, 1
    mov rax, 60                 ; sys_exit
    syscall
    
.cleanup_temp:
    ; Delete temporary .p file
    lea rdi, [rel tmp_path]
    call unlink
    
.open_and_print:
    ; Check if print_flag == "true"
    mov rdi, [print_flag_ptr]
    lea rsi, [rel true_str]
    call strcasecmp
    
    test eax, eax
    jnz .exit_success           ; If not "true", skip printing
    
    ; Open df file for reading
    mov rdi, [df_path_ptr]
    lea rsi, [rel read_mode]
    call fopen
    
    test rax, rax
    jz .exit_error
    mov [file_descriptor], rax
    
.read_loop:
    ; Read and print file contents
    lea rdi, [rel file_buffer]
    mov rsi, 1
    mov rdx, 8191
    mov rcx, [file_descriptor]
    call fread
    
    test rax, rax
    jz .close_and_exit
    
    mov [bytes_read], rax
    
    ; Null-terminate
    lea rdi, [rel file_buffer]
    add rdi, rax
    mov byte [rdi], 0
    
    ; Print
    lea rdi, [rel file_buffer]
    xor rax, rax
    call printf
    
    cmp qword [bytes_read], 8191
    je .read_loop
    
.close_and_exit:
    mov rdi, [file_descriptor]
    call fclose
    
.exit_success:
    xor rdi, rdi
    call exit
    
.exit_error:
    mov rdi, 1
    call exit
