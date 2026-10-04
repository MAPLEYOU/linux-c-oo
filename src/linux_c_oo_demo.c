/*
 * linux_c_oo_demo.c
 * ---------------------------------------------------------------------------
 * Linux 内核式 C 面向对象 —— 完整可运行演示
 *
 *   1. 四层首成员继承链   —— 地址恒等性验证（upcast 零成本）
 *   2. 非首成员组合       —— container_of 反向定位（downcast）
 *   3. 误用对照           —— 直接强转到底读到了什么垃圾
 *   4. 虚表多态           —— 内核版的 virtual function
 *   5. 通用链表           —— list_head + container_of 捞出任意宿主结构
 *
 * 编译（GCC / Clang / MinGW 均可）：
 *     gcc -std=gnu11 -O0 -g -Wall -Wextra linux_c_oo_demo.c -o demo
 *
 * 运行：
 *     ./demo        (Linux / macOS / MinGW bash)
 *     demo.exe      (Windows cmd / PowerShell)
 *
 * 想看 upcast 在汇编里是不是"零指令"：
 *     gcc -std=gnu11 -O2 -S linux_c_oo_demo.c -o demo.s
 *     然后在 demo.s 里搜 tcp_sk / inet_sk —— 它们应当被完全 inline 掉，
 *     连一条 mov 都留不下。
 * ---------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stddef.h>
#include <string.h>

/* ===========================================================================
 * 0. 基础设施：container_of
 * ===========================================================================
 *
 * 内核原版（include/linux/container_of.h）。拆开看就三件事：
 *   1) 用 void * 剥掉指针类型，拿到一个裸地址
 *   2) 减去成员在结构体内的偏移量，指针"往回退"到宿主对象起点
 *   3) 强转成宿主类型 —— 这时才真正获得子类的完整视野
 *
 * 中间那句 _Static_assert 做编译期类型校验：你传进来的指针所指类型，
 * 必须与 ((type *)0)->member 的类型一致，写错了直接编译不过。
 *
 * 注：内核 6.17 起另提供 container_of_const()，专门处理 const 指针
 *     （否则强转会丢掉 const 限定符，编译告警）。本 demo 接口不带 const，
 *     用原版即可。
 */
#define container_of(ptr, type, member) ({                              \
        void *__mptr = (void *)(ptr);                                   \
        _Static_assert(__builtin_types_compatible_p(                    \
                __typeof__(*(ptr)), __typeof__(((type *)0)->member)),   \
                "container_of: pointer type mismatch");                 \
        ((type *)(__mptr - offsetof(type, member))); })

/* 小工具：分节标题 */
static void title(const char *s)
{
    printf("\n--------------------------------------------------------------\n");
    printf("  %s\n", s);
    printf("--------------------------------------------------------------\n");
}

/* ===========================================================================
 * 1. 四层首成员继承链
 * ===========================================================================
 *
 * 取自内核 TCP 栈的真实层次（字段已大幅简化，只保留形状）：
 *
 *     struct sock                         <-- 祖父
 *       struct inet_sock                  <-- 父   : 首成员 struct sock sk
 *         struct inet_connection_sock     <-- 子   : 首成员 struct inet_sock icsk_inet
 *           struct tcp_sock               <-- 孙   : 首成员 struct inet_connection_sock inet_conn
 *
 * 每一层都把父类放在首位，于是四层的起始地址完全重合。
 * 这就是 C 语言实现继承的全部机关。
 */

struct sock {
    int family;
    int fd;
    int sk_state;
};

struct inet_sock {
    struct sock  sk;                 /* 首成员 */
    unsigned int inet_sport;
    unsigned int inet_dport;
};

struct inet_connection_sock {
    struct inet_sock icsk_inet;      /* 首成员 */
    int              icsk_retransmits;
    unsigned int     icsk_rto;
};

struct tcp_sock {
    struct inet_connection_sock inet_conn;   /* 首成员 */
    unsigned int snd_nxt;
    unsigned int rcv_nxt;
    unsigned int snd_cwnd;
};

/* --- 这三行就是内核里的 upcast，写法照抄 net/ipv4/tcp.c 与 include/net/inet_sock.h ---
 * 因为首成员偏移恒为 0，直接用指针强转即可，不需要任何算术。
 * 汇编层面：inline 之后一条指令都不剩，纯编译期行为。
 */
