/* The search of reference_list_remove (source/structures/cluster_partitions.c), from the bundle's wii_reference_find.S
 * (the Broadway assembly bundle of 2026-11-01, CC0, see README.md here), unchanged in what it does: integer work only.
 *
 * r3 = the 12-byte references, r4 = the count of them in use (not negative), r5 = whether an identifier of zero is invalid
 * (the data array's identifier_zero_invalid), r6 = the link that holds the first index, r7 = the datum index.
 * Returns the link that holds the index of the reference with that datum index, or NULL when the checked C has to run: it
 * is not on the list, or a reference on the way is one that datum_get (source/memory/data.c) would not return.
 * Validates before dereferencing. Volatile EABI registers only.
 */
    .machine "broadway"
    .section .text.halo_bw_reference,"ax",@progbits
    .balign 4
    .global halo_bw_range_reference_begin
halo_bw_range_reference_begin:
    .global halo_bw_asm_reference_find
    .type halo_bw_asm_reference_find,@function
halo_bw_asm_reference_find:
1:
    lwz         8,0(6)
    cmpwi       8,-1
    beq         4f
    extsh       9,8
    cmplw       9,4
    bge         4f
    srwi        11,8,16
    cmpwi       11,0
    bne         2f
    cmpwi       5,0
    bne         4f
2:
    mulli       9,9,12
    add         12,3,9
    lhz         10,0(12)
    cmpwi       10,0
    beq         4f
    cmpwi       11,0
    beq         3f
    cmplw       10,11
    bne         4f
3:
    lwz         9,4(12)
    cmpw        9,7
    beq         5f
    addi        6,12,8
    b           1b
4:
    li          3,0
    blr
5:
    mr          3,6
    blr
    .size halo_bw_asm_reference_find,.-halo_bw_asm_reference_find
    .global halo_bw_range_reference_end
halo_bw_range_reference_end:
