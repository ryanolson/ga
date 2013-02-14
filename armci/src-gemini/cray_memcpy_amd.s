#
# Copyright  2013  Cray Inc.  All Rights Reserved.
#
# This is /cray/css/users/cja/cray_memcpy.15.s
# memcpy implementation optimzied for Sandy Bridge (SNB) hardware.
# Written by Chris Ashton (cja)
#
# vim: ts=8:sts=4:sw=4:et

# FIXMEs:
# * Try to get ymm working in more cases (align to 32B)
# ** Improve performance where output unaligned > 64B
# ** Improve performance for input unaligned sizes > 2MB
# *** Fix regression where output is offset 16B

# Changelog:
# v.15:Fix index error in 32B peel off loops, error in extracted offsets
# v.14:Change v13 so it builds under gas 2.19, v.13 should be slightly better perf
# v.13: ???
# v.12:Switch medium sized aligned to prefetcht0 to keep lines around
# v.11:Improve unaligned performance
#      Add additional path for unaligned
#      Switch range3 (large) unaligned to avx in order NTA
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
    .globl  _cray_armci_memcpy_amd
    .type   _cray_armci_memcpy_amd,@function
    .globl  _cray_armci_memcpy_amd_
    .type   _cray_armci_memcpy_amd_,@function

.align 32
_cray_armci_memcpy_amd:
_cray_armci_memcpy_amd_:
__cray_armci_memcpy_amd:
    # __dst = %rdi
    # __src = %rsi
    # __n   = %rdx
    # return __dst in %rax
    mov     %rdi, %rax  # rax = return value

    cmp     $256, %rdx
    jge     __cray_armci_memcpy_large

#    test    $0xf0, %rdx
#    jz      __cray_armci_memcpy_15B

########### Small Path #############
__cray_armci_memcpy_255B:
    test    $128, %rdx
    jz      __cray_armci_memcpy_127B

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
    jz      __cray_armci_memcpy_ret   # Fast exit for n*128 size

__cray_armci_memcpy_127B:
    test    $64, %rdx
    jz      __cray_armci_memcpy_63B

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
    jz      __cray_armci_memcpy_ret   # Fast exit for n*64 size

__cray_armci_memcpy_63B:
    test    $32, %rdx
    jz      __cray_armci_memcpy_31B

    vmovups 0x00(%rsi), %xmm0
    vmovups 0x10(%rsi), %xmm1
    vmovups %xmm0, 0x00(%rdi)
    vmovups %xmm1, 0x10(%rdi)
    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    jz      __cray_armci_memcpy_ret   # Fast exit for n*32 size

__cray_armci_memcpy_31B:
    test    $16, %rdx
    jz      __cray_armci_memcpy_15B

    vmovups 0x00(%rsi), %xmm0
    vmovups %xmm0, 0x00(%rdi)
    add     $16, %rsi
    add     $16, %rdi
    sub     $16, %rdx
    jz      __cray_armci_memcpy_ret   # Fast exit for n*16 size

__cray_armci_memcpy_15B:
    test    $8, %rdx
    jz      __cray_armci_memcpy_7B

    mov     0x00(%rsi), %r8
    mov     %r8, 0x00(%rdi)
    add     $8, %rsi
    add     $8, %rdi
    sub     $8, %rdx
    jz      __cray_armci_memcpy_ret   # Fast exit for n*8 size

__cray_armci_memcpy_7B:
    test    $4, %rdx
    jz      __cray_armci_memcpy_3B

    mov     0x00(%rsi), %r8d
    mov     %r8d, 0x00(%rdi)
    add     $4, %rsi
    add     $4, %rdi
    sub     $4, %rdx
    jz      __cray_armci_memcpy_ret   # Fast exit for n*4 size

__cray_armci_memcpy_3B:
    test    $2, %rdx
    jz      __cray_armci_memcpy_1B

    mov     0x00(%rsi), %r8w
    mov     %r8w, 0x00(%rdi)
    add     $2, %rsi
    add     $2, %rdi
    sub     $2, %rdx

__cray_armci_memcpy_1B:
    test    $1, %rdx
    jz      __cray_armci_memcpy_ret

    mov     0x00(%rsi), %r8b
    mov     %r8b, 0x00(%rdi)

__cray_armci_memcpy_ret:
    ret

################ Large Path #############
__cray_armci_memcpy_large:
    mov     %rdi, %r10
    or      %rsi, %r10
    and     $31, %r10
    jnz     __cray_armci_memcpy_unaligned

