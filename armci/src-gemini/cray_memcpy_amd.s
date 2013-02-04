#
# Copyright  2012  Cray Inc.  All Rights Reserved.
#
# $HeadURL: $
# $Date: $
# $Rev: $
# $Author: $
#
# This is /cray/css/users/cja/cray_libc/int/memcpy/cray_mpi_memcpy_int.10.s, a
# memcpy implementation optimzied for Interlagos hardware.
# Written by Chris Ashton (cja)
#
# vim: ts=8:sts=4:sw=4:et

# Changelog:
# v.10:Improve aligned case so all sizes beat libc (except tiny)
#      Add additional path for aligned 256B->4KB
#      -> unorder loads and stores for this size and the 1M+ size
# v.9: Improves 2^13+ for aligned, hurts 2^8->2^12
#      -> order all loads and stores, re-add prefetchw
# v.8: improve unaligned destination performance
#      -> make loads and stores consecutive (eliminate many Store to Load forwarding chances)
#      -> re-enable non-nta path for unaligned
# v.7: improve unaligned destination performance
#      -> unaligned normal path slower than NTA/prefetch at 1M+ (.5B/clk -> 1B/clk)
#      -> stop calling non-nta path for unaligned
# v.6: change remaining SSE to AVX equivalent

.text
    .globl  _cray_mpi_memcpy_int
    .type   _cray_mpi_memcpy_int,@function

.align 32
_cray_mpi_memcpy_int:
__cray_mpi_memcpy_int:
    # __dst = %rdi
    # __src = %rsi
    # __n   = %rdx
    # return __dst in %rax
    mov     %rdi, %rax  # rax = return value

    cmp     $256, %rdx
    jge     __cray_mpi_memcpy_int_large

#    test    $0xf0, %rdx
#    jz      __cray_mpi_memcpy_int_15B

########### Small Path #############
__cray_mpi_memcpy_int_255B:
    test    $128, %rdx
    jz      __cray_mpi_memcpy_int_127B

    vmovups 0x00(%rsi), %xmm0
    vmovups 0x10(%rsi), %xmm1
    vmovups 0x20(%rsi), %xmm2
    vmovups 0x30(%rsi), %xmm3
    vmovups 0x40(%rsi), %xmm4
    vmovups 0x50(%rsi), %xmm5
    vmovups 0x60(%rsi), %xmm6
    vmovups 0x70(%rsi), %xmm7
    vmovups %xmm0, 0x00(%rdi)
    vmovups %xmm1, 0x10(%rdi)
    vmovups %xmm2, 0x20(%rdi)
    vmovups %xmm3, 0x30(%rdi)
    vmovups %xmm4, 0x40(%rdi)
    vmovups %xmm5, 0x50(%rdi)
    vmovups %xmm6, 0x60(%rdi)
    vmovups %xmm7, 0x70(%rdi)
    add     $128, %rsi
    add     $128, %rdi
    sub     $128, %rdx
    jz      __cray_mpi_memcpy_int_ret   # Fast exit for n*128 size

__cray_mpi_memcpy_int_127B:
    test    $64, %rdx
    jz      __cray_mpi_memcpy_int_63B

    vmovups 0x00(%rsi), %xmm0
    vmovups 0x10(%rsi), %xmm1
    vmovups 0x20(%rsi), %xmm2
    vmovups 0x30(%rsi), %xmm3
    vmovups %xmm0, 0x00(%rdi)
    vmovups %xmm1, 0x10(%rdi)
    vmovups %xmm2, 0x20(%rdi)
    vmovups %xmm3, 0x30(%rdi)
    add     $64, %rsi
    add     $64, %rdi
    sub     $64, %rdx
    jz      __cray_mpi_memcpy_int_ret   # Fast exit for n*64 size

__cray_mpi_memcpy_int_63B:
    test    $32, %rdx
    jz      __cray_mpi_memcpy_int_31B

    vmovups 0x00(%rsi), %xmm0
    vmovups 0x10(%rsi), %xmm1
    vmovups %xmm0, 0x00(%rdi)
    vmovups %xmm1, 0x10(%rdi)
    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    jz      __cray_mpi_memcpy_int_ret   # Fast exit for n*32 size

