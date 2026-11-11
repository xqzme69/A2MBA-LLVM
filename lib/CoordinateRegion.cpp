#include "CoordinateRegion.h"

#include "a2mba/Context.h"
#include "a2mba/Eligibility.h"
#include "a2mba/Metadata.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/ValueHandle.h"

#include <utility>

namespace a2mba {
namespace {

#include "CoordinateRules.inc"

using llvm::BinaryOperator;
using llvm::Instruction;
using llvm::Value;

// Keep the graph in SSA form. A shared child counts once, even if both arms use it.
struct Region {
  llvm::SmallVector<BinaryOperator *, 12> nodes;
  llvm::SmallPtrSet<Value *, 16> members;
  llvm::SmallPtrSet<Value *, 8> inputs;
  bool hasAdd = false;

  bool collect(Value *v, llvm::BasicBlock *block) {
    auto *op = llvm::dyn_cast<BinaryOperator>(v);
    if (!op || op->getParent() != block || !checkCandidate(*op) ||
        (op->getOpcode() != Instruction::Add && op->getOpcode() != Instruction::Xor &&
         op->getOpcode() != Instruction::And && op->getOpcode() != Instruction::Or)) {
      if (!llvm::isa<llvm::ConstantInt>(v))
        inputs.insert(v);
      return inputs.size() <= 8;
    }
    if (members.contains(v))
      return true;
    members.insert(v);
    if (members.size() > 12)
      return false;
    for (Value *input : op->operands())
      if (!collect(input, block))
        return false;
    nodes.push_back(op);
    hasAdd |= op->getOpcode() == Instruction::Add;
    return true;
  }

  bool closed() const {
    if (nodes.size() < 2 || inputs.size() < 2 || inputs.size() > 8 || !hasAdd)
      return false;
    for (BinaryOperator *op : llvm::ArrayRef(nodes).drop_back())
      for (auto *user : op->users())
        if (!members.contains(user))
          return false;
    return true;
  }

  void rebuildInputs() {
    inputs.clear();
    hasAdd = false;
    for (auto *op : nodes) {
      hasAdd |= op->getOpcode() == Instruction::Add;
      for (Value *v : op->operands())
        if (!members.contains(v) && !llvm::isa<llvm::ConstantInt>(v))
          inputs.insert(v);
    }
  }

  // Leave the first layer in plain SSA if the rest still forms a closed region.
  void trimLeaves() {
    Region core;
    for (auto *op : nodes) {
      if (!members.contains(op->getOperand(0)) && !members.contains(op->getOperand(1)))
        continue;
      core.nodes.push_back(op);
      core.members.insert(op);
    }
    core.rebuildInputs();
    if (core.closed())
      *this = std::move(core);
  }
};

struct Emitter {
  llvm::IRBuilder<llvm::ConstantFolder, llvm::IRBuilderCallbackInserter> builder;
  const Region &region;
  const unsigned width;
  const unsigned rotation;
  llvm::DenseMap<Value *, Value *> words;

  Emitter(const Region &region, uint64_t seed)
      : builder(region.nodes.back()->getContext(), llvm::ConstantFolder(),
                llvm::IRBuilderCallbackInserter([](Instruction *inst) { markGenerated(*inst); })),
        region(region), width(region.nodes.back()->getType()->getIntegerBitWidth()),
        rotation(1 + static_cast<unsigned>(seed % (width - 1))) {
    builder.SetInsertPoint(region.nodes.back());
    builder.SetCurrentDebugLocation(region.nodes.back()->getDebugLoc());
  }

  Value *rotate(Value *v, unsigned amount) {
    auto *lo = builder.CreateLShr(v, width - amount);
    auto *hi = builder.CreateShl(v, amount);
    return builder.CreateOr(hi, lo, "a2mba.coord.rot");
  }