##################################################
############## 32B Aligned Large Loop ############
##################################################
__cray_armci_memcpy_32B_aligned:
    cmp     $255, %rdx
    jle     __cray_armci_memcpy_32B_aligned_range1_255B

    cmp     $4*1024, %rdx
    jl      __cray_armci_memcpy_32B_aligned_range1_entry

    cmp     $1024*1024, %rdx
    jl      __cray_armci_memcpy_32B_aligned_range2_entry

############
# For Range 3 [2^20B,],
# [X] Alternate loads and stores
# [X] Prefetch loads
# [ ] Prefetch stores
# [X] NT loads
# [X] NT stores
############
__cray_armci_memcpy_32B_aligned_range3_entry:
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
__cray_armci_memcpy_32B_aligned_range3_256B_loop:
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
    jg      __cray_armci_memcpy_32B_aligned_range3_256B_loop

__cray_armci_memcpy_32B_aligned_range3_255B:
    sfence

    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret

    cmp     $31, %rdx
    jle     __cray_armci_memcpy_31B

__cray_armci_memcpy_32B_aligned_range3_32B_loop:
    vmovntdqa 0x00(%rsi), %xmm0
    vmovntdq %xmm0, 0x00(%rdi)
    vmovntdqa 0x10(%rsi), %xmm1
    vmovntdq %xmm1, 0x10(%rdi)

    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $31, %rdx
    jg      __cray_armci_memcpy_32B_aligned_range3_32B_loop

    sfence

    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret

    jmp     __cray_armci_memcpy_31B

############
# For Range 2 [2^12B,2^20B],
# [ ] Alternate loads and stores
# [X] Prefetch loads
# [ ] Prefetch stores
# [ ] NT loads
# [ ] NT stores
############
__cray_armci_memcpy_32B_aligned_range2_entry:
    prefetcht0 0x000(%rsi)
    prefetcht0 0x040(%rsi)
    prefetcht0 0x080(%rsi)
    prefetcht0 0x0c0(%rsi)
    prefetcht0 0x100(%rsi)
    prefetcht0 0x140(%rsi)
    prefetcht0 0x180(%rsi)
    prefetcht0 0x1c0(%rsi)
    prefetcht0 0x200(%rsi)
    prefetcht0 0x240(%rsi)
    prefetcht0 0x280(%rsi)
    prefetcht0 0x2c0(%rsi)

.align 32
__cray_armci_memcpy_32B_aligned_range2_256B_loop:
    prefetcht0 0x300(%rsi)
    prefetcht0 0x340(%rsi)
    prefetcht0 0x380(%rsi)
    prefetcht0 0x3c0(%rsi)

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
    jg      __cray_armci_memcpy_32B_aligned_range2_256B_loop

__cray_armci_memcpy_32B_aligned_range2_255B:
    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret

    cmp     $31, %rdx
    jle     __cray_armci_memcpy_31B

.align 32
__cray_armci_memcpy_32B_aligned_range2_32B_loop:
    vmovaps 0x00(%rsi), %xmm0
    vmovaps 0x10(%rsi), %xmm1
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps %xmm1, 0x10(%rdi)

    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $31, %rdx
    jg      __cray_armci_memcpy_32B_aligned_range2_32B_loop

    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret

    jmp     __cray_armci_memcpy_31B

############
# For Range 1 [256B,2^12B],
# [X] Alternate loads and stores
# [ ] Prefetch loads
# [ ] Prefetch stores
# [ ] NT loads
# [ ] NT stores
############
.align 32
__cray_armci_memcpy_32B_aligned_range1_entry:
__cray_armci_memcpy_32B_aligned_range1_256B_loop:
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
    jg      __cray_armci_memcpy_32B_aligned_range1_256B_loop

__cray_armci_memcpy_32B_aligned_range1_255B:
    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret

    cmp     $31, %rdx
    jle     __cray_armci_memcpy_31B

.align 32
__cray_armci_memcpy_32B_aligned_range1_32B_loop:
    vmovaps 0x00(%rsi), %xmm0
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps 0x10(%rsi), %xmm1
    vmovaps %xmm1, 0x10(%rdi)

    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $31, %rdx
    jg      __cray_armci_memcpy_32B_aligned_range1_32B_loop

    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret   # Fast exit for n*32 size

    jmp     __cray_armci_memcpy_31B