__cray_mpi_memcpy_int_31B:
    test    $16, %rdx
    jz      __cray_mpi_memcpy_int_15B

    vmovups 0x00(%rsi), %xmm0
    vmovups %xmm0, 0x00(%rdi)
    add     $16, %rsi
    add     $16, %rdi
    sub     $16, %rdx
    jz      __cray_mpi_memcpy_int_ret   # Fast exit for n*16 size

__cray_mpi_memcpy_int_15B:
    test    $8, %rdx
    jz      __cray_mpi_memcpy_int_7B

    mov     0x00(%rsi), %r8
    mov     %r8, 0x00(%rdi)
    add     $8, %rsi
    add     $8, %rdi
    sub     $8, %rdx
    jz      __cray_mpi_memcpy_int_ret   # Fast exit for n*8 size

__cray_mpi_memcpy_int_7B:
    test    $4, %rdx
    jz      __cray_mpi_memcpy_int_3B

    mov     0x00(%rsi), %r8d
    mov     %r8d, 0x00(%rdi)
    add     $4, %rsi
    add     $4, %rdi
    sub     $4, %rdx
    jz      __cray_mpi_memcpy_int_ret   # Fast exit for n*4 size

__cray_mpi_memcpy_int_3B:
    test    $2, %rdx
    jz      __cray_mpi_memcpy_int_1B

    mov     0x00(%rsi), %r8w
    mov     %r8w, 0x00(%rdi)
    add     $2, %rsi
    add     $2, %rdi
    sub     $2, %rdx

__cray_mpi_memcpy_int_1B:
    test    $1, %rdx
    jz      __cray_mpi_memcpy_int_ret

    mov     0x00(%rsi), %r8b
    mov     %r8b, 0x00(%rdi)

__cray_mpi_memcpy_int_ret:
    ret

################ Large Path #############
__cray_mpi_memcpy_int_large:
    mov     %rdi, %r10
    or      %rsi, %r10
    and     $15, %r10
    jnz     __cray_mpi_memcpy_int_unaligned

############## Aligned Large Loop ############
__cray_mpi_memcpy_int_aligned:
    cmp     $4*1024, %rdx
    jl      __cray_mpi_memcpy_int_aligned_range1_entry

    cmp     $1024*1024, %rdx
    jl      __cray_mpi_memcpy_int_aligned_range2_entry

############
# For Range 3 [2^20B,],
# [X] Alternate loads and stores
# [X] Prefetch loads
# [ ] Prefetch stores
# [X] NT loads
# [X] NT stores
############
__cray_mpi_memcpy_int_aligned_range3_entry:
    prefetchnta 0x000(%rsi)
    prefetchnta 0x040(%rsi)
    prefetchnta 0x080(%rsi)
    prefetchnta 0x0c0(%rsi)
    prefetchnta 0x100(%rsi)
    prefetchnta 0x140(%rsi)
    prefetchnta 0x180(%rsi)
    prefetchnta 0x1c0(%rsi)
    prefetchnta 0x200(%rsi)
    prefetchnta 0x240(%rsi)
    prefetchnta 0x280(%rsi)
    prefetchnta 0x2c0(%rsi)

