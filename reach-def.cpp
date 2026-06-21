// reach-def.cpp -- Reaching Definitions Analyzer
// Worklist 算法 + Use-Def 链
// 编译: g++ -std=c++11 -O2 -o reach-def reach-def.cpp
// 交叉编译: x86_64-w64-mingw32-g++ -std=c++11 -O2 -static -o reach-def.exe reach-def.cpp

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <iomanip>
#include <algorithm>

using namespace std;

struct Instruction {
    string raw, result, opcode;
    vector<string> operands;
    string trueTarget, falseTarget;
    bool isBranch = false, isCondBranch = false;
};

struct BasicBlock {
    string name;
    vector<Instruction> instructions;
    vector<string> successors;
    vector<string> predecessors;
};

struct Def {
    int id, instIdx;
    string varName, blockName, instStr;
};

typedef unsigned long long BitVec;

string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

bool startsWith(const string& s, const string& p) { return s.compare(0, p.size(), p) == 0; }
bool contains(const string& s, const string& sub) { return s.find(sub) != string::npos; }

string extractVarName(const string& reg) {
    if (reg.empty() || reg[0] != '%') return "";
    string n = reg.substr(1);
    while (!n.empty() && isdigit(n.back())) n.pop_back();
    return n;
}

string bitvecToStr(BitVec bv, int total) {
    if (bv == 0) return "{}";
    string r = "{";
    bool first = true;
    for (int i = 0; i < total; i++) {
        if (bv & (1ULL << i)) {
            if (!first) r += ", ";
            r += "d" + to_string(i + 1);
            first = false;
        }
    }
    return r + "}";
}

map<string, BasicBlock> parseIR(const string& fn) {
    map<string, BasicBlock> blocks;
    ifstream f(fn);
    if (!f.is_open()) { cerr << "Error: Cannot open " << fn << endl; return blocks; }
    string line;
    BasicBlock* cur = nullptr;
    while (getline(f, line)) {
        string t = trim(line);
        if (t.empty() || t[0] == ';') continue;
        if (startsWith(t, "define ") || startsWith(t, "attributes ") ||
            startsWith(t, "target ") || startsWith(t, "source_filename") ||
            startsWith(t, "ModuleID")) continue;
        if (t.back() == ':' && !contains(t, "=") && !contains(t, "%")) {
            string nm = t.substr(0, t.size() - 1);
            blocks[nm] = BasicBlock();
            blocks[nm].name = nm;
            cur = &blocks[nm];
            continue;
        }
        if (!cur) continue;
        Instruction inst; inst.raw = t;
        if (startsWith(t, "br ")) {
            inst.isBranch = true; inst.opcode = "br";
            size_t p = 0;
            while (p < t.size()) {
                size_t s = t.find("label %", p);
                if (s == string::npos) break; s += 7;
                size_t e = t.find_first_of(",; \t)", s);
                if (e == string::npos) e = t.size();
                string tg = t.substr(s, e - s);
                if (inst.trueTarget.empty()) inst.trueTarget = tg;
                else { inst.falseTarget = tg; inst.isCondBranch = true; }
                cur->successors.push_back(tg);
                p = e;
            }
            cur->instructions.push_back(inst);
            continue;
        }
        if (startsWith(t, "ret ")) { inst.opcode = "ret"; inst.isBranch = true; cur->instructions.push_back(inst); continue; }
        size_t eq = t.find('=');
        if (eq != string::npos) {
            inst.result = trim(t.substr(0, eq));
            string right = trim(t.substr(eq + 1));
            istringstream iss(right);
            iss >> inst.opcode;
            string tok;
            while (iss >> tok) {
                if (tok == ",") continue;
                if (!tok.empty() && tok.back() == ',') tok.pop_back();
                if (!tok.empty() && (tok[0] == '%' || isdigit(tok[0]) || tok[0] == '-'))
                    inst.operands.push_back(tok);
            }
        }
        cur->instructions.push_back(inst);
    }
    for (auto& p : blocks)
        for (auto& s : p.second.successors)
            if (blocks.count(s)) blocks[s].predecessors.push_back(p.first);
    return blocks;
}

