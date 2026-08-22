; RUN: env A2MBA_OPTIONS="mode=verified;level=balanced;seed=12;functions=all;transform=rule-explosion;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s

target triple = "x86_64-unknown-linux-gnu"

define i64 @add_i64(i64 %lhs, i64 %rhs) {
entry:
  %sum = add i64 %lhs, %rhs
  ret i64 %sum
}

; CHECK-LABEL: define i64 @add_i64(i64 %lhs, i64 %rhs) {{.*}}!a2mba.protected
; CHECK-NEXT: entry:
; CHECK-NEXT: [[LHS_SCALED:%[^ ]+]] = mul i64 %lhs, [[SCALE:-?[0-9]+]], !a2mba.generated ![[GENERATED:[0-9]+]]
; CHECK-NEXT: [[RHS_SCALED:%[^ ]+]] = mul i64 %rhs, [[SCALE]], !a2mba.generated ![[GENERATED]]
; CHECK-NEXT: [[SCALED_SUM:%[^ ]+]] = add i64 [[LHS_SCALED]], [[RHS_SCALED]], !a2mba.generated ![[GENERATED]]
; CHECK-NEXT: [[SUM:%[^ ]+]] = mul i64 [[SCALED_SUM]], [[INVERSE:-?[0-9]+]], !a2mba.generated ![[GENERATED]]
; CHECK-NEXT: ret i64 [[SUM]]
; CHECK-NEXT: }
; CHECK: !a2mba.processed = !{
