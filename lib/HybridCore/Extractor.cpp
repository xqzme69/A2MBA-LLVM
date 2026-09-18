#include "Internal.h"

#include <algorithm>
#include <bit>
#include <memory>
#include <sstream>
#include <unordered_map>

namespace a2mba::hybrid_core::detail {
namespace {

struct Shape {
  Node node;
  std::shared_ptr<const Shape> left, right;
  Metrics metrics;
  unsigned operatorMask = 0;
  std::string key;
  std::int64_t score = 0;
  std::uint64_t tie = 0;
};

using ShapePtr = std::shared_ptr<const Shape>;

int category(Op op) {
  switch (op) {
  case Op::And:
  case Op::Or:
  case Op::Xor:
  case Op::Not:
    return 1;
  case Op::Add:
  case Op::Sub:
  case Op::Mul:
  case Op::Neg:
    return 2;
  default:
    return 0;
  }
}

ShapePtr makeShape(Node node, ShapePtr left, ShapePtr right, const SearchOptions &options) {
  auto shape = std::make_shared<Shape>();
  shape->node = node;
  shape->left = left;
  shape->right = right;
  shape->metrics.astNodes = 1;
  shape->metrics.depth = 1;
  shape->metrics.estimatedCost = node.op == Op::Input ? 0 : node.op == Op::Mul ? 3 : 1;
  shape->operatorMask = arity(node.op) ? 1U << static_cast<unsigned>(node.op) : 0;
  for (const auto &child : {left, right}) {
    if (!child) {
      continue;
    }
    shape->metrics.astNodes += child->metrics.astNodes;
    shape->metrics.depth = std::max(shape->metrics.depth, child->metrics.depth + 1);
    shape->metrics.estimatedCost += child->metrics.estimatedCost;
    shape->metrics.alternations += child->metrics.alternations;
    shape->metrics.trivialPatterns += child->metrics.trivialPatterns;
    if (category(node.op) && category(child->node.op) &&
        category(node.op) != category(child->node.op)) {
      ++shape->metrics.alternations;
    }
    shape->operatorMask |= child->operatorMask;
  }
  if (shape->metrics.astNodes > options.maxAstNodes || shape->metrics.depth > options.maxDepth) {
    return {};
  }
  if (left && right && left->key == right->key &&
      (node.op == Op::Sub || node.op == Op::Xor || node.op == Op::And || node.op == Op::Or)) {
    ++shape->metrics.trivialPatterns;
  }
  if (left && ((node.op == Op::Neg && left->node.op == Op::Neg) ||
               (node.op == Op::Not && left->node.op == Op::Not))) {
    ++shape->metrics.trivialPatterns;
  }
  if (right && right->node.op == Op::Constant &&
      ((right->node.payload == 0 &&
        (node.op == Op::Add || node.op == Op::Sub || node.op == Op::Xor || node.op == Op::Or)) ||
       (right->node.payload == 1 && node.op == Op::Mul))) {
    ++shape->metrics.trivialPatterns;
  }
  shape->metrics.operatorKinds = static_cast<unsigned>(std::popcount(shape->operatorMask));
  if (!arity(node.op)) {
    shape->key = std::string(opName(node.op)) + ':' + std::to_string(node.payload);
  } else {
    shape->key = '(' + std::string(opName(node.op)) + ' ' + left->key +
                 (right ? ' ' + right->key : "") + ')';
  }
  shape->score = 12LL * shape->metrics.alternations + 4LL * shape->metrics.operatorKinds +
                 shape->metrics.depth - 2LL * shape->metrics.estimatedCost -
                 12LL * shape->metrics.trivialPatterns;
  Random tieBreaker(stableHash(shape->key) ^ options.seed);
  shape->tie = tieBreaker.next();
  return shape;
}

bool better(const ShapePtr &left, const ShapePtr &right) {
  if (left->score != right->score) {
    return left->score > right->score;
  }
  if (left->tie != right->tie) {
    return left->tie < right->tie;
  }
  return left->key < right->key;
}

void retain(std::vector<ShapePtr> &list, ShapePtr value, unsigned beamWidth) {
  if (!value) {
    return;
  }
  for (const auto &shape : list) {
    if (shape->key == value->key) {
      return;
    }
  }
  list.push_back(std::move(value));
  auto cheapest =
      *std::min_element(list.begin(), list.end(), [](const ShapePtr &left, const ShapePtr &right) {
        if (left->metrics.astNodes != right->metrics.astNodes) {
          return left->metrics.astNodes < right->metrics.astNodes;
        }
        return better(left, right);
      });
  std::sort(list.begin(), list.end(), better);
  if (list.size() <= beamWidth) {
    return;
  }
  list.resize(beamWidth);
  if (std::none_of(list.begin(), list.end(),
                   [&](const ShapePtr &shape) { return shape->key == cheapest->key; })) {
    list.back() = std::move(cheapest);
  }
  std::sort(list.begin(), list.end(), better);
}

Program exportShape(const ShapePtr &shape, Width width) {
  Program program;
  program.width = width;
  std::unordered_map<std::string, Id> seen;
  auto emit = [&](auto &&self, const ShapePtr &current) -> Id {
    if (auto it = seen.find(current->key); it != seen.end()) {
      return it->second;
    }
    Node node = current->node;
    if (current->left) {
      node.left = self(self, current->left);
    }
    if (current->right) {
      node.right = self(self, current->right);
    }
    Id id = static_cast<Id>(program.nodes.size());
    program.nodes.push_back(node);
    seen[current->key] = id;
    return id;
  };
  program.root = emit(emit, shape);
  return program;
}

} // namespace

std::vector<Candidate> extract(const EGraph &graph, Id root, const SearchOptions &options) {
  const auto roots = graph.roots();
  std::vector<std::vector<ShapePtr>> previous(graph.slots());
  for (unsigned depth = 1; depth <= options.maxDepth; ++depth) {
    auto current = previous;
    for (Id id : roots) {
      for (const Node &node : graph.nodes(id)) {
        if (!arity(node.op)) {
          retain(current[id], makeShape(node, {}, {}, options), options.beamWidth);
          continue;
        }
        const auto &leftCandidates = previous[graph.find(node.left)];
        for (const auto &left : leftCandidates) {
          if (arity(node.op) == 1) {
            retain(current[id], makeShape(node, left, {}, options), options.beamWidth);
          } else {
            for (const auto &right : previous[graph.find(node.right)]) {
              retain(current[id], makeShape(node, left, right, options), options.beamWidth);
            }
          }
        }
      }
    }
    previous = std::move(current);
  }
  std::vector<Candidate> result;
  for (const auto &shape : previous[graph.find(root)]) {
    if (shape->metrics.astNodes < options.minimumAstNodes) {
      continue;
    }
    auto program = exportShape(shape, graph.width());
    auto metrics = shape->metrics;
    metrics.dagNodes = static_cast<unsigned>(program.nodes.size());
    result.push_back({std::move(program), metrics, shape->score});
  }
  return result;
}

} // namespace a2mba::hybrid_core::detail
