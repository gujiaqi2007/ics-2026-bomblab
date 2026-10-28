# BombLab 实验报告

实验是在 WSL2 的 Ubuntu 里做的（`uname -sm` 输出 `Linux x86_64`），工具是 gcc 15.2、binutils 2.46
和 gdb 17.1。除了 `make bomb` 领到的个人炸弹，我只动了 `defuser.c`，另外加了这份报告和
`images/` 里的截图；`bomb` 已经提交进仓库，Actions 能跑。

用的方法就是实验指导里那两条：先用 `objdump -d --no-show-raw-insn bomb > all.S` 把汇编导出来读，
猜出一个计算关系，再到 gdb 里停在对应的 `cmp` 前面看程序实际算出来的值；对得上了才写进
`defuser.c`，然后 `./defuser | ./bomb` 跑一遍。六关的入口长得几乎一样，所以先说这个共同点。

## 入口的固定套路

```asm
0000000000402090 <phase_01>:
  40209a: mov    %rsi,%r12          # r12 = challenge
  40209d: mov    $0x1,%esi          # 这一关要几个整数
  4020a3: mov    %rdi,%rbx          # rbx = 输入的那一行
  4020aa: call   402c80 <valid_u32_fields>
  4020af: test   %al,%al
  4020b1: jne    4020c0             # 格式不对就直接返回 0
```

`valid_u32_fields(line, n)` 要求这一行恰好是 n 个无符号十进制整数：跳过空白、读数字（用
`0xcccccccd` 做乘法判断有没有超过 `UINT32_MAX`）、字段之间要有空白、多了少了都不行。所以每关
口令有几个整数，直接看 `mov $n,%esi` 就知道：第 1、4、6 关是 1 个，第 2 关 6 个，第 3、5 关 2 个。
对应的 `sscanf` 格式串在 `.rodata` 的 `0x403189`（`"%u"`）、`0x40317a`（六个 `%u`）和
`0x403186`（两个 `%u`）。

## 第 1 关

`phase_01` 先把 challenge 传给 `p1_compute`，由它依次调用三个只有三条指令的小函数：

```asm
0000000000402020 <p1_step0>:        # (3a) ^ (b + 0x87de)
  402024: lea    (%rdi,%rdi,2),%eax
  402027: add    $0x87de,%esi
  40202d: xor    %esi,%eax
0000000000402030 <p1_step1>:        # (3a + b) ^ 0x4929
  402034: lea    (%rdi,%rdi,2),%eax
  402037: add    %esi,%eax
  402039: xor    $0x4929,%eax
0000000000402040 <p1_step2>:        # (a + 8b) ^ 0x1a5f
  402044: lea    (%rdi,%rsi,8),%eax
  402047: xor    $0x1a5f,%eax
```

调用顺序和参数是这一关唯一的坑：

```asm
  40205b: mov    %edi,%esi          # esi = challenge 的低 32 位
  40205d: shr    $0x20,%rax         # rax = 高 32 位
  402064: shr    $0x10,%rdx         # rdx = challenge >> 16（留给后面的 call）
  402068: mov    %eax,%edi          # edi = 高 32 位
  40206d: call   402040 <p1_step2>  # 参数是 (高 32 位, 低 32 位)
  402072: mov    %rdx,%rsi          # 到这里 rsi 才变成 (challenge >> 16)
  402075: mov    %r8d,%edi          # edi = 低 32 位
  402078: mov    %eax,%ecx          # 先把 step2 的返回值存到 ecx
  40207a: call   402020 <p1_step0>
  40207f: mov    %ecx,%esi          # step1(step0 的结果, step2 的结果)
  402081: mov    %eax,%edi
  402083: call   402030 <p1_step1>
```

我一开始按“`rdx` 已经是 `challenge >> 16`，所以第一个 call 的第二个参数是它”去读，算出来的数
和炸弹期望的不一样；后来把每个 call 前一刻的寄存器重新列了一遍才发现，`p1_step2` 拿到的是
低 32 位，`challenge >> 16` 是第二次调用才用的。另外 `mov %edi,%esi`、`mov %eax,%edi` 这类
32 位写会清零高半部分，所以下面全部按 uint32 回绕：

