; REQUIRES: x86-registered-target
; RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=44;functions=all;transform=sbb;probability=100;depth=1" %a2mba_opt -passes=a2mba %s -o - | %llc -mtriple=x86_64-pc-windows-msvc -O0 -o - | %FileCheck %s

target triple = "x86_64-pc-windows-msvc"

define i32 @sbb_i32(i32 %lhs, i32 %rhs) {
entry:
  %sum = add i32 %lhs, %rhs
  ret i32 %sum
}

define i64 @sbb_i64(i64 %lhs, i64 %rhs) {
entry:
  %sum = add i64 %lhs, %rhs
  ret i64 %sum
}

; CHECK-LABEL: sbb_i32:
; CHECK: pushfq
; CHECK: stc
; CHECK: sbbl
; CHECK-COUNT-2: addl
; CHECK: popfq
; CHECK-LABEL: sbb_i64:
; CHECK: pushfq
; CHECK: stc
; CHECK: sbbq
; CHECK-COUNT-2: addq
; CHECK: popfq
