# Linux C 面向对象：结构体内嵌与 container_of

> [!NOTE] 一句话答案
> C 没有 `class`，Linux 内核用**结构体内嵌**模拟继承（父类放子类首位）、用**函数指针表**模拟多态；
> `container_of(ptr, type, member)` 则解决反方向的问题 —— **只拿到内嵌成员的地址，反推出宿主结构体的地址**。
> 它的本质是一次指针减法：把成员指针**降级成字节指针** → 减去 `offsetof` → 转回目标类型。

仓库：<https://github.com/MAPLEYOU/linux-c-oo> · 完整可运行代码在 [`src/`](src/)

---

## 一、前置知识

### 1.1 struct 的字节偏移怎么算

成员按声明顺序排列，但起始偏移受**对齐要求**约束，中间和尾部都可能被填充。

**为什么必须对齐**：CPU 与内存之间是按「字」传输的（一次 4 或 8 字节）。
一个 `int` 若跨在字边界上，硬件就得**读两次再移位拼接** —— x86-64 能软容忍，
但性能下降且**失去原子性**；部分 ARM / SPARC / MIPS 直接抛 `SIGBUS` 硬件异常。
对齐的根本目的：**保证一次访问就能取到一个完整的基本类型值**。

**三条规则**：

| # | 规则 |
| --- | --- |
| ① | 成员偏移 = 向上取整到**该成员对齐值**的倍数（不足则填充） |
| ② | 结构体对齐值 = `max(所有成员的对齐值)` |
| ③ | 结构体大小 = 向上取整到**结构体对齐值**的倍数（尾部填充） |

规则三为什么存在？**只为了数组**：

```c
struct tail { int a; char b; };    /* 字段只用到 5 字节 */
sizeof(struct tail);               /* 但等于 8，不是 5 */

struct tail arr[2];                /* 若 sizeof 是 5，arr[1].a 就落在偏移 5 → 未对齐 */
```

**完整推演**（本仓库 demo 用的就是这个结构）：

```c
struct item { int a; int b; };                         /* size 8, align 4 */

struct box  { char tag; struct item it; int magic; };
/*  tag   @0    align 1，随便放
 *  it    @4    0+1=1 不是 4 的倍数 → 补 3 字节
 *  magic @12   4+8=12 是 4 的倍数 → 直接放
 *  末尾 16，alignof(box)=4，16%4==0 → 无需再补
 *  sizeof = 16
 */
```

基本类型的对齐值（x86-64）：`char` 1 · `short` 2 · `int` 4 · `float` 4 · `double` 8 · 指针 8。
**通常等于自身大小，且必为 2 的幂。**

> [!WARNING] 注意 `long` 的平台差异
> Windows 是 **LLP64**：`long` = 4 字节；Linux / macOS 是 **LP64**：`long` = 8 字节。
> 跨平台的二进制协议里不要用 `long`。

> [!TIP] 两个实用推论
> **① 重排成员能省内存**：`{char; int; char;}` 占 12 字节，改成 `{int; char; char;}` 只要 8 字节。
> **② `packed` 是有代价的**：`__attribute__((packed))` 能取消填充，但成员地址就不再对齐 ——
> 只该用在「协议报文 / 磁盘格式」这种必须逐字节对应的场合。

实测程序（打印每个中间数字）：[`src/alignment.c`](src/alignment.c)

偏移量是**编译期常量**，用 `offsetof(struct box, it)` 拿到，值就是 `4`。

> [!IMPORTANT] 关键区分
> `offsetof` 返回的是**字节数**，不是"几个成员"。后面所有指针运算都建立在这个单位上。

### 1.2 指针的「类型」决定步进单位

```c
struct item *p = &b.it;
p + 1;      /* 前进 sizeof(struct item) = 8 字节，不是 1 字节！ */
```

| 指针类型 | `+1` 实际前进 | 备注 |
| --- | --- | --- |
| `char *` | 1 字节 | 标准 C 的"字节指针" |
| `void *` | 1 字节 | **仅 GCC 扩展**，标准 C 不允许 void 做算术 |
| `struct item *` | 8 字节 | 步进单位 = `sizeof(struct item)` |

