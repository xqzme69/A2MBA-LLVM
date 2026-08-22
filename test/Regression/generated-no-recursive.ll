; RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=61;functions=all;transform=rule-explosion;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o %t.once.ll
; RUN: %FileCheck %s --check-prefix=BOTH --input-file=%t.once.ll
; RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=61;functions=all;transform=rule-explosion;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %t.once.ll -o %t.twice.ll
; RUN: %FileCheck %s --check-prefix=BOTH --input-file=%t.twice.ll

target triple = "x86_64-unknown-linux-gnu"

define i32 @transform_once(i32 %lhs, i32 %rhs) {
entry:
  %sum = add i32 %lhs, %rhs
  ret i32 %sum
}

; BOTH-LABEL: define i32 @transform_once(i32 %lhs, i32 %rhs) {{.*}}!a2mba.protected
; BOTH-NEXT: entry:
; BOTH-NEXT: {{%[^ ]+}} = mul i32 %lhs, {{-?[0-9]+}}, !a2mba.generated
; BOTH-NEXT: {{%[^ ]+}} = mul i32 %rhs, {{-?[0-9]+}}, !a2mba.generated
; BOTH-NEXT: {{%[^ ]+}} = add i32 {{%[^,]+}}, {{%[^,]+}}, !a2mba.generated
; BOTH-NEXT: [[SUM:%[^ ]+]] = mul i32 {{%[^,]+}}, {{-?[0-9]+}}, !a2mba.generated
; BOTH-NEXT: ret i32 [[SUM]]
; BOTH-NEXT: }