static struct sock *tcp_sk(const struct tcp_sock *tp) { return (struct sock *)tp; }
static struct inet_sock *inet_sk(const struct sock *sk) { return (struct inet_sock *)sk; }
static struct inet_connection_sock *inet_csk(const struct sock *sk) { return (struct inet_connection_sock *)sk; }

static void demo_inheritance(void)
{
    struct tcp_sock tp;
    memset(&tp, 0, sizeof(tp));

    /* 填几个可辨认的值，方便下面验证"祖父指针能读到孙子写的数据" */
    tp.inet_conn.icsk_inet.sk.family = 2;      /* AF_INET */
    tp.inet_conn.icsk_inet.sk.fd     = 7;
    tp.inet_conn.icsk_inet.inet_dport = 80;
    tp.inet_conn.icsk_rto            = 200;
    tp.snd_cwnd                      = 10;

    printf("  同一块内存，四层视角的地址：\n\n");
    printf("  %-34s %-8s %s\n", "表达式", "偏移", "地址");
    printf("  %-34s %-8s %p\n", "&tp", "-", (void *)&tp);
    printf("  %-34s %-8llu %p\n", "&tp.inet_conn",
           (unsigned long long)offsetof(struct tcp_sock, inet_conn),
           (void *)&tp.inet_conn);
    printf("  %-34s %-8llu %p\n", "&tp.inet_conn.icsk_inet",
           (unsigned long long)offsetof(struct inet_connection_sock, icsk_inet),
           (void *)&tp.inet_conn.icsk_inet);
    printf("  %-34s %-8llu %p\n", "&tp.inet_conn.icsk_inet.sk",
           (unsigned long long)offsetof(struct inet_sock, sk),
           (void *)&tp.inet_conn.icsk_inet.sk);

    printf("\n  sizeof: sock=%llu  inet_sock=%llu  icsk=%llu  tcp_sock=%llu\n",
           (unsigned long long)sizeof(struct sock),
           (unsigned long long)sizeof(struct inet_sock),
           (unsigned long long)sizeof(struct inet_connection_sock),
           (unsigned long long)sizeof(struct tcp_sock));

    /* --- 硬验证：四层地址是否完全相等 --- */
    int all_same = ((void *)&tp == (void *)&tp.inet_conn) &&
                   ((void *)&tp == (void *)&tp.inet_conn.icsk_inet) &&
                   ((void *)&tp == (void *)&tp.inet_conn.icsk_inet.sk);

    printf("\n  四层地址完全相等 ?  %s\n",
           all_same ? "YES   <-- 首成员继承的全部秘密" : "NO    <-- 布局写错了");

    /* --- upcast：孙子一步变祖父 --- */
    printf("\n  [upcast]  struct sock *sk = tcp_sk(&tp);\n");
    struct sock *sk = tcp_sk(&tp);
    printf("            (void *)sk == (void *)&tp ?  %s\n",
           ((void *)sk == (void *)&tp) ? "YES" : "NO");
    printf("            sk->family = %d   (期望 2)\n", sk->family);
    printf("            sk->fd     = %d   (期望 7)\n", sk->fd);

    /* --- 侧向 downcast：祖父指针变回某一层子类 --- */
    printf("\n  [downcast] inet_sk(sk) / inet_csk(sk)\n");
    struct inet_sock *isk = inet_sk(sk);
    struct inet_connection_sock *icsk = inet_csk(sk);
    printf("            inet_sk(sk)->inet_dport = %u   (期望 80)\n", isk->inet_dport);
    printf("            inet_csk(sk)->icsk_rto  = %u   (期望 200)\n", icsk->icsk_rto);

    printf("\n  注意：这一整条链上的转换全是「指针强转」，偏移量恒为 0。\n");
    printf("  容器地址和成员地址重合 —— 这是首成员继承的特权。\n");
}

/* ===========================================================================
 * 2. 非首成员组合：container_of 反向定位
 * ===========================================================================
 *
 * 对比第 1 节。真实内核里的 struct i2c_client，其 dev 成员并不在首位：
 *
 *     struct i2c_client {
 *         unsigned short flags;
 *         unsigned short addr;
 *         char           name[I2C_NAME_SIZE];
 *         struct device *adapter;
 *         struct device  dev;        <-- 父类成员在中间！
 *         int            irq;
 *     };
 *
 * 这种情况下 (struct i2c_client *)dev 是彻底错误的：偏移量不是 0。
 * container_of 存在的根本原因就在这里 —— 成员可以长在任意位置。
 *
 * 区别归纳：
 *   首成员继承  -> 双向自由，upcast/downcast 都是强转
 *   非首成员组合 -> 只能单向，靠 container_of 从成员地址反推容器地址
 */

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
    struct device  dev;              /* 父类成员，不在首位 */
    int            irq;
};

