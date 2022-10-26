.text
// see System V Application Binary Interface AMD64 Architecture Processor Supplement
// section 3.4.1 Initial Stack and Register State
.global _asm_starter
_asm_starter:
    push %rcx
    xor %rax,%rax
    push %rax
    push %rdx
    push %rsi
    xor %rbp,%rbp
    xor %rdx, %rdx // no function to register with atexit
    jmp *%rdi