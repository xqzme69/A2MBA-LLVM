; RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=11;functions=all;transform=rule-explosion;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s

target triple = "x86_64-unknown-linux-gnu"

define i32 @add_i32(i32 %lhs, i32 %rhs) {
entry:
  %sum = add i32 %lhs, %rhs
  ret i32 %sum
}

; CHECK-LABEL: define i32 @add_i32(i32 %lhs, i32 %rhs) {{.*}}!a2mba.protected
; CHECK-NEXT: entry:
; CHECK-NEXT: [[LHS_SCALED:%[^ ]+]] = mul i32 %lhs, [[SCALE:-?[0-9]+]], !a2mba.generated ![[GENERATED:[0-9]+]]
; CHECK-NEXT: [[RHS_SCALED:%[^ ]+]] = mul i32 %rhs, [[SCALE]], !a2mba.generated ![[GENERATED]]
; CHECK-NEXT: [[SCALED_SUM:%[^ ]+]] = add i32 [[LHS_SCALED]], [[RHS_SCALED]], !a2mba.generated ![[GENERATED]]
; CHECK-NEXT: [[SUM:%[^ ]+]] = mul i32 [[SCALED_SUM]], [[INVERSE:-?[0-9]+]], !a2mba.generated ![[GENERATED]]
; CHECK-NEXT: ret i32 [[SUM]]
; CHECK-NEXT: }
; CHECK: !a2mba.processed = !{
