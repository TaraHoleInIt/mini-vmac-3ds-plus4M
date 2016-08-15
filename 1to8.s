.arm
.code 32


/*
0000    0xFFFF  0xFFFF  0xFFFF  0xFFFF
0001    0xFFFF  0xFFFF  0xFFFF  0x0000
*/

.long BitsTable 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF

// r0    u8* Src
// r1    u16* Dest
// r2    int Size

// r3   current byte
// r4   pixel 1
// r5   pixel 2
// r6   pixel 3
// r7   pixel 4
.global Convert1BPP
Convert1BPP:
    push {r3-r7}

Loop:
    ldrh r3, [r0]!
    add r0, r0, #1

    subs r2, #8
    bne Loop

Done:
    pop {r3-r7}
    bx lr