> [!WARNING] 这是理解 `container_of` 的唯一门槛
> 想让指针按**字节**移动，必须先把它降级成 `char *` 或 `void *`。
> 否则 `- 4` 会被解释成"退 4 个结构体"，而不是退 4 个字节。

### 1.3 Linux C 的「面向对象三件套」

| OO 概念 | C 的实现手段 | 内核代表 |
| --- | --- | --- |
| 继承 | 父类结构体作子类**首成员** | `sock → inet_sock → icsk → tcp_sock` |
| 向下转型 | `container_of()` | `to_i2c_client()` / `to_usb_device()` |
| 多态 | `const struct xxx_ops *` 函数指针表 | `file_operations` / `net_device_ops` |
| 泛型容器 | `list_head` + `container_of` | `list_for_each_entry()` |

### 1.4 container_of 到底要解决什么问题

内核里到处是**父类指针**：总线把 `struct device *` 一路传下来。

可驱动要的是**自己的子类对象**。父类指针只给了对象内部某一个成员的地址，怎么回到宿主结构体的起点？

```c
struct device *dev;                                /* 手上只有父类指针 */

struct i2c_client *client = to_i2c_client(dev);    /* 想要的却是子类对象 */
```

> [!NOTE] 定义
> `container_of` = 已知「成员地址 + 成员名 + 宿主类型」，反推出「宿主对象地址」。
> 名字里的 container 就是"容器"—— 装下这个成员的那个大结构体。

**它在面向对象里的位置：这就是 C 版的「向下转型（downcast）」。**

有了它，内核那套"父类接口 + 子类实现"才转得起来：

```c
/* 总线只认父类，驱动在里面找回自己 */
static int i2c_device_probe(struct device *dev)
{
    struct i2c_client *client = to_i2c_client(dev);   /* 父 → 子 */
    ...
}
```

---

## 二、继承：把父类放在首位

### 2.1 核心规则

```c
struct base {                     /* 父类 */
    int type;
    void (*func)(struct base *self);
};

struct derived {                  /* 子类 */
    struct base base;             /* ★ 必须是第一个成员 */
    int         extra;
};
```

C 标准 6.7.2.1p15 保证：**结构体的地址等于其首成员的地址**。

```c
struct derived d;
(void *)&d == (void *)&d.base;    /* 恒为真 */
```

于是子类指针可以随时当父类指针用 —— 这就是继承的全部秘密。

### 2.2 内核实例：TCP 栈的四层继承链

```
struct sock                  { ... };                      /* 祖父 */
struct inet_sock             { struct sock sk; ... };      /* 父   */
struct inet_connection_sock  { struct inet_sock icsk_inet; ... };        /* 子 */
struct tcp_sock              { struct inet_connection_sock inet_conn; ... }; /* 孙 */
```

层层首成员，四层地址**完全相同**（实测输出）：

| 表达式 | 偏移 | 地址 |
| --- | --- | --- |
| `&tp` | — | `0x…FD70` |
| `&tp.inet_conn` | 0 | `0x…FD70` |
| `&tp.inet_conn.icsk_inet` | 0 | `0x…FD70` |
| `&tp.inet_conn.icsk_inet.sk` | 0 | `0x…FD70` |

`struct sock` 里还留着一条著名的注释，专门守住这个约定：

```c
struct sock {
    /*
     * ... please just don't add nothing before this first member
     * (__sk_common) --acme
     */
    struct sock_common __sk_common;
```

### 2.3 向上转型（upcast）是零成本的

```c
static struct sock *tcp_sk(const struct tcp_sock *tp)   { return (struct sock *)tp; }
static struct inet_sock *inet_sk(const struct sock *sk) { return (struct inet_sock *)sk; }
```

偏移恒为 0，转换就是**指针原样传递**。编译到汇编只剩参数搬运：

```asm
upcast_tcp_to_sock:
        mov     rax, rcx      ; 没有哪怕一条算术指令
        ret
```

