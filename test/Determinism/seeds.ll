; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=71;functions=all;transform=rule-explosion;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o %t.same-a.ll
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=71;functions=all;transform=rule-explosion;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o %t.same-b.ll
; RUN: diff %t.same-a.ll %t.same-b.ll
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=72;functions=all;transform=rule-explosion;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o %t.different.ll
; RUN: not diff %t.same-a.ll %t.different.ll

target triple = "x86_64-unknown-linux-gnu"

define i32 @seeded_i32(i32 %lhs, i32 %rhs) {
entry:
  %sum0 = add i32 %lhs, %rhs
  %sum1 = add i32 %sum0, %lhs
  %sum2 = add i32 %sum1, %rhs
  ret i32 %sum2
}

define i64 @seeded_i64(i64 %lhs, i64 %rhs) {
entry:
  %sum0 = add i64 %lhs, %rhs
  %sum1 = add i64 %sum0, %lhs
  %sum2 = add i64 %sum1, %rhs
  ret i64 %sum2
}