/* 内核 include/linux/i2c.h 原文 */
#define to_i2c_client(d) container_of(d, struct i2c_client, dev)

static void demo_downcast(void)
{
    struct i2c_client cli;
    memset(&cli, 0, sizeof(cli));

    cli.flags = 0x0001;
    cli.addr  = 0x50;
    snprintf(cli.name, sizeof(cli.name), "%s", "at24c02");
    cli.dev.init_name = "0-0050";
    cli.dev.id        = 7;
    cli.irq           = 42;

    printf("  struct i2c_client 内部布局：\n\n");
    printf("    flags     @ %-4llu\n", (unsigned long long)offsetof(struct i2c_client, flags));
    printf("    addr      @ %-4llu\n", (unsigned long long)offsetof(struct i2c_client, addr));
    printf("    name      @ %-4llu\n", (unsigned long long)offsetof(struct i2c_client, name));
    printf("    adapter   @ %-4llu\n", (unsigned long long)offsetof(struct i2c_client, adapter));
    printf("    dev       @ %-4llu   <-- 父类成员在中间，不是 0\n",
           (unsigned long long)offsetof(struct i2c_client, dev));
    printf("    irq       @ %-4llu\n", (unsigned long long)offsetof(struct i2c_client, irq));
    printf("    sizeof    = %llu\n", (unsigned long long)sizeof(struct i2c_client));

    struct device *d = &cli.dev;      /* upcast：拿到父类指针交给驱动模型 */

    printf("\n  &cli        = %p   (对象起点)\n", (void *)&cli);
    printf("  &cli.dev    = %p   (父类指针 d)\n", (void *)d);

    /* --- 正确姿势 --- */
    struct i2c_client *back = to_i2c_client(d);
    printf("\n  [正确] struct i2c_client *back = to_i2c_client(d);\n");
    printf("         back = %p\n", (void *)back);
    printf("         back == &cli ?              %s\n", back == &cli ? "YES" : "NO");
    printf("         back->addr = 0x%04X  (期望 0x0050)\n", back->addr);
    printf("         back->irq  = %d        (期望 42)\n", back->irq);
    printf("         back->dev.id = %d      (期望 7)\n", back->dev.id);

    /* --- 错误示范：漏掉偏移修正 --- */
    struct i2c_client *wrong = (struct i2c_client *)d;
    printf("\n  [误用] struct i2c_client *wrong = (struct i2c_client *)d;\n");
    printf("         wrong = %p\n", (void *)wrong);
    printf("         落在对象内部第 %lld 字节处，而不是起点\n",
           (long long)((char *)wrong - (char *)&cli));
    printf("         wrong->addr = 0x%04X  <-- 读到的是 dev.init_name 指针的中间两个字节\n",
           wrong->addr);
    printf("\n  编译器一声不吭，运行时也不报错 —— 这就是 C 没有 RTTI 的代价。\n");
    printf("  你以为你是什么，你就是什么；转错了只能靠你自己发现。\n");
}

/* ===========================================================================
 * 3. 虚表多态：内核版的 virtual function
 * ===========================================================================
 *
 * C++ 的 virtual 编译后长成什么样？就是"对象里塞一根 vptr + 查表跳转"。
 * 内核把这套手工写出来：
 *
 *     struct xxx_ops   <-- 虚函数表
 *     obj->ops->fn(obj)  <-- 虚函数调用（手动传 this）
 *
 * 代表：struct file_operations / net_device_ops / inode_operations
 */

struct animal;

struct animal_ops {
    const char *type;
    void (*speak)(struct animal *self);
    void (*move)(struct animal *self, int dx, int dy);
};

struct animal {                        /* 父类 */
    const struct animal_ops *ops;      /* vptr：指向虚表 */
    char                     name[16];
};

struct dog {                           /* 子类 1 */
    struct animal animal;              /* 首成员 */
    int           tail_wag;
};

struct cat {                           /* 子类 2 */
    struct animal animal;              /* 首成员 */
    int           lives;
};