.align 32
__cray_mpi_memcpy_int_aligned_range3_256B_loop:
    prefetchnta 0x300(%rsi)
    prefetchnta 0x340(%rsi)
    prefetchnta 0x380(%rsi)
    prefetchnta 0x3c0(%rsi)

    vmovntdqa 0x00(%rsi), %xmm0
    vmovntdq %xmm0, 0x00(%rdi)
    vmovntdqa 0x10(%rsi), %xmm1
    vmovntdq %xmm1, 0x10(%rdi)
    vmovntdqa 0x20(%rsi), %xmm2
    vmovntdq %xmm2, 0x20(%rdi)
    vmovntdqa 0x30(%rsi), %xmm3
    vmovntdq %xmm3, 0x30(%rdi)
    vmovntdqa 0x40(%rsi), %xmm4
    vmovntdq %xmm4, 0x40(%rdi)
    vmovntdqa 0x50(%rsi), %xmm5
    vmovntdq %xmm5, 0x50(%rdi)
    vmovntdqa 0x60(%rsi), %xmm6
    vmovntdq %xmm6, 0x60(%rdi)
    vmovntdqa 0x70(%rsi), %xmm7
    vmovntdq %xmm7, 0x70(%rdi)

    add     $128, %rsi
    add     $128, %rdi

    vmovntdqa 0x00(%rsi), %xmm0
    vmovntdq %xmm0, 0x00(%rdi)
    vmovntdqa 0x10(%rsi), %xmm1
    vmovntdq %xmm1, 0x10(%rdi)
    vmovntdqa 0x20(%rsi), %xmm2
    vmovntdq %xmm2, 0x20(%rdi)
    vmovntdqa 0x30(%rsi), %xmm3
    vmovntdq %xmm3, 0x30(%rdi)
    vmovntdqa 0x40(%rsi), %xmm4
    vmovntdq %xmm4, 0x40(%rdi)
    vmovntdqa 0x50(%rsi), %xmm5
    vmovntdq %xmm5, 0x50(%rdi)
    vmovntdqa 0x60(%rsi), %xmm6
    vmovntdq %xmm6, 0x60(%rdi)
    vmovntdqa 0x70(%rsi), %xmm7
    vmovntdq %xmm7, 0x70(%rdi)

    add     $128, %rsi
    add     $128, %rdi
    sub     $256, %rdx
    cmp     $255, %rdx
    jg      __cray_mpi_memcpy_int_aligned_range3_256B_loop

__cray_mpi_memcpy_int_aligned_range3_255B:
    sfence

    test    %rdx, %rdx
    jz      __cray_mpi_memcpy_int_ret

    cmp     $31, %rdx
    jle     __cray_mpi_memcpy_int_31B

__cray_mpi_memcpy_int_aligned_range3_32B_loop:
    vmovntdqa 0x00(%rsi), %xmm0
    vmovntdq %xmm0, 0x00(%rdi)
    vmovntdqa 0x10(%rsi), %xmm1
    vmovntdq %xmm1, 0x10(%rdi)

    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $31, %rdx
    jg      __cray_mpi_memcpy_int_aligned_range3_32B_loop

    sfence

    test    %rdx, %rdx
    jz      __cray_mpi_memcpy_int_ret

    jmp     __cray_mpi_memcpy_int_31B

############
# For Range 2 [2^12B,2^20B],
# [ ] Alternate loads and stores
# [X] Prefetch loads
# [ ] Prefetch stores
# [ ] NT loads
# [ ] NT stores
############
__cray_mpi_memcpy_int_aligned_range2_entry:
    prefetchnta 0x000(%rsi)
    prefetchnta 0x040(%rsi)
    prefetchnta 0x080(%rsi)
    prefetchnta 0x0c0(%rsi)
    prefetchnta 0x100(%rsi)
    prefetchnta 0x140(%rsi)
    prefetchnta 0x180(%rsi)
    prefetchnta 0x1c0(%rsi)
    prefetchnta 0x200(%rsi)
    prefetchnta 0x240(%rsi)
    prefetchnta 0x280(%rsi)
    prefetchnta 0x2c0(%rsi)

