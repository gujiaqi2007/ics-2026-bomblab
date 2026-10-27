#include "defuser.h"

#include <inttypes.h>
#include <stdio.h>

#define MAX_FIELDS 8

/*
 * 把 values 中的 count 个整数写成一行“空格分隔的无符号十进制整数”，存入 out。
 * 成功返回 0；count 为 0（还没有实现）、snprintf 出错或结果被截断时返回非零。
 */
static int write_answer(char *out, size_t cap, const uint32_t *values, size_t count) {
    size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        int size = snprintf(out + used, cap - used, "%s%" PRIu32, i ? " " : "", values[i]);
        if (size < 0 || (size_t)size >= cap - used) return 1;
        used += (size_t)size;
    }
    return count == 0;
}

/* ------------------------------------------------------------------ */
/* 第 1 关：p1_compute 内部依次调用 p1_step2、p1_step0、p1_step1。      */
/* ------------------------------------------------------------------ */
int solve_phase_1(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    uint32_t low = (uint32_t)challenge;             /* challenge 低 32 位 */
    uint32_t high = (uint32_t)(challenge >> 32);    /* challenge 高 32 位 */
    uint32_t mid = (uint32_t)(challenge >> 16);     /* challenge 右移 16 位 */

    /* p1_step2(high, low) = high + 8 * low, 再异或 0x1a5f */
    uint32_t t = (high + 8u * low) ^ UINT32_C(0x1a5f);
    /* p1_step0(low, mid) = (3 * low) ^ (mid + 0x87de) */
    uint32_t a = 3u * low ^ (mid + UINT32_C(0x87de));
    /* p1_step1(a, t) = (3 * a + t) ^ 0x4929 */
    values[0] = (3u * a + t) ^ UINT32_C(0x4929);
    return write_answer(out, cap, values, 1);
}

/* ------------------------------------------------------------------ */
/* 第 2 关：首项由 challenge & 0xf 决定，之后按递推关系逐项求出。      */
/* ------------------------------------------------------------------ */
int solve_phase_2(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    uint32_t current = ((uint32_t)challenge & UINT32_C(0xf)) ^ UINT32_C(0x1c);
    values[0] = current;
    /* 循环用 cl = 28, 33, 38, 43, 48 依次右移，异或常数是 4, 8, 12, 16, 20 */
    for (unsigned i = 0; i < 5; ++i) {
        uint32_t digit = (uint32_t)(challenge >> (28u + 5u * i)) & UINT32_C(0xf);
        current = (digit + current) ^ (UINT32_C(4) * (i + 1));
        values[i + 1] = current;
    }
    return write_answer(out, cap, values, 6);
}

/* ------------------------------------------------------------------ */
/* 第 3 关：challenge & 7 选出跳转表中的一个分支，另一个数是它的返回值 */
/* ------------------------------------------------------------------ */
static uint32_t p3_case(uint32_t index, uint32_t x) {
    switch (index) {
    case 0: return (x ^ UINT32_C(0xff19)) + (x >> 2);
    case 1: return 5u * x + UINT32_C(0x20d73);
    case 2: return 4u * x ^ UINT32_C(0x1ec);
    case 3: return 3u * x + UINT32_C(0xc7dd);
    case 4: return 2u * x ^ UINT32_C(0xb2c3);
    case 5: return 3u * x + UINT32_C(0x60f8);
    case 6: return 5u * x + UINT32_C(0x2adaf);
    default: return (x ^ UINT32_C(0xd628)) + (x >> 1);
    }
}

/* p3 的跳转表，按 .rodata 0x405820 的 8 个表项编码成 p3_case 的编号 */
static const unsigned p3_table[8] = { 2, 0, 5, 7, 1, 3, 4, 6 };

int solve_phase_3(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    uint32_t index = (uint32_t)challenge & UINT32_C(7);
    uint32_t argument = ((uint32_t)(challenge >> 3)) ^ ((uint32_t)(challenge >> 32));

    values[0] = index;
    values[1] = p3_case(p3_table[index], argument);
    return write_answer(out, cap, values, 2);
}