```
lo  = ch & 0xffffffff
hi  = ch >> 32
mid = (ch >> 16) & 0xffffffff
t = (hi + 8*lo) ^ 0x1a5f
a = (3*lo) ^ (mid + 0x87de)
口令 = (3*a + t) ^ 0x4929
```

在 gdb 里核对的方式是：`break *phase_01`，停下后 `p/x $rsi` 拿到 `0x28efe333b266f103`，用
上面几个式子算出 `2552915410`；再 `break *0x4020e0`（就是那条 `cmp`）继续运行，`p/u $eax` 和
`x/1uw $rbp-0x14` 也都是 `2552915410`，说明式子没写错。截图在 `images/04-gdb-phase1.png`。

## 第 2 关

六个数是 `sscanf` 依次写到 `-0x40(%rbp)` 的，正好是一个 uint32 数组。第一个数的判断很直接：

```asm
  40215a: mov    %r13d,%eax          # r13 = challenge
  40215d: mov    -0x40(%rbp),%esi
  402160: and    $0xf,%eax
  402163: xor    $0x1c,%eax
  402166: cmp    %eax,%esi
  402168: jne    402115
```

后面是一个五轮的循环，每轮用 challenge 里不同的四位去算下一个数：

```asm
  40216e: mov    $0x4,%edi
  402173: mov    $0x1c,%ecx          # cl 一开始是 28
  402178: sub    %edx,%edi           # edi = 4 - (int)&a[0]
  ...
  40217d: shr    %cl,%rax            # challenge >> cl
  402180: and    $0xf,%eax
  402183: add    %esi,%eax           # 加上前一个数
  402185: lea    (%rdi,%rdx,1),%esi  # 这里 rdx 是 &a[i]
  402188: xor    %esi,%eax
  40218a: mov    0x4(%rdx),%esi      # 下一个数，成为下次的“前一个”
  402191: add    $0x5,%ecx           # cl += 5
  402194: add    $0x4,%rdx
  402198: cmp    $0x35,%ecx
  40219b: jne    40217a
```

`mov $0x4,%edi` 加 `sub %edx,%edi`，再在循环里 `lea (%rdi,%rdx,1),%esi`，看着像在算地址，
其实低 32 位正好是 `4 + 4*i`，也就是第 i 轮异或的常数。`cl` 从 28 开始每次加 5，循环在
`ecx == 0x35` 时结束，所以取的是 28、33、38、43、48，一共五轮，校验 `a[1]` 到 `a[5]`。

```
a[0]   = (ch & 0xf) ^ 0x1c
a[i+1] = (((ch >> (28+5*i)) & 0xf) + a[i]) ^ (4*(i+1))     i = 0..4
```

最高用到 `ch >> 48`，刚好还在 64 位里。

## 第 3 关

第一关那个数是选择子，第二个数由跳转表选出来的分支函数算：

```asm
  4022fc: mov    %r12d,%edi
  402301: and    $0x7,%edi           # 选择子
  402304: cmp    %edi,-0x18(%rbp)
  40230f: shr    $0x3,%rax
  402313: shr    $0x20,%rsi
  402317: xor    %eax,%esi           # 参数 = (ch>>3) ^ (ch>>32)
  402319: call   402250 <p3_dispatch>

0000000000402250 <p3_dispatch>:
  402258: cmp    $0x7,%eax
  40225b: ja     4022a5              # 越界就返回 0
  40225d: notrack jmp *0x405820(,%rax,8)
```

表在 `0x405820`，用 `objdump -s -j .rodata` 读出来是八个指针。这里容易被坑一次：表的下标顺序
和 case 的编号不是一回事，`ch & 7 == 0` 跳到的是 `case_2`：

```
下标 0 → 0x402278 (case_2)    下标 4 → 0x402298 (case_1)
下标 1 → 0x402280 (case_0)    下标 5 → 0x4022a0 (case_3)
下标 2 → 0x402288 (case_5)    下标 6 → 0x402268 (case_4)
下标 3 → 0x402290 (case_7)    下标 7 → 0x402270 (case_6)
```

八个分支函数各算一式（x 是参数）：

