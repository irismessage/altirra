#ifndef f_TEST_H
#define f_TEST_H

#include <at/attest/test.h>

typedef ATTestAssertionException AssertionException;

#define DEFINE_TEST AT_DEFINE_TEST
#define DEFINE_TEST_NONAUTO AT_DEFINE_TEST_NONAUTO

#define TEST_ASSERT AT_TEST_ASSERT
#define TEST_ASSERTF AT_TEST_ASSERTF

#endif