/* --- 狗的虚函数实现 --- */
static void dog_speak(struct animal *self)
{
    /* downcast：拿回子类视野才能访问 tail_wag */
    struct dog *d = container_of(self, struct dog, animal);
    printf("          %-6s (%-3s) : 汪汪！尾巴摇了 %d 下\n",
           self->name, self->ops->type, d->tail_wag);
}

static void dog_move(struct animal *self, int dx, int dy)
{
    struct dog *d = container_of(self, struct dog, animal);
    printf("          %-6s (%-3s) : 小跑 %+d,%+d，尾巴跟随摆动 %d 次\n",
           self->name, self->ops->type, dx, dy, d->tail_wag + 1);
}

/* --- 猫的虚函数实现 --- */
static void cat_speak(struct animal *self)
{
    struct cat *c = container_of(self, struct cat, animal);
    printf("          %-6s (%-3s) : 喵～还有 %d 条命\n",
           self->name, self->ops->type, c->lives);
}

static void cat_move(struct animal *self, int dx, int dy)
{
    struct cat *c = container_of(self, struct cat, animal);
    printf("          %-6s (%-3s) : 无声潜行 %+d,%+d，剩余 %d 条命\n",
           self->name, self->ops->type, dx, dy, c->lives);
}

/* --- 两张虚表，放在只读段 --- */
static const struct animal_ops dog_ops = {
    .type  = "dog",
    .speak = dog_speak,
    .move  = dog_move,
};

static const struct animal_ops cat_ops = {
    .type  = "cat",
    .speak = cat_speak,
    .move  = cat_move,
};

/* --- 构造函数：装上 vptr --- */
static void dog_init(struct dog *d, const char *name, int wag)
{
    d->animal.ops = &dog_ops;
    snprintf(d->animal.name, sizeof(d->animal.name), "%s", name);
    d->tail_wag = wag;
}

static void cat_init(struct cat *c, const char *name, int lives)
{
    c->animal.ops = &cat_ops;
    snprintf(c->animal.name, sizeof(c->animal.name), "%s", name);
    c->lives = lives;
}

/* --- 统一入口：内核里 vfs_read() / dev_queue_xmit() 就是这个角色 --- */
static void animal_speak(struct animal *a)              { a->ops->speak(a); }
static void animal_move (struct animal *a, int dx, int dy) { a->ops->move(a, dx, dy); }

static void demo_polymorphism(void)
{
    struct dog d;
    struct cat c;

    dog_init(&d, "旺财", 3);
    cat_init(&c, "咪咪", 9);

    printf("  &d = %p,  &d.animal = %p   ->  %s\n",
           (void *)&d, (void *)&d.animal,
           ((void *)&d == (void *)&d.animal) ? "addr 相同，upcast 零成本" : "异常");
    printf("  &c = %p,  &c.animal = %p   ->  %s\n\n",
           (void *)&c, (void *)&c.animal,
           ((void *)&c == (void *)&c.animal) ? "addr 相同，upcast 零成本" : "异常");

    /* 两个不同子类，装进同一个父类指针数组 —— 这就是多态的入口 */
    struct animal *zoo[2];
    zoo[0] = &d.animal;
    zoo[1] = &c.animal;

    printf("  调用方只认 struct animal *，循环体里没有任何 if / switch：\n\n");
    for (int i = 0; i < 2; i++) {
        animal_speak(zoo[i]);
        animal_move(zoo[i], 3, -2);
    }

    printf("\n  具体落到哪个实现，由对象里装的那张表决定。\n");
    printf("  a->ops->speak(a) 展开后就是：取 vptr -> 取表项 -> 间接调用 + 传 this。\n");
    printf("  C++ 的 virtual 编译出来，一模一样。\n");
}

/* ===========================================================================
 * 4. 通用链表：list_head + container_of
 * ===========================================================================
 *
 * 内核链表最妙的地方：struct list_head 里没有任何数据，只有两个指针。
 * 所以它能挂进任意宿主结构 —— 挂在结构体的哪个位置都行，因为
 * list_for_each_entry 靠 container_of 反推宿主地址，而不是靠猜布局。
 */

struct list_head {
    struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name)      struct list_head name = LIST_HEAD_INIT(name)