```
case_0: (x ^ 0xff19) + (x >> 2)      case_1: 5x + 0x20d73
case_2: (4x) ^ 0x1ec                 case_3: 3x + 0xc7dd
case_4: (2x) ^ 0xb2c3                case_5: 3x + 0x60f8
case_6: 5x + 0x2adaf                 case_7: (x ^ 0xd628) + (x >> 1)
```

所以口令是 `ch & 7` 和“下标 `ch & 7` 对应分支( `(ch>>3) ^ (ch>>32)` )”。我第一版直接把下标当
case 编号用，公开测试里第 3 关十个 seed 全错，`wrong_answer`，后来重新对了表才通。

## 第 4 关

`p4_walk(target, lo, hi, key)` 是个二分递归：

```asm
0000000000402330 <p4_walk>:
  402334: mov    %edi,%eax           # eax = target
  402336: mov    %edx,%edi
  402338: sub    %esi,%edi           # hi - lo
  40233a: shr    $1,%edi
  40233c: add    %esi,%edi           # mid = lo + ((hi-lo)>>1)
  40233e: cmp    %eax,%edi
  402340: je     402390              # target == mid，递归到底
  402347: lea    (%rdi,%rcx,1),%ebx  # ebx = mid + key
  40234e: cmp    %edi,%eax
  402350: jb     402370              # target < mid 往左
  402352: lea    0x1(%rdi),%esi      # 往右：lo = mid + 1
  402357: call   402330 <p4_walk>
  40235c: lea    (%rax,%rax,2),%eax  # 3 * 返回值
  40235f: xor    %ebx,%eax
  402366: xor    $0x19,%eax
  ...
  402370: lea    -0x1(%rdi),%edx     # 往左：hi = mid - 1
  402375: call   402330 <p4_walk>
  40237a: lea    (%rax,%rax,2),%eax
  40237d: xor    %ebx,%eax
  402384: xor    $0x1f,%eax
  402390: lea    0x23(%rdi),%eax
  402393: xor    %ecx,%eax           # (target + 0x23) ^ key
```

入口在 `phase_04` 里：`target = challenge & 0x7f`，`lo = 0`，`hi = 0x7f`，
`key = (ch>>7) ^ (ch>>32)`。`mid` 用的是逻辑右移 `shr`，`lo/hi` 也是 32 位的，所以整个区间就是
无符号的 `[0,127]`，递归一定会停在某个 `target == mid` 上。

返回值是这么回溯的：最里层算 `(target + 0x23) ^ key`，每往上一层做
`((3 * 下层结果) ^ (mid + key)) ^ 常数`，常数按方向取，往右 0x19、往左 0x1f。这一点我特意用
`bt` 确认过：栈里的返回地址 `0x40235c` 是右边那条路、`0x40237a` 是左边那条，和两处的 `xor`
对得上（`images/05-gdb-phase4.png`）。

写进 `defuser.c` 的时候我没有用递归，而是按同样的规则从 `[0,127]` 往下走，把每层的 `mid` 和
方向存起来，走到 `target == mid` 之后再倒着算回去：

```c
result = (target + 0x23) ^ key;
while (depth > 0) {
    --depth;
    result = ((3 * result) ^ (mids[depth] + key)) ^ (right[depth] ? 0x19 : 0x1f);
}
```

## 第 5 关

这一关有个链表。`phase_05` 调 `p5_traverse(challenge, &out)`，然后把返回值跟第一个数比、把
`out` 里存下的 tag 跟第二个数比：

```asm
  4024ec: cmp    %edx,-0x1c(%rbp)    # 第一个数 = fold 结果
  4024f1: mov    -0x14(%rbp),%eax
  4024f4: cmp    %eax,-0x18(%rbp)    # 第二个数 = 最后访问的结点的 tag
```

`p5_traverse` 本身：

```asm
  40242d: and    $0x7,%eax
  402434: mov    0x405b60(,%rax,8),%rdx   # 从 8 个表项里选一个结点出发
  40243f: shr    $0x20,%rdi
  402443: shr    $0x6,%rax
  402447: xor    %edi,%eax           # (ch>>6) ^ (ch>>32)
  402449: mov    0x8(%rdx),%esi      # 结点的 +0x8
  40244e: xor    $0xd86a,%eax
  402453: add    0xc(%rdx),%eax      # 加上结点的 +0xc
  402456: lea    (%rax,%rax,4),%eax  # 乘 5
  402459: mov    %esi,(%r8)          # 存 tag
  40245c: xor    %esi,%eax           # 再异或 tag
  40245e: and    $0x3,%edi           # 还要走几步 = (ch>>3) & 3
```

