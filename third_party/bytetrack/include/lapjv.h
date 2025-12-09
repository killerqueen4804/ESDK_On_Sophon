//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// SOPHON-DEMO is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#ifndef BYTETRACK_LAPJV_H
#define BYTETRACK_LAPJV_H

#define LARGE 1000000

#if !defined TRUE
#define TRUE 1
#endif
#if !defined FALSE
#define FALSE 0
#endif

#define NEW(x, t, n)                            \
  if ((x = (t*)malloc(sizeof(t) * (n))) == 0) { \
    return -1;                                  \
  }
#define FREE(x) \
  if (x != 0) { \
    free(x);    \
    x = 0;      \
  }
#define SWAP_INDICES(a, b) \
  {                        \
    int_t _temp_index = a; \
    a = b;                 \
    b = _temp_index;       \
  }

// 调试宏（默认关闭）
#define ASSERT(cond)
#define PRINTF(fmt, ...)
#define PRINT_COST_ARRAY(a, n)
#define PRINT_INDEX_ARRAY(a, n)

typedef signed int int_t;
typedef unsigned int uint_t;
typedef double cost_t;
typedef char boolean;
typedef enum fp_t { FP_1 = 1, FP_2 = 2, FP_DYNAMIC = 3 } fp_t;

/**
 * @brief 匈牙利算法内部实现
 * @param n 矩阵大小
 * @param cost 代价矩阵
 * @param x 行分配结果
 * @param y 列分配结果
 * @return 0 成功，-1 失败
 */
extern int_t lapjv_internal(const uint_t n, cost_t* cost[], int_t* x, int_t* y);

#endif  // BYTETRACK_LAPJV_H
