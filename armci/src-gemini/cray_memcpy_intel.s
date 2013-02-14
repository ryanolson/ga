#
# Copyright  2013  Cray Inc.  All Rights Reserved.
#
# This is /cray/css/users/cja/cray_libc/snb/memcpy_bak/cray_memcpy.7.s
# memcpy implementation optimzied for Sandy Bridge (SNB) hardware.
# Written by Chris Ashton (cja)
#
# vim: ts=8:sts=4:sw=4:et
.text
    .globl  _cray_armci_memcpy_snb
    .type   _cray_armci_memcpy_snb,@function

.align 32
_cray_armci_memcpy_snb:
    # __dst = %rdi
    # __src = %rsi
    # __n   = %rdx
    # return __dst in %rax
    mov     %rdi, %rax  # rax = return value

    cmp     $256, %rdx
    jge      __cray_armci_memcpy_snb_large

########### Small Path #############
__cray_armci_memcpy_snb_255B:
    test    $128, %rdx
    jz      __cray_armci_memcpy_snb_127B

    vmovups  0x00(%rsi), %xmm0
    vmovups  0x10(%rsi), %xmm1
    vmovups  0x20(%rsi), %xmm2
    vmovups  0x30(%rsi), %xmm3
    vmovups  0x40(%rsi), %xmm4
    vmovups  0x50(%rsi), %xmm5
    vmovups  0x60(%rsi), %xmm6
    vmovups  0x70(%rsi), %xmm7
    vmovups  %xmm0, 0x00(%rdi)
    vmovups  %xmm1, 0x10(%rdi)
    vmovups  %xmm2, 0x20(%rdi)
    vmovups  %xmm3, 0x30(%rdi)
    vmovups  %xmm4, 0x40(%rdi)
    vmovups  %xmm5, 0x50(%rdi)
    vmovups  %xmm6, 0x60(%rdi)
    vmovups  %xmm7, 0x70(%rdi)
    add     $128, %rsi
    add     $128, %rdi
    sub     $128, %rdx
    jz      __cray_armci_memcpy_snb_ret   # Fast exit for n*128 size

__cray_armci_memcpy_snb_127B:
    test    $64, %rdx
    jz      __cray_armci_memcpy_snb_63B

    vmovups  0x00(%rsi), %xmm0
    vmovups  0x10(%rsi), %xmm1
    vmovups  0x20(%rsi), %xmm2
    vmovups  0x30(%rsi), %xmm3
    vmovups  %xmm0, 0x00(%rdi)
    vmovups  %xmm1, 0x10(%rdi)
    vmovups  %xmm2, 0x20(%rdi)
    vmovups  %xmm3, 0x30(%rdi)
    add     $64, %rsi
    add     $64, %rdi
    sub     $64, %rdx
    jz      __cray_armci_memcpy_snb_ret   # Fast exit for n*64 size

__cray_armci_memcpy_snb_63B:
    test    $32, %rdx
    jz      __cray_armci_memcpy_snb_31B

    vmovups  0x00(%rsi), %xmm0
    vmovups  0x10(%rsi), %xmm1
    vmovups  %xmm0, 0x00(%rdi)
    vmovups  %xmm1, 0x10(%rdi)
    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    jz      __cray_armci_memcpy_snb_ret   # Fast exit for n*32 size

__cray_armci_memcpy_snb_31B:
    test    $16, %rdx
    jz      __cray_armci_memcpy_snb_15B

    vmovups  0x00(%rsi), %xmm0
    vmovups  %xmm0, 0x00(%rdi)
    add     $16, %rsi
    add     $16, %rdi
    sub     $16, %rdx
    jz      __cray_armci_memcpy_snb_ret   # Fast exit for n*16 size

__cray_armci_memcpy_snb_15B:
    test    $8, %rdx
    jz      __cray_armci_memcpy_snb_7B

    mov     0x00(%rsi), %r8
    mov     %r8, 0x00(%rdi)
    add     $8, %rsi
    add     $8, %rdi
    sub     $8, %rdx
    jz      __cray_armci_memcpy_snb_ret   # Fast exit for n*8 size

__cray_armci_memcpy_snb_7B:
    test    $4, %rdx
    jz      __cray_armci_memcpy_snb_3B

    mov     0x00(%rsi), %r8d
    mov     %r8d, 0x00(%rdi)
    add     $4, %rsi
    add     $4, %rdi
    sub     $4, %rdx
    jz      __cray_armci_memcpy_snb_ret   # Fast exit for n*4 size

__cray_armci_memcpy_snb_3B:
    test    $2, %rdx
    jz      __cray_armci_memcpy_snb_1B

    mov     0x00(%rsi), %r8w
    mov     %r8w, 0x00(%rdi)
    add     $2, %rsi
    add     $2, %rdi
    sub     $2, %rdx