所以一个结点是 16 字节：`+0x0` 是 next 指针，`+0x8` 是 tag，`+0xc` 是个常数（我叫它 salt）。
`0x405b60` 那张表选出起点，之后每走一步都是“加上新结点的 salt、乘 5、异或它的 tag”，同时把
tag 覆盖到输出位置上。十二个结点我照 `.rodata` 抄成了数组（按地址顺序编号，next 写成下标，
空指针写 -1）：

```
下标 地址     next  tag    salt
 0  405ba0     8   361   0x8ea4
 1  405bb0     7   1579  0xe782
 2  405bc0     9   2395  0xbcc1
 3  405bd0     2   1883  0x50c1
 4  405be0    10   596   0x51aa
 5  405bf0     3   2935  0x0902
 6  405c00     4   1337  0xa50e
 7  405c10     -   1071  0x4c38
 8  405c20     6   45    0x2a
 9  405c30    11   892   0xfd77
10  405c40     1   2595  0x6815
11  405c50     0   2108  0x92b8
```

表 `0x405b60` 的八项依次指向 5、3、2、9、11、0、8、6 号结点。

把汇编翻成式子就是：

```
node  = 表[ch & 7]
fold  = (((ch>>6) ^ (ch>>32)) ^ 0xd86a + node.salt) * 5
fold ^= node.tag
重复 (ch>>3)&3 次：
    node = node.next
    fold = (fold + node.salt) * 5
    fold ^= node.tag
第二个数 = 最后访问的那个结点的 tag
```

这里我犯过一个错，第五关有十个公开 seed 都过了，但换成随机 challenge 之后，凡是
`ch & 7 == 7` 且要走到第三步的都失败。原因是结点 10（`0x405c40`）的 next 我抄成了 `0x405ba0`，
实际是 `0x405bb0`——两个地址只差最后一位，`objdump -s` 里就是 `b0 5b 40 00` 和 `a0 5b 40 00`
的区别。改过来之后这一关才真的稳。

## 第 6 关

这一关两次查函数指针表：

```asm
0000000000402580 <p6_dispatch>:
  40258c: shr    $0x6,%rdi
  402593: shr    $0x20,%rax
  402597: xor    %eax,%edi           # 第一次调用的参数 = (ch>>6) ^ (ch>>32)
  4025a0: and    $0x38,%ebx          # 第二个下标，按字节算
  4025a3: and    $0x7,%eax           # 第一个下标
  4025a6: call   *0x405e00(,%rax,8)
  4025ad: xor    $0x3558,%eax
  4025b2: mov    %eax,%edi
  4025b4: call   *0x405e00(%rbx)     # 第二次用的是第一次的结果
  4025bf: add    $0xdc0f,%eax
```

表在 `0x405e00`，读出来是

```
表[0..7] = op_0, op_2, op_4, op_4, op_3, op_1, op_5, op_5
```

（`op_4`、`op_5` 各出现两次，`op_3` 在第 4 位，同样不能拿下标当编号。）六个运算：

```
op_0: (x + 0x3450) ^ (x >> 3)
op_1: rol(x, 2) ^ 0x4ca2
op_2: ((x << 7) + 0x1f88) ^ (5x)
op_3: ror(x, 2)
op_4: (x ^ 0xbb14) + 4x
op_5: ((x >> 3) ^ x) + 0xaa45
```

于是

```
x = (ch>>6) ^ (ch>>32)
r = 表[ch & 7](x)
r = r ^ 0x3558
r = 表[(ch>>3) & 7](r)
口令 = r + 0xdc0f
```

那条 `and $0x38,%ebx` 是按字节偏移寻址，所以第二个下标是 `(ch>>3) & 7`，不是 `ch & 0x38`。

## Honor

六关都过了、而且输入是从管道或文件来的时候，`main` 会再读一行（空行和只有空白的行会跳过），
然后调 `secret_gate(line, phase_challenge(options, 7), transcript_context(六行口令))`。

