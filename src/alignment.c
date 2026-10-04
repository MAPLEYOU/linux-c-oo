/*
 * alignment.c —— struct 字节对齐规则实测
 * ---------------------------------------------------------------------------
 * 配套 README.md 第一章「前置知识」1.1 节。
 *
 * 编译运行：
 *     gcc -std=gnu11 -O0 -g -Wall -Wextra alignment.c -o align && ./align
 * ---------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stddef.h>

#define ULL(x) ((unsigned long long)(x))

static void sec(const char *s)
{
    printf("\n--------------------------------------------------------------\n");
    printf("  %s\n", s);
    printf("--------------------------------------------------------------\n");
}

static void show_type(const char *name, unsigned long long size, unsigned long long align)
{
    printf("  %-14s size=%-3llu align=%llu\n", name, size, align);
}

#define SHOW_TYPE(t) show_type(#t, ULL(sizeof(t)), ULL(_Alignof(t)))

/* ===========================================================================
 * 被测结构体
 * =========================================================================== */

/* 规则一演示：成员偏移必须是自身对齐值的整数倍 */
struct rule1 { char a; int b; char c; };

/* 同样三个字段，换个顺序 */
struct reordered { int b; char a; char c; };

/* 嵌套：结构体的对齐值会传给外层 */
struct item { int a; int b; };
struct box  { char tag; struct item it; int magic; };

/* 规则三演示：为什么尾部必须补齐 */
struct tail { int a; char b; };

/* 强制取消对齐 */
struct __attribute__((packed)) packed_t { char a; int b; char c; };

/* container_of 的真实场景 */
struct device {
    const char *init_name;
    void       *driver_data;
    int         id;
};

struct i2c_client {
    unsigned short flags;
    unsigned short addr;
    char           name[16];
    void          *adapter;
    struct device  dev;
    int            irq;
};

/* =========================================================================== */