__cray_armci_memcpy_snb_1B:
    test    $1, %rdx
    jz      __cray_armci_memcpy_snb_ret

    mov     0x00(%rsi), %r8b
    mov     %r8b, 0x00(%rdi)

__cray_armci_memcpy_snb_ret:
    ret

################ Large Loop Path #############
__cray_armci_memcpy_snb_large:
    mov     %rdi, %r10
    or      %rsi, %r10
    and     $15, %r10
    jnz     __cray_armci_memcpy_snb_unaligned

############## Aligned Large Loop ############
__cray_armci_memcpy_snb_aligned:
    cmp     $255, %rdx
    jle     __cray_armci_memcpy_snb_aligned_255B

__cray_armci_memcpy_snb_aligned_loop_256B:
    vmovaps 0x00(%rsi), %xmm0
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps 0x10(%rsi), %xmm0
    vmovaps %xmm0, 0x10(%rdi)
    vmovaps 0x20(%rsi), %xmm0
    vmovaps %xmm0, 0x20(%rdi)
    vmovaps 0x30(%rsi), %xmm0
    vmovaps %xmm0, 0x30(%rdi)
    vmovaps 0x40(%rsi), %xmm0
    vmovaps %xmm0, 0x40(%rdi)
    vmovaps 0x50(%rsi), %xmm0
    vmovaps %xmm0, 0x50(%rdi)
    vmovaps 0x60(%rsi), %xmm0
    vmovaps %xmm0, 0x60(%rdi)
    vmovaps 0x70(%rsi), %xmm0
    vmovaps %xmm0, 0x70(%rdi)

    add     $128, %rsi
    add     $128, %rdi

    vmovaps 0x00(%rsi), %xmm0
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps 0x10(%rsi), %xmm0
    vmovaps %xmm0, 0x10(%rdi)
    vmovaps 0x20(%rsi), %xmm0
    vmovaps %xmm0, 0x20(%rdi)
    vmovaps 0x30(%rsi), %xmm0
    vmovaps %xmm0, 0x30(%rdi)
    vmovaps 0x40(%rsi), %xmm0
    vmovaps %xmm0, 0x40(%rdi)
    vmovaps 0x50(%rsi), %xmm0
    vmovaps %xmm0, 0x50(%rdi)
    vmovaps 0x60(%rsi), %xmm0
    vmovaps %xmm0, 0x60(%rdi)
    vmovaps 0x70(%rsi), %xmm0
    vmovaps %xmm0, 0x70(%rdi)

    add     $128, %rsi
    add     $128, %rdi
    sub     $256, %rdx
    cmp     $255, %rdx
    jg      __cray_armci_memcpy_snb_aligned_loop_256B

__cray_armci_memcpy_snb_aligned_255B:
    test    %rdx, %rdx
    jz      __cray_armci_memcpy_snb_ret

    cmp     $31, %rdx
    jle     __cray_armci_memcpy_snb_31B

__cray_armci_memcpy_snb_aligned_loop_32B:
    vmovaps 0x00(%rsi), %xmm0
    vmovaps %xmm0, 0x00(%rdi)

    vmovaps 0x10(%rsi), %xmm0
    vmovaps %xmm0, 0x10(%rdi)

    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $31, %rdx
    jg      __cray_armci_memcpy_snb_aligned_loop_32B

    test    %rdx, %rdx
    jz      __cray_armci_memcpy_snb_ret   # Fast exit for n*32 size

    jmp     __cray_armci_memcpy_snb_31B

########### Unaligned Large Loop #############
__cray_armci_memcpy_snb_unaligned:
    mov     %rsi, %r10
    mov     %rdi, %r9
    and     $15, %r10
    and     $15, %r9

    cmp     %r10, %r9
    je      __cray_armci_memcpy_snb_fix_both

########### Process Store Unaligned Bytes ##########
__cray_armci_memcpy_snb_fix_st:
    neg     %r9
#    add     $16, %r9 # removes high bits (not really needed)
    test    $8, %r9
    jz      __cray_armci_memcpy_snb_unaligned_st_7B

    mov     0x00(%rsi), %r8
    mov     %r8, 0x00(%rdi)
    add     $8, %rsi
    add     $8, %rdi
    sub     $8, %rdx

__cray_armci_memcpy_snb_unaligned_st_7B:
    test    $4, %r9
    jz      __cray_armci_memcpy_snb_unaligned_st_3B

    mov     0x00(%rsi), %r8d
    mov     %r8d, 0x00(%rdi)
    add     $4, %rsi
    add     $4, %rdi
    sub     $4, %rdx

__cray_armci_memcpy_snb_unaligned_st_3B:
    test    $2, %r9
    jz      __cray_armci_memcpy_snb_unaligned_st_1B

    mov     0x00(%rsi), %r8w
    mov     %r8w, 0x00(%rdi)
    add     $2, %rsi
    add     $2, %rdi
    sub     $2, %rdx

