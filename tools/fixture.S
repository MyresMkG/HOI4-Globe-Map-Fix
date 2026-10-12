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

.globl FixturePick
.seh_proc FixturePick
FixturePick:
    .byte 0x4c,0x8b,0xdc
    movb %r9b, 0x20(%r11)
    movq %rdx, 0x10(%r11)
    movq %rcx, 0x08(%r11)
    pushq %rbp
    .seh_pushreg %rbp
    pushq %rbx
    .seh_pushreg %rbx
    pushq %rsi
    .seh_pushreg %rsi
    pushq %rdi
    .seh_pushreg %rdi
    leaq -0xca8(%r11), %rbp
    subq $0xd88, %rsp
    .seh_stackalloc 0xd88
    .seh_endprologue
    cmpq $0, g_fixture_active(%rip)
    movzbl %r9b, %esi
    .byte 0x48,0x8b,0xc2
    .byte 0x0f,0x84
    .long FixturePickDone - . - 4
    movq 0x20(%rcx), %rdi
    testq %rdi, %rdi
    .byte 0x0f,0x84
    .long FixturePickDone - . - 4
    movq g_fixture_terrain(%rip), %rax
FixturePickDone:
    addq $0xd88, %rsp
    popq %rdi
    popq %rsi
    popq %rbx
    popq %rbp
    jmp HostPick
    # Unexecuted structural context belongs to this fixture, not game code.
    .byte 0x33,0xdb
    leaq 0x10(%rdi), %rax
    movq %rax, -0x50(%rbp)
    movl %ebx, 0x68(%rsp)
    testb %sil, %sil
    movq %rax, 0x20(%rsp)
    call FixtureRay
    movl $256, 0x50(%rsp)
.seh_endproc

.globl FixtureRay
.seh_proc FixtureRay
FixtureRay:
    .byte 0x48,0x8b,0xc4
    movq %rbx, 0x10(%rax)
    .seh_savereg %rbx, 0x10
    movq %rsi, 0x18(%rax)
    .seh_savereg %rsi, 0x18
    pushq %rdi
    .seh_pushreg %rdi
    subq $0x70, %rsp
    .seh_stackalloc 0x70
    movaps %xmm6, -0x18(%rax)
    .seh_endprologue
    .byte 0x48,0x8b,0xda,0x48,0x8b,0xf9
    addq $0x70, %rsp
    popq %rdi
    movq 0x10(%rsp), %rbx
    movq 0x18(%rsp), %rsi
    jmp HostRay
.seh_endproc

.globl FixtureHeight
FixtureHeight:
    addss FixtureHalf(%rip), %xmm1
    addss FixtureHalf(%rip), %xmm2
    movq 0x178(%rcx), %r8
    .byte 0x33,0xc9
    cvttss2si %xmm1, %edx
    cvttss2si %xmm2, %r10d
    jmp HostHeight
    .space 1100, 0x90
.section .rdata,"dr"
FixtureHalf: .float 0.5
.text

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
