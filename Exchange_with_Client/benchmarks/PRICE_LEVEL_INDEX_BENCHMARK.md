# 价格索引 A/B 基准

## 比较范围

`price_level_index_benchmark.cpp` 单独比较 `std::unordered_map<Price, MEOrdersAtPrice*>` 与排序连续 `std::vector<Entry>` 的价格索引操作。它先运行 BUY/SELL 两侧正确性检查，再测命中查找、未命中查找及插入/删除。价格查询由固定随机种子 `0xC0FFEE` 生成；同一进程中的两种实现使用相同查询轨迹。测试的活跃档位数为 16、64、128、256，符合 `ME_MAX_PRICE_LEVELS=256` 的当前配置。

这是索引微基准，不是完整撮合引擎端到端测试：档位对象/订单节点、FIFO 队列、撮合事件、网络、日志和响应队列不在计时路径中。flat 版连续存储价格键与档位指针，`MEOrdersAtPrice` 和订单对象仍沿用原来的内存池布局。若要验证完整订单簿或简历中的端到端表述，后续还需增加确定性订单流回放并比较最终簿状态、回报和成交。

## Ubuntu 编译与运行

在 `Exchange_with_Client` 目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target price_level_index_benchmark -j
./build/price_level_index_benchmark
```

默认模式会先做正确性检查，再输出两种实现的纳秒/操作中位数。

## 分开采集硬件计数器

为了让 `perf stat` 的计数只对应一种索引，使用两个独立进程：

```bash
perf stat -r 5 -e cycles,instructions,branches,branch-misses,cache-references,cache-misses \
  ./build/price_level_index_benchmark --only unordered_map

perf stat -r 5 -e cycles,instructions,branches,branch-misses,cache-references,cache-misses \
  ./build/price_level_index_benchmark --only flat
```

两条命令使用相同固定种子和规模集合。记录 CPU 型号、编译器版本、内核、编译选项及 VM vCPU 配置；尽可能在同一台空闲 VM 上交替运行两种模式。若 `perf` 报权限错误，需要在 Ubuntu VM 配置内核性能计数器访问权限，或使用允许 `perf` 的宿主机环境。

不要把该微基准单独当作性能结论。`unordered_map` 平均查找复杂度为 O(1)，vector 查找为 O(log N)，vector 插入/删除为 O(N)；缓存和分支计数需要结合实际数据与工作负载解释。网络双 VM 适合后续验证客户端—交易端集成，不适合归因索引本身的延迟。