static void INIT_LIST_HEAD(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

/* 内核原版：在 prev 与 next 之间插入 new */
static void __list_add(struct list_head *new_node,
                       struct list_head *prev,
                       struct list_head *next)
{
    next->prev      = new_node;
    new_node->next  = next;
    new_node->prev  = prev;
    prev->next      = new_node;
}

static void list_add_tail(struct list_head *new_node, struct list_head *head)
{
    __list_add(new_node, head->prev, head);
}

/* 内核原版三个宏 */
#define list_entry(ptr, type, member) \
        container_of(ptr, type, member)

#define list_first_entry(head, type, member) \
        list_entry((head)->next, type, member)

#define list_next_entry(pos, member) \
        list_entry((pos)->member.next, __typeof__(*(pos)), member)

#define list_for_each_entry(pos, head, member)                          \
        for (pos = list_first_entry(head, __typeof__(*(pos)), member);  \
             &(pos)->member != (head);                                  \
             pos = list_next_entry(pos, member))

/* 宿主结构：故意把 node 放在中间，证明 container_of 不依赖首位布局 */
struct task {
    int              pid;
    char             comm[16];
    struct list_head node;        /* <-- 不在首位 */
    int              prio;
};

static void demo_list(void)
{
    /* 运行时初始化一个空链表（内核里两种写法并存：
     *   LIST_HEAD(tasks);        <-- 静态初始化，靠 LIST_HEAD_INIT 宏
     *   INIT_LIST_HEAD(&tasks);  <-- 运行时初始化，本 demo 用这个
     */
    struct list_head tasks;
    INIT_LIST_HEAD(&tasks);

    struct task t1 = { .pid = 1,   .prio = 120 };
    struct task t2 = { .pid = 42,  .prio = 100 };
    struct task t3 = { .pid = 777, .prio = 90  };
    struct task t4 = { .pid = 900, .prio = 110 };
    snprintf(t1.comm, sizeof(t1.comm), "%s", "init");
    snprintf(t2.comm, sizeof(t2.comm), "%s", "kworker");
    snprintf(t3.comm, sizeof(t3.comm), "%s", "myapp");
    snprintf(t4.comm, sizeof(t4.comm), "%s", "bash");

    list_add_tail(&t1.node, &tasks);
    list_add_tail(&t2.node, &tasks);
    list_add_tail(&t3.node, &tasks);
    list_add_tail(&t4.node, &tasks);

    printf("  offsetof(struct task, node) = %llu\n",
           (unsigned long long)offsetof(struct task, node));
    printf("  offsetof(struct task, prio) = %llu   (节点后面还有字段)\n\n",
           (unsigned long long)offsetof(struct task, prio));

    printf("  list_for_each_entry 遍历结果：\n\n");
    printf("    %-8s %-12s %-6s %s\n", "pid", "comm", "prio", "宿主地址");
    struct task *pos;
    list_for_each_entry(pos, &tasks, node) {
        printf("    %-8d %-12s %-6d %p\n",
               pos->pid, pos->comm, pos->prio, (void *)pos);
    }

    printf("\n  遍历宏里从头到尾没出现过 struct task —— 链表本身是完全泛型的。\n");
    printf("  是 container_of 在每一步把 node 的地址换算成了 task 的地址。\n");
    printf("  这就是内核能用一套 list_head 管理全宇宙所有链表的原因。\n");
}

/* =========================================================================== */

int main(void)
{
    printf("\n==============================================================\n");
    printf("  Linux 内核式 C 面向对象\n");
    printf("  继承 / 多态 / container_of 完整演示\n");
    printf("==============================================================\n");

    title("1. 四层首成员继承链 —— 地址恒等性");
    demo_inheritance();

    title("2. 非首成员组合 —— container_of 反向定位");
    demo_downcast();

    title("3. 虚表多态 —— 内核版的 virtual function");
    demo_polymorphism();

    title("4. 通用链表 —— list_head + container_of");
    demo_list();

    printf("\n==============================================================\n");
    printf("  演示结束。\n\n");
    printf("  想确认 upcast 真的零开销，看汇编：\n");
    printf("      gcc -std=gnu11 -O2 -S linux_c_oo_demo.c -o demo.s\n");
    printf("  在 demo.s 里搜 tcp_sk / inet_sk —— 应已被完全 inline，\n");
    printf("  一条指令都不剩。而 container_of 会留下一条 sub 减法。\n");
    printf("==============================================================\n\n");
    return 0;
}
