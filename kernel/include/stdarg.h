//
//  stdarg.h
//  minima
//
//  Created by Valtteri Koskivuori on 04/02/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#pragma once

// For now, we'll stick with gcc's built-in VA stuff

typedef __builtin_va_list va_list;

#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v, l) __builtin_va_arg(v, l)