/* ------------------------------------------------------------------ */
/* 第 4 关：p4_walk 是二分递归，把递归展开成“下行 + 回溯”两次循环。    */
/* ------------------------------------------------------------------ */
#define P4_LEVELS 8

int solve_phase_4(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    uint32_t target = (uint32_t)challenge & UINT32_C(0x7f);
    uint32_t key = ((uint32_t)(challenge >> 7)) ^ ((uint32_t)(challenge >> 32));
    uint32_t mids[P4_LEVELS];
    unsigned right[P4_LEVELS];
    size_t depth = 0;
    uint32_t lo = 0, hi = UINT32_C(0x7f);

    for (;;) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        if (target == mid) break;                 /* 递归的基准情形 */
        mids[depth] = mid;
        if (target < mid) {
            right[depth] = 0;
            hi = mid - 1;
        } else {
            right[depth] = 1;
            lo = mid + 1;
        }
        ++depth;
    }

    /* 基准：target + 0x23 再异或 key；回溯时每层乘 3、异或 (mid + key)、再异或方向常数 */
    uint32_t result = (target + UINT32_C(0x23)) ^ key;
    while (depth > 0) {
        --depth;
        uint32_t folded = (UINT32_C(3) * result) ^ (mids[depth] + key);
        result = folded ^ (right[depth] ? UINT32_C(0x19) : UINT32_C(0x1f));
    }

    values[0] = result;
    return write_answer(out, cap, values, 1);
}

/* ------------------------------------------------------------------ */
/* 第 5 关：p5_traverse 从 8 个表项之一出发，沿 next 指针走 1+(ch>>3&3) */
/* 个结点；每个结点贡献自己的 tag 和 fold 值。结点表按 bomb 的 .rodata */
/* 原样编码（next 用下标表示，避免在解法里写绝对地址）。                */
/* ------------------------------------------------------------------ */
struct p5_node {
    int32_t next;      /* -1 表示空指针 */
    uint32_t value;    /* 0x8(%node) */
    uint32_t offset;   /* 0xc(%node) */
};

static const struct p5_node p5_nodes[] = {
    /* 0x405ba0 */ { 8, 361, UINT32_C(0x8ea4) },
    /* 0x405bb0 */ { 7, 1579, UINT32_C(0xe782) },
    /* 0x405bc0 */ { 9, 2395, UINT32_C(0xbcc1) },
    /* 0x405bd0 */ { 2, 1883, UINT32_C(0x50c1) },
    /* 0x405be0 */ { 10, 596, UINT32_C(0x51aa) },
    /* 0x405bf0 */ { 3, 2935, UINT32_C(0x0902) },
    /* 0x405c00 */ { 4, 1337, UINT32_C(0xa50e) },
    /* 0x405c10 */ { -1, 1071, UINT32_C(0x4c38) },
    /* 0x405c20 */ { 6, 45, UINT32_C(0x2a) },
    /* 0x405c30 */ { 11, 892, UINT32_C(0xfd77) },
    /* 0x405c40 */ { 1, 2595, UINT32_C(0x6815) },
    /* 0x405c50 */ { 0, 2108, UINT32_C(0x92b8) },
};

static const uint32_t p5_table[8] = { 5, 3, 2, 9, 11, 0, 8, 6 };

int solve_phase_5(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    uint32_t node = p5_table[(uint32_t)challenge & UINT32_C(7)];
    uint32_t fold = ((uint32_t)(challenge >> 6)) ^ ((uint32_t)(challenge >> 32)) ^ UINT32_C(0xd86a);
    unsigned steps = ((uint32_t)(challenge >> 3)) & UINT32_C(3);

    fold = (fold + p5_nodes[node].offset) * UINT32_C(5);
    uint32_t tag = p5_nodes[node].value;
    fold ^= tag;

    for (unsigned i = 0; i < steps; ++i) {
        if (p5_nodes[node].next < 0) break;   /* 走到链表末尾就停下（本题不会走到） */
        node = (uint32_t)p5_nodes[node].next;
        fold = (fold + p5_nodes[node].offset) * UINT32_C(5);
        tag = p5_nodes[node].value;
        fold ^= tag;
    }

    values[0] = fold;
    values[1] = tag;
    return write_answer(out, cap, values, 2);
}

