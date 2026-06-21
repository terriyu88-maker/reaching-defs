# Reaching Definitions Analyzer

基于 Worklist 算法的到达定值 (Reaching Definitions) 数据流分析工具。解析 LLVM IR，构建 CFG，迭代求解到达定值，输出 Use-Def 链。

## 快速开始

```bash
# Python (推荐，跨平台)
python3 reach-def.py example.ll -reach -ud

# C++ (Linux / MinGW Windows 交叉编译)
./reach-def example.ll -reach -ud
```

## 选项

| 选项 | 说明 |
|------|------|
| `-reach` | 运行到达定值分析 |
| `-ud` | 显示 Use-Def 链 |
| `-v` | 详细计算步骤 |
| `-cfg` | 输出 CFG DOT 格式 |
| `-o <file>` | 保存输出到文件 |

## 示例

```bash
# 简洁表格模式
python3 reach-def.py example.ll -reach

# 表格 + Use-Def 链
python3 reach-def.py example.ll -reach -ud

# 完整详细输出
python3 reach-def.py example.ll -reach -ud -v

# 保存到文件
python3 reach-def.py example.ll -reach -ud -o result.txt

# 生成 CFG 图
python3 reach-def.py example.ll -cfg | dot -Tpng -o cfg.png
```

## 输入格式

简化 LLVM IR，纯 SSA 形式（不使用 alloca/store），支持分支指令。参见 `example.ll`。

## 算法

采用 **Worklist 迭代算法** (forward may-analysis):

```
OUT[B] = {}  初始所有块
Worklist = 所有块

while Worklist 非空:
    取当前 batch 中的块 B
    IN[B]  = ∪ OUT[P]   (P 为 B 的前驱)
    OUT[B] = Gen[B] ∪ (IN[B] - Kill[B])
    若 OUT[B] 变化:
        将 B 的所有后继加入 Worklist
```

相比固定顺序迭代，Worklist 跳过不变块，加速收敛。

## 输出内容

1. 定义信息 (d1~dN)
2. Gen/Kill 集合
3. Worklist 轮次状态对比表
4. 最终解析 (到达各块入口的定义)
5. Use-Def 链 (可选)

完整分析文档: `docs/reaching-defs-analysis.md`

## 文件

| 文件 | 说明 |
|------|------|
| `reach-def.py` | Python 实现 (推荐) |
| `reach-def.cpp` | C++ 实现 |
| `reach-def` | Linux 可执行文件 |
| `reach-def.exe` | Windows 可执行文件 |
| `example.ll` | 示例 LLVM IR 输入 |
| `example_output.txt` | 示例运行输出 |
| `cfg.png` | CFG 可视化图 |

## 编译

```bash
# Linux
g++ -std=c++11 -O2 -o reach-def reach-def.cpp

# Windows 交叉编译
x86_64-w64-mingw32-g++ -std=c++11 -O2 -static -o reach-def.exe reach-def.cpp
```
