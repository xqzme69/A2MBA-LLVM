; REQUIRES: clang, host-executable, x86-registered-target
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=917;functions=regex:^candidate_.*$;transform=context-trap;probability=100;depth=1" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba,verify -S "%s" -o "%t.classic.ll"
; RUN: %clang -O0 "%t.classic.ll" -o "%t.classic.exe"
; RUN: "%t.classic.exe"
; RUN: %clang -O3 "%t.classic.ll" -o "%t.classic-o3.exe"
; RUN: "%t.classic-o3.exe"
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=917;functions=regex:^candidate_.*$;hybrid=ir;hybrid-layers=none;probability=100" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba,verify -S "%s" -o "%t.operation.ll"
; RUN: %opt "-passes=default<O3>,verify" -S "%t.operation.ll" -o "%t.operation-o3.ll"
; RUN: %FileCheck "%s" --check-prefix=RESULT --input-file="%t.operation-o3.ll"
; RUN: %clang -O0 "%t.operation.ll" -o "%t.operation.exe"
; RUN: "%t.operation.exe"
; RUN: %clang -O3 "%t.operation.ll" -o "%t.operation-o3.exe"
; RUN: "%t.operation-o3.exe"
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=917;functions=regex:^candidate_.*$;hybrid=native;hybrid-layers=context-random;probability=100" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba,verify -S "%s" -o "%t.operation-native.ll"
; RUN: %clang -O0 "%t.operation-native.ll" -o "%t.operation-native.exe"
; RUN: "%t.operation-native.exe"
; RUN: %clang -O3 "%t.operation-native.ll" -o "%t.operation-native-o3.exe"
; RUN: "%t.operation-native-o3.exe"
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=917;functions=regex:^candidate_.*$;hybrid=ir;hybrid-region=stateful;hybrid-layers=none;probability=100;stats=true" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba,verify -S "%s" -o "%t.ir.ll" 2>&1 | %FileCheck "%s" --check-prefix=STATS
; RUN: %opt "-passes=default<O3>,verify" -S "%t.ir.ll" -o "%t.ir-o3.ll"
; RUN: %FileCheck "%s" --check-prefix=RESULT --input-file="%t.ir-o3.ll"
; RUN: %clang -O0 "%t.ir.ll" -o "%t.ir.exe"
; RUN: "%t.ir.exe"
; RUN: %clang -O3 "%t.ir.ll" -o "%t.ir-o3.exe"
; RUN: "%t.ir-o3.exe"
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=917;functions=regex:^candidate_.*$;hybrid=ir;hybrid-region=stateful;hybrid-layers=context-random;probability=100;stats=true" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba,verify -S "%s" -o "%t.ir-layered.ll" 2>&1 | %FileCheck "%s" --check-prefix=STATS
; RUN: %clang -O0 "%t.ir-layered.ll" -o "%t.ir-layered.exe"
; RUN: "%t.ir-layered.exe"
; RUN: %clang -O3 "%t.ir-layered.ll" -o "%t.ir-layered-o3.exe"
; RUN: "%t.ir-layered-o3.exe"
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=917;functions=regex:^candidate_.*$;hybrid=native;hybrid-region=stateful;hybrid-layers=none;probability=100;stats=true" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba,verify -S "%s" -o "%t.native.ll" 2>&1 | %FileCheck "%s" --check-prefix=STATS
; RUN: %clang -O0 "%t.native.ll" -o "%t.native.exe"
; RUN: "%t.native.exe"
; RUN: %clang -O3 "%t.native.ll" -o "%t.native-o3.exe"
; RUN: "%t.native-o3.exe"
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=917;functions=regex:^candidate_.*$;hybrid=native;hybrid-region=stateful;hybrid-layers=context-random;probability=100;stats=true" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba,verify -S "%s" -o "%t.native-layered.ll" 2>&1 | %FileCheck "%s" --check-prefix=STATS
; RUN: %clang -O0 "%t.native-layered.ll" -o "%t.native-layered.exe"
; RUN: "%t.native-layered.exe"
; RUN: %clang -O3 "%t.native-layered.ll" -o "%t.native-layered-o3.exe"
; RUN: "%t.native-layered-o3.exe"

; STATS: stateful regions: 6

target triple = "x86_64-pc-windows-msvc"

define i32 @candidate_masked32() noinline {
entry:
  %masked = and i32 undef, 0
  %result = add i32 %masked, 7
  ret i32 %result
}

; RESULT-LABEL: define{{.*}} i32 @candidate_masked32()
; RESULT: ret i32 7

define i64 @candidate_masked64() noinline {
entry:
  %masked = and i64 undef, 0
  %result = add i64 %masked, 7
  ret i64 %result
}

; RESULT-LABEL: define{{.*}} i64 @candidate_masked64()
; RESULT: ret i64 7

define i32 @candidate_parity32() noinline {
entry:
  %doubled = mul i32 undef, 2
  %result = and i32 %doubled, 1
  ret i32 %result
}

; RESULT-LABEL: define{{.*}} i32 @candidate_parity32()
; RESULT: ret i32 0

define i64 @candidate_parity64() noinline {
entry:
  %doubled = mul i64 undef, 2
  %result = and i64 %doubled, 1
  ret i64 %result
}

; RESULT-LABEL: define{{.*}} i64 @candidate_parity64()
; RESULT: ret i64 0

define i32 @candidate_argument(i32 %x) noinline {
entry:
  %masked = and i32 %x, 0
  %result = add i32 %masked, 7
  ret i32 %result
}

define i64 @candidate_derived() noinline {
entry:
  %derived = lshr i64 undef, 1
  %masked = and i64 %derived, 0
  %result = add i64 %masked, 7
  ret i64 %result
}

; RESULT-LABEL: define{{.*}} i64 @candidate_derived()
; RESULT: ret i64 7

define i32 @main() {
entry:
  %masked32 = call i32 @candidate_masked32()
  %masked64 = call i64 @candidate_masked64()
  %parity32 = call i32 @candidate_parity32()
  %parity64 = call i64 @candidate_parity64()
  %argument = call i32 @candidate_argument(i32 undef)
  %derived = call i64 @candidate_derived()
  %bad32 = icmp ne i32 %masked32, 7
  %bad64 = icmp ne i64 %masked64, 7
  %odd32 = icmp ne i32 %parity32, 0
  %odd64 = icmp ne i64 %parity64, 0
  %bad_argument = icmp ne i32 %argument, 7
  %bad_derived = icmp ne i64 %derived, 7
  %bad_mask = or i1 %bad32, %bad64
  %bad_parity = or i1 %odd32, %odd64
  %bad_input = or i1 %bad_argument, %bad_derived
  %bad_direct = or i1 %bad_mask, %bad_parity
  %bad = or i1 %bad_direct, %bad_input
  %status = zext i1 %bad to i32
  ret i32 %status
}