map<pair<string,int>, string> getDefLabels(const map<string, BasicBlock>& blks) {
    vector<string> order;
    for (auto& p : blks) order.push_back(p.first);

    // rawId, varName, blockName, instIdx
    vector<tuple<int,string,string,int>> allRaw;
    map<string, vector<int>> varDefs;

    for (auto& bname : order) {
        auto& blk = blks.at(bname);
        for (size_t ii = 0; ii < blk.instructions.size(); ii++) {
            auto& inst = blk.instructions[ii];
            if (inst.result.empty()) continue;
            string vn = extractVarName(inst.result);
            if (vn.empty()) continue;
            int rawId = (int)allRaw.size() + 1;
            allRaw.push_back(make_tuple(rawId, vn, bname, (int)ii));
            varDefs[vn].push_back(rawId);
        }
    }

    set<string> interesting;
    for (auto& vd : varDefs) {
        set<string> dbs;
        for (int did : vd.second)
            dbs.insert(get<2>(allRaw[did - 1]));  // blockName at index 2
        if (dbs.size() > 1 && vd.first != "t" && vd.first != "cond")
            interesting.insert(vd.first);
    }

    map<pair<string,int>, string> labels;
    int newId = 0;
    for (auto& t : allRaw) {
        string vn = get<1>(t);
        if (interesting.count(vn)) {
            newId++;
            labels[make_pair(get<2>(t), get<3>(t))] = "d" + to_string(newId);
        }
    }
    return labels;
}

void outputDOT(const map<string, BasicBlock>& blks, ostream& out) {
    auto defLabels = getDefLabels(blks);

    out << "digraph CFG {" << endl;
    out << "  rankdir=TD;\n  node [shape=record, fontname=\"Consolas\", fontsize=11];\n  edge [fontname=\"Consolas\", fontsize=9];\n\n";
    for (auto& p : blks) {
        out << "  \"" << p.first << "\" [label=\"{";
        out << p.first << " | ";
        for (size_t i = 0; i < p.second.instructions.size(); i++) {
            auto& inst = p.second.instructions[i];
            if (i) out << "\\l";
            auto key = make_pair(p.first, (int)i);
            if (defLabels.count(key)) out << defLabels.at(key) << ": ";
            if (inst.isBranch) out << inst.raw;
            else if (!inst.result.empty()) {
                out << inst.result << " = " << inst.opcode;
                for (auto& op : inst.operands) out << " " << op;
            }
        }
        out << "}\"];" << endl;
    }
    out << endl;
    for (auto& p : blks)
        for (auto& s : p.second.successors)
            out << "  \"" << p.first << "\" -> \"" << s << "\";" << endl;
    out << "}" << endl;
}

void printDefInfo(const vector<Def>& defs, const map<string, vector<int>>& vdefs, int totalIR) {
    cout << endl << string(66, '=') << endl << "  定义信息" << endl << string(66, '=') << endl;
    cout << "  IR 定义总数: " << totalIR << "    纳入分析: " << defs.size() << endl;
    cout << "  分析变量:   ";
    for (auto& vd : vdefs) {
        cout << vd.first << "={";
        for (size_t i = 0; i < vd.second.size(); i++) {
            if (i) cout << ", ";
            cout << "d" << vd.second[i];
        }
        cout << "}  ";
    }
    cout << endl << string(66, '-') << endl;
    for (auto& d : defs) {
        cout << "  d" << d.id << string(4 - to_string(d.id).size(), ' ')
             << d.varName << string(9 - d.varName.size(), ' ')
             << d.blockName << string(7 - d.blockName.size(), ' ')
             << d.instStr << endl;
    }
}

void printGenKill(const vector<string>& order, const map<string, BitVec>& gen,
                  const map<string, BitVec>& kill, int total) {
    cout << endl << string(66, '=') << endl << "  Gen / Kill 集合" << endl << string(66, '=') << endl;
    cout << "  " << left << setw(8) << "Block" << setw(30) << "Gen[B]" << setw(30) << "Kill[B]" << endl;
    cout << string(68, '-') << endl;
    for (auto& b : order)
        cout << "  " << left << setw(8) << b << setw(30) << bitvecToStr(gen.at(b), total)
             << setw(30) << bitvecToStr(kill.at(b), total) << endl;
}