##################################################
############## 16B Aligned Large Loop ############
##################################################
__cray_armci_memcpy_16B_aligned:
    cmp     $255, %rdx
    jle     __cray_armci_memcpy_16B_aligned_range1_255B

    cmp     $4*1024, %rdx
    jl      __cray_armci_memcpy_16B_aligned_range1_entry

    cmp     $1024*1024, %rdx
    jl      __cray_armci_memcpy_16B_aligned_range2_entry

############
# For Range 3 [2^20B,],
# [X] Alternate loads and stores
# [X] Prefetch loads
# [ ] Prefetch stores
# [X] NT loads
# [X] NT stores
############
__cray_armci_memcpy_16B_aligned_range3_entry:
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
__cray_armci_memcpy_16B_aligned_range3_256B_loop:
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
    jg      __cray_armci_memcpy_16B_aligned_range3_256B_loop

__cray_armci_memcpy_16B_aligned_range3_255B:
    sfence

    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret

    cmp     $31, %rdx
    jle     __cray_armci_memcpy_31B

__cray_armci_memcpy_16B_aligned_range3_32B_loop:
    vmovntdqa 0x00(%rsi), %xmm0
    vmovntdq %xmm0, 0x00(%rdi)
    vmovntdqa 0x10(%rsi), %xmm1
    vmovntdq %xmm1, 0x10(%rdi)

    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $31, %rdx
    jg      __cray_armci_memcpy_16B_aligned_range3_32B_loop

    sfence

    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret

    jmp     __cray_armci_memcpy_31B

############
# For Range 2 [2^12B,2^20B],
# [ ] Alternate loads and stores
# [X] Prefetch loads
# [ ] Prefetch stores
# [ ] NT loads
# [ ] NT stores
############
__cray_armci_memcpy_16B_aligned_range2_entry:
    prefetcht0 0x000(%rsi)
    prefetcht0 0x040(%rsi)
    prefetcht0 0x080(%rsi)
    prefetcht0 0x0c0(%rsi)
    prefetcht0 0x100(%rsi)
    prefetcht0 0x140(%rsi)
    prefetcht0 0x180(%rsi)
    prefetcht0 0x1c0(%rsi)
    prefetcht0 0x200(%rsi)
    prefetcht0 0x240(%rsi)
    prefetcht0 0x280(%rsi)
    prefetcht0 0x2c0(%rsi)

.align 32
__cray_armci_memcpy_16B_aligned_range2_256B_loop:
    prefetcht0 0x300(%rsi)
    prefetcht0 0x340(%rsi)
    prefetcht0 0x380(%rsi)
    prefetcht0 0x3c0(%rsi)

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
    jg      __cray_armci_memcpy_16B_aligned_range2_256B_loop

__cray_armci_memcpy_16B_aligned_range2_255B:
    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret

    cmp     $31, %rdx
    jle     __cray_armci_memcpy_31B

.align 32
__cray_armci_memcpy_16B_aligned_range2_32B_loop:
    vmovaps 0x00(%rsi), %xmm0
    vmovaps 0x10(%rsi), %xmm1
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps %xmm1, 0x10(%rdi)

    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $31, %rdx
    jg      __cray_armci_memcpy_16B_aligned_range2_32B_loop

    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret

    jmp     __cray_armci_memcpy_31B

############
# For Range 1 [256B,2^12B],
# [X] Alternate loads and stores
# [ ] Prefetch loads
# [ ] Prefetch stores
# [ ] NT loads
# [ ] NT stores
############
.align 32
__cray_armci_memcpy_16B_aligned_range1_entry:
__cray_armci_memcpy_16B_aligned_range1_256B_loop:
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
    jg      __cray_armci_memcpy_16B_aligned_range1_256B_loop

__cray_armci_memcpy_16B_aligned_range1_255B:
    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret

    cmp     $31, %rdx
    jle     __cray_armci_memcpy_31B

.align 32
__cray_armci_memcpy_16B_aligned_range1_32B_loop:
    vmovaps 0x00(%rsi), %xmm0
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps 0x10(%rsi), %xmm1
    vmovaps %xmm1, 0x10(%rdi)

    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $31, %rdx
    jg      __cray_armci_memcpy_16B_aligned_range1_32B_loop

    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret   # Fast exit for n*32 size

    jmp     __cray_armci_memcpy_31B

###################################
########### Unaligned #############
##### Peel To Aligned Stores ######
###################################
__cray_armci_memcpy_unaligned:
    mov     %rdi, %r9
    neg     %r9 # Invert, add 1