### 2.4 位置决定能力：首成员 vs 非首成员

| | 父类成员位置 | upcast（子→父） | downcast（父→子） |
| --- | --- | --- | --- |
| `tcp_sock` / `sock` | 首位 | `(struct sock *)tp` ✓ | `(struct tcp_sock *)sk` ✓ |
| `i2c_client` / `device` | 中间（offset 32） | ✗ 不成立 | `container_of()` ✓ |

> [!WARNING] 为什么 `i2c_client` 不能强转
> 真实的 `struct i2c_client` 里，`dev` 成员前面还有 `flags`/`addr`/`name`/`adapter`，
> `offsetof(struct i2c_client, dev) = 32`。直接 `(struct i2c_client *)dev` 会错位 32 字节，
> **编译器不报错、运行时不崩溃**，读到的是垃圾数据。

> [!NOTE] 顺带纠正一个常见说法
> `to_i2c_client()` / `to_usb_device()` 严格讲**不是"继承"**，而是**组合 + 反向定位** ——
> 它们只有单向能力，不能像 TCP 那条链一样自由上下转型。真正符合首成员继承的是后者。

---

## 三、container_of 拆解

### 3.1 指针步进陷阱（唯一的坑）

`p = &b.it`，要把指针退回 `&b`：

```
需要退回的字节数 = offsetof(struct box, it) = 4
但 p 的类型是 struct item *   →   p - 1 意味着退 8 字节
```

| 写法 | 计数单位 | 实际退回 | 落到哪 |
| --- | --- | --- | --- |
| `p - 1` | 1 个 `struct item` | 8 字节 | **越过** `&b`，差了 4 字节 ✗ |
| `(char *)p - 4` | 1 字节 | 4 字节 | 正好命中 `&b` ✓ |

> [!TIP] 换个比喻
> 一排停车位，每辆车占 8 个车位。`p - 1` 是"往前挪一辆**车**"（8 个车位）；
> 而你要挪的是 4 个车位——半辆车。数车的尺子量不出半辆，得**换成"数车位"**。
> `(char *)p` 就是那把新尺子。

### 3.2 `offsetof` 的原理

```c
#define offsetof(type, member) ((size_t)&((type *)0)->member)
```

把地址 `0` 当作一个 `type` 对象的起点，那么 `member` 的地址数值**就等于偏移量**。
纯编译期常量，不产生任何指令。

### 3.3 内核原版宏逐行读

完整可运行版本：[`src/container_of_step_by_step.c`](src/container_of_step_by_step.c)

```c
#define container_of(ptr, type, member) ({                              \
        void *__mptr = (void *)(ptr);                                   \
        _Static_assert(__builtin_types_compatible_p(                    \
                __typeof__(*(ptr)), __typeof__(((type *)0)->member)),   \
                "container_of: pointer type mismatch");                 \
        ((type *)(__mptr - offsetof(type, member))); })
```

| # | 零件 | 作用 |
| --- | --- | --- |
| 1 | `({ ... })` | GCC **语句表达式**扩展：允许在表达式位置写语句块，**块内最后一条语句的值就是整个宏的值** |
| 2 | `void *__mptr = (void *)(ptr);` | **擦掉类型**——换刻度，解决 3.1 的步进陷阱 |
| 3 | `_Static_assert(...)` | **编译期类型防呆**，传错类型直接编译不过 |
| 4 | `((type *)(__mptr - offsetof(type, member)))` | **真正的算法**：裸地址减偏移，再转回目标类型 |

> [!IMPORTANT] 一句话剥掉所有包装
> **降级成字节指针 → 减 `offsetof` → 转回目标类型。**
> 剩下三件（语句表达式 / `void *` / `static_assert`）都是工程防御，不是算法本身。

标准 C 版本，任何编译器都能用：

```c
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
```

### 3.4 汇编验证：开销到底有多大

源码 [`src/asm_check.c`](src/asm_check.c)，`gcc -O2 -masm=intel -S`：