void printVerboseStep(const string& bname, const vector<string>& preds, BitVec in, BitVec out, BitVec diff,
                      const map<string, BitVec>& OUT, const map<string, BitVec>& gen, const map<string, BitVec>& kill,
                      int total, bool chg) {
    cout << endl << "  [" << bname << "]" << endl;
    if (!preds.empty()) {
        cout << "    IN  = OR( ";
        for (size_t i = 0; i < preds.size(); i++) { if (i) cout << " , "; cout << "OUT[" << preds[i] << "]"; }
        cout << " )" << endl;
        for (auto& p : preds) cout << "        = OR( ..., " << bitvecToStr(OUT.at(p), total) << " )" << endl;
    } else cout << "    IN  = OR( entry )" << endl;
    cout << "        = " << bitvecToStr(in, total) << endl;
    cout << "    OUT = Gen U (IN - Kill)" << endl;
    cout << "        = " << bitvecToStr(gen.at(bname), total) << " U ("
         << bitvecToStr(in, total) << " - " << bitvecToStr(kill.at(bname), total) << ")" << endl;
    cout << "        = " << bitvecToStr(gen.at(bname), total) << " U " << bitvecToStr(diff, total) << endl;
    cout << "        = " << bitvecToStr(out, total);
    if (chg) cout << "  ** CHANGED **";
    cout << endl;
}

struct Snap { int round; vector<string> ins, outs; vector<bool> chgs; };

void printCmpTable(const vector<Snap>& snaps, const vector<string>& order) {
    cout << endl << string(66, '=') << endl;
    cout << "  Worklist 状态对比 (处理 " << (snaps.size() - 1) << " 轮后收敛)" << endl;
    cout << string(66, '=') << endl << endl;
    int cw = 28;
    cout << left << setw(8) << "Block";
    for (auto& s : snaps) {
        string h = "Round " + to_string(s.round);
        int pl = (cw - (int)h.size()) / 2;
        cout << string(pl, ' ') << h << string(cw - (int)h.size() - pl, ' ');
    }
    cout << endl;
    cout << string(8, '-'); for (size_t i = 0; i < snaps.size(); i++) cout << string(cw, '-'); cout << endl;
    cout << endl << "  [ IN[B] ]" << endl;
    for (size_t bi = 0; bi < order.size(); bi++) {
        cout << left << setw(8) << order[bi];
        for (auto& s : snaps) {
            string v = s.ins[bi];
            int pl = (cw - (int)v.size()) / 2;
            cout << string(pl, ' ') << v << string(cw - (int)v.size() - pl, ' ');
        }
        cout << endl;
    }
    cout << endl << "  [ OUT[B] ]" << endl;
    for (size_t bi = 0; bi < order.size(); bi++) {
        cout << left << setw(8) << order[bi];
        for (auto& s : snaps) {
            string v = s.outs[bi];
            if (s.chgs[bi]) v = "*" + v;
            int pl = (cw - (int)v.size()) / 2;
            cout << string(pl, ' ') << v << string(cw - (int)v.size() - pl, ' ');
        }
        cout << endl;
    }
    cout << endl << "  ( * 表示本轮 OUT[B] 发生变化 )" << endl << endl;
}

void printFinal(const vector<string>& order, const map<string, BitVec>& IN,
                const vector<Def>& defs, int total) {
    cout << string(66, '=') << endl << "  最终解析 (到达 B 入口的定义)" << endl << string(66, '=') << endl;
    for (auto& b : order) {
        BitVec v = IN.at(b);
        cout << "  " << left << setw(8) << b;
        if (v == 0) cout << "(no definitions reach)";
        else for (auto& d : defs) if (v & (1ULL << (d.id - 1))) cout << "d" << d.id << "(" << d.varName << ") ";
        cout << endl;
    }
}

void printUseDef(const vector<string>& order, const map<string, BitVec>& IN,
                 const vector<Def>& defs, const map<string, BasicBlock>& blocks,
                 const set<string>& interesting, const map<pair<string,string>, int>& defBitOf) {
    cout << endl << string(66, '=') << endl << "  Use-Def 链 (每个 Use 可到达的定义)" << endl << string(66, '=') << endl;
    for (auto& bname : order) {
        BitVec cr = IN.at(bname);
        auto& blk = blocks.at(bname);
        for (size_t ii = 0; ii < blk.instructions.size(); ii++) {
            auto& inst = blk.instructions[ii];
            for (auto& op : inst.operands) {
                string vn = extractVarName(op);
                if (vn.empty() || !interesting.count(vn)) continue;
                vector<int> rd;
                for (auto& d : defs)
                    if (d.varName == vn && (cr & (1ULL << (d.id - 1))))
                        rd.push_back(d.id);
                if (!rd.empty()) {
                    cout << "  [" << bname << ":" << ii << "] use " << op << "(" << vn << ") <- {";
                    for (size_t j = 0; j < rd.size(); j++) { if (j) cout << ", "; cout << "d" << rd[j]; }
                    cout << "}" << endl;
                }
            }
            if (!inst.result.empty()) {
                string dv = extractVarName(inst.result);
                if (!dv.empty() && interesting.count(dv)) {
                    for (auto& d : defs) if (d.varName == dv) cr &= ~(1ULL << (d.id - 1));
                    auto key = make_pair(bname, dv);
                    if (defBitOf.count(key)) cr |= (1ULL << defBitOf.at(key));
                }
            }
        }
    }
}

