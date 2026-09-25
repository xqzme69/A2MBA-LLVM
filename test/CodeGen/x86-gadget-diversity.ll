; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=7001;functions=all;transform=adc;probability=100;depth=1" %a2mba_opt -passes=a2mba -S "%s" -o "%t.adc.ll"
; RUN: %python "%S/../tools/check_architectural_diversity.py" "%t.adc.ll" --kind=adc --expected=16 --minimum-skeletons=8 --minimum-setups=4
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=7002;functions=all;transform=sbb;probability=100;depth=1" %a2mba_opt -passes=a2mba -S "%s" -o "%t.sbb.ll"
; RUN: %python "%S/../tools/check_architectural_diversity.py" "%t.sbb.ll" --kind=sbb --expected=16 --minimum-skeletons=8 --minimum-setups=4

target triple = "x86_64-pc-windows-msvc"

define i64 @gadget00(i64 %lhs, i64 %rhs) {
  %v = add i64 %lhs, %rhs
  ret i64 %v
}
define i64 @gadget01(i64 %lhs, i64 %rhs) {
  %v = add i64 %lhs, %rhs
  ret i64 %v
}
define i64 @gadget02(i64 %lhs, i64 %rhs) {
  %v = add i64 %lhs, %rhs
  ret i64 %v
}
define i64 @gadget03(i64 %lhs, i64 %rhs) {
  %v = add i64 %lhs, %rhs
  ret i64 %v
}
define i64 @gadget04(i64 %lhs, i64 %rhs) {
  %v = add i64 %lhs, %rhs
  ret i64 %v
}
define i64 @gadget05(i64 %lhs, i64 %rhs) {
  %v = add i64 %lhs, %rhs
  ret i64 %v
}
define i64 @gadget06(i64 %lhs, i64 %rhs) {
  %v = add i64 %lhs, %rhs
  ret i64 %v
}
define i64 @gadget07(i64 %lhs, i64 %rhs) {
  %v = add i64 %lhs, %rhs
  ret i64 %v
}
define i64 @gadget08(i64 %lhs, i64 %rhs) {
  %v = add i64 %lhs, %rhs
  ret i64 %v
}
define i64 @gadget09(i64 %lhs, i64 %rhs) {
  %v = add i64 %lhs, %rhs
  ret i64 %v
}
define i64 @gadget10(i64 %lhs, i64 %rhs) {
  %v = add i64 %lhs, %rhs
  ret i64 %v
}
define i64 @gadget11(i64 %lhs, i64 %rhs) {
  %v = add i64 %lhs, %rhs
  ret i64 %v
}
define i64 @gadget12(i64 %lhs, i64 %rhs) {
  %v = add i64 %lhs, %rhs
  ret i64 %v
}
define i64 @gadget13(i64 %lhs, i64 %rhs) {
  %v = add i64 %lhs, %rhs
  ret i64 %v
}
define i64 @gadget14(i64 %lhs, i64 %rhs) {
  %v = add i64 %lhs, %rhs
  ret i64 %v
}
define i64 @gadget15(i64 %lhs, i64 %rhs) {
  %v = add i64 %lhs, %rhs
  ret i64 %v
}