.align 32
__cray_mpi_memcpy_int_aligned_range2_256B_loop:
    prefetchnta 0x300(%rsi)
    prefetchnta 0x340(%rsi)
    prefetchnta 0x380(%rsi)
    prefetchnta 0x3c0(%rsi)

    vmovaps 0x00(%rsi), %xmm0
    vmovaps 0x10(%rsi), %xmm1
    vmovaps 0x20(%rsi), %xmm2
    vmovaps 0x30(%rsi), %xmm3
    vmovaps 0x40(%rsi), %xmm4
    vmovaps 0x50(%rsi), %xmm5
    vmovaps 0x60(%rsi), %xmm6
    vmovaps 0x70(%rsi), %xmm7
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps %xmm1, 0x10(%rdi)
    vmovaps %xmm2, 0x20(%rdi)
    vmovaps %xmm3, 0x30(%rdi)
    vmovaps %xmm4, 0x40(%rdi)
    vmovaps %xmm5, 0x50(%rdi)
    vmovaps %xmm6, 0x60(%rdi)
    vmovaps %xmm7, 0x70(%rdi)

    add     $128, %rsi
    add     $128, %rdi

    vmovaps 0x00(%rsi), %xmm0
    vmovaps 0x10(%rsi), %xmm1
    vmovaps 0x20(%rsi), %xmm2
    vmovaps 0x30(%rsi), %xmm3
    vmovaps 0x40(%rsi), %xmm4
    vmovaps 0x50(%rsi), %xmm5
    vmovaps 0x60(%rsi), %xmm6
    vmovaps 0x70(%rsi), %xmm7
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps %xmm1, 0x10(%rdi)
    vmovaps %xmm2, 0x20(%rdi)
    vmovaps %xmm3, 0x30(%rdi)
    vmovaps %xmm4, 0x40(%rdi)
    vmovaps %xmm5, 0x50(%rdi)
    vmovaps %xmm6, 0x60(%rdi)
    vmovaps %xmm7, 0x70(%rdi)

    add     $128, %rsi
    add     $128, %rdi
    sub     $256, %rdx
    cmp     $255, %rdx
    jg      __cray_mpi_memcpy_int_aligned_range2_256B_loop

__cray_mpi_memcpy_int_aligned_range2_255B:
    test    %rdx, %rdx
    jz      __cray_mpi_memcpy_int_ret

    cmp     $31, %rdx
    jle     __cray_mpi_memcpy_int_31B

.align 32
__cray_mpi_memcpy_int_aligned_range2_32B_loop:
    vmovaps 0x00(%rsi), %xmm0
    vmovaps 0x10(%rsi), %xmm1
    vmovaps %xmm0, 0x10(%rdi)
    vmovaps %xmm1, 0x00(%rdi)

    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $31, %rdx
    jg      __cray_mpi_memcpy_int_aligned_range2_32B_loop

    test    %rdx, %rdx
    jz      __cray_mpi_memcpy_int_ret

    jmp     __cray_mpi_memcpy_int_31B

############
# For Range 1 [256B,2^12B],
# [X] Alternate loads and stores
# [ ] Prefetch loads
# [ ] Prefetch stores
# [ ] NT loads
# [ ] NT stores
############
.align 32
__cray_mpi_memcpy_int_aligned_range1_entry:
__cray_mpi_memcpy_int_aligned_range1_256B_loop:
    vmovaps 0x00(%rsi), %xmm0
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps 0x10(%rsi), %xmm1
    vmovaps %xmm1, 0x10(%rdi)
    vmovaps 0x20(%rsi), %xmm2
    vmovaps %xmm2, 0x20(%rdi)
    vmovaps 0x30(%rsi), %xmm3
    vmovaps %xmm3, 0x30(%rdi)
    vmovaps 0x40(%rsi), %xmm4
    vmovaps %xmm4, 0x40(%rdi)
    vmovaps 0x50(%rsi), %xmm5
    vmovaps %xmm5, 0x50(%rdi)
    vmovaps 0x60(%rsi), %xmm6
    vmovaps %xmm6, 0x60(%rdi)
    vmovaps 0x70(%rsi), %xmm7
    vmovaps %xmm7, 0x70(%rdi)

    add     $128, %rsi
    add     $128, %rdi

    vmovaps 0x00(%rsi), %xmm0
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps 0x10(%rsi), %xmm1
    vmovaps %xmm1, 0x10(%rdi)
    vmovaps 0x20(%rsi), %xmm2
    vmovaps %xmm2, 0x20(%rdi)
    vmovaps 0x30(%rsi), %xmm3
    vmovaps %xmm3, 0x30(%rdi)
    vmovaps 0x40(%rsi), %xmm4
    vmovaps %xmm4, 0x40(%rdi)
    vmovaps 0x50(%rsi), %xmm5
    vmovaps %xmm5, 0x50(%rdi)
    vmovaps 0x60(%rsi), %xmm6
    vmovaps %xmm6, 0x60(%rdi)
    vmovaps 0x70(%rsi), %xmm7
    vmovaps %xmm7, 0x70(%rdi)

    add     $128, %rsi
    add     $128, %rdi
    sub     $256, %rdx
    cmp     $255, %rdx
    jg      __cray_mpi_memcpy_int_aligned_range1_256B_loop

