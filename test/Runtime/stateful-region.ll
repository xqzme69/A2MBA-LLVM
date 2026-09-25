; REQUIRES: clang, host-executable, x86-registered-target
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=905;functions=regex:^candidate_stateful.*$;hybrid=ir;hybrid-region=stateful;hybrid-layers=none;probability=100;depth=8;stats=true" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba,verify -S "%s" -o "%t.ir.ll" 2>&1 | %FileCheck "%s" --check-prefix=IR-STATS
; RUN: %clang -O0 "%t.ir.ll" -o "%t.ir.exe"
; RUN: "%t.ir.exe"
; RUN: %clang -O3 "%t.ir.ll" -o "%t.ir-o3.exe"
; RUN: "%t.ir-o3.exe"
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=906;functions=regex:^candidate_stateful.*$;hybrid=ir;hybrid-region=stateful;hybrid-layers=context-random;probability=100;depth=8;stats=true" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba,verify -S "%s" -o "%t.ir-layered.ll" 2>&1 | %FileCheck "%s" --check-prefix=IR-STATS
; RUN: %clang -O0 "%t.ir-layered.ll" -o "%t.ir-layered.exe"
; RUN: "%t.ir-layered.exe"
; RUN: %clang -O3 "%t.ir-layered.ll" -o "%t.ir-layered-o3.exe"
; RUN: "%t.ir-layered-o3.exe"
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=907;functions=regex:^candidate_stateful.*$;hybrid=native;hybrid-region=stateful;hybrid-layers=none;probability=100;depth=8;stats=true" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba,verify -S "%s" -o "%t.native.ll" 2>&1 | %FileCheck "%s" --check-prefix=NATIVE-STATS
; RUN: %clang -O0 "%t.native.ll" -o "%t.native.exe"
; RUN: "%t.native.exe"
; RUN: %clang -O3 "%t.native.ll" -o "%t.native-o3.exe"
; RUN: "%t.native-o3.exe"
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=908;functions=regex:^candidate_stateful.*$;hybrid=native;hybrid-region=stateful;hybrid-layers=context-random;probability=100;depth=8;stats=true" %a2mba_opt -mtriple=%a2mba_host_triple -passes=a2mba,verify -S "%s" -o "%t.native-layered.ll" 2>&1 | %FileCheck "%s" --check-prefix=NATIVE-STATS
; RUN: %clang -O0 "%t.native-layered.ll" -o "%t.native-layered.exe"
; RUN: "%t.native-layered.exe"
; RUN: %clang -O3 "%t.native-layered.ll" -o "%t.native-layered-o3.exe"
; RUN: "%t.native-layered-o3.exe"

; IR-STATS: hybrid IR: 18
; IR-STATS: hybrid native: 0
; IR-STATS: stateful regions: 5
; NATIVE-STATS: hybrid IR: 0
; NATIVE-STATS: hybrid native: 18
; NATIVE-STATS: stateful regions: 5

target triple = "x86_64-unknown-linux-gnu"

define i64 @reference_stateful(i64 %x, i64 %y, i64 %z) noinline {
entry:
  %first = add i64 %x, %y
  %second = xor i64 %first, %z
  %third = and i64 %second, -7
  %fourth = mul i64 %third, 3
  %fifth = or i64 %fourth, %y
  %sixth = sub i64 %x, %fifth
  ret i64 %sixth
}

define i64 @candidate_stateful(i64 %x, i64 %y, i64 %z) noinline {
entry:
  %first = add i64 %x, %y
  %second = xor i64 %first, %z
  %third = and i64 %second, -7
  %fourth = mul i64 %third, 3
  %fifth = or i64 %fourth, %y
  %sixth = sub i64 %x, %fifth
  ret i64 %sixth
}

define i32 @reference_stateful_i32(i32 %x, i32 %y, i32 %z) noinline {
entry:
  %first = add i32 %x, %y
  %second = xor i32 %first, %z
  %third = and i32 %second, -7
  %fourth = mul i32 %third, 3
  %fifth = or i32 %fourth, %y
  %sixth = sub i32 %x, %fifth
  ret i32 %sixth
}

define i32 @candidate_stateful_i32(i32 %x, i32 %y, i32 %z) noinline {
entry:
  %first = add i32 %x, %y
  %second = xor i32 %first, %z
  %third = and i32 %second, -7
  %fourth = mul i32 %third, 3
  %fifth = or i32 %fourth, %y
  %sixth = sub i32 %x, %fifth
  ret i32 %sixth
}

