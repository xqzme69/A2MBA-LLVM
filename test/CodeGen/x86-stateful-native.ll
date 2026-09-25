; REQUIRES: x86-registered-target
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=904;functions=regex:^stateful_native_chain$;hybrid=native;hybrid-region=stateful;hybrid-layers=none;probability=100;depth=8;stats=true" %a2mba_opt -passes=a2mba -disable-output "%s" 2>&1 | %FileCheck "%s" --check-prefix=STATS
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=904;functions=regex:^stateful_native_chain$;hybrid=native;hybrid-region=stateful;hybrid-layers=none;probability=100;depth=8" %a2mba_opt -passes=a2mba -S "%s" -o - | %FileCheck "%s" --check-prefix=IR
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=904;functions=regex:^stateful_native_chain$;hybrid=native;hybrid-region=stateful;hybrid-layers=none;probability=100;depth=8" %a2mba_opt -passes=a2mba "%s" -o - | %llc -mtriple=x86_64-unknown-linux-gnu -O0 -o - | %FileCheck "%s" --check-prefix=ASM

target triple = "x86_64-unknown-linux-gnu"

define i64 @stateful_native_chain(i64 %x, i64 %y, i64 %z) {
entry:
  %first = add i64 %x, %y
  %second = xor i64 %first, %z
  %third = add i64 %second, 17
  ret i64 %third
}

; STATS: hybrid IR: 0
; STATS: hybrid native: 3
; STATS: stateful regions: 1

; IR-LABEL: define i64 @stateful_native_chain
; IR: call {{.*}} asm
; IR: a2mba.region.decode.exit.scale = mul i64
; IR-NOT: %first = add i64 %x, %y
; IR-NOT: %second = xor i64 %first, %z
; IR-NOT: %third = add i64 %second, 17

; ASM-LABEL: stateful_native_chain:
; ASM: {{(mov|add|sub|imul|and|or|xor|not|neg)}}q
