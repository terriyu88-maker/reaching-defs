#!/usr/bin/env python3
"""reach-def.py -- 到达定值分析器 (Worklist 算法 + Use-Def 链)
用法: python3 reach-def.py example.ll [-cfg] [-reach] [-v] [-ud] [-o <file>]
"""

import sys
import re
from collections import OrderedDict, deque


# ======================== IR 数据结构 ========================

class Instruction:
    def __init__(self):
        self.raw = ""
        self.result = ""
        self.opcode = ""
        self.operands = []
        self.true_target = ""
        self.false_target = ""
        self.is_branch = False
        self.is_cond_branch = False


class BasicBlock:
    def __init__(self, name):
        self.name = name
        self.instructions = []
        self.successors = []
        self.predecessors = []


class Def:
    def __init__(self, did, var_name, block_name, inst_str, inst_idx=-1):
        self.id = did
        self.var_name = var_name
        self.block_name = block_name
        self.inst_str = inst_str
        self.inst_idx = inst_idx


# ======================== 工具函数 ========================

def extract_var_name(reg):
    if not reg or reg[0] != '%':
        return ""
    name = reg[1:]
    name = name.rstrip('0123456789')
    return name


# ======================== IR 解析器 ========================

def parse_ir(filename):
    blocks = OrderedDict()
    current_block = None

    with open(filename, 'r') as f:
        for line in f:
            trimmed = line.strip()
            if not trimmed or trimmed.startswith(';'):
                continue
            if trimmed.startswith('define ') or trimmed.startswith('attributes ') or \
               trimmed.startswith('target ') or trimmed.startswith('source_filename') or \
               trimmed.startswith('ModuleID'):
                continue

            if trimmed.endswith(':') and '=' not in trimmed and '%' not in trimmed:
                block_name = trimmed[:-1]
                blocks[block_name] = BasicBlock(block_name)
                current_block = blocks[block_name]
                continue

            if current_block is None:
                continue

            inst = Instruction()
            inst.raw = trimmed

            if trimmed.startswith('br '):
                inst.is_branch = True
                inst.opcode = 'br'
                targets = re.findall(r'label\s+%(\w+)', trimmed)
                if len(targets) == 1:
                    inst.true_target = targets[0]
                    current_block.successors.append(targets[0])
                elif len(targets) >= 2:
                    inst.is_cond_branch = True
                    inst.true_target = targets[0]
                    inst.false_target = targets[1]
                    current_block.successors.append(targets[0])
                    current_block.successors.append(targets[1])
                current_block.instructions.append(inst)
                continue

            if trimmed.startswith('ret '):
                inst.opcode = 'ret'
                inst.is_branch = True
                current_block.instructions.append(inst)
                continue

            eq_pos = trimmed.find('=')
            if eq_pos != -1:
                inst.result = trimmed[:eq_pos].strip()
                right = trimmed[eq_pos + 1:].strip()
                parts = right.split()
                inst.opcode = parts[0]
                for tok in parts[1:]:
                    tok = tok.rstrip(',')
                    if tok and (tok.startswith('%') or tok.lstrip('-').isdigit()):
                        inst.operands.append(tok)

            current_block.instructions.append(inst)

    # 构建前驱列表
    for name, block in blocks.items():
        for succ in block.successors:
            if succ in blocks:
                blocks[succ].predecessors.append(name)

    return blocks


# ======================== 定义编号计算 (供 DOT 和分析共用) ========================

def get_def_labels(blocks):
    """返回 {(blockName, instIndex): 'dN', ...} 表示每条指令的定义编号"""
    block_order = list(blocks.keys())
    all_raw = []
    var_defs = OrderedDict()

    for bname in block_order:
        for ii, inst in enumerate(blocks[bname].instructions):
            if not inst.result:
                continue
            var_name = extract_var_name(inst.result)
            if not var_name:
                continue
            all_raw.append((len(all_raw) + 1, var_name, bname, ii))
            var_defs.setdefault(var_name, []).append(len(all_raw))

    interesting_vars = set()
    for vn, ids in var_defs.items():
        def_blocks = set(all_raw[i - 1][2] for i in ids)
        if len(def_blocks) > 1 and vn != 't':
            interesting_vars.add(vn)

    filtered = [d for d in all_raw if d[1] in interesting_vars]
    labels = {}
    for new_id, (old_id, var_name, bname, ii) in enumerate(filtered, 1):
        labels[(bname, ii)] = f'd{new_id}'
    return labels