void runReachingDefs(const map<string, BasicBlock>& blocks, bool verbose, bool showUD) {
    vector<string> order;
    for (auto& p : blocks) order.push_back(p.first);

    vector<Def> allDefs;
    map<string, vector<int>> varDefs;
    for (auto& bname : order) {
        auto& blk = blocks.at(bname);
        for (size_t ii = 0; ii < blk.instructions.size(); ii++) {
            auto& inst = blk.instructions[ii];
            if (inst.result.empty()) continue;
            string vn = extractVarName(inst.result);
            if (vn.empty()) continue;
            string istr = inst.result + " = " + inst.opcode;
            for (auto& op : inst.operands) istr += " " + op;
            Def d; d.id = (int)allDefs.size() + 1; d.varName = vn; d.blockName = bname;
            d.instStr = istr; d.instIdx = (int)ii;
            allDefs.push_back(d);
            varDefs[vn].push_back(d.id);
        }
    }
    int totalIR = (int)allDefs.size();

    set<string> interesting;
    for (auto& vd : varDefs) {
        set<string> dbs;
        for (int did : vd.second) dbs.insert(allDefs[did - 1].blockName);
        if (dbs.size() > 1 && vd.first != "t" && vd.first != "cond")
            interesting.insert(vd.first);
    }

    vector<Def> filt;
    for (auto& d : allDefs) if (interesting.count(d.varName)) filt.push_back(d);
    allDefs = filt; varDefs.clear();
    for (int i = 0; i < (int)allDefs.size(); i++) {
        allDefs[i].id = i + 1;
        varDefs[allDefs[i].varName].push_back(i + 1);
    }
    int total = (int)allDefs.size();

    map<pair<string,string>, int> defBitOf;
    for (auto& d : allDefs) defBitOf[make_pair(d.blockName, d.varName)] = d.id - 1;

    printDefInfo(allDefs, varDefs, totalIR);

    map<string, BitVec> gen, kill;
    for (auto& bname : order) {
        BitVec g = 0, k = 0;
        for (auto& d : allDefs) if (d.blockName == bname) g |= (1ULL << (d.id - 1));
        for (auto& d : allDefs)
            if (d.blockName != bname)
                for (auto& d2 : allDefs)
                    if (d2.blockName == bname && d2.varName == d.varName) { k |= (1ULL << (d.id - 1)); break; }
        gen[bname] = g; kill[bname] = k;
    }
    printGenKill(order, gen, kill, total);

    map<string, BitVec> IN, OUT;
    for (auto& b : order) IN[b] = OUT[b] = 0;

    vector<Snap> snaps;
    {
        Snap s0; s0.round = 0;
        for (auto& b : order) {
            s0.ins.push_back(bitvecToStr(IN[b], total));
            s0.outs.push_back(bitvecToStr(OUT[b], total));
            s0.chgs.push_back(false);
        }
        snaps.push_back(s0);
    }

    deque<string> worklist(order.begin(), order.end());
    int round = 0, processed = 0;

    if (verbose) {
        cout << endl << string(66, '=') << endl << "  Worklist 算法执行过程" << endl << string(66, '=') << endl;
    }

    while (!worklist.empty()) {
        round++;
        deque<string> batch(worklist.begin(), worklist.end());
        worklist.clear();
        set<string> seen;

        while (!batch.empty()) {
            string bname = batch.front(); batch.pop_front();
            if (seen.count(bname)) continue;
            seen.insert(bname);
            processed++;

            BitVec newIN = 0;
            for (auto& p : blocks.at(bname).predecessors) newIN |= OUT[p];

            BitVec diff = newIN & ~kill[bname];
            BitVec newOUT = gen[bname] | diff;
            bool chg = (newOUT != OUT[bname]);

            if (verbose) {
                cout << "  [Round " << round << ", #" << processed << "] " << bname;
                cout << (chg ? "  ** CHANGED **" : "  (unchanged)") << endl;
                auto& preds = blocks.at(bname).predecessors;
                if (!preds.empty()) {
                    cout << "    IN  = OR( ";
                    for (size_t i = 0; i < preds.size(); i++) { if (i) cout << " , "; cout << "OUT[" << preds[i] << "]"; }
                    cout << " )" << endl;
                    for (auto& p : preds) cout << "        = OR( ..., " << bitvecToStr(OUT[p], total) << " )" << endl;
                } else cout << "    IN  = OR( entry )" << endl;
                cout << "        = " << bitvecToStr(newIN, total) << endl;
                cout << "    OUT = Gen U (IN - Kill) = " << bitvecToStr(newOUT, total) << endl;
            }

            IN[bname] = newIN; OUT[bname] = newOUT;

            if (chg)
                for (auto& s : blocks.at(bname).successors)
                    if (!seen.count(s)) worklist.push_back(s);
        }

        Snap sn; sn.round = round;
        for (auto& b : order) {
            string is = bitvecToStr(IN[b], total), os = bitvecToStr(OUT[b], total);
            bool chg = (os != snaps.back().outs[sn.ins.size()]);
            sn.ins.push_back(is); sn.outs.push_back(os); sn.chgs.push_back(chg);
        }
        snaps.push_back(sn);
    }

    if (verbose) cout << endl << "  Worklist 收敛: 共处理 " << processed << " 个块, " << round << " 轮" << endl;

    printCmpTable(snaps, order);
    printFinal(order, IN, allDefs, total);

    if (showUD)
        printUseDef(order, IN, allDefs, blocks, interesting, defBitOf);
}

