/*
 * asm_check.c —— 用汇编验证 upcast 与 downcast 的真实开销
 * ---------------------------------------------------------------------------
 * 这个文件只干一件事：让编译器把两种转换编译出来，看看各剩几条指令。
 *
 *     gcc -std=gnu11 -O2 -masm=intel -S asm_check.c -o asm_check.s
 *
 * 预期结论：
 *     upcast   -> 完全没有算术，只有 ABI 要求的参数搬运
 *     downcast -> 一条减法（把偏移量减掉）
 *
 * 也就是说：继承本身在运行时是零成本的，只有"从父类找子类"这一步
 * 要付出一个减法的代价 —— 这就是 container_of 的全部开销。
 * ---------------------------------------------------------------------------
 */

#include <stddef.h>

/* ===========================================================================
 * 场景 A：首成员继承（真·继承链），偏移恒为 0
 * =========================================================================== */

struct sock      { int family, fd, state; };
struct inet_sock { struct sock sk; unsigned sport, dport; };    /* 首成员 */
struct tcp_sock  { struct inet_sock inet; unsigned snd, rcv; }; /* 首成员 */

/* 孙 -> 祖父，看这里还剩几条指令 */
struct sock *upcast_tcp_to_sock(struct tcp_sock *tp)
{
    return (struct sock *)tp;
}

/* 祖父 -> 子，偏移依然是 0 */
struct inet_sock *upcast_sock_to_inet(struct sock *sk)
{
    return (struct inet_sock *)sk;
}

/* ===========================================================================
 * 场景 B：非首成员组合，必须减偏移
 * =========================================================================== */

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
    struct device  dev;      /* 不在首位，offsetof = 32 */
    int            irq;
};

/* 父类指针 -> 子类指针，看编译器是不是老老实实减了 32 */
struct i2c_client *downcast_dev_to_client(struct device *d)
{
    return (struct i2c_client *)((char *)d - offsetof(struct i2c_client, dev));
}