  Value *word(Value *v) {
    if (auto it = words.find(v); it != words.end())
      return it->second;
    assert(!region.members.contains(v) && "region operand was not emitted");
    Value *input = v;
    if (!llvm::isGuaranteedNotToBeUndefOrPoison(v))
      input = builder.CreateFreeze(v, "a2mba.coord.input");
    auto *bits = rotate(input, rotation);
    words.insert({v, bits});
    return bits;
  }

  llvm::Expected<Value *> run(HybridComposition *composition) {
    BinaryOperator *root = region.nodes.back();
    BinaryOperator *cut = region.nodes.front();
    for (auto *node : llvm::ArrayRef(region.nodes).drop_back())
      if (!node->hasOneUse()) {
        cut = node;
        break;
      }

    std::optional<ContextTrapParameters> trap;
    if (composition) {
      auto &layers = composition->layers;
      if (layers.context)
        trap = std::exchange(layers.context, std::nullopt);
      else if (!layers.contextCuts.empty()) {
        // The layer planner picks one site. Apply it to the memoized coordinate word.
        assert(layers.contextCuts.size() == 1);
        trap = layers.contextCuts.front().parameters;
        layers.contextCuts.clear();
      }
    }
    for (auto *op : region.nodes) {
      Value *a = word(op->getOperand(0));
      Value *b = word(op->getOperand(1));
      Value *bits;
      if (op == root && composition) {
        auto emitted =
            hybrid::emitWithOperands(builder, *op, composition->base, *a, *b, composition->layers);
        if (!emitted)
          return emitted.takeError();
        bits = *emitted;
      } else {
        bits = builder.CreateBinOp(op->getOpcode(), a, b, "a2mba.coord.bits");
      }
      if (op->getOpcode() == Instruction::Add)
        bits = packedRotatedAdd(builder, a, b, bits, builder.getIntN(width, rotation),
                                builder.getIntN(width, width - rotation),
                                builder.getIntN(width, (uint64_t{1} << rotation) - 1));
      if (op == cut && trap)
        bits = applyContextTrap(builder, *bits, *trap);
      words[op] = bits;
    }
    return rotate(words.lookup(root), width - rotation);
  }
};

} // namespace

llvm::Expected<bool> transformCoordinateRegions(llvm::Function &function, A2MBAContext &context) {
  bool changed = false;
  for (auto &block : function) {
    llvm::SmallVector<llvm::WeakVH, 32> roots;
    for (auto &inst : llvm::reverse(block)) {
      const EligibilityResult candidate = checkCandidate(inst);
      ++context.statistics.instructionsVisited;
      if (candidate) {
        ++context.statistics.candidates;
        roots.emplace_back(&inst);
      } else {
        context.statistics.recordSkip(candidate.reason);
      }
    }

    for (const auto &handle : roots) {
      auto *root = llvm::dyn_cast_or_null<BinaryOperator>(static_cast<Value *>(handle));
      if (!root)
        continue;
      Region region;
      if (!region.collect(root, &block) || !region.closed())
        continue;
      region.trimLeaves();
      auto selected = context.random.chance(context.config.profile().candidateProbability);
      if (!selected)
        return selected.takeError();
      if (!*selected)
        continue;
      auto seed = context.random.next64();
      if (!seed)
        return seed.takeError();
      std::optional<HybridComposition> composition;
      if (context.config.hybridLayers != HybridLayerMode::None ||
          context.config.forcedTransform != TransformKind::Auto) {
        composition = planCoordinateRoot(*root, context);
        if (!composition)
          continue;
      }
      Emitter emitter(region, *seed);
      auto result = emitter.run(composition ? &*composition : nullptr);
      if (!result)
        return result.takeError();
      root->replaceAllUsesWith(*result);
      for (auto *node : llvm::reverse(region.nodes))
        node->eraseFromParent();
      ++context.statistics.coordinateRegions;
      context.statistics.instructionsTransformed += region.nodes.size();
      changed = true;
    }
  }
  return changed;
}

} // namespace a2mba
