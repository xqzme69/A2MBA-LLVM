; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=9001;functions=all;hybrid=ir;probability=100;depth=12" %a2mba_opt -passes=a2mba -S "%s" -o "%t.hybrid.pre.ll"
; RUN: %opt "-passes=default<O3>" -S "%t.hybrid.pre.ll" -o "%t.hybrid.ll"
; RUN: %opt -passes=verify -disable-output "%t.hybrid.ll"
; RUN: %python "%S/../tools/check_nonlinear_ir.py" "%t.hybrid.ll" --function=hardening_pair --minimum=8
; RUN: %python "%S/../tools/check_nonlinear_ir.py" "%t.hybrid.ll" --function=hardening_constant --minimum=8
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=9002;functions=all;transform=context-trap;probability=100;depth=1" %a2mba_opt -passes=a2mba -S "%s" -o "%t.context.pre.ll"
; RUN: %opt "-passes=default<O3>" -S "%t.context.pre.ll" -o "%t.context.ll"
; RUN: %opt -passes=verify -disable-output "%t.context.ll"
; RUN: %python "%S/../tools/check_nonlinear_ir.py" "%t.context.ll" --function=hardening_pair --minimum=8
; RUN: %python "%S/../tools/check_nonlinear_ir.py" "%t.context.ll" --function=hardening_constant --minimum=8

target triple = "x86_64-unknown-linux-gnu"

define i64 @hardening_pair(i64 %lhs, i64 %rhs) {
entry:
  %value = xor i64 %lhs, %rhs
  ret i64 %value
}

define i64 @hardening_constant(i64 %value) {
entry:
  %mixed = add i64 %value, 81985529216486895
  ret i64 %mixed
}
