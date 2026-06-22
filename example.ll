; ============================================================================
; Reaching Definitions Analysis 示例 LLVM IR
; 
; 基于 Dragon Book 到达定值经典示例
; 
; 变量命名规则: %vN 表示变量 v 的第 N 个定义 (dN)
;   %i1, %i2, %i3  -> 变量 i (被 d1, d4, d7 定值)
;   %j1, %j2       -> 变量 j (被 d2, d5 定值)
;   %a1, %a2       -> 变量 a (被 d3, d6 定值)
;
; CFG 结构:
;   B1 -> B2 -> B3 -> B4 -> B2 (循环)
;          B2 -> exit (循环出口)
;
;   即: B2 有两个前驱: B1 和 B4 (通过循环)
;       B4 有两个前驱: B2 和 B3
;       B3 有一个前驱: B2
;
; ============================================================================

define void @reaching_defs_example(i32 %m, i32 %n, i32 %u1, i32 %u2, i32 %u3) {
B1:
  %i1 = sub i32 %m, 1       ; d1: i = m-1
  %j1 = mov %n             ; d2: j = n
  %a1 = mov %u1             ; d3: a = u1
  br label %B2

B2:
  %i2 = add i32 %i1, 1      ; d4: i = i+1 (use i1)
  %j2 = sub i32 %j1, 1      ; d5: j = j-1 (use j1)
  %t1 = add i32 %i2, %j2
  %cond = icmp slt i32 %t1, 10
  br i1 %cond, label %B3, label %exit

B3:
  %a2 = mov %u2             ; d6: a = u2
  %t2 = add i32 %i2, %a2
  br label %B4

B4:
  %i3 = mov %u3             ; d7: i = u3
  %t3 = add i32 %i3, %j2
  br label %B2

exit:
  ret void
}
