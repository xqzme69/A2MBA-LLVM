// REQUIRES: clang, a2mba-wrapper
// RUN: %clang -O0 -S -emit-llvm %s -o %t.ll
// RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=2;functions=annotated;transform=rule-explosion;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %t.ll -o - | %FileCheck %s
// RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=3;functions=annotated;transform=rule-explosion;probability=100;depth=1" %clang -O1 -fpass-plugin=%a2mba_plugin -S -emit-llvm %s -o - | %FileCheck %s --check-prefix=PLUGIN-O1
// RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=1;functions=annotated;transform=context-trap;probability=100;depth=1" %clang -O1 -fpass-plugin=%a2mba_plugin -S -emit-llvm %s -o - | %FileCheck %s --check-prefix=PLUGIN-AGT-O1
// RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=3;functions=annotated;transform=rule-explosion;probability=100;depth=1" %clang -O0 -fpass-plugin=%a2mba_plugin -S -emit-llvm %s -o - | %FileCheck %s --check-prefix=PLUGIN-O0
// RUN: %a2mba_wrapper --mode verified --level balanced --seed 1 --functions all -O3 -MMD -MF %t.d -c %s -o %t.obj
// RUN: %python -c "from pathlib import Path; assert Path(r'%t.obj').stat().st_size > 0"
// RUN: %FileCheck %s --check-prefix=DEPENDENCY --input-file=%t.d

__attribute__((annotate("a2mba")))
unsigned protected_add(unsigned lhs, unsigned rhs) {
  return lhs + rhs;
}

unsigned plain_add(unsigned lhs, unsigned rhs) {
  return lhs + rhs;
}

// DEPENDENCY: source-annotation.c

// CHECK-LABEL: define {{.*}}i32 @protected_add({{.*}}) {{.*}}!a2mba.protected
// CHECK: mul i32 {{.*}}, {{-?[0-9]+}}, !a2mba.generated
// CHECK: mul i32 {{.*}}, {{-?[0-9]+}}, !a2mba.generated
// CHECK: add i32 {{.*}}, {{.*}}, !a2mba.generated
// CHECK: mul i32 {{.*}}, {{-?[0-9]+}}, !a2mba.generated
// CHECK: ret i32
// CHECK-LABEL: define {{.*}}i32 @plain_add(
// CHECK-NOT: !a2mba.generated
// CHECK: add i32
// CHECK-NOT: !a2mba.generated
// CHECK: ret i32

// PLUGIN-O1-LABEL: define {{.*}}i32 @protected_add(
// PLUGIN-O1: mul i32 {{.*}}, {{-?[0-9]+}}, !a2mba.generated
// PLUGIN-O1: mul i32 {{.*}}, {{-?[0-9]+}}, !a2mba.generated
// PLUGIN-O1: add i32 {{.*}}, {{.*}}, !a2mba.generated
// PLUGIN-O1: mul i32 {{.*}}, {{-?[0-9]+}}, !a2mba.generated
// PLUGIN-O1: ret i32
// PLUGIN-O1: !a2mba.processed = !{

// PLUGIN-AGT-O1-LABEL: define {{.*}}i32 @protected_add(
// PLUGIN-AGT-O1: shl i32 {{.*}}, {{[1-8]}}, !a2mba.generated
// PLUGIN-AGT-O1: ashr i32 {{.*}}, {{[1-8]}}, !a2mba.generated
// PLUGIN-AGT-O1: shl i32 {{.*}}, {{[1-8]}}, !a2mba.generated
// PLUGIN-AGT-O1: ashr i32 {{.*}}, {{[1-8]}}, !a2mba.generated
// PLUGIN-AGT-O1: !a2mba.processed = !{

// PLUGIN-O0-LABEL: define {{.*}}i32 @protected_add(
// PLUGIN-O0-NOT: !a2mba.generated
// PLUGIN-O0: add i32
// PLUGIN-O0-NOT: !a2mba.generated
// PLUGIN-O0: ret i32