# ======================== DOT 格式输出 ========================

def output_dot(blocks, out):
    def_labels = get_def_labels(blocks)

    out.write("digraph CFG {\n")
    out.write("  rankdir=TD;\n")
    out.write('  node [shape=record, fontname="Consolas", fontsize=11];\n')
    out.write('  edge [fontname="Consolas", fontsize=9];\n\n')

    for name, block in blocks.items():
        out.write(f'  "{name}" [label="{{')
        out.write(f'{name} | ')
        first = True
        for ii, inst in enumerate(block.instructions):
            if not first:
                out.write('\\l')
            first = False
            dl = def_labels.get((name, ii), '')
            if dl:
                out.write(dl + ': ')
            if inst.is_branch:
                out.write(inst.raw)
            elif inst.result:
                out.write(f'{inst.result} = {inst.opcode}')
                for op in inst.operands:
                    out.write(f' {op}')
        out.write('}"];\n')

    out.write('\n')
    for name, block in blocks.items():
        for succ in block.successors:
            out.write(f'  "{name}" -> "{succ}";\n')
    out.write('}\n')


# ======================== 到达定值分析 ========================

def bitvec_to_str(bv, total_defs):
    if bv == 0:
        return '{}'
    items = [f'd{i + 1}' for i in range(total_defs) if bv & (1 << i)]
    return '{' + ', '.join(items) + '}'


