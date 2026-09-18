#include "Internal.h"

#include <algorithm>
#include <cassert>
#include <numeric>
#include <tuple>

namespace a2mba::hybrid_core::detail {

std::size_t NodeHash::operator()(const Node &node) const noexcept {
  std::uint64_t hash = static_cast<unsigned>(node.op) + UINT64_C(0x9e3779b97f4a7c15);
  for (auto value : {std::uint64_t(node.left), std::uint64_t(node.right), node.payload}) {
    hash ^= value + UINT64_C(0x9e3779b97f4a7c15) + (hash << 6) + (hash >> 2);
  }
  return static_cast<std::size_t>(hash);
}

Id EGraph::find(Id id) const {
  assert(id < parents_.size());
  Id root = id;
  while (parents_[root] != root) {
    root = parents_[root];
  }
  while (parents_[id] != id) {
    Id next = parents_[id];
    parents_[id] = root;
    id = next;
  }
  return root;
}

Node EGraph::canonical(Node node) const {
  if (arity(node.op)) {
    node.left = find(node.left);
  }
  if (arity(node.op) == 2) {
    node.right = find(node.right);
  }
  if (node.op == Op::Constant) {
    node.payload &= widthMask(width_);
  }
  return node;
}

std::optional<Id> EGraph::add(Node node) {
  node = canonical(node);
  if (auto it = memo_.find(node); it != memo_.end()) {
    return find(it->second);
  }
  if (created() >= limit_) {
    budgetHit_ = true;
    return std::nullopt;
  }
  Id id = created();
  parents_.push_back(id);
  classes_.push_back({node});
  memo_[node] = id;
  return id;
}

bool EGraph::unite(Id left, Id right) {
  left = find(left);
  right = find(right);
  if (left == right) {
    return false;
  }
  if (right < left) {
    std::swap(left, right);
  }
  parents_[right] = left;
  auto &into = classes_[left];
  auto &from = classes_[right];
  into.insert(into.end(), from.begin(), from.end());
  from.clear();
  return true;
}

void EGraph::rebuild() {
  for (;;) {
    std::unordered_map<Node, Id, NodeHash> fresh;
    std::vector<std::pair<Id, Id>> collisions;
    for (Id id : roots()) {
      for (Node &node : classes_[id]) {
        node = canonical(node);
        auto [it, inserted] = fresh.emplace(node, id);
        if (!inserted && it->second != id) {
          collisions.emplace_back(it->second, id);
        }
      }
    }
    bool changed = false;
    for (auto [left, right] : collisions) {
      changed |= unite(left, right);
    }
    if (changed) {
      continue;
    }
    memo_ = std::move(fresh);
    for (Id id : roots()) {
      auto &list = classes_[id];
      std::sort(list.begin(), list.end(), [](const Node &left, const Node &right) {
        return std::tie(left.op, left.left, left.right, left.payload) <
               std::tie(right.op, right.left, right.right, right.payload);
      });
      list.erase(std::unique(list.begin(), list.end()), list.end());
    }
    return;
  }
}

Result<Id> EGraph::import(const Program &program) {
  if (program.width != width_) {
    return Result<Id>::fail("mixed-width import");
  }
  if (auto reason = validate(program); !reason.empty()) {
    return Result<Id>::fail(reason);
  }
  std::vector<Id> mapping;
  mapping.reserve(program.nodes.size());
  for (Node node : program.nodes) {
    if (arity(node.op)) {
      node.left = mapping[node.left];
    }
    if (arity(node.op) == 2) {
      node.right = mapping[node.right];
    }
    auto id = add(node);
    if (!id) {
      return Result<Id>::fail("seed exceeds e-graph node budget");
    }
    mapping.push_back(*id);
  }
  return Result<Id>::ok(mapping[program.root]);
}

std::vector<Id> EGraph::roots() const {
  std::vector<Id> result;
  for (Id id = 0; id < parents_.size(); ++id) {
    if (find(id) == id) {
      result.push_back(id);
    }
  }
  return result;
}

std::string EGraph::checkInvariants() const {
  std::unordered_map<Node, Id, NodeHash> observed;
  for (Id id = 0; id < slots(); ++id) {
    if (find(id) != id) {
      if (!classes_[id].empty()) {
        return "non-root owns nodes";
      }
      continue;
    }
    if (classes_[id].empty()) {
      return "empty root";
    }
    for (auto node : classes_[id]) {
      if (canonical(node) != node) {
        return "non-canonical child";
      }
      if (!observed.emplace(node, id).second) {
        return "duplicate canonical node";
      }
      auto memo = memo_.find(node);
      if (memo == memo_.end() || find(memo->second) != id) {
        return "bad hash-cons mapping";
      }
    }
  }
  return {};
}

std::uint64_t Random::next() {
  auto value = (state_ += UINT64_C(0x9e3779b97f4a7c15));
  value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31);
}

