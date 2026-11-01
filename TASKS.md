# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。

## 未完成

- [ ] B-ID57 server_mvc generator_sources 红：生成产物断言 "api json projection" 失败（Zan.Mvc 源生成器 Gen.zan 的输出内容，非 routegen）。已修闭：admin_gate 金样 193 为旧"属性壳"架构（387 动作/29 控制器）的内容快照，b1752059e 重构改直连动作（150 动作）后真值 173 且行为断言（gate/trio/preAnonymous/menu/txprobe）全绿——金样已按内容漂移更新；ai_endpoint/timezone 双案因 CMakeLists 残留 ZanWeb 旧路径瞬时红亦已修。
- [ ] B-ID58 CodeFirst 主键链断裂（f896826d3 TypeInfo 切换回归）：`[Column(IsPrimary = true)]` 在反射面零消费（irgen_reflect.c 位表只认 [Key]/[PrimaryKey]/IsIdentity），SyncAsync 建表丢 PRIMARY KEY，而 Insert.OnConflict 以 PkCols 生成 `ON CONFLICT(主键列)` → SQLite "ON CONFLICT clause does not match any PRIMARY KEY"；且 2e3932369 把 OnConflict 复合键 lambda 降为单列 string（inner.OC 可累加可链式补救）。正确修复需把 IsIdentity/IsPrimary 拆成两个反射位（编译器 ABI + selfhost 镜像 + Meta.BuildFor 接线 + PkList 口径），selfhost 镜像为并发会话在飞文件，落地需协调。已修部分：SyncStructureAsync 包装 async void→async bool（await 挂 async void 是火后不理，MetricsStore.Start 曾在建表完成前还池）；MetricsStore 直调 SyncAsync 绕开泛型扩展特化失真（泛型扩展体内 Meta.For<T> 静默 false，最小复现：扩展方法+泛型 T+await 内层）。metrics_flush_retry 仍红（建表已通、卡 ON CONFLICT）。
- [ ] B-ID59 ORM 查询链泛型推断断裂（2e3932369 DbExtensions 重写 +805 行的爆炸半径，HEAD 既有，非 11-01/11-02 批次引入）：E2108 泛型不变性——`ToList<RouteStat>(a => new RouteStat {...})` / `ToList(a => a.path)` 等链式投影报 cannot convert List<MetricMinute> to List<string>；case_orm_group_aggregate 双案、case_linq_and_zandb_opt、case_orm_metadata_concurrency、case_chart_data_range 等约 20+ 案红（server_mvc/zandb/orm 族 -j2 扫 129 中 24 红）。