```asm
upcast_tcp_to_sock:              ; 向上转型
        mov     rax, rcx         ; 零算术
        ret

downcast_dev_to_client:          ; 向下转型 = container_of
        lea     rax, -32[rcx]    ; ★ 全部开销就是这一条减法
        ret
```

`-32` 正是 `offsetof(struct i2c_client, dev)`。

**成本模型**：继承本身免费；只有"从父类找回子类"要付一条减法。

### 3.5 移植坑：MSVC 与 C++ 都不认内核原版

| 语法 | GCC C 模式 | g++ / C++ | MSVC |
| --- | --- | --- | --- |
| `({ ... })` 语句表达式 | ✓ | ✓（GNU 扩展） | ✗ |
| `_Static_assert` | ✓（C11） | ✗（C++ 里叫 `static_assert`） | ✗ |
| `__builtin_types_compatible_p` | ✓ | ✗ | ✗ |

正确做法是条件编译降级：

```c
#if defined(__GNUC__) && !defined(__cplusplus)
#  define container_of(ptr, type, member) ({ /* 内核原版 */ })
#else
#  define container_of(ptr, type, member) \
       ((type *)((char *)(ptr) - offsetof(type, member)))
#endif
```

> [!TIP] 结论
> **内核原版宏只在「`.c` 文件 + GCC/Clang」下成立** —— 这也解释了为什么内核全是 `.c` 文件。

---

## 四、多态：手写一张虚表

```c
struct animal;                                    /* 前置声明 */

struct animal_ops {                               /* ← 虚函数表 */
    void (*speak)(struct animal *self);
    void (*move) (struct animal *self, int dx, int dy);
};

struct animal {                                   /* 父类 */
    const struct animal_ops *ops;                 /* vptr */
    char name[16];
};

struct dog { struct animal animal; int tail_wag; };   /* 子类 */
```

两个关键点：

1. **调用方只认父类接口** —— `a->ops->speak(a)`：手动传 `this` + 查表跳转
2. **实现里用 `container_of` 拿回子类视野** —— 否则访问不到子类独有字段

```c
static void dog_speak(struct animal *self)
{
    struct dog *d = container_of(self, struct dog, animal);   /* 向下转型 */
    printf("%s: 汪汪！尾巴摇了 %d 下\n", self->name, d->tail_wag);
}
```

`a->ops->speak(a)` 展开后 = 取 vptr → 取表项 → 间接调用，**和 C++ `virtual` 编译出来的一模一样**。

内核代表：`file_operations`、`net_device_ops`、`inode_operations`。VFS 靠它撑起"一切皆文件"。

---

## 五、实战：`list_head` 为什么能通用

内核链表节点里**没有任何数据**，只有两个指针：

```c
struct list_head { struct list_head *next, *prev; };
```

所以它能挂进任意宿主结构 —— **挂在哪都行**：

```c
struct task {
    int              pid;
    struct list_head node;      /* ← 故意不在首位 */
    int              prio;
};
```

遍历宏里从头到尾**没有出现过 `struct task`**，靠 `container_of` 每一步换算：

```c
#define list_for_each_entry(pos, head, member)                          \
        for (pos = list_first_entry(head, __typeof__(*(pos)), member);  \
             &(pos)->member != (head);                                  \
             pos = list_next_entry(pos, member))
```

```c
struct task *pos;
list_for_each_entry(pos, &tasks, node) {
    printf("pid=%d comm=%s\n", pos->pid, pos->comm);   /* 已经是宿主指针了 */
}
```

这就是一套 `list_head` 能管理全内核所有链表的原因。

---

## 六、能力对照表与踩坑清单

### 6.1 与 C++ / Java 对照

| C++ / Java | C 语言实现 | 内核代表 |
| --- | --- | --- |
| 继承 | 父类结构体放首位 | `struct device` 内嵌 `kobject` |
| 向上转型 | `&d->base`（偏移 0） | 传 `struct device *` 给总线 |
| 向下转型 | `container_of()` | `to_i2c_client()` |
| 虚函数表 | `const struct xxx_ops *` | `file_operations` |
| 虚函数调用 | `obj->ops->fn(obj)` | `vfs_read()` |
| RTTI | ❌ 不存在，靠手工标记 | `dev->type` / `dev->bus` |