void printUsage(const char* p) {
    cout << "Usage: " << p << " <input.ll> [options]\n\nOptions:\n"
         << "  -cfg       Output CFG in DOT format\n"
         << "  -reach     Run Reaching Definitions (worklist algorithm)\n"
         << "  -ud        Show Use-Def chains\n"
         << "  -v         Verbose computation steps\n"
         << "  -o <file>  Save output to file\n"
         << "  -h, --help Show this help\n\nExample:\n"
         << "  " << p << " example.ll -reach\n"
         << "  " << p << " example.ll -reach -ud -v\n"
         << "  " << p << " example.ll -reach -o result.txt\n"
         << "  " << p << " example.ll -cfg > cfg.dot\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) { printUsage(argv[0]); return 1; }

    string inputFile = argv[1], outputFile;
    bool showCFG = false, showReach = false, verbose = false, showUD = false;

    for (int i = 2; i < argc; i++) {
        string a = argv[i];
        if (a == "-cfg") showCFG = true;
        else if (a == "-reach") showReach = true;
        else if (a == "-v") verbose = true;
        else if (a == "-ud") showUD = true;
        else if (a == "-o" && i + 1 < argc) outputFile = argv[++i];
        else if (a == "-h" || a == "--help") { printUsage(argv[0]); return 0; }
    }

    auto blocks = parseIR(inputFile);
    if (blocks.empty()) { cerr << "Error: No basic blocks found" << endl; return 1; }

    cerr << "Parsed " << blocks.size() << " basic blocks:" << endl;
    for (auto& p : blocks) {
        cerr << "  " << p.first << " -> [";
        for (size_t i = 0; i < p.second.successors.size(); i++) {
            if (i) cerr << ", "; cerr << p.second.successors[i];
        }
        cerr << "]" << endl;
    }

    if (showCFG) outputDOT(blocks, cout);

    if (showReach) {
        if (!outputFile.empty()) {
            ofstream fout(outputFile);
            if (!fout.is_open()) { cerr << "Error: Cannot open " << outputFile << endl; return 1; }
            streambuf* ob = cout.rdbuf(); cout.rdbuf(fout.rdbuf());
            runReachingDefs(blocks, verbose, showUD);
            cout.rdbuf(ob);
            cerr << "Output saved to " << outputFile << endl;
        } else {
            runReachingDefs(blocks, verbose, showUD);
        }
    }
    return 0;
}