int main(void)
{
    printf("\n==============================================================\n");
    printf("  struct 字节对齐规则实测\n");
    printf("==============================================================\n");

    sec("1. 基础类型的 size 与 alignof");
    SHOW_TYPE(char);
    SHOW_TYPE(short);
    SHOW_TYPE(int);
    SHOW_TYPE(long);
    SHOW_TYPE(long long);
    SHOW_TYPE(float);
    SHOW_TYPE(double);
    SHOW_TYPE(void *);
    printf("\n  规律：对齐值通常等于自身大小，且必然是 2 的幂。\n");
    printf("  硬件的最小寻址/传输单位是字节，但 CPU 与内存之间是按「字」走的 ——\n");
    printf("  一次能搬 4 或 8 字节，这就是对齐要求的物理来源。\n");

    sec("2. 规则一：成员偏移 = 向上取整到该成员对齐值的倍数");
    printf("  struct rule1 { char a; int b; char c; };\n\n");
    printf("    a  @ %-3llu  char，对齐 1，随便放\n",
           ULL(offsetof(struct rule1, a)));
    printf("    b  @ %-3llu  int 对齐 4 → 0+1=1 不是 4 的倍数 → 补 3 字节再放\n",
           ULL(offsetof(struct rule1, b)));
    printf("    c  @ %-3llu  char，紧接其后\n",
           ULL(offsetof(struct rule1, c)));
    printf("\n  字段用完 = 4 + 4 + 1 = 9 字节，但 sizeof = %llu，尾部又补了 3 字节。\n",
           ULL(sizeof(struct rule1)));

    sec("3. 规则二：结构体对齐 = 成员中对齐值最大的那个");
    printf("  struct box { char tag; struct item it; int magic; };\n\n");
    printf("    char        align = 1\n");
    printf("    struct item align = %llu   ← 嵌套结构体把对齐值带进外层\n",
           ULL(_Alignof(struct item)));
    printf("    int         align = 4\n");
    printf("\n    → alignof(struct box) = max(1, %llu, 4) = %llu\n",
           ULL(_Alignof(struct item)), ULL(_Alignof(struct box)));
    printf("    → tag @ %llu, it @ %llu, magic @ %llu, sizeof = %llu\n",
           ULL(offsetof(struct box, tag)),
           ULL(offsetof(struct box, it)),
           ULL(offsetof(struct box, magic)),
           ULL(sizeof(struct box)));

    sec("4. 规则三：总大小必须是结构体对齐值的整数倍 —— 为了数组");
    printf("  struct tail { int a; char b; };\n\n");
    printf("    a @ %llu, b @ %llu，字段只用到第 %llu 字节\n",
           ULL(offsetof(struct tail, a)),
           ULL(offsetof(struct tail, b)),
           ULL(offsetof(struct tail, b) + sizeof(char)));
    printf("    但 sizeof(struct tail) = %llu（不是 5）\n", ULL(sizeof(struct tail)));
    printf("\n  原因只有一个：数组。struct tail arr[2]; 必须让 arr[1].a 也保持 4 对齐。\n");
    printf("    若 sizeof = 5 → arr[1] 从偏移 5 开始 → arr[1].a 落在 5，未对齐 ✗\n");
    printf("    实际 sizeof = %llu → arr[1] 从偏移 %llu 开始 → arr[1].a 落在 %llu，对齐 ✓\n",
           ULL(sizeof(struct tail)), ULL(sizeof(struct tail)), ULL(sizeof(struct tail)));

    sec("5. 重排成员顺序能省内存");
    printf("  struct { char a; int b; char c; }   sizeof = %-3llu\n", ULL(sizeof(struct rule1)));
    printf("  struct { int b; char a; char c; }   sizeof = %-3llu\n", ULL(sizeof(struct reordered)));
    printf("\n  同样三个字段，把 int 提到最前面，省下 %llu 字节纯填充。\n",
           ULL(sizeof(struct rule1) - sizeof(struct reordered)));
    printf("  （结构体内部字段顺序可以自由调整时，把大对齐的往前放即可。）\n");

    sec("6. 强制取消对齐：packed");
    printf("  struct __attribute__((packed)) { char a; int b; char c; };\n\n");
    printf("    a @ %llu\n", ULL(offsetof(struct packed_t, a)));
    printf("    b @ %llu   ← 不再补齐，紧贴着放\n", ULL(offsetof(struct packed_t, b)));
    printf("    c @ %llu\n", ULL(offsetof(struct packed_t, c)));
    printf("    sizeof = %llu, alignof = %llu\n",
           ULL(sizeof(struct packed_t)), ULL(_Alignof(struct packed_t)));
    printf("\n  ⚠ 代价：b 的地址不再 4 对齐。\n");
    printf("     x86-64 硬件能容忍（但可能跨 cache line，性能下降且失去原子性）；\n");
    printf("     部分 ARM / SPARC / MIPS 会直接抛 SIGBUS 硬件异常。\n");
    printf("     packed 只该用在「协议报文 / 磁盘格式」这种必须逐字节对齐的场合。\n");

    sec("7. 这和 container_of 有什么关系");
    printf("  offsetof(struct i2c_client, dev) = %llu\n\n",
           ULL(offsetof(struct i2c_client, dev)));
    printf("  i2c_client 逐字段：\n");
    printf("    flags    @ %-3llu  unsigned short, align 2\n", ULL(offsetof(struct i2c_client, flags)));
    printf("    addr     @ %-3llu  unsigned short, align 2\n", ULL(offsetof(struct i2c_client, addr)));
    printf("    name     @ %-3llu  char[16],      align 1\n", ULL(offsetof(struct i2c_client, name)));
    printf("    adapter  @ %-3llu  void *,        align 8 → 20 补到 24\n", ULL(offsetof(struct i2c_client, adapter)));
    printf("    dev      @ %-3llu  struct device  ← container_of 要减掉的正是这个数\n", ULL(offsetof(struct i2c_client, dev)));
    printf("    irq      @ %-3llu\n", ULL(offsetof(struct i2c_client, irq)));
    printf("    sizeof   = %llu, alignof = %llu\n",
           ULL(sizeof(struct i2c_client)), ULL(_Alignof(struct i2c_client)));
    printf("\n  这个 %llu 不是拍脑袋定的 —— 是前 4 个字段的类型 + 对齐规则共同推出来的。\n",
           ULL(offsetof(struct i2c_client, dev)));
    printf("  改动任何一个字段的类型或顺序，dev 的偏移就变，\n");
    printf("  container_of 里那条汇编减法（lea rax, -32[rcx]）的立即数也随之变。\n");
    printf("  好消息：编译器自动算，你永远不用手写这个数。\n");

    sec("小结：三条规则");
    printf("  ① 成员偏移   = 向上取整到【该成员对齐值】的倍数（不足则填充）\n");
    printf("  ② 结构体对齐 = max(所有成员的对齐值)\n");
    printf("  ③ 结构体大小 = 向上取整到【结构体对齐值】的倍数（尾部填充）\n");
    printf("\n  对齐的根本目的：让一次内存访问就能取到一个完整的基本类型值。\n");
    printf("  未对齐 = 跨越总线宽度 = 两次读取 + 拼接（x86 软容忍），或硬件异常（部分架构）。\n\n");

    return 0;
}