__cray_armci_memcpy_peel_31B:
    test    $16, %r9
    jz      __cray_armci_memcpy_peel_15B

    mov     0x00(%rsi), %r8
    mov     %r8, 0x00(%rdi)
    mov     0x08(%rsi), %r8
    mov     %r8, 0x08(%rdi)
    add     $16, %rsi
    add     $16, %rdi
    sub     $16, %rdx

__cray_armci_memcpy_peel_15B:
    test    $8, %r9
    jz      __cray_armci_memcpy_peel_7B

    mov     0x00(%rsi), %r8
    mov     %r8, 0x00(%rdi)
    add     $8, %rsi
    add     $8, %rdi
    sub     $8, %rdx

__cray_armci_memcpy_peel_7B:
    test    $4, %r9
    jz      __cray_armci_memcpy_peel_3B

    mov     0x00(%rsi), %r8d
    mov     %r8d, 0x00(%rdi)
    add     $4, %rsi
    add     $4, %rdi
    sub     $4, %rdx

__cray_armci_memcpy_peel_3B:
    test    $2, %r9
    jz      __cray_armci_memcpy_peel_1B

    mov     0x00(%rsi), %r8w
    mov     %r8w, 0x00(%rdi)
    add     $2, %rsi
    add     $2, %rdi
    sub     $2, %rdx

__cray_armci_memcpy_peel_1B:
    test    $1, %r9
    jz      __cray_armci_memcpy_32B_aligned_st

    mov     0x00(%rsi), %r8b
    mov     %r8b, 0x00(%rdi)
    add     $1, %rsi
    add     $1, %rdi
    sub     $1, %rdx

##########################################
############ Store Now Aligned ###########
##########################################
__cray_armci_memcpy_32B_aligned_st:
    # Test for 32B aligned loads
    test    $0x1f, %rsi
    jz      __cray_armci_memcpy_32B_aligned

    # Test for 16B aligned loads
    test    $0xf, %rsi
    jz      __cray_armci_memcpy_16B_aligned

    # Test for smaller sized moves
    cmp     $255, %rdx
    jle     __cray_armci_memcpy_unaligned_range1_255B

    cmp     $8*1024, %rdx
    jl      __cray_armci_memcpy_unaligned_range1_entry

    cmp     $4*1024*1024, %rdx
    jl      __cray_armci_memcpy_unaligned_range2_entry

__cray_armci_memcpy_unaligned_range3_entry:
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
__cray_armci_memcpy_unaligned_range3_256B_loop:
    prefetchnta 0x300(%rsi)
    prefetchnta 0x340(%rsi)
    prefetchnta 0x380(%rsi)
    prefetchnta 0x3c0(%rsi)

    vmovups 0x00(%rsi), %ymm0
    vmovups 0x20(%rsi), %ymm1
    vmovups 0x40(%rsi), %ymm2
    vmovups 0x60(%rsi), %ymm3
    vmovups 0x80(%rsi), %ymm4
    vmovups 0xa0(%rsi), %ymm5
    vmovups 0xc0(%rsi), %ymm6
    vmovups 0xe0(%rsi), %ymm7
    vmovups 0x100(%rsi), %ymm8
    vmovups 0x120(%rsi), %ymm9
    vmovups 0x140(%rsi), %ymm10
    vmovups 0x160(%rsi), %ymm11
    vmovups 0x180(%rsi), %ymm12
    vmovups 0x1a0(%rsi), %ymm13
    vmovups 0x1c0(%rsi), %ymm14
    vmovups 0x1e0(%rsi), %ymm15
    vmovntdq %xmm0, 0x00(%rdi)
    vextractf128 $1, %ymm0, %xmm0
    vmovntdq %xmm0, 0x10(%rdi)
    vmovntdq %xmm1, 0x20(%rdi)
    vextractf128 $1, %ymm1, %xmm1
    vmovntdq %xmm1, 0x30(%rdi)
    vmovntdq %xmm2, 0x40(%rdi)
    vextractf128 $1, %ymm2, %xmm2
    vmovntdq %xmm2, 0x50(%rdi)
    vmovntdq %xmm3, 0x60(%rdi)
    vextractf128 $1, %ymm3, %xmm3
    vmovntdq %xmm3, 0x70(%rdi)
    vmovntdq %xmm4, 0x80(%rdi)
    vextractf128 $1, %ymm4, %xmm4
    vmovntdq %xmm4, 0x90(%rdi)
    vmovntdq %xmm5, 0xa0(%rdi)
    vextractf128 $1, %ymm5, %xmm5
    vmovntdq %xmm5, 0xb0(%rdi)
    vmovntdq %xmm6, 0xc0(%rdi)
    vextractf128 $1, %ymm6, %xmm6
    vmovntdq %xmm6, 0xd0(%rdi)
    vmovntdq %xmm7, 0xe0(%rdi)
    vextractf128 $1, %ymm7, %xmm7
    vmovntdq %xmm7, 0xf0(%rdi)
    vmovntdq %xmm8, 0x100(%rdi)
    vextractf128 $1, %ymm8, %xmm8
    vmovntdq %xmm8, 0x110(%rdi)
    vmovntdq %xmm9, 0x120(%rdi)
    vextractf128 $1, %ymm9, %xmm9
    vmovntdq %xmm9, 0x130(%rdi)
    vmovntdq %xmm10, 0x140(%rdi)
    vextractf128 $1, %ymm10, %xmm10
    vmovntdq %xmm10, 0x150(%rdi)
    vmovntdq %xmm11, 0x160(%rdi)
    vextractf128 $1, %ymm11, %xmm11
    vmovntdq %xmm11, 0x170(%rdi)
    vmovntdq %xmm12, 0x180(%rdi)
    vextractf128 $1, %ymm12, %xmm12
    vmovntdq %xmm12, 0x190(%rdi)
    vmovntdq %xmm13, 0x1a0(%rdi)
    vextractf128 $1, %ymm13, %xmm13
    vmovntdq %xmm13, 0x1b0(%rdi)
    vmovntdq %xmm14, 0x1c0(%rdi)
    vextractf128 $1, %ymm14, %xmm14
    vmovntdq %xmm14, 0x1d0(%rdi)
    vmovntdq %xmm15, 0x1e0(%rdi)
    vextractf128 $1, %ymm15, %xmm15
    vmovntdq %xmm15, 0x1f0(%rdi)

    add     $512, %rsi
    add     $512, %rdi
    sub     $512, %rdx
    cmp     $511, %rdx
    jg      __cray_armci_memcpy_unaligned_range3_256B_loop