__cray_mpi_memcpy_int_aligned_range1_255B:
    test    %rdx, %rdx
    jz      __cray_mpi_memcpy_int_ret

    cmp     $31, %rdx
    jle     __cray_mpi_memcpy_int_31B

.align 32
__cray_mpi_memcpy_int_aligned_range1_32B_loop:
    vmovaps 0x00(%rsi), %xmm0
    vmovaps %xmm0, 0x10(%rdi)
    vmovaps 0x10(%rsi), %xmm1
    vmovaps %xmm1, 0x00(%rdi)

    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $31, %rdx
    jg      __cray_mpi_memcpy_int_aligned_range1_32B_loop

    test    %rdx, %rdx
    jz      __cray_mpi_memcpy_int_ret   # Fast exit for n*32 size

    jmp     __cray_mpi_memcpy_int_31B

########### Unaligned Large Loop #############
__cray_mpi_memcpy_int_unaligned:
    mov     %rsi, %r10
    mov     %rdi, %r9
    and     $15, %r10
    and     $15, %r9

    cmp     %r10, %r9
    je      __cray_mpi_memcpy_int_fix_both

########### Process Store Unaligned Bytes ##########
__cray_mpi_memcpy_int_fix_st:
    neg     %r9
    test    $8, %r9
    jz      __cray_mpi_memcpy_int_unaligned_st_7B

    mov     0x00(%rsi), %r8
    mov     %r8, 0x00(%rdi)
    add     $8, %rsi
    add     $8, %rdi
    sub     $8, %rdx

__cray_mpi_memcpy_int_unaligned_st_7B:
    test    $4, %r9
    jz      __cray_mpi_memcpy_int_unaligned_st_3B

    mov     0x00(%rsi), %r8d
    mov     %r8d, 0x00(%rdi)
    add     $4, %rsi
    add     $4, %rdi
    sub     $4, %rdx

__cray_mpi_memcpy_int_unaligned_st_3B:
    test    $2, %r9
    jz      __cray_mpi_memcpy_int_unaligned_st_1B

    mov     0x00(%rsi), %r8w
    mov     %r8w, 0x00(%rdi)
    add     $2, %rsi
    add     $2, %rdi
    sub     $2, %rdx

__cray_mpi_memcpy_int_unaligned_st_1B:
    test    $1, %r9
    jz      __cray_mpi_memcpy_int_aligned_st

    mov     0x00(%rsi), %r8b
    mov     %r8b, 0x00(%rdi)
    add     $1, %rsi
    add     $1, %rdi
    sub     $1, %rdx

################# Store Now Aligned ################
__cray_mpi_memcpy_int_aligned_st:
    cmp     $255, %rdx
    jle     __cray_mpi_memcpy_int_unaligned_255B

    cmp     $1024*1024, %rdx
    jl      __cray_mpi_memcpy_int_unaligned_loop_256B

    prefetchnta 0x000(%rsi)
    prefetchnta 0x040(%rsi)
    prefetchnta 0x080(%rsi)
    prefetchnta 0x0c0(%rsi)
    prefetchnta 0x100(%rsi)
    prefetchnta 0x140(%rsi)
    prefetchnta 0x180(%rsi)
    prefetchnta 0x1c0(%rsi)
    prefetchnta 0x200(%rsi)
    prefetchnta 0x240(%rsi)
    prefetchnta 0x280(%rsi)
    prefetchnta 0x2c0(%rsi)

    prefetchw 0x000(%rdi)
    prefetchw 0x040(%rdi)
    prefetchw 0x080(%rdi)
    prefetchw 0x0c0(%rdi)
    prefetchw 0x100(%rdi)
    prefetchw 0x140(%rdi)
    prefetchw 0x180(%rdi)
    prefetchw 0x1c0(%rdi)
    prefetchw 0x200(%rdi)
    prefetchw 0x240(%rdi)
    prefetchw 0x280(%rdi)
    prefetchw 0x2c0(%rdi)

