/*
 * container_of_step_by_step.c
 * ---------------------------------------------------------------------------
 * 用最小的例子，把 container_of 这个宏拆成零件看。
 *
 * 编译运行：
 *     gcc -std=gnu11 -O0 -g -Wall -Wextra container_of_step_by_step.c -o cofs
 *     ./cofs
 * ---------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stddef.h>

/* ===========================================================================
 * 例子：一个"内嵌成员"的场景
 * ===========================================================================
 *
 *     struct box {
 *         char        tag;      <- 偏移 0
 *         struct item it;       <- 偏移 4  （对齐补了 3 字节）
 *         int         magic;    <- 偏移 12
 *     };
 *
 *   sizeof(struct item) = 8，而 offsetof(struct box, it) = 4。
 *
 *   ★ 这两个数字不相等，是本次演示的关键 —— 它正是"指针步进陷阱"的来源。
 *
 *   问题：手上只有一个 struct item *p（指向 b.it），怎么找回 struct box * ？
 */
struct item {
    int a;
    int b;
};

struct box {
    char        tag;
    struct item it;
    int         magic;
};

/* ===========================================================================
 * 版本 1：标准 C 写法 —— 没有 GCC 扩展，逻辑最清楚
 * ===========================================================================
 *
 *   (char *)(ptr)                  先把指针降级成"字节指针"
 *   - offsetof(type, member)       再退回去 offsetof 这么多"字节"
 *   (type *)                       最后转成目标类型
 */
#define container_of_std(ptr, type, member) \
        ((type *)((char *)(ptr) - offsetof(type, member)))

/* ===========================================================================
 * 版本 2：内核原版 —— 多了类型检查，逻辑完全一样
 * ===========================================================================
 *
 * 逐行看：
 *
 *   ({ ... })                 GCC 的「语句表达式」扩展：允许在表达式的位置
 *                             写一整个语句块，块里最后一条语句的值，
 *                             就是整个块的值。
 *
 *   void *__mptr = (void *)(ptr);
 *                             先把类型擦掉，得到一个"裸地址"。
 *                             为什么必须擦？见 main() 里的 [3] 和 [4]。
 *
 *   _Static_assert(...)       编译期类型检查：你传进来的指针所指类型，
 *                             必须和 ((type *)0)->member 的类型一致。
 *                             传错了直接编译不过，不会等到运行时。
 *
 *   ((type *)(__mptr - offsetof(type, member)))
 *                             裸地址做减法（纯字节算术），再转成目标类型。
 *                             这个值就是整个语句表达式的值。
 */
#define container_of(ptr, type, member) ({                              \
        void *__mptr = (void *)(ptr);                                   \
        _Static_assert(__builtin_types_compatible_p(                    \
                __typeof__(*(ptr)), __typeof__(((type *)0)->member)),   \
                "container_of: pointer type mismatch");                 \
        ((type *)(__mptr - offsetof(type, member))); })

int main(void)
{
    struct box b = { .tag = 'B', .it = { 11, 22 }, .magic = 0xCAFE };

    struct item *p = &b.it;          /* 手上只有这个 —— 一个指向内嵌成员的指针 */

    printf("\n==============================================================\n");
    printf("  container_of 逐层拆解\n");
    printf("==============================================================\n");

    /* ---------------------------------------------------------------- */
    printf("\n  [1] 结构体布局：两个数字不相等\n\n");
    printf("      sizeof(struct item)        = %llu   <- 指针算术的步进单位\n",
           (unsigned long long)sizeof(struct item));
    printf("      offsetof(struct box, it)   = %llu   <- 真正要退回的字节数\n",
           (unsigned long long)offsetof(struct box, it));
    printf("      sizeof(struct box)         = %llu\n",
           (unsigned long long)sizeof(struct box));
    printf("\n      8 != 4，这就是陷阱所在。\n");

    /* ---------------------------------------------------------------- */
    printf("\n  [2] 三个地址的关系\n\n");
    printf("      &b          = %p   <- 对象起点，我们要找的东西\n", (void *)&b);
    printf("      &b.it   (p) = %p   <- 手上有的东西，比 &b 大 %lld\n",
           (void *)p, (long long)((char *)p - (char *)&b));
    printf("      &b.magic    = %p\n", (void *)&b.magic);

    /* ---------------------------------------------------------------- */
    printf("\n  [3] 错误做法：直接 p - 1\n\n");
    printf("      指针算术是按【元素大小】步进的，不是按字节！\n");
    printf("      p - 1  等价于  退 sizeof(struct item) = %llu 字节\n",
           (unsigned long long)sizeof(struct item));
    printf("      算出来的地址 = %p\n",
           (void *)((char *)p - sizeof(struct item)));
    printf("      已经越过对象起点了 —— 差之毫厘，谬以千里\n");

    /* ---------------------------------------------------------------- */
    printf("\n  [4] 正确做法：先变成字节指针，再退 offsetof 字节\n\n");
    printf("      (char *)p               = %p   (降级成字节指针)\n", (void *)(char *)p);
    printf("      - offsetof(box, it)     = -%llu\n",
           (unsigned long long)offsetof(struct box, it));
    printf("      ------------------------------\n");
    printf("      (char *)p - 4           = %p\n",
           (void *)((char *)p - offsetof(struct box, it)));
    printf("      等于 &b ?                 %s\n",
           ((void *)((char *)p - offsetof(struct box, it)) == (void *)&b) ? "YES" : "NO");

    /* ---------------------------------------------------------------- */
    printf("\n  [5] 两个版本的宏，结果一致\n\n");

    struct box *back_std = container_of_std(p, struct box, it);
    struct box *back     = container_of(p, struct box, it);

    printf("      container_of_std(p, struct box, it)  = %p  %s\n",
           (void *)back_std, (back_std == &b) ? "== &b ✓" : "!= &b ✗");
    printf("      container_of    (p, struct box, it)  = %p  %s\n",
           (void *)back, (back == &b) ? "== &b ✓" : "!= &b ✗");

    printf("\n      拿回完整视野后，所有字段都能读了：\n\n");
    printf("      back->tag   = '%c'\n", back->tag);
    printf("      back->it.a  = %d\n",   back->it.a);
    printf("      back->it.b  = %d\n",   back->it.b);
    printf("      back->magic = 0x%04X\n", back->magic);

    /* ---------------------------------------------------------------- */
    printf("\n==============================================================\n");
    printf("  一句话总结\n");
    printf("==============================================================\n");
    printf("\n  container_of 的全部内容：\n");
    printf("\n      把成员指针降级成字节指针，减去它相对宿主的偏移，再转回来。\n");
    printf("\n  难点只有一个 —— 别忘了先降级成字节指针。\n");
    printf("  剩下的 (void *)/语句表达式/static_assert，都是工程上的防御措施，\n");
    printf("  不是算法本身。\n\n");

    return 0;
}
