# 同步原语与优先级反转

本文档讲清三个新原语的实现，重点是**优先级反转**与**优先级继承**——RTOS 面试的经典题。
所有可移植逻辑都在宿主机上单元测试过（`make test`，25 项全过）。

## 1. 优先级反转（priority inversion）

```
优先级:  H(高) > M(中) > L(低)
时刻 t0: L 拿到了互斥量，进入临界区
时刻 t1: H 就绪，抢占 L，但要的资源被 L 占着 -> H 阻塞在互斥量上
时刻 t2: M 就绪（不需要这个资源），抢占 L
         => L 被 M 压着跑不完，H 就一直等 L、间接被 M 拖死
            高优先级的 H 反被中优先级的 M 阻塞 —— 这就是优先级反转
```

火星探路者（Mars Pathfinder）著名的复位 bug 就是这个。

## 2. 优先级继承（priority inheritance）—— 我们的解法

当 H 阻塞在 L 持有的互斥量上时，**把 L 的优先级临时提升到 H 的优先级**，这样 M 不能
再抢占 L，L 能尽快跑完临界区并释放；释放时再把 L 降回原优先级，并把锁交给等待者中
优先级最高的那个。

```c
// mutex_lock：竞争时继承
if (current_tcb->priority > m->owner->priority)
    m->owner->priority = current_tcb->priority;   // 提升持有者
os_block_and_yield(m, p);

// mutex_unlock：恢复 + 直接交接（无竞争窗口）
current_tcb->priority = current_tcb->base_priority;  // 降回原值
tcb_t *w = os_highest_waiter(m);
if (w) { os_unblock(w); m->owner = w; }              // 交给最高优先级等待者
else     m->owner = 0;
```

TCB 因此多了一个 `base_priority`（创建时的原始优先级）和 `priority`（当前有效优先级，
可能被提升）。

**单元测试验证**（`test/host_test.c`，全过）：
- L 拿锁后 `priority==1`；
- H 竞争 → H 阻塞、`L.priority` 被提升到 `3`、`owner` 仍是 L；
- L 释放 → `L.priority` 恢复 `1`、锁交给 H、H 变为可运行。

> 本实现是**单层**继承；嵌套互斥量的传递继承（transitive PI）是可选延伸，文末列出。

## 3. 消息队列（rendezvous 交接，无自旋）

队列用**交接语义**，没有"唤醒后重试"的自旋循环，因此不会丢唤醒、逻辑可确定性测试：

- **空队列 + 有接收者在等**：发送方直接把数据 `copy` 进接收者的缓冲区并唤醒它
  （rendezvous），不入队。
- **满队列 + 有发送者在等**：接收方出队后，把阻塞发送者的数据拉进空出的槽并唤醒它。
- 其余情况：数据正常进/出环形缓冲区。

接收方阻塞时把目的缓冲区指针存进 `tcb->msg`；发送方据此**直接投递**——单元测试里
`out == 99` 正是验证数据被真实送达阻塞的接收方（不只是状态对）。

## 4. 软件定时器

活动定时器挂在单链表上，`timers_tick()`（由 `os_tick` 调用）逐个递减，到点触发回调：
周期定时器重装，单次定时器置为非活动。回调跑在 tick（ISR）上下文，必须短。
单元测试验证：周期 5 tick 在第 5、10 tick 各触发一次；`timer_stop` 后不再触发；
单次定时器只触发一次。

## 5. 可移植性 = 可测试性

把所有 CPU 相关代码收进 `port_*`（ARM 的 PendSV/SysTick/PRIMASK 在 `src/port.c`），
内核逻辑（调度器、互斥量、队列、定时器）就是纯可移植 C。于是同一份逻辑：
- 在 STM32 上用真实 PendSV 上下文切换运行；
- 在宿主机上配一个桩 port（`test/host_port.c`）跑单元测试，断言状态机转换。

这正是项目二（RV32I 模拟器）"逻辑与 port 分离、宿主可测"思路在 RTOS 上的复用。

## 6. 可选延伸

- 嵌套互斥量的**传递优先级继承**（H 等 L、L 又等更低的 K，需链式提升）。
- 互斥量记录"持有的锁列表"，释放时按剩余等待者**重算**有效优先级（比直接降到 base 更精确）。
- 事件标志组、带超时的阻塞（`os_delay` 与等待队列结合）。