/* ------------------------------------------------------------------ */
/* 第 6 关：两次查函数指针表，challenge & 7 选第一个，ch>>3 & 7 选第二个 */
/* ------------------------------------------------------------------ */
static uint32_t p6_op(unsigned index, uint32_t x) {
    switch (index) {
    case 0: return (x + UINT32_C(0x3450)) ^ (x >> 3);
    case 2: return ((x << 7) + UINT32_C(0x1f88)) ^ (UINT32_C(5) * x);
    case 4: return (x ^ UINT32_C(0xbb14)) + UINT32_C(4) * x;
    case 3: return (x >> 2) | (x << 30);                       /* ror 2 */
    case 1: return ((x << 2) | (x >> 30)) ^ UINT32_C(0x4ca2);  /* rol 2 */
    default: return ((x >> 3) ^ x) + UINT32_C(0xaa45);
    }
}

/* p6 的函数指针表，按 .rodata 0x405e00 的 8 个表项编码成 op 编号 */
static const unsigned p6_table[8] = { 0, 2, 4, 4, 3, 1, 5, 5 };

int solve_phase_6(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    uint32_t first = p6_table[(uint32_t)challenge & UINT32_C(7)];
    uint32_t second = p6_table[((uint32_t)challenge >> 3) & UINT32_C(7)];
    uint32_t argument = ((uint32_t)(challenge >> 6)) ^ ((uint32_t)(challenge >> 32));

    uint32_t result = p6_op(first, argument) ^ UINT32_C(0x3558);
    result = p6_op(second, result) + UINT32_C(0xdc0f);

    values[0] = result;
    return write_answer(out, cap, values, 1);
}

/* ------------------------------------------------------------------ */
/* Honor：secret_gate 把整行复制进一个结构体，结构体 +0x20 处必须是     */
/* "OPEN"（即行首需要 32 字节前缀），+0x24 起是 secret_phase 读的三个   */
/* 无符号整数。三个数由 challenge 和 context 共同决定。                 */
/* ------------------------------------------------------------------ */
#define SECRET_PREFIX "BOMBLAB-2026-FDU-HONOR-TICKET---"   /* 恰好 32 字节 */
_Static_assert(sizeof(SECRET_PREFIX) - 1 == 32, "Honor 前缀必须正好 32 字节");

int solve_secret(uint64_t challenge, uint64_t context, char *out, size_t cap) {
    uint32_t ch_lo = (uint32_t)challenge;
    uint32_t ch_hi = (uint32_t)(challenge >> 32);
    uint32_t ctx_lo = (uint32_t)context;
    uint32_t ctx_hi = (uint32_t)(context >> 32);

    /* secret_phase 第三个参数（context）的低 2 位就是第一个数 */
    uint32_t first = ctx_lo & UINT32_C(3);
    /* secret_mix( (ch_hi^ch_lo) ^ (ctx_hi^ctx_lo) ) ^ (0xd6b9*first) ^ (context>>16) */
    uint32_t x = (ch_hi ^ ch_lo) ^ (ctx_hi ^ ctx_lo);
    uint32_t mixed = ((((x << 3) | (x >> 29)) + UINT32_C(0xd6b9))) ^ UINT32_C(0x2d7f);
    uint32_t second = mixed ^ (UINT32_C(0xd6b9) * first) ^ (uint32_t)(context >> 16);
    /* 第三个数：第二个数再异或 (ctx_hi^ctx_lo) + first + 0x2d7f + (ch_hi^ch_lo)>>3 */
    uint32_t third = second ^ ((ctx_hi ^ ctx_lo) + first + UINT32_C(0x2d7f) +
                               ((ch_hi ^ ch_lo) >> 3));

    int size = snprintf(out, cap, "%sOPEN%" PRIu32 " %" PRIu32 " %" PRIu32,
                        SECRET_PREFIX, first, second, third);
    return size < 0 || (size_t)size >= cap;
}
