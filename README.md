# 项目三 · 从零实现 Cortex-M3 抢占式 RTOS 内核

[![ci](https://github.com/RiKai6821/cortex-m-rtos/actions/workflows/ci.yml/badge.svg)](https://github.com/RiKai6821/cortex-m-rtos/actions)

> 状态：**内核 + 同步原语全部实现** ✅。`make` 产出 ARM 固件 `rtos.elf`；
> **可移植内核逻辑（调度器 / 互斥量 / 队列 / 定时器）在宿主机上单元测试 39/39 通过**
> （`make test`，无需硬件）；PendSV 上下文切换汇编已反汇编验证。需 STM32F103 实体板
> 观察上板调度行为。

把"会用 FreeRTOS"升级成"会造 RTOS"：在 STM32F103（Cortex-M3）上自己写一个能
**抢占调度**的迷你内核，吃透上下文切换、异常栈帧、调度器、临界区、**优先级继承**。

> 一条"硬件 → 模拟器 → 操作系统内核"嵌入式作品集的第三站（配套项目：STM32 寄存器级
> 传感器系统、RV32I 指令集模拟器）。

## 已实现

```
include/
  rtos.h            内核 API + TCB + 信号量/互斥量/队列/定时器 + port 接口
  kernel_internal.h 跨模块内部接口（供原语与宿主测试用）
  stm32f103.h       手写寄存器子集（SysTick / SCB / RCC / GPIO）
src/
  port.c    ★Cortex-M 相关：PendSV 上下文切换（naked 汇编）+ SysTick 节拍 +
            临界区(PRIMASK) + 任务栈帧构造 + idle(wfi)
  kernel.c  TCB、调度器（优先级抢占 + 同优先级轮转）、节拍、os_delay、信号量
            —— 纯可移植 C，宿主机也能编译运行
  mutex.c   ★优先级继承互斥量
  queue.c   ★消息队列（rendezvous 交接，无自旋）
  timer.c   软件定时器（周期/单次，tick 驱动）
  system.c  时钟树 72MHz
  main.c    demo：软件定时器闪灯 + 生产者→队列→消费者 + 互斥量保护共享总数
test/
  host_port.c  桩 port（宿主机用，无真实上下文切换）
  host_test.c  调度器 / 优先级继承 / 信号量 / 队列(含数据) / 定时器 单元测试
startup/ + linker/   Cortex-M3 向量表 + 64K/20K 布局
```

## 构建与测试

```bash
make            # ARM 固件（需 arm-none-eabi-gcc）-> build/rtos.elf
make test       # 宿主机单元测试（39 项，无需硬件）-> 验证内核逻辑
```

`make test` 输出（节选）——**真实验证优先级继承与队列数据投递**：

```
[ mutex: priority inheritance ]
  ok   g_tasks[L].priority == 3      <- L 继承了 H 的优先级
  ok   m.owner == &g_tasks[H]        <- 释放后锁交给 H
[ queue: buffered + blocking rendezvous (with data) ]
  ok   out == 99                     <- 数据直接送达阻塞的接收方
PASSED: 39 checks, 0 failed
```

## 核心一：PendSV 上下文切换（已反汇编验证）

```asm
PendSV_Handler:
  mrs   r0, psp              ; 取当前任务 PSP
  cbz   r2, 1f               ; 首次切换无需保存
  stmdb r0!, {r4-r11}        ; 软件保存 R4-R11（硬件已自动压 xPSR/PC/LR/R12/R3-R0）
  str   r0, [r2]            ; current_tcb->sp = psp
1:... 切 next ...
  ldmia r0!, {r4-r11}        ; 恢复 R4-R11
  msr   psp, r0
  ldr   r0, =0xFFFFFFFD      ; EXC_RETURN：返回 Thread 模式、用 PSP
  bx    r0
```

## 核心二：优先级继承互斥量

高优先级任务 H 阻塞在低优先级 L 持有的锁上时，**把 L 临时提升到 H 的优先级**，避免中
优先级任务 M 把 L 压着、间接拖死 H（优先级反转）。详见
[docs/01-priority-inversion-and-primitives.md](docs/01-priority-inversion-and-primitives.md)。

## 设计亮点：可移植 = 可测试

所有 CPU 相关代码收进 `port_*`，内核逻辑是纯 C，于是同一份代码：在 STM32 上用真实
PendSV 切换运行；在宿主机配桩 port 跑单元测试。这是项目二"逻辑/port 分离、宿主可测"
思路在 RTOS 上的复用。

## 面试深问预案

- **"什么是优先级反转？怎么解决？"** 见上：优先级继承——竞争时提升锁持有者优先级，
  释放时恢复并把锁交给最高优先级等待者。单测里能看到 `L.priority` 被提升到 3 再恢复。
- **"为什么上下文切换放 PendSV？"** PendSV 设最低异常优先级，保证切换只在所有其它 ISR
  跑完后发生——不会在高优先级中断中途切走，切换对线程原子。
- **"异常栈帧里硬件压了哪些、软件压了哪些？"** 硬件自动压 `xPSR/PC/LR/R12/R3-R0`；
  软件在 PendSV 里手动存 `R4-R11`。
- **"首次切换怎么'假装从中断返回'进入任务？"** 任务创建时构造伪异常栈帧（`xPSR` 置
  Thumb 位、`PC`=入口），首个 PendSV 用 `EXC_RETURN` 弹入任务。
- **"队列为什么不用自旋重试？"** 用 rendezvous 交接语义（唤醒即交付），无丢唤醒、逻辑
  可确定性测试。

## 后续

- 嵌套互斥量的传递优先级继承；带超时的阻塞；事件标志组
- 上板用 LED/串口观察调度，对比 FreeRTOS 行为
