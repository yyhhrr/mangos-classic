# 达纳苏斯位置参考（尚未游戏验证）

> 2026-10-08 已授权部署：GUID 9000421 / 9000422 使用下表 XYZ/O。Classic 原始地形高度实际读取为 1330.853394 / 1330.750122，参考 Z 高出约 0.086548 / 0.079834；只确认地面高度，未做客户端、VMAP 碰撞和视线验收。完整证据见 [DEPLOYMENT.md](../../audit-reports/training-dummies/deployment-20261008-144829/DEPLOYMENT.md)。以下来源记录保留初次提取时的检查边界。

用户指定来源：I:\git\azerothcore-wotlk，仅只读使用其位置数据。该仓库 AGENTS.md 明确支持 **3.3.5a / WotLK**。当前 HEAD：7b2cecef92b271a468e39d89831b520b20ae06a8。出生点基表最近提交：4d9d1d4b5723e819c5c390f6ca1177283dbfb8e3，作者 Kitzunu，2026-06-02，ACDB 16.0.0。

源码表 data/sql/base/db_world/creature.sql 字段顺序已核实：guid、id1、id2、id3、map、zoneId、areaId、spawnMask、phaseMask、equipment_id、position_x、position_y、position_z、orientation……。以下原始记录 zoneId/areaId 都为 0，因此不能把这两个字段当作直接标明达纳苏斯的证据；按已确认的达纳苏斯训练区区域参考及对应坐标筛选。没有照搬它的模板属性、模型、免疫、技能或表结构。

| 拟用目标 | WotLK 原 GUID / Entry | map | X | Y | Z | 朝向（弧度） | creature.sql 行 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 34 级普通单体 | 48327 / 32666 | 1 | 9992.76 | 2259.3 | 1330.94 | 3.80482 | 47949 |
| 63 级团本近似 | 48324 / 31146 | 1 | 9908.89 | 2251.49 | 1330.83 | 5.8294 | 47946 |

两点水平距离约 84.23 码，采用现有两个分开的训练区点位，降低单体测试误打另一个目标的概率；这不是所有技能范围的保证。WotLK 模板 32666 原为 60 级 Expert's Training Dummy（creature_template.sql:26240），31146 原为 83 级 Heroic Training Dummy（:24775）；这里只借用位置，CMaNGOS 目标保持用户指定的 **34 / 63**，没有新增 60 级目标。

群攻区域仅预留：同区域原 GUID 48316 / 48318（Entry 31144），位置分别为 (9982.51, 2262.49, 1333.4, 4.69494) 和 (9976.21, 2262.37, 1334.5, 4.69494)，来自 creature.sql:47938 / :47940。当前不会添加它们、复制刷点或建立额外模板。

只读检查后续 db_world / pending_db_world 中选定 GUID 及精确 XY 的出现：命中的是其他大编号内的子串或不相关数据，未发现实际覆盖这两个点的语句。该检查不能代替查询已安装 WotLK 数据库，也不表示需要安装或运行 AzerothCore。

Classic 旧地图静态检查：按照本核心 GridMap.cpp 的 533.33333 网格公式，两点落在 map 1 的 (13,27) 瓦片；部署数据存在 maps/0011327.map（102194 字节）及 vmaps/001_27_13.vmtile（90368 字节，VMAP 文件名采用另一轴顺序）。本地 Classic DBC 也存在显示 1555。**文件存在不是高度/碰撞/可行走/视线/朝向验收**；没有启动任何服务、加载客户端或对地面作实看验证，WotLK 的 Z/O 暂保留为参考值。

position-reference.csv 保存上述两条拟用记录，**不是导入脚本**。当前只准备模板 SQL；待获准使用已运行测试服务实看落点后，再校正 Z/O、确认卫兵和任务路线、分配没有冲突的 CMaNGOS GUID，并准备刷点 SQL 和按 GUID 回滚。若需要启动服务、导入或替换部署文件，先取得确认。