__cray_armci_memcpy_unaligned_range3_255B:
    sfence

    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret

    cmp     $31, %rdx
    jle     __cray_armci_memcpy_31B

.align 32
__cray_armci_memcpy_unaligned_range3_32B_loop:
    vmovups 0x00(%rsi), %xmm0
    vmovaps %xmm0, 0x00(%rdi)
    vmovups 0x10(%rsi), %xmm1
    vmovaps %xmm1, 0x10(%rdi)

    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $31, %rdx
    jg      __cray_armci_memcpy_unaligned_range3_32B_loop

    sfence

    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret   # Fast exit for n*32 size

    jmp     __cray_armci_memcpy_31B

__cray_armci_memcpy_unaligned_range2_entry:
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
__cray_armci_memcpy_unaligned_range2_256B_loop:
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
    jg      __cray_armci_memcpy_unaligned_range2_256B_loop

__cray_armci_memcpy_unaligned_range2_255B:
    sfence

    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret

    cmp     $31, %rdx
    jle     __cray_armci_memcpy_31B

.align 32
__cray_armci_memcpy_unaligned_range2_32B_loop:
    vmovupd 0x00(%rsi), %xmm0
    vmovupd 0x10(%rsi), %xmm1
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps %xmm1, 0x10(%rdi)

    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $31, %rdx
    jg      __cray_armci_memcpy_unaligned_range2_32B_loop

    sfence

    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret   # Fast exit for n*32 size

    jmp     __cray_armci_memcpy_31B

.align 32
__cray_armci_memcpy_unaligned_range1_entry:
__cray_armci_memcpy_unaligned_range1_256B_loop:
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
    jg      __cray_armci_memcpy_unaligned_range1_256B_loop

__cray_armci_memcpy_unaligned_range1_255B:
    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret

    cmp     $31, %rdx
    jle     __cray_armci_memcpy_31B

.align 32
__cray_armci_memcpy_unaligned_range1_32B_loop:
    vmovups 0x00(%rsi), %xmm0
    vmovups 0x10(%rsi), %xmm1
    vmovaps %xmm0, 0x00(%rdi)
    vmovaps %xmm1, 0x10(%rdi)

    add     $32, %rsi
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $31, %rdx
    jg      __cray_armci_memcpy_unaligned_range1_32B_loop

    test    %rdx, %rdx
    jz      __cray_armci_memcpy_ret

    jmp     __cray_armci_memcpy_31B
