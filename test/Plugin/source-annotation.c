// REQUIRES: clang, a2mba-wrapper
// RUN: %clang -O0 -S -emit-llvm "%s" -o "%t.ll"
// RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=2;functions=annotated;transform=rule-explosion;probability=100;depth=1" %a2mba_opt -passes=a2mba -S "%t.ll" -o - | %FileCheck "%s"
// RUN: %a2mba_wrapper --mode verified --level balanced --seed 1 --functions all -O3 -MMD -MF "%t.d" -c "%s" -o "%t.obj"
// RUN: %python -c "from pathlib import Path; assert Path(r'%t.obj').stat().st_size > 0"
// RUN: %FileCheck "%s" --check-prefix=DEPENDENCY --input-file="%t.d"

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