__cray_armci_memcpy_snb_unaligned_st_1B:
    test    $1, %r9
    jz      __cray_armci_memcpy_snb_aligned_st

    mov     0x00(%rsi), %r8b
    mov     %r8b, 0x00(%rdi)
    add     $1, %rsi
    add     $1, %rdi
    sub     $1, %rdx

################# Store Now Aligned ################
__cray_armci_memcpy_snb_aligned_st:
    cmp     $255, %rdx
    jle     __cray_armci_memcpy_snb_unaligned_255B

__cray_armci_memcpy_snb_unaligned_loop_256B:
    vmovups 0x00(%rsi), %xmm0
    vmovaps %xmm0, 0x00(%rdi)
    vmovups 0x10(%rsi), %xmm0
    vmovaps %xmm0, 0x10(%rdi)
    vmovups 0x20(%rsi), %xmm0
    vmovaps %xmm0, 0x20(%rdi)
    vmovups 0x30(%rsi), %xmm0
    vmovaps %xmm0, 0x30(%rdi)
    vmovups 0x40(%rsi), %xmm0
    vmovaps %xmm0, 0x40(%rdi)
    vmovups 0x50(%rsi), %xmm0
    vmovaps %xmm0, 0x50(%rdi)
    vmovups 0x60(%rsi), %xmm0
    vmovaps %xmm0, 0x60(%rdi)
    vmovups 0x70(%rsi), %xmm0
    vmovaps %xmm0, 0x70(%rdi)

    add     $128, %rsi
    add     $128, %rdi

    vmovups 0x00(%rsi), %xmm0
    vmovaps %xmm0, 0x00(%rdi)
    vmovups 0x10(%rsi), %xmm0
    vmovaps %xmm0, 0x10(%rdi)
    vmovups 0x20(%rsi), %xmm0
    vmovaps %xmm0, 0x20(%rdi)
    vmovups 0x30(%rsi), %xmm0
    vmovaps %xmm0, 0x30(%rdi)
    vmovups 0x40(%rsi), %xmm0
    vmovaps %xmm0, 0x40(%rdi)
    vmovups 0x50(%rsi), %xmm0
    vmovaps %xmm0, 0x50(%rdi)
    vmovups 0x60(%rsi), %xmm0
    vmovaps %xmm0, 0x60(%rdi)
    vmovups 0x70(%rsi), %xmm0
    vmovaps %xmm0, 0x70(%rdi)

    add     $128, %rsi
    add     $128, %rdi
    sub     $256, %rdx
    cmp     $255, %rdx
    jg      __cray_armci_memcpy_snb_unaligned_loop_256B

__cray_armci_memcpy_snb_unaligned_255B:
    test    %rdx, %rdx
    jz      __cray_armci_memcpy_snb_ret

    cmp     $31, %rdx
    jle     __cray_armci_memcpy_snb_31B

__cray_armci_memcpy_snb_unaligned_loop_32B:
    vmovups 0x00(%rsi), %xmm0
    vmovaps %xmm0, 0x00(%rdi)

    vmovups 0x10(%rsi), %xmm1
    vmovaps %xmm1, 0x10(%rdi)

    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $31, %rdx
    jg      __cray_armci_memcpy_snb_unaligned_loop_32B

    test    %rdx, %rdx
    jz      __cray_armci_memcpy_snb_ret   # Fast exit for n*32 size

    jmp     __cray_armci_memcpy_snb_31B

########### Process Equal Unaligned Bytes ##########
__cray_armci_memcpy_snb_fix_both:
    neg     %r10
    add     $16, %r10
    test    $8, %r10
    jz      __cray_armci_memcpy_snb_unaligned_7B

    mov     0x00(%rsi), %r8
    mov     %r8, 0x00(%rdi)
    add     $8, %rsi
    add     $8, %rdi
    sub     $8, %rdx

__cray_armci_memcpy_snb_unaligned_7B:
    test    $4, %r10
    jz      __cray_armci_memcpy_snb_unaligned_3B

    mov     0x00(%rsi), %r8d
    mov     %r8d, 0x00(%rdi)
    add     $4, %rsi
    add     $4, %rdi
    sub     $4, %rdx

__cray_armci_memcpy_snb_unaligned_3B:
    test    $2, %r10
    jz      __cray_armci_memcpy_snb_unaligned_1B

    mov     0x00(%rsi), %r8w
    mov     %r8w, 0x00(%rdi)
    add     $2, %rsi
    add     $2, %rdi
    sub     $2, %rdx

__cray_armci_memcpy_snb_unaligned_1B:
    test    $1, %r10
    jz      __cray_armci_memcpy_snb_aligned

    mov     0x00(%rsi), %r8b
    mov     %r8b, 0x00(%rdi)
    add     $1, %rsi
    add     $1, %rdi
    sub     $1, %rdx

    jmp     __cray_armci_memcpy_snb_aligned
