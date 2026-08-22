; RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=1;functions=all;transform=rule-explosion;probability=100;depth=1;stats=true" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s

target triple = "x86_64-unknown-linux-gnu"

define i32 @plugin_load(i32 %lhs, i32 %rhs) {
entry:
  %sum = add i32 %lhs, %rhs
  ret i32 %sum
}

; CHECK-LABEL: define i32 @plugin_load(i32 %lhs, i32 %rhs) {{.*}}!a2mba.protected
; CHECK: mul i32 %lhs, {{-?[0-9]+}}, !a2mba.generated
; CHECK: mul i32 %rhs, {{-?[0-9]+}}, !a2mba.generated
; CHECK: add i32
; CHECK: mul i32
; CHECK: !a2mba.processed = !{