.align 32
__cray_mpi_memcpy_int_unaligned_loop_256B_nta:
    prefetchnta 0x300(%rsi)
    prefetchnta 0x340(%rsi)
    prefetchnta 0x380(%rsi)
    prefetchnta 0x3c0(%rsi)

    prefetchw 0x300(%rdi)
    prefetchw 0x340(%rdi)
    prefetchw 0x380(%rdi)
    prefetchw 0x3c0(%rdi)

    vmovups 0x00(%rsi), %xmm0
    vmovups 0x10(%rsi), %xmm1
    vmovups 0x20(%rsi), %xmm2
    vmovups 0x30(%rsi), %xmm3
    vmovups 0x40(%rsi), %xmm4
    vmovups 0x50(%rsi), %xmm5
    vmovups 0x60(%rsi), %xmm6
    vmovups 0x70(%rsi), %xmm7
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps %xmm1, 0x10(%rdi)
    vmovaps %xmm2, 0x20(%rdi)
    vmovaps %xmm3, 0x30(%rdi)
    vmovaps %xmm4, 0x40(%rdi)
    vmovaps %xmm5, 0x50(%rdi)
    vmovaps %xmm6, 0x60(%rdi)
    vmovaps %xmm7, 0x70(%rdi)

    add     $128, %rsi
    add     $128, %rdi

    vmovups 0x00(%rsi), %xmm0
    vmovups 0x10(%rsi), %xmm1
    vmovups 0x20(%rsi), %xmm2
    vmovups 0x30(%rsi), %xmm3
    vmovups 0x40(%rsi), %xmm4
    vmovups 0x50(%rsi), %xmm5
    vmovups 0x60(%rsi), %xmm6
    vmovups 0x70(%rsi), %xmm7
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps %xmm1, 0x10(%rdi)
    vmovaps %xmm2, 0x20(%rdi)
    vmovaps %xmm3, 0x30(%rdi)
    vmovaps %xmm4, 0x40(%rdi)
    vmovaps %xmm5, 0x50(%rdi)
    vmovaps %xmm6, 0x60(%rdi)
    vmovaps %xmm7, 0x70(%rdi)

    add     $128, %rsi
    add     $128, %rdi
    sub     $256, %rdx
    cmp     $255, %rdx
    jg      __cray_mpi_memcpy_int_unaligned_loop_256B_nta

__cray_mpi_memcpy_int_unaligned_255B_nta:
    sfence
    
    test    %rdx, %rdx
    jz      __cray_mpi_memcpy_int_ret

    cmp     $31, %rdx
    jle     __cray_mpi_memcpy_int_31B

.align 32
__cray_mpi_memcpy_int_unaligned_loop_32B_nta:
    vmovupd 0x00(%rsi), %xmm0
    vmovupd 0x10(%rsi), %xmm1
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps %xmm1, 0x10(%rdi)
 
    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $31, %rdx
    jg      __cray_mpi_memcpy_int_unaligned_loop_32B_nta

    sfence

    test    %rdx, %rdx
    jz      __cray_mpi_memcpy_int_ret   # Fast exit for n*32 size

    jmp     __cray_mpi_memcpy_int_31B

