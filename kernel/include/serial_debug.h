//
//  serial_debug.h
//
//  Created by Valtteri Koskivuori on 03/02/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#pragma once

void serial_setup(void);
void serial_out_byte(char c);
void serial_enable_buffering(void);
