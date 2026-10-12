.text
.globl FixtureProject
.seh_proc FixtureProject
FixtureProject:
    subq $0x68, %rsp
    .seh_stackalloc 0x68
    .seh_endprologue
    movss 4(%rdx), %xmm4
    movss 8(%rcx), %xmm5
    movaps %xmm4, %xmm2
    addq $0x68, %rsp
    jmp HostProject
.seh_endproc

.globl FixtureIcon
.seh_proc FixtureIcon
FixtureIcon:
    pushq %rbp
    .seh_pushreg %rbp
    pushq %rdi
    .seh_pushreg %rdi
    pushq %rsi
    .seh_pushreg %rsi
    subq $0x50, %rsp
    .seh_stackalloc 0x50
    .seh_endprologue
    leaq -0x20(%r8), %rbp
    leaq -0x90(%rcx), %rsi
    leaq 0x20(%rsp), %rdi
    movq $0, (%rdi)
    movq g_viewport_base(%rip), %r9
    leaq 0x20(%rbp), %r8
    addq $0x180, %r9
    leaq 0x90(%rsi), %rcx
    call FixtureProject
    movss %xmm0, 0x20(%rdi)
    movq %rdi, %rcx
    movq (%rdi), %rax
    addq $0x50, %rsp
    popq %rsi
    popq %rdi
    popq %rbp
    ret
.seh_endproc