.align 32
__cray_mpi_memcpy_int_unaligned_loop_256B:
    vmovups 0x00(%rsi), %xmm0
    vmovups 0x10(%rsi), %xmm1
    vmovups 0x20(%rsi), %xmm2
    vmovups 0x30(%rsi), %xmm3
    vmovups 0x40(%rsi), %xmm4
    vmovups 0x50(%rsi), %xmm5
    vmovups 0x60(%rsi), %xmm6
    vmovups 0x70(%rsi), %xmm7
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps %xmm1, 0x10(%rdi)
    vmovaps %xmm2, 0x20(%rdi)
    vmovaps %xmm3, 0x30(%rdi)
    vmovaps %xmm4, 0x40(%rdi)
    vmovaps %xmm5, 0x50(%rdi)
    vmovaps %xmm6, 0x60(%rdi)
    vmovaps %xmm7, 0x70(%rdi)

    add     $128, %rsi
    add     $128, %rdi

    vmovups 0x00(%rsi), %xmm0
    vmovups 0x10(%rsi), %xmm1
    vmovups 0x20(%rsi), %xmm2
    vmovups 0x30(%rsi), %xmm3
    vmovups 0x40(%rsi), %xmm4
    vmovups 0x50(%rsi), %xmm5
    vmovups 0x60(%rsi), %xmm6
    vmovups 0x70(%rsi), %xmm7
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps %xmm1, 0x10(%rdi)
    vmovaps %xmm2, 0x20(%rdi)
    vmovaps %xmm3, 0x30(%rdi)
    vmovaps %xmm4, 0x40(%rdi)
    vmovaps %xmm5, 0x50(%rdi)
    vmovaps %xmm6, 0x60(%rdi)
    vmovaps %xmm7, 0x70(%rdi)

    add     $128, %rsi
    add     $128, %rdi
    sub     $256, %rdx
    cmp     $255, %rdx
    jg      __cray_mpi_memcpy_int_unaligned_loop_256B

__cray_mpi_memcpy_int_unaligned_255B:
    test    %rdx, %rdx
    jz      __cray_mpi_memcpy_int_ret

    cmp     $31, %rdx
    jle     __cray_mpi_memcpy_int_31B

.align 32
__cray_mpi_memcpy_int_unaligned_loop_32B:
    vmovups 0x00(%rsi), %xmm0
    vmovups 0x10(%rsi), %xmm1
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps %xmm1, 0x10(%rdi)

    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $31, %rdx
    jg      __cray_mpi_memcpy_int_unaligned_loop_32B
    
    test    %rdx, %rdx
    jz      __cray_mpi_memcpy_int_ret   # Fast exit for n*32 size
    
    jmp     __cray_mpi_memcpy_int_31B

########### Process Equal Unaligned Bytes ##########
__cray_mpi_memcpy_int_fix_both:
    neg     %r10
    add     $16, %r10
    test    $8, %r10
    jz      __cray_mpi_memcpy_int_unaligned_7B

    mov     0x00(%rsi), %r8
    mov     %r8, 0x00(%rdi)
    add     $8, %rsi
    add     $8, %rdi
    sub     $8, %rdx

__cray_mpi_memcpy_int_unaligned_7B:
    test    $4, %r10
    jz      __cray_mpi_memcpy_int_unaligned_3B

    mov     0x00(%rsi), %r8d
    mov     %r8d, 0x00(%rdi)
    add     $4, %rsi
    add     $4, %rdi
    sub     $4, %rdx

__cray_mpi_memcpy_int_unaligned_3B:
    test    $2, %r10
    jz      __cray_mpi_memcpy_int_unaligned_1B

    mov     0x00(%rsi), %r8w
    mov     %r8w, 0x00(%rdi)
    add     $2, %rsi
    add     $2, %rdi
    sub     $2, %rdx

__cray_mpi_memcpy_int_unaligned_1B:
    test    $1, %r10
    jz      __cray_mpi_memcpy_int_aligned

    mov     0x00(%rsi), %r8b
    mov     %r8b, 0x00(%rdi)
    add     $1, %rsi
    add     $1, %rdi
    sub     $1, %rdx

    jmp     __cray_mpi_memcpy_int_aligned
