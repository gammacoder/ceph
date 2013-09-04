// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include <string.h>
#include "include/crc32c.h"

#include "gtest/gtest.h"

TEST(Crc32c, Small) {
  const char *a = "foo bar baz";
  const char *b = "whiz bang boom";
  ASSERT_EQ(4119623852u, ceph_crc32c(0, (unsigned char *)a, strlen(a)));
  ASSERT_EQ(881700046u, ceph_crc32c(1234, (unsigned char *)a, strlen(a)));
  ASSERT_EQ(2360230088u, ceph_crc32c(0, (unsigned char *)b, strlen(b)));
  ASSERT_EQ(3743019208u, ceph_crc32c(5678, (unsigned char *)b, strlen(b)));
}

TEST(Crc32c, Big) {
  int len = 4096000;
  char *a = (char *)malloc(len);
  memset(a, 1, len);
  ASSERT_EQ(31583199u, ceph_crc32c(0, (unsigned char *)a, len));
  ASSERT_EQ(1400919119u, ceph_crc32c(1234, (unsigned char *)a, len));
}

