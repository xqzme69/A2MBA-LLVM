; RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=21;functions=all;transform=rule-explosion;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s

target triple = "x86_64-unknown-linux-gnu"

define i32 @skip_nsw(i32 %lhs, i32 %rhs) {
entry:
  %sum = add nsw i32 %lhs, %rhs
  ret i32 %sum
}

define i64 @skip_nuw(i64 %lhs, i64 %rhs) {
entry:
  %sum = add nuw i64 %lhs, %rhs
  ret i64 %sum
}

define i32 @skip_both(i32 %lhs, i32 %rhs) {
entry:
  %sum = add nuw nsw i32 %lhs, %rhs
  ret i32 %sum
}

; CHECK-LABEL: define i32 @skip_nsw(i32 %lhs, i32 %rhs) {
; CHECK-NEXT: entry:
; CHECK-NEXT: %sum = add nsw i32 %lhs, %rhs
; CHECK-NEXT: ret i32 %sum
; CHECK-NEXT: }
; CHECK-LABEL: define i64 @skip_nuw(i64 %lhs, i64 %rhs) {
; CHECK-NEXT: entry:
; CHECK-NEXT: %sum = add nuw i64 %lhs, %rhs
; CHECK-NEXT: ret i64 %sum
; CHECK-NEXT: }
; CHECK-LABEL: define i32 @skip_both(i32 %lhs, i32 %rhs) {
; CHECK-NEXT: entry:
; CHECK-NEXT: %sum = add nuw nsw i32 %lhs, %rhs
; CHECK-NEXT: ret i32 %sum
; CHECK-NEXT: }
