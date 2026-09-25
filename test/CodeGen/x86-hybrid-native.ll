; REQUIRES: x86-registered-target
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=55;functions=all;hybrid=native;transform=auto;probability=100;depth=8;stats=true" %a2mba_opt -passes=a2mba -disable-output "%s" 2>&1 | %FileCheck "%s" --check-prefix=STATS
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=55;functions=all;hybrid=native;transform=auto;probability=100;depth=8" %a2mba_opt -passes=a2mba -S "%s" -o - | %FileCheck "%s" --check-prefix=IR
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=55;functions=all;hybrid=native;transform=auto;probability=100;depth=8" %a2mba_opt -passes=a2mba "%s" -o - | %llc -mtriple=x86_64-unknown-linux-gnu -O0 -o - | %FileCheck "%s" --check-prefix=ASM

target triple = "x86_64-unknown-linux-gnu"

define i64 @hybrid_native(i64 %lhs, i64 %rhs) {
entry:
  %sum = add i64 %lhs, %rhs
  ret i64 %sum
}

; STATS: hybrid IR: 0
; STATS: hybrid native: 1
; IR-LABEL: define i64 @hybrid_native(i64 %lhs, i64 %rhs) {{.*}}!a2mba.protected
; IR: {{%[^ ]+}} = freeze i64 %lhs, !a2mba.generated
; IR-NEXT: {{%[^ ]+}} = freeze i64 %rhs, !a2mba.generated
; IR-NOT: freeze i64
; IR: call {{.*}} asm
; IR-SAME: ~{flags}
; IR-NOT: %sum = add i64 %lhs, %rhs

; ASM-LABEL: hybrid_native:
; ASM: {{(mov|add|sub|imul|and|or|xor|not|neg)}}q