`secret_gate` 开头先开了一块局部结构体并清零，然后做长度检查和无长度检查的复制：

```asm
  402749: lea    -0xd0(%rbp),%r12    # 结构体基址
  402760: rep stos %rax,(%rdi)
  40276c: call   401190 <strlen@plt>
  402771: sub    $0x24,%rax
  402775: cmp    $0x7f,%rax
  402779: jbe    402790              # 0x24 <= strlen(line) <= 0xa3
  402796: call   402710 <secret_copy>   # while ((*d++ = *s++));
  40279b: cmpl   $0x4e45504f,-0xb0(%rbp) # +0x20 处要是 "OPEN"
  4027ad: lea    -0xac(%rbp),%rdi      # +0x24 处是数字串
  4027b4: call   402650 <secret_phase>
```

结构体在 `rbp-0xd0`，`-0xb0` 就是 `+0x20`，`-0xac` 就是 `+0x24`。所以一行的布局是：

```
0x00 ~ 0x1f   32 字节，随便什么，程序只是拿它凑长度
0x20 ~ 0x23   "OPEN"（小端立即数 0x4e45504f）
0x24 ~        三个十进制整数
```

`strlen` 下界正好是 0x24 = 32 + 4，也算是旁证。gdb 里复制完 `x/40cb $rbp-0xd0`，第 33 到 36
个字节就是 `'O' 'P' 'E' 'N'`，`x/s $rbp-0xac` 打出 `"1 1428158640 3807249850"`
（`images/06-gdb-secret.png`）。我用的前缀是 `BOMBLAB-2026-FDU-HONOR-TICKET---`，正好 32 字节，
`defuser.c` 里加了一句 `_Static_assert` 防止以后改前缀时长度不对。

三个数在 `secret_phase` 里算，用的是 challenge 和 context：

```asm
  4026b5: and    $0x3,%ecx
  4026b8: cmp    %ecx,-0x2c(%rbp)    # 第一个数 = context & 3
  4026c0: imul   $0xd6b9,%ecx,%r9d
  4026d2: xor    %r12d,%esi          # (ch>>32) ^ ch
  4026d5: xor    %r13d,%r8d          # (ctx>>32) ^ ctx
  4026de: xor    %r13d,%r9d          # r9 ^= (context >> 16)
  4026e1: xor    %r8d,%edi
  4026e4: call   402630 <secret_mix> # rol(x,3) + 0xd6b9 再 ^ 0x2d7f
  4026e9: xor    %r9d,%eax
  4026ec: cmp    %eax,-0x28(%rbp)    # 第二个数
  4026f1: shr    $0x3,%esi
  4026f4: lea    0x2d7f(%r8,%rcx,1),%edx
  4026fe: xor    %edx,%eax
  402700: cmp    -0x24(%rbp),%eax    # 第三个数
```

整理一下（都是 uint32）：

```
n0 = ctx & 3
x  = ((ch>>32) ^ ch) ^ ((ctx>>32) ^ ctx)
n1 = (rol(x,3) + 0xd6b9 ^ 0x2d7f) ^ (0xd6b9 * n0) ^ (ctx >> 16)
n2 = n1 ^ ( ((ctx>>32) ^ ctx) + n0 + 0x2d7f + (((ch>>32) ^ ch) >> 3) )
```

## 测试

公开测试全过：

```
$ python3 public_test.py
...
Public tests: 60/60
...
Secret public tests: 10/10
```

另外每个关都拿公开列表之外的 seed 验过一遍（`tests/public.json` 里只有 0、1、7、42、99、123、
2026、65535、987654321、2^64-1）：

- 第 1 关：seed 20261027，challenge `0x635b6202ab529643`，`Phase 1 defused.`
- 第 2 关：seed 20261027，challenge `0xe0c8e9be2aab39d4`，`Phase 2 defused.`
- 第 3 关：seed 20261027，challenge `0x9551bee1da18c95f`，`Phase 3 defused.`
- 第 4 关：seed 20261027 `0xf8c6dde2f944ff26`，另外 seed 424242 `0x974ae7d5606237d7`，都 defused
- 第 5 关：seed 20261027，challenge `0xc64e3e27d20df988`，`Phase 5 defused.`
- 第 6 关：seed 20261027 `0xf0b8b1d373d19725`，另外 `--challenge 0xdeadbeefcafef00d`，都 defused
- Honor：seed 20261027 六关加第七行，`Secret phase defused.`

