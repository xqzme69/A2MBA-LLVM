// REQUIRES: clang, a2mba-plugin
// RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=3;functions=annotated;transform=rule-explosion;probability=100;depth=1" %clang -O1 -fpass-plugin=%a2mba_plugin -S -emit-llvm "%s" -o - | %FileCheck "%s" --check-prefix=O1
// RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=1;functions=annotated;transform=context-trap;probability=100;depth=1" %clang -O1 -fpass-plugin=%a2mba_plugin -S -emit-llvm "%s" -o - | %FileCheck "%s" --check-prefix=AGT
// RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=3;functions=annotated;transform=rule-explosion;probability=100;depth=1" %clang -O0 -fpass-plugin=%a2mba_plugin -S -emit-llvm "%s" -o - | %FileCheck "%s" --check-prefix=O0

__attribute__((annotate("a2mba")))
unsigned protected_add(unsigned lhs, unsigned rhs) {
  return lhs + rhs;
}

// O1-LABEL: define {{.*}}i32 @protected_add(
// O1: mul i32 {{.*}}, {{-?[0-9]+}}, !a2mba.generated
// O1: mul i32 {{.*}}, {{-?[0-9]+}}, !a2mba.generated
// O1: add i32 {{.*}}, {{.*}}, !a2mba.generated
// O1: mul i32 {{.*}}, {{-?[0-9]+}}, !a2mba.generated
// O1: ret i32
// O1: !a2mba.processed = !{

// AGT-LABEL: define {{.*}}i32 @protected_add(
// AGT: shl i32 {{.*}}, {{[1-8]}}, !a2mba.generated
// AGT: ashr i32 {{.*}}, {{[1-8]}}, !a2mba.generated
// AGT: shl i32 {{.*}}, {{[1-8]}}, !a2mba.generated
// AGT: ashr i32 {{.*}}, {{[1-8]}}, !a2mba.generated
// AGT: !a2mba.processed = !{

// O0-LABEL: define {{.*}}i32 @protected_add(
// O0-NOT: !a2mba.generated
// O0: add i32
// O0-NOT: !a2mba.generated
// O0: ret i32
