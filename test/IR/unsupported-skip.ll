; RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=22;functions=all;transform=rule-explosion;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s

target triple = "x86_64-unknown-linux-gnu"

define i16 @skip_i16(i16 %lhs, i16 %rhs) {
entry:
  %sum = add i16 %lhs, %rhs
  ret i16 %sum
}

define i128 @skip_i128(i128 %lhs, i128 %rhs) {
entry:
  %sum = add i128 %lhs, %rhs
  ret i128 %sum
}

define <4 x i32> @skip_vector(<4 x i32> %lhs, <4 x i32> %rhs) {
entry:
  %sum = add <4 x i32> %lhs, %rhs
  ret <4 x i32> %sum
}

define i32 @skip_boolean_rule(i32 %lhs, i32 %rhs) {
entry:
  %masked = and i32 %lhs, %rhs
  ret i32 %masked
}

define i32 @skip_unsupported_opcode(i32 %lhs, i32 %rhs) {
entry:
  %quotient = udiv i32 %lhs, %rhs
  ret i32 %quotient
}

; CHECK-LABEL: define i16 @skip_i16(i16 %lhs, i16 %rhs) {
; CHECK-NEXT: entry:
; CHECK-NEXT: %sum = add i16 %lhs, %rhs
; CHECK-NEXT: ret i16 %sum
; CHECK-NEXT: }
; CHECK-LABEL: define i128 @skip_i128(i128 %lhs, i128 %rhs) {
; CHECK-NEXT: entry:
; CHECK-NEXT: %sum = add i128 %lhs, %rhs
; CHECK-NEXT: ret i128 %sum
; CHECK-NEXT: }
; CHECK-LABEL: define <4 x i32> @skip_vector(<4 x i32> %lhs, <4 x i32> %rhs) {
; CHECK-NEXT: entry:
; CHECK-NEXT: %sum = add <4 x i32> %lhs, %rhs
; CHECK-NEXT: ret <4 x i32> %sum
; CHECK-NEXT: }
; CHECK-LABEL: define i32 @skip_boolean_rule(i32 %lhs, i32 %rhs) {
; CHECK-NEXT: entry:
; CHECK-NEXT: %masked = and i32 %lhs, %rhs
; CHECK-NEXT: ret i32 %masked
; CHECK-NEXT: }
; CHECK-LABEL: define i32 @skip_unsupported_opcode(i32 %lhs, i32 %rhs) {
; CHECK-NEXT: entry:
; CHECK-NEXT: %quotient = udiv i32 %lhs, %rhs
; CHECK-NEXT: ret i32 %quotient
; CHECK-NEXT: }
