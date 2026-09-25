; REQUIRES: clang, host-executable, x86-registered-target
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=1313;functions=regex:^candidate_input_;hybrid=ir;hybrid-region=stateful;hybrid-layers=none;probability=100;stats=true" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba,verify -S "%s" -o "%t.ir.ll" 2>&1 | %FileCheck "%s" --check-prefix=STATS
; RUN: %clang -O0 "%t.ir.ll" -o "%t.ir.exe"
; RUN: "%t.ir.exe"
; RUN: %clang -O3 "%t.ir.ll" -o "%t.ir-o3.exe"
; RUN: "%t.ir-o3.exe"
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=1314;functions=regex:^candidate_input_;hybrid=native;hybrid-region=stateful;hybrid-layers=none;probability=100;stats=true" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba,verify -S "%s" -o "%t.native.ll" 2>&1 | %FileCheck "%s" --check-prefix=STATS
; RUN: %clang -O0 "%t.native.ll" -o "%t.native.exe"
; RUN: "%t.native.exe"
; RUN: %clang -O3 "%t.native.ll" -o "%t.native-o3.exe"
; RUN: "%t.native-o3.exe"

; STATS: stateful regions: 2

target triple = "x86_64-pc-windows-msvc"

@input = global i64 0

define i64 @reference_input_load(ptr %source, i64 %x, i64 %y) noinline {
entry:
  %loaded = load volatile i64, ptr %source
  %first = add i64 %loaded, %x
  %second = xor i64 %first, %y
  ret i64 %second
}

define i64 @candidate_input_load(ptr %source, i64 %x, i64 %y) noinline {
entry:
  %loaded = load volatile i64, ptr %source
  %first = add i64 %loaded, %x
  %second = xor i64 %first, %y
  ret i64 %second
}

define i64 @reference_input_phi(i1 %condition, i64 %x, i64 %y, i64 %z) noinline {
entry:
  br i1 %condition, label %left, label %right
left:
  br label %merge
right:
  br label %merge
merge:
  %chosen = phi i64 [ %x, %left ], [ %y, %right ]
  %first = sub i64 %chosen, %z
  %second = and i64 %first, %x
  ret i64 %second
}

define i64 @candidate_input_phi(i1 %condition, i64 %x, i64 %y, i64 %z) noinline {
entry:
  br i1 %condition, label %left, label %right
left:
  br label %merge
right:
  br label %merge
merge:
  %chosen = phi i64 [ %x, %left ], [ %y, %right ]
  %first = sub i64 %chosen, %z
  %second = and i64 %first, %x
  ret i64 %second
}

define i32 @main() {
entry:
  store i64 -1, ptr @input
  %load.reference = call i64 @reference_input_load(ptr @input, i64 -9223372036854775808, i64 81985529216486895)
  %load.candidate = call i64 @candidate_input_load(ptr @input, i64 -9223372036854775808, i64 81985529216486895)
  %load.bad = icmp ne i64 %load.reference, %load.candidate
  %phi0.reference = call i64 @reference_input_phi(i1 false, i64 -1, i64 9223372036854775807, i64 -81985529216486895)
  %phi0.candidate = call i64 @candidate_input_phi(i1 false, i64 -1, i64 9223372036854775807, i64 -81985529216486895)
  %phi0.bad = icmp ne i64 %phi0.reference, %phi0.candidate
  %phi1.reference = call i64 @reference_input_phi(i1 true, i64 -1, i64 9223372036854775807, i64 -81985529216486895)
  %phi1.candidate = call i64 @candidate_input_phi(i1 true, i64 -1, i64 9223372036854775807, i64 -81985529216486895)
  %phi1.bad = icmp ne i64 %phi1.reference, %phi1.candidate
  %bad01 = or i1 %load.bad, %phi0.bad
  %bad = or i1 %bad01, %phi1.bad
  %result = zext i1 %bad to i32
  ret i32 %result
}
