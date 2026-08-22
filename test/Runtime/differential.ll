; REQUIRES: clang, host-executable
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=51;functions=regex:^candidate_.*$;transform=rule-explosion;probability=100;depth=1" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba -S %s -o %t.rule.ll
; RUN: %clang -O0 %t.rule.ll -o %t.rule.exe
; RUN: %t.rule.exe
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=52;functions=regex:^candidate_.*$;transform=adc;probability=100;depth=1" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba -S %s -o %t.adc.ll
; RUN: %clang -O0 %t.adc.ll -o %t.adc.exe
; RUN: %t.adc.exe
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=53;functions=regex:^candidate_.*$;transform=sbb;probability=100;depth=1" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba -S %s -o %t.sbb.ll
; RUN: %clang -O0 %t.sbb.ll -o %t.sbb.exe
; RUN: %t.sbb.exe
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=1;functions=regex:^candidate_.*$;transform=context-trap;probability=100;depth=1" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba -S %s -o %t.agt0.ll
; RUN: %clang -O0 %t.agt0.ll -o %t.agt0.exe
; RUN: %t.agt0.exe
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=2;functions=regex:^candidate_.*$;transform=context-trap;probability=100;depth=1" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba -S %s -o %t.agt1.ll
; RUN: %clang -O0 %t.agt1.ll -o %t.agt1.exe
; RUN: %t.agt1.exe
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=5;functions=regex:^candidate_.*$;transform=context-trap;probability=100;depth=1" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba -S %s -o %t.agt2.ll
; RUN: %clang -O0 %t.agt2.ll -o %t.agt2.exe
; RUN: %t.agt2.exe
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=8;functions=regex:^candidate_.*$;transform=context-trap;probability=100;depth=1" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba -S %s -o %t.agt3.ll
; RUN: %clang -O0 %t.agt3.ll -o %t.agt3.exe
; RUN: %t.agt3.exe
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=12;functions=regex:^candidate_.*$;transform=context-trap;probability=100;depth=1" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba -S %s -o %t.agt4.ll
; RUN: %clang -O0 %t.agt4.ll -o %t.agt4.exe
; RUN: %t.agt4.exe
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=16;functions=regex:^candidate_.*$;transform=context-trap;probability=100;depth=1" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba -S %s -o %t.agt5.ll
; RUN: %clang -O0 %t.agt5.ll -o %t.agt5.exe
; RUN: %t.agt5.exe

target triple = "x86_64-unknown-linux-gnu"

define i32 @reference_i32(i32 %lhs, i32 %rhs) {
entry:
  %sum = add i32 %lhs, %rhs
  ret i32 %sum
}

define i32 @candidate_i32(i32 %lhs, i32 %rhs) {
entry:
  %sum = add i32 %lhs, %rhs
  ret i32 %sum
}

define i64 @reference_i64(i64 %lhs, i64 %rhs) {
entry:
  %sum = add i64 %lhs, %rhs
  ret i64 %sum
}

define i64 @candidate_i64(i64 %lhs, i64 %rhs) {
entry:
  %sum = add i64 %lhs, %rhs
  ret i64 %sum
}

define i32 @main() {
entry:
  %reference_zero = call i32 @reference_i32(i32 0, i32 0)
  %candidate_zero = call i32 @candidate_i32(i32 0, i32 0)
  %bad_zero = icmp ne i32 %reference_zero, %candidate_zero

  %reference_wrap32 = call i32 @reference_i32(i32 2147483647, i32 1)
  %candidate_wrap32 = call i32 @candidate_i32(i32 2147483647, i32 1)
  %bad_wrap32 = icmp ne i32 %reference_wrap32, %candidate_wrap32

  %reference_mixed32 = call i32 @reference_i32(i32 -19088744, i32 1985229328)
  %candidate_mixed32 = call i32 @candidate_i32(i32 -19088744, i32 1985229328)
  %bad_mixed32 = icmp ne i32 %reference_mixed32, %candidate_mixed32

  %reference_wrap64 = call i64 @reference_i64(i64 9223372036854775807, i64 1)
  %candidate_wrap64 = call i64 @candidate_i64(i64 9223372036854775807, i64 1)
  %bad_wrap64 = icmp ne i64 %reference_wrap64, %candidate_wrap64

  %reference_mixed64 = call i64 @reference_i64(i64 -81985529216486895, i64 1147797409030816545)
  %candidate_mixed64 = call i64 @candidate_i64(i64 -81985529216486895, i64 1147797409030816545)
  %bad_mixed64 = icmp ne i64 %reference_mixed64, %candidate_mixed64

  %bad0 = or i1 %bad_zero, %bad_wrap32
  %bad1 = or i1 %bad_mixed32, %bad_wrap64
  %bad2 = or i1 %bad0, %bad1
  %bad = or i1 %bad2, %bad_mixed64
  %exit_code = zext i1 %bad to i32
  ret i32 %exit_code
}