终端记录在 `images/03-extra-seeds.png`，里面还有 `--challenge 18446744073709551615` 跑第 2 关。

公开 seed 太少了，只能说明那十个点对。我另外写了个脚本，每关取 18 个边界值（0、1、0x7f、
0xffffffff、2^64-1、最高位置 1 之类）加 300 个随机 64 位值，直接比较
`./defuser --challenge C --phase P` 和 `./bomb --challenge C --phase P`；Honor 再取 40 个随机
seed 走完整流程。最后一共 1948 次，全过（`images/07-fuzz.png`）。上面第 5 关那个 next 指针的
笔误就是这轮抓出来的，`--challenge` 这个参数在排查时比 `--seed` 好用很多。

push 之后 Actions 也跑了，提交 `51326be` 上七个任务都是 success，包括 Honor：

```
Phase 1 public tests   success     Phase 5 public tests   success
Phase 2 public tests   success     Phase 6 public tests   success
Phase 3 public tests   success     Honor public tests     success
Phase 4 public tests   success
```

## 截图

seed 42 一次跑完六关再进 Honor（`./defuser --seed 42` 和 `./defuser --secret --seed 42` 一起
喂给炸弹）：

![六关与 Honor 运行结果](images/01-run-all.png)

公开测试：

![公开测试](images/02-public-tests.png)

非公开 seed：

![非公开 seed 验证](images/03-extra-seeds.png)

第 1 关的 gdb 核对：

![GDB 核对第 1 关](images/04-gdb-phase1.png)

第 4 关递归，`bt` 里能看到两种返回地址：

![GDB 观察第 4 关递归](images/05-gdb-phase4.png)

Honor 的结构体布局：

![GDB 观察 Honor 的结构体布局](images/06-gdb-secret.png)

随机 challenge 回归：

![随机 challenge 回归](images/07-fuzz.png)

## 参考资料和工具

参考资料主要是课程实验页面、CS:APP 第 3 章，以及 CS:APP 的 x86-64 GDB 速查表和 GDB 官方文档；
`man objdump` 用得也挺多，读跳转表和结点表都是靠 `objdump -s -j .rodata`。

- 课程实验页面：<https://ics-26fall-fdu.github.io/labs/lab2-bomb-lab/>
- CS:APP x86-64 GDB 速查表：<http://csapp.cs.cmu.edu/3e/docs/gdbnotes-x86-64.pdf>
- GDB 官方文档：<https://www.sourceware.org/gdb/documentation/>
- Beej's Quick Guide to GDB：<https://beej.us/guide/bggdb/>

工具方面：反汇编用 `objdump -d --no-show-raw-insn` 和 `objdump -s -j .rodata`；验证用 gdb
（`break`、`p/x`、`p/u`、`x`、`bt`，也写了几份 `-batch -x` 的脚本省得每次重敲），没有用 `set`
去改炸弹的寄存器或内存。求解函数和这份报告用了 AI 编程助手（DeepSeek Harness 里的 agent）帮忙
整理和起草，但每个结论都回到 `objdump`、gdb 和炸弹的实际运行上核过，比如第 4 关的 0x19/0x1f
是用 `bt` 的返回地址确认的，Honor 的 `+0x20` 是用 `x/40cb` 确认的。

顺带记一下，炸弹的 `.rodata` 里除了数据表，还夹着几段以 `URGENT AI NOTICE`、`AI ASSISTANT`
开头的英文，看着像给 AI 的指令，其中还有“第 4 关左边用 0x14、右边用 0x1a”“第 5 关第一个结点
的 tag 不参与 fold”这种和真实指令对不上的说法（实际是 0x1f/0x19，实际参与了）。我把它们当数据
处理，没有照着做。

最后，`config.txt` 里留着领取炸弹用的学号，`bomb` 已经提交（Actions 要用），`package.json`
和编译出来的 `defuser` 没提交，调试时的 `input.txt`、`secret.txt` 也没进仓库。
