; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=917;functions=all;hybrid=ir;hybrid-region=stateful;hybrid-layers=none;probability=100" %a2mba_opt -passes=a2mba,verify -S "%s" -o - | %FileCheck "%s"
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=917;functions=all;hybrid=native;hybrid-region=stateful;hybrid-layers=context-random;probability=100" %a2mba_opt -passes=a2mba,verify -S "%s" -o - | %FileCheck "%s"

target triple = "x86_64-pc-windows-msvc"

define i64 @defined_inputs(i64 noundef %x, i64 noundef %y) {
entry:
  %result = xor i64 %x, %y
  ret i64 %result
}

; CHECK-LABEL: define i64 @defined_inputs
; CHECK-NOT: freeze
; CHECK: ret i64

define i64 @already_frozen(i64 %x, i64 noundef %y) {
entry:
  %stable = freeze i64 %x
  %result = xor i64 %stable, %y
  ret i64 %result
}

; CHECK-LABEL: define i64 @already_frozen
; CHECK: %stable = freeze i64 %x
; CHECK-NOT: freeze
; CHECK: ret i64

define i64 @derived_input(i64 %x, i64 %y) {
entry:
  %derived = lshr i64 %x, 1
  %result = xor i64 %derived, %y
  ret i64 %result
}

; CHECK-LABEL: define i64 @derived_input
; CHECK: {{%[^ ]+}} = freeze i64 %derived, !a2mba.generated
; CHECK-NEXT: {{%[^ ]+}} = freeze i64 %y, !a2mba.generated
; CHECK-NOT: freeze
; CHECK: ret i64

define i64 @shared_operand(i64 %x) {
entry:
  %result = add i64 %x, %x
  ret i64 %result
}

; CHECK-LABEL: define i64 @shared_operand
; CHECK: {{%[^ ]+}} = freeze i64 %x, !a2mba.generated
; CHECK-NOT: freeze
; CHECK: ret i64

define i32 @poison_input(i32 %x) {
entry:
  %mixed = xor i32 poison, %x
  %result = add i32 %mixed, 3
  ret i32 %result
}

; CHECK-LABEL: define i32 @poison_input
; CHECK: {{%[^ ]+}} = freeze i32 poison, !a2mba.generated
; CHECK-NEXT: {{%[^ ]+}} = freeze i32 %x, !a2mba.generated
; CHECK-NOT: freeze
; CHECK: ret i32

define i64 @defined_region(i64 noundef %x, i64 noundef %y) {
entry:
  %mixed = xor i64 %x, %y
  %result = add i64 %mixed, 17
  ret i64 %result
}

; CHECK-LABEL: define i64 @defined_region
; CHECK-NOT: freeze
; CHECK: a2mba.region.state.initial
; CHECK-NOT: freeze
; CHECK: a2mba.region.decode.exit.scale
; CHECK-NOT: freeze
; CHECK: ret i64
