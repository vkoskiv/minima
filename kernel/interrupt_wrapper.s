//
//  interrupt_wrapper.s
//  xcode
//
//  Created by Valtteri Koskivuori on 24/01/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

/* filename: isr_wrapper.s */
.globl   isr_wrapper
.align   4

isr_wrapper:
pushal
cld /* C code following the sysV ABI requires DF to be clear on function entry */
call interrupt_handler
popal
iret
