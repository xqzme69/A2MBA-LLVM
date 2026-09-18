#pragma once

#include "a2mba/HybridCore.h"

#include <array>
#include <unordered_map>

namespace a2mba::hybrid_core::detail {

struct NodeHash {
  std::size_t operator()(const Node &node) const noexcept;
};

struct Pattern {
  std::vector<Node> nodes;
  Id root;
};

struct Rule {
  std::string name;
  bool negationFamily;
  Pattern lhs;
  Pattern rhs;
};

const std::vector<Rule> &rules();

class EGraph {
public:
  EGraph(Width width, unsigned limit) : width_(width), limit_(limit) {}
  Id find(Id id) const;
  std::optional<Id> add(Node node);
  bool unite(Id left, Id right);
  void rebuild();
  Result<Id> import(const Program &program);
  std::vector<Id> roots() const;
  const std::vector<Node> &nodes(Id id) const { return classes_[find(id)]; }
  unsigned created() const { return static_cast<unsigned>(parents_.size()); }
  unsigned slots() const { return created(); }
  Width width() const { return width_; }
  bool budgetHit() const { return budgetHit_; }
  std::string checkInvariants() const;

private:
  Node canonical(Node node) const;
  Width width_;
  unsigned limit_;
  bool budgetHit_ = false;
  mutable std::vector<Id> parents_;
  std::vector<std::vector<Node>> classes_;
  std::unordered_map<Node, Id, NodeHash> memo_;
};

using Binding = std::array<Id, 3>;

struct MatchBudget {
  unsigned steps = 0;
  unsigned maxSteps;
  unsigned maxResults;
  bool hit = false;
  bool tick() {
    if (steps >= maxSteps) {
      hit = true;
      return false;
    }
    ++steps;
    return true;
  }
};

std::vector<Binding> match(const EGraph &, const Pattern &, Id root, MatchBudget &);
std::optional<Id> instantiate(EGraph &, const Pattern &, const Binding &);
std::vector<Candidate> extract(const EGraph &, Id root, const SearchOptions &);

class Random {
public:
  explicit Random(std::uint64_t seed) : state_(seed) {}
  std::uint64_t next();
  std::uint64_t bounded(std::uint64_t bound);

private:
  std::uint64_t state_;
};

std::uint64_t stableHash(const std::string &text);

} // namespace a2mba::hybrid_core::detail