define i64 @candidate_stateful_fallback(i64 %x, i64 %y) noinline {
entry:
  %first = xor i64 %x, %x
  %second = sub i64 %first, %y
  ret i64 %second
}

define i64 @reference_stateful_control(i64 %x, i64 %y, i64 %z) noinline {
entry:
  %first = add i64 %x, %y
  %second = xor i64 %first, %z
  %condition = icmp ult i64 %second, %x
  br i1 %condition, label %left, label %right
left:
  %third = sub i64 %x, %z
  %fourth = and i64 %third, %y
  ret i64 %fourth
right:
  ret i64 %second
}

define i64 @candidate_stateful_control(i64 %x, i64 %y, i64 %z) noinline {
entry:
  %first = add i64 %x, %y
  %second = xor i64 %first, %z
  %condition = icmp ult i64 %second, %x
  br i1 %condition, label %left, label %right
left:
  %third = sub i64 %x, %z
  %fourth = and i64 %third, %y
  ret i64 %fourth
right:
  ret i64 %second
}

define i1 @check_stateful(i64 %x, i64 %y, i64 %z) {
entry:
  %reference = call i64 @reference_stateful(i64 %x, i64 %y, i64 %z)
  %candidate = call i64 @candidate_stateful(i64 %x, i64 %y, i64 %z)
  %bad64 = icmp ne i64 %reference, %candidate
  %x32 = trunc i64 %x to i32
  %y32 = trunc i64 %y to i32
  %z32 = trunc i64 %z to i32
  %reference32 = call i32 @reference_stateful_i32(i32 %x32, i32 %y32, i32 %z32)
  %candidate32 = call i32 @candidate_stateful_i32(i32 %x32, i32 %y32, i32 %z32)
  %bad32 = icmp ne i32 %reference32, %candidate32
  %expected.fallback = sub i64 0, %y
  %fallback = call i64 @candidate_stateful_fallback(i64 %x, i64 %y)
  %bad.fallback = icmp ne i64 %expected.fallback, %fallback
  %reference.control = call i64 @reference_stateful_control(i64 %x, i64 %y, i64 %z)
  %candidate.control = call i64 @candidate_stateful_control(i64 %x, i64 %y, i64 %z)
  %bad.control = icmp ne i64 %reference.control, %candidate.control
  %bad.widths = or i1 %bad64, %bad32
  %bad.other = or i1 %bad.fallback, %bad.control
  %bad = or i1 %bad.widths, %bad.other
  ret i1 %bad
}

define i32 @main() {
entry:
  %bad0 = call i1 @check_stateful(i64 0, i64 0, i64 0)
  %bad1 = call i1 @check_stateful(i64 9223372036854775807, i64 1, i64 -1)
  %bad2 = call i1 @check_stateful(i64 -81985529216486895, i64 1147797409030816545, i64 1085102592571150095)
  %bad3 = call i1 @check_stateful(i64 -9223372036854775808, i64 -1, i64 2147483647)
  %bad4 = call i1 @check_stateful(i64 2147483647, i64 1, i64 2147483648)
  %bad5 = call i1 @check_stateful(i64 -1, i64 -1, i64 -1)
  %bad01 = or i1 %bad0, %bad1
  %bad23 = or i1 %bad2, %bad3
  %bad45 = or i1 %bad4, %bad5
  %bad0123 = or i1 %bad01, %bad23
  %bad = or i1 %bad0123, %bad45
  br i1 %bad, label %fail, label %loop

loop:
  %index = phi i64 [ 0, %entry ], [ %next.index, %latch ]
  %state = phi i64 [ -7046029254386353131, %entry ], [ %next.state, %latch ]
  %shift.left = shl i64 %state, 13
  %mixed.left = xor i64 %state, %shift.left
  %shift.right = lshr i64 %mixed.left, 7
  %mixed.right = xor i64 %mixed.left, %shift.right
  %shift.final = shl i64 %mixed.right, 17
  %random = xor i64 %mixed.right, %shift.final
  %x = add i64 %random, %index
  %y = xor i64 %random, -3335678366873096957
  %z = mul i64 %random, -7046029254386353131
  %mismatch = call i1 @check_stateful(i64 %x, i64 %y, i64 %z)
  br i1 %mismatch, label %fail, label %latch

latch:
  %next.index = add i64 %index, 1
  %next.state = add i64 %random, 2685821657736338717
  %done = icmp eq i64 %next.index, 1024
  br i1 %done, label %pass, label %loop

pass:
  ret i32 0

fail:
  ret i32 1
}
