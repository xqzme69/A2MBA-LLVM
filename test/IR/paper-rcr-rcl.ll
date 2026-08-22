; RUN: env A2MBA_OPTIONS="mode=paper;level=medium;seed=32;functions=all;transform=paper-rcr-rcl;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s

target triple = "x86_64-unknown-linux-gnu"

define i32 @paper_i32(i32 %lhs, i32 %rhs) {
entry:
  %sum = add i32 %lhs, %rhs
  ret i32 %sum
}

define i64 @paper_i64(i64 %lhs, i64 %rhs) {
entry:
  %sum = add i64 %lhs, %rhs
  ret i64 %sum
}

; CHECK-LABEL: define i32 @paper_i32(i32 %lhs, i32 %rhs) {{.*}}!a2mba.protected
; CHECK: [[SUM32:%[^ ]+]] = add i32 %lhs, %rhs, !a2mba.generated ![[GENERATED:[0-9]+]]
; CHECK-NEXT: [[IDENTITY32:%[^ ]+]] = call i32 asm sideeffect
; CHECK-SAME: pushfq
; CHECK-SAME: stc
; CHECK-SAME: rcll
; CHECK-SAME: rcrl
; CHECK-SAME: popfq
; CHECK-SAME: "=&r,0,~{memory},~{flags}"(i32 [[SUM32]])
; CHECK-SAME: !a2mba.generated ![[GENERATED]]
; CHECK-NEXT: ret i32 [[IDENTITY32]]
; CHECK-LABEL: define i64 @paper_i64(i64 %lhs, i64 %rhs) {{.*}}!a2mba.protected
; CHECK: [[SUM64:%[^ ]+]] = add i64 %lhs, %rhs, !a2mba.generated ![[GENERATED]]
; CHECK-NEXT: [[IDENTITY64:%[^ ]+]] = call i64 asm sideeffect
; CHECK-SAME: pushfq
; CHECK-SAME: stc
; CHECK-SAME: rclq
; CHECK-SAME: rcrq
; CHECK-SAME: popfq
; CHECK-SAME: "=&r,0,~{memory},~{flags}"(i64 [[SUM64]])
; CHECK-SAME: !a2mba.generated ![[GENERATED]]
; CHECK-NEXT: ret i64 [[IDENTITY64]]