### 6.2 踩坑清单

| 坑 | 后果 | 正确做法 |
| --- | --- | --- |
| 父类不在首位还强转 | 静默读错内存 | 用 `container_of`，或老实写 `&d->base` |
| 忘了先转 `char *` / `void *` | 退错 `sizeof` 倍的距离 | 记住"换刻度"这件事 |
| `member` 名字写错 | 编译期可能过，运行期读脏内存 | 用 `to_xxx()` 封装宏集中管理 |
| 拿 MSVC / C++ 编内核原版宏 | 一堆语法错误 | 条件编译降级到标准版 |
| 以为 `container_of` 有运行时校验 | 转错类型不报错 | 它只是指针减法，**零校验** |

### 6.3 常见理解偏差

> [!NOTE] 理解偏差校准
> | 常见说法 | 校准后 |
> | --- | --- |
> | "`container_of` 是子类推导父类" | 反了。它是**父类指针 → 子类指针**（downcast）。方向只看**指针类型从谁变到谁** |
> | "upcast 是地址往上走" | 上/下是 **UML 类图的坐标系**（父在上）。upcast 偏移恒为 0，地址根本不动 |
> | "转 `void *` 是为了转换类型" | 核心目的是**换计数单位**。不换单位，减法会按 `sizeof(成员类型)` 步进 |
> | "`offsetof` 要等到运行时才算" | 它是**编译期常量**，汇编里直接变成立即数（如 `-32`） |

---

## 面试速答

> `container_of` 解决的是"已知内嵌成员地址，反推宿主对象地址"的问题，内核用它实现**父类指针到子类指针的向下转型**（`to_i2c_client()`、`to_usb_device()` 等）。
>
> 实现上就是一次指针减法：先把成员指针降级成字节指针（`(void *)` 或 `(char *)`），减去 `offsetof(type, member)`，再转回目标类型。**必须先降级**，否则指针算术会按 `sizeof(成员类型)` 步进，退错位置。
>
> 配套的继承机制是"父类结构体放子类首位"：此时偏移恒为 0，向上转型零成本（汇编里只剩 `mov`）；向下转型的全部开销就是一条 `lea rax, -32[rcx]`。
>
> 注意 C 没有 RTTI，`container_of` 无任何运行时校验，转错了编译器不报错 —— 内核靠封装 `to_xxx()` 宏和 `dev->type`、`dev->bus` 这类手工标记兜底。另外内核原版宏依赖 GCC 的语句表达式和 `_Static_assert`，**只在 `.c` + GCC/Clang 下成立**。

---

## 附录：完整代码

仓库：<https://github.com/MAPLEYOU/linux-c-oo>

| 文件 | 内容 |
| --- | --- |
| [`src/alignment.c`](src/alignment.c) | 字节对齐规则实测：三条规则逐条验证 / 重排省内存 / `packed` 代价 / 与 `container_of` 的关系 |
| [`src/linux_c_oo_demo.c`](src/linux_c_oo_demo.c) | 完整演示：四层继承链 / `container_of` 反向定位 / 误用对照 / 虚表多态 / `list_head` 遍历 |
| [`src/container_of_step_by_step.c`](src/container_of_step_by_step.c) | `container_of` 最小拆解，打印每个中间数值 |
| [`src/asm_check.c`](src/asm_check.c) | 汇编验证：upcast 零指令、downcast 一条减法 |
| [`src/asm_check.s`](src/asm_check.s) | 生成的汇编（`-O2 -masm=intel`） |

编译运行：

```bash
gcc -std=gnu11 -O0 -g -Wall -Wextra src/alignment.c -o align && ./align
gcc -std=gnu11 -O0 -g -Wall -Wextra src/linux_c_oo_demo.c -o demo && ./demo
gcc -std=gnu11 -O0 -g -Wall -Wextra src/container_of_step_by_step.c -o cofs && ./cofs
gcc -std=gnu11 -O2 -masm=intel -S src/asm_check.c -o src/asm_check.s
```
