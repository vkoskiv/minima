//
//  idt.h
//
//  Created by Valtteri on 25.1.2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#pragma once

void eoi(unsigned char irq);
void idt_init(void);
