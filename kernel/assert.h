//
//  assert.h
//  SPARC
//
//  Created by Valtteri Koskivuori on 11.12.2019.
//  Copyright © 2019-2020 Valtteri Koskivuori. All rights reserved.
//

#pragma once

void assertFailed(const char *file, const char *func, int line, const char *expr);

#define ASSERT(expr) \
	if ((expr)) \
		{} \
	else \
		assertFailed(__FILE__, __FUNCTION__, __LINE__, #expr)

#define ASSERT_NOT_REACHED() \
	assertFailed(__FILE__, __FUNCTION__, __LINE__, "ASSERT_NOT_REACHED")