def run_reaching_defs(blocks, verbose=False, show_ud=False):
    block_order = list(blocks.keys())

    # Step 1: 收集所有定义
    all_defs = []
    var_defs = OrderedDict()

    for bname in block_order:
        block = blocks[bname]
        for ii, inst in enumerate(block.instructions):
            if not inst.result:
                continue
            var_name = extract_var_name(inst.result)
            if not var_name:
                continue

            inst_str = f'{inst.result} = {inst.opcode}'
            for op in inst.operands:
                inst_str += f' {op}'

            d = Def(len(all_defs) + 1, var_name, bname, inst_str, ii)
            all_defs.append(d)
            var_defs.setdefault(var_name, []).append(d.id)

    total_ir_defs = len(all_defs)

    # 筛选有意义变量
    interesting_vars = set()
    for var_name, def_ids in var_defs.items():
        def_blocks = set(all_defs[did - 1].block_name for did in def_ids)
        if len(def_blocks) > 1 and var_name != 't':
            interesting_vars.add(var_name)

    filtered_defs = [d for d in all_defs if d.var_name in interesting_vars]
    all_defs = filtered_defs
    var_defs.clear()
    for i, d in enumerate(all_defs):
        d.id = i + 1
        var_defs.setdefault(d.var_name, []).append(d.id)

    total_defs = len(all_defs)

    # 建索引: (blockName, varName) -> def bit
    def_bit_of = {}  # (blockName, varName) -> bit (0-indexed)
    for d in all_defs:
        def_bit_of[(d.block_name, d.var_name)] = d.id - 1

    # 打印定义信息
    print('\n' + '=' * 66)
    print('  定义信息')
    print('=' * 66)
    print(f'  IR 定义总数: {total_ir_defs}    纳入分析: {total_defs}')
    print(f'  分析变量:   ', end='')
    for var_name, def_ids in var_defs.items():
        ids_str = ', '.join(f'd{did}' for did in def_ids)
        print(f'{var_name}={{{ids_str}}}  ', end='')
    print()
    print('-' * 66)
    for d in all_defs:
        print(f'  d{d.id:<4} {d.var_name:<8} {d.block_name:<6} {d.inst_str}')

    # Step 2: 构建 gen[B] 和 kill[B]
    gen = {}
    kill = {}

    for bname in block_order:
        g = 0
        k = 0

        for d in all_defs:
            if d.block_name == bname:
                g |= (1 << (d.id - 1))

        for d in all_defs:
            if d.block_name != bname:
                for d2 in all_defs:
                    if d2.block_name == bname and d2.var_name == d.var_name:
                        k |= (1 << (d.id - 1))
                        break

        gen[bname] = g
        kill[bname] = k

    print(f'\n{"=" * 66}')
    print(f'  Gen / Kill 集合')
    print(f'{"=" * 66}')
    _print_state_row("Block", "Gen[B]", "Kill[B]")
    print('-' * 66)
    for bname in block_order:
        _print_state_row(bname, bitvec_to_str(gen[bname], total_defs), bitvec_to_str(kill[bname], total_defs))

    # Step 3: 初始化
    IN = {bname: 0 for bname in block_order}
    OUT = {bname: 0 for bname in block_order}

    # 初始快照
    snapshots = []  # [(round_num, [(in_str, out_str, changed), ...])]
    snapshots.append((0, [
        (bitvec_to_str(IN[b], total_defs), bitvec_to_str(OUT[b], total_defs), False)
        for b in block_order
    ]))

    # Step 4: Worklist 算法
    worklist = deque(block_order)  # 初始全入队
    processed_count = 0
    round_num = 0

    if verbose:
        print(f'\n{"=" * 66}')
        print(f'  Worklist 算法执行过程')
        print(f'{"=" * 66}')

    while worklist:
        round_num += 1
        # 本轮的 batch: 当前 worklist 中的所有块
        batch = deque(worklist)
        worklist.clear()
        seen_this_round = set()

        while batch:
            bname = batch.popleft()
            if bname in seen_this_round:
                continue
            seen_this_round.add(bname)
            processed_count += 1

            new_in = 0
            for pred in blocks[bname].predecessors:
                new_in |= OUT[pred]

            diff = new_in & ~kill[bname]
            new_out = gen[bname] | diff

            block_changed = (new_out != OUT[bname])

            if verbose:
                preds = blocks[bname].predecessors
                print(f'  [Round {round_num}, #{processed_count}] {bname}', end='')
                if block_changed:
                    print('  ** CHANGED **')
                else:
                    print('  (unchanged)')
                if preds:
                    print(f'    IN  = OR( ' + ' , '.join(f'OUT[{p}]' for p in preds) + ' )')
                    for p in preds:
                        print(f'        = OR( ..., {bitvec_to_str(OUT[p], total_defs)} )')
                else:
                    print(f'    IN  = OR( entry )')
                print(f'        = {bitvec_to_str(new_in, total_defs)}')
                print(f'    OUT = Gen U (IN - Kill) = {bitvec_to_str(new_out, total_defs)}')

            IN[bname] = new_in
            OUT[bname] = new_out

            if block_changed:
                for succ in blocks[bname].successors:
                    if succ not in seen_this_round:
                        worklist.append(succ)

        # 本轮结束，记录快照
        round_data = []
        for b in block_order:
            in_str = bitvec_to_str(IN[b], total_defs)
            out_str = bitvec_to_str(OUT[b], total_defs)
            # 检查与本轮开始时相比是否变化
            prev_in, prev_out, _ = snapshots[-1][1][block_order.index(b)]
            changed = (out_str != prev_out)
            round_data.append((in_str, out_str, changed))
        snapshots.append((round_num, round_data))

    if verbose:
        print(f'\n  Worklist 收敛: 共处理 {processed_count} 个块, {round_num} 轮')

    # Step 5: 打印对比表
    print(f'\n{"=" * 66}')
    print(f'  Worklist 状态对比 (处理 {processed_count} 块, {round_num} 轮后收敛)')
    print(f'{"=" * 66}')
    print()

    col_w = 28
    header = f'{"Block":<8}'
    for s in snapshots:
        header += f'{"Round " + str(s[0]):^{col_w}}'
    print(header)

    sep = f'{"":-<8}'
    for _ in snapshots:
        sep += f'{"":-^{col_w}}'
    print(sep)

    print('\n  [ IN[B] ]')
    for bi, bname in enumerate(block_order):
        row = f'{bname:<8}'
        for s in snapshots:
            row += f'{s[1][bi][0]:^{col_w}}'
        print(row)

    print()
    print('  [ OUT[B] ]')
    for bi, bname in enumerate(block_order):
        row = f'{bname:<8}'
        for s in snapshots:
            out_str = s[1][bi][1]
            if s[1][bi][2]:
                out_str = '*' + out_str
            row += f'{out_str:^{col_w}}'
        print(row)

    print()
    print('  ( * 表示本轮 OUT[B] 发生变化 )')
    print()

    # Step 6: 最终解析
    print(f'{"=" * 66}')
    print(f'  最终解析 (到达 B 入口的定义)')
    print(f'{"=" * 66}')
    for bname in block_order:
        in_val = IN[bname]
        if in_val == 0:
            items_str = '(no definitions reach)'
        else:
            items = [f'd{d.id}({d.var_name})' for d in all_defs if in_val & (1 << (d.id - 1))]
            items_str = ' '.join(items)
        print(f'  {bname:<8} {items_str}')

    # Step 7: Use-Def 链
    if show_ud:
        print(f'\n{"=" * 66}')
        print(f'  Use-Def 链 (每个 Use 可到达的定义)')
        print(f'{"=" * 66}')

        for bname in block_order:
            block = blocks[bname]
            current_reaching = IN[bname]  # B 入口处的到达定值

            for ii, inst in enumerate(block.instructions):
                uses_in_inst = []

                for op_str in inst.operands:
                    var_name = extract_var_name(op_str)
                    if not var_name or var_name not in interesting_vars:
                        continue

                    # 哪些定义的变量 == var_name 且仍在 current_reaching 中存活
                    reaching_defs = []
                    for d in all_defs:
                        if d.var_name == var_name and (current_reaching & (1 << (d.id - 1))):
                            reaching_defs.append(f'd{d.id}')

                    if reaching_defs:
                        uses_in_inst.append((op_str, var_name, reaching_defs))

                # 打印 use 信息
                if uses_in_inst:
                    for op_str, var_name, reaching_defs in uses_in_inst:
                        defs_str = ', '.join(reaching_defs)
                        print(f'  [{bname}:{ii}] use {op_str}({var_name}) <- {{{defs_str}}}')

                # 更新 current_reaching: 如果本条指令产生了定义，kill 同变量旧定义，加入新定义
                if inst.result:
                    def_var = extract_var_name(inst.result)
                    if def_var and def_var in interesting_vars:
                        for d in all_defs:
                            if d.var_name == def_var:
                                current_reaching &= ~(1 << (d.id - 1))
                        if (bname, def_var) in def_bit_of:
                            current_reaching |= (1 << def_bit_of[(bname, def_var)])