std::uint64_t Random::bounded(std::uint64_t bound) {
  assert(bound);
  const auto threshold = (UINT64_C(0) - bound) % bound;
  for (;;) {
    auto value = next();
    if (value >= threshold) {
      return value % bound;
    }
  }
}

std::uint64_t stableHash(const std::string &text) {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (char character : text) {
    hash ^= static_cast<unsigned char>(character);
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

namespace {
std::vector<Binding> matchAt(const EGraph &graph, const Pattern &pattern, Id patternId, Id classId,
                             Binding binding, MatchBudget &budget) {
  if (!budget.tick()) {
    return {};
  }
  const Node &patternNode = pattern.nodes[patternId];
  classId = graph.find(classId);
  if (patternNode.op == Op::Input) {
    auto &slot = binding[patternNode.payload];
    if (slot == invalidId) {
      slot = classId;
    } else if (graph.find(slot) != classId) {
      return {};
    }
    return {binding};
  }
  std::vector<Binding> matches;
  for (const Node &node : graph.nodes(classId)) {
    if (!budget.tick()) {
      break;
    }
    if (node.op != patternNode.op || node.payload != patternNode.payload) {
      continue;
    }
    if (!arity(node.op)) {
      matches.push_back(binding);
      break;
    }
    auto leftMatches = matchAt(graph, pattern, patternNode.left, node.left, binding, budget);
    for (const auto &leftBinding : leftMatches) {
      if (arity(node.op) == 1) {
        matches.push_back(leftBinding);
      } else {
        auto rightMatches =
            matchAt(graph, pattern, patternNode.right, node.right, leftBinding, budget);
        for (auto rightBinding : rightMatches) {
          if (matches.size() >= budget.maxResults) {
            break;
          }
          matches.push_back(rightBinding);
        }
      }
      if (matches.size() >= budget.maxResults) {
        break;
      }
    }
    if (matches.size() >= budget.maxResults) {
      budget.hit = true;
      break;
    }
  }
  std::sort(matches.begin(), matches.end());
  matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
  return matches;
}

} // namespace

std::vector<Binding> match(const EGraph &graph, const Pattern &pattern, Id root,
                           MatchBudget &budget) {
  if (!budget.maxResults) {
    budget.hit = true;
    return {};
  }
  return matchAt(graph, pattern, pattern.root, root, {invalidId, invalidId, invalidId}, budget);
}

std::optional<Id> instantiate(EGraph &graph, const Pattern &pattern, const Binding &binding) {
  std::vector<Id> ids;
  for (auto node : pattern.nodes) {
    if (node.op == Op::Input) {
      ids.push_back(graph.find(binding[node.payload]));
      continue;
    }
    if (arity(node.op)) {
      node.left = ids[node.left];
    }
    if (arity(node.op) == 2) {
      node.right = ids[node.right];
    }
    auto id = graph.add(node);
    if (!id) {
      return std::nullopt;
    }
    ids.push_back(*id);
  }
  return ids[pattern.root];
}

} // namespace a2mba::hybrid_core::detail

namespace a2mba::hybrid_core {

Result<SearchResult> search(const Program &seed, const SearchOptions &options) {
  if (auto reason = validate(seed); !reason.empty()) {
    return Result<SearchResult>::fail(reason);
  }
  if (!options.rounds || options.rounds > 16 || options.maxEGraphNodes < 3 ||
      options.maxEGraphNodes > 4096 || !options.maxMatches || options.maxMatches > 100000 ||
      !options.maxMatchSteps || options.maxMatchSteps > 2000000 || options.maxDepth < 2 ||
      options.maxDepth > 16 || options.maxAstNodes < 3 || options.maxAstNodes > 255 ||
      options.minimumAstNodes < 3 || options.minimumAstNodes > options.maxAstNodes ||
      options.beamWidth < 2 || options.beamWidth > 8) {
    return Result<SearchResult>::fail("invalid/out-of-range search budgets");
  }
  detail::EGraph graph(seed.width, options.maxEGraphNodes);
  auto root = graph.import(seed);
  if (!root) {
    return Result<SearchResult>::fail(root.error);
  }
  detail::Random random(options.seed);
  SearchReport report;
  const auto &library = detail::rules();
  std::vector<unsigned> order(library.size());
  std::iota(order.begin(), order.end(), 0);
  struct Pending {
    unsigned rule;
    Id root;
    detail::Binding binding;
  };
  for (unsigned round = 0; round < options.rounds; ++round) {
    ++report.rounds;
    graph.rebuild();
    for (std::size_t index = order.size(); index > 1; --index) {
      std::swap(order[index - 1], order[random.bounded(index)]);
    }
    auto classes = graph.roots();
    for (std::size_t index = classes.size(); index > 1; --index) {
      std::swap(classes[index - 1], classes[random.bounded(index)]);
    }
    std::vector<Pending> pending;
    for (unsigned ruleIndex : order) {
      const auto &rule = library[ruleIndex];
      if (rule.negationFamily && !options.enableNegationRules) {
        continue;
      }
      for (Id id : classes) {
        if (report.matches >= options.maxMatches || report.matchSteps >= options.maxMatchSteps) {
          break;
        }
        detail::MatchBudget budget{report.matchSteps, options.maxMatchSteps,
                                   options.maxMatches - report.matches};
        auto bindings = detail::match(graph, rule.lhs, id, budget);
        report.matchSteps = budget.steps;
        report.matchBudgetHit |= budget.hit;
        for (auto binding : bindings) {
          pending.push_back({ruleIndex, id, binding});
        }
        report.matches += static_cast<unsigned>(bindings.size());
      }
    }
    bool changed = false;
    const unsigned nodesBefore = graph.created();
    for (const auto &rewrite : pending) {
      auto replacement = detail::instantiate(graph, library[rewrite.rule].rhs, rewrite.binding);
      if (!replacement) {
        break;
      }
      if (graph.unite(rewrite.root, *replacement)) {
        changed = true;
        ++report.applications;
        const auto &name = library[rewrite.rule].name;
        if (std::find(report.rulesUsed.begin(), report.rulesUsed.end(), name) ==
            report.rulesUsed.end()) {
          report.rulesUsed.push_back(name);
        }
      }
    }
    graph.rebuild();
    report.nodeBudgetHit = graph.budgetHit();
    report.matchBudgetHit |=
        report.matches >= options.maxMatches || report.matchSteps >= options.maxMatchSteps;
    if (!changed && nodesBefore == graph.created() && !report.matchBudgetHit &&
        !report.nodeBudgetHit) {
      report.fixedPointReached = true;
      break;
    }
    if (report.nodeBudgetHit || report.matchBudgetHit) {
      break;
    }
  }
  if (auto reason = graph.checkInvariants(); !reason.empty()) {
    return Result<SearchResult>::fail("e-graph invariant: " + reason);
  }
  report.createdNodes = graph.created();
  report.liveClasses = static_cast<unsigned>(graph.roots().size());
  auto candidates = detail::extract(graph, *root.value, options);
  if (candidates.empty()) {
    return Result<SearchResult>::fail("no candidate fits extraction budgets");
  }
  return Result<SearchResult>::ok({std::move(candidates), std::move(report)});
}

} // namespace a2mba::hybrid_core
