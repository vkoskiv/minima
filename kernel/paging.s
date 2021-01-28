//
//  paging.s
//  xcode
//
//  Created by Valtteri Koskivuori on 28/01/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

.text
.globl loadPageDirectory
loadPageDirectory:
push %ebp
mov %esp, %ebp
mov 8(%esp), %eax
mov %eax, %cr3
mov %ebp, %esp
pop %ebp
ret

.text
.globl enablePaging
enablePaging:
push %ebp
mov %esp, %ebp
mov %cr0, %eax
or $0x80000000, %eax
mov %eax, %cr0
mov %ebp, %esp
pop %ebp
ret
