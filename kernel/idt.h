//
//  idt.h
//  xcode
//
//  Created by Valtteri on 25.1.2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#pragma once

void idt_init(void);

// Stop interrupts
void cli(void);

// Restore interrupts
void sti(void);