def _print_state_row(block_label, col1, col2):
    print(f'  {block_label:<8} {col1:<30} {col2:<30}')


# ======================== 帮助 ========================

def print_usage(prog):
    print(f'Usage: {prog} <input.ll> [options]')
    print()
    print('Options:')
    print('  -cfg       Output CFG in DOT format')
    print('  -reach     Run Reaching Definitions (worklist algorithm)')
    print('  -ud        Show Use-Def chains')
    print('  -v         Verbose computation steps')
    print('  -o <file>  Save output to file')
    print('  -h, --help Show this help')
    print()
    print('Example:')
    print(f'  {prog} example.ll -reach')
    print(f'  {prog} example.ll -reach -ud')
    print(f'  {prog} example.ll -reach -ud -v')
    print(f'  {prog} example.ll -reach -o result.txt')


# ======================== 主函数 ========================

def main():
    if len(sys.argv) < 2:
        print_usage(sys.argv[0])
        sys.exit(1)

    input_file = sys.argv[1]
    show_cfg = '-cfg' in sys.argv
    show_reach = '-reach' in sys.argv
    verbose = '-v' in sys.argv
    show_ud = '-ud' in sys.argv

    output_file = None
    try:
        o_idx = sys.argv.index('-o')
        if o_idx + 1 < len(sys.argv):
            output_file = sys.argv[o_idx + 1]
    except ValueError:
        pass

    if '-h' in sys.argv or '--help' in sys.argv:
        print_usage(sys.argv[0])
        sys.exit(0)

    blocks = parse_ir(input_file)
    if not blocks:
        print(f'Error: No basic blocks found in {input_file}', file=sys.stderr)
        sys.exit(1)

    print(f'Parsed {len(blocks)} basic blocks:', file=sys.stderr)
    for name, block in blocks.items():
        succ_str = ', '.join(block.successors)
        print(f'  {name} -> [{succ_str}]', file=sys.stderr)

    if show_cfg:
        output_dot(blocks, sys.stdout)

    if show_reach:
        if output_file:
            with open(output_file, 'w', encoding='utf-8') as f:
                old_stdout = sys.stdout
                sys.stdout = f
                try:
                    run_reaching_defs(blocks, verbose, show_ud)
                finally:
                    sys.stdout = old_stdout
            print(f'Output saved to {output_file}', file=sys.stderr)
        else:
            run_reaching_defs(blocks, verbose, show_ud)


if __name__ == '__main__':
    main()
