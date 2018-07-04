//===- AsmPrinter.cpp - MLIR Assembly Printer Implementation --------------===//
//
// Copyright 2019 The MLIR Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// =============================================================================
//
// This file implements the MLIR AsmPrinter class, which is used to implement
// the various print() methods on the core IR objects.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/AffineExpr.h"
#include "mlir/IR/AffineMap.h"
#include "mlir/IR/CFGFunction.h"
#include "mlir/IR/MLFunction.h"
#include "mlir/IR/Module.h"
#include "mlir/IR/Types.h"
#include "mlir/Support/STLExtras.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/raw_ostream.h"
using namespace mlir;


//===----------------------------------------------------------------------===//
// Function printing
//===----------------------------------------------------------------------===//

static void printFunctionSignature(const Function *fn, raw_ostream &os) {
  auto type = fn->getType();

  os << "@" << fn->getName() << '(';
  interleave(type->getInputs(),
             [&](Type *eltType) { os << *eltType; },
             [&]() { os << ", "; });
  os << ')';

  switch (type->getResults().size()) {
  case 0: break;
  case 1:
    os << " -> " << *type->getResults()[0];
    break;
  default:
    os << " -> (";
    interleave(type->getResults(),
               [&](Type *eltType) { os << *eltType; },
               [&]() { os << ", "; });
    os << ')';
    break;
  }
}

void ExtFunction::print(raw_ostream &os) const {
  os << "extfunc ";
  printFunctionSignature(this, os);
  os << "\n";
}

//===----------------------------------------------------------------------===//
// CFG Function printing
//===----------------------------------------------------------------------===//

namespace {
class CFGFunctionState {
public:
  CFGFunctionState(const CFGFunction *function, raw_ostream &os);

  const CFGFunction *getFunction() const { return function; }

  void print();
  void print(const BasicBlock *block);

  void print(const Instruction *inst);
  void print(const OperationInst *inst);
  void print(const ReturnInst *inst);
  void print(const BranchInst *inst);

  unsigned getBBID(const BasicBlock *block) {
    auto it = basicBlockIDs.find(block);
    assert(it != basicBlockIDs.end() && "Block not in this function?");
    return it->second;
  }

private:
  const CFGFunction *function;
  raw_ostream &os;
  DenseMap<const BasicBlock*, unsigned> basicBlockIDs;
};
} // end anonymous namespace

CFGFunctionState::CFGFunctionState(const CFGFunction *function, raw_ostream &os)
  : function(function), os(os) {

  // Each basic block gets a unique ID per function.
  unsigned blockID = 0;
  for (auto &block : *function)
    basicBlockIDs[&block] = blockID++;
}

void CFGFunctionState::print() {
  os << "cfgfunc ";
  printFunctionSignature(this->getFunction(), os);
  os << " {\n";

  for (auto &block : *function)
    print(&block);
  os << "}\n\n";
}

void CFGFunctionState::print(const BasicBlock *block) {
  os << "bb" << getBBID(block) << ":\n";

  // TODO Print arguments.
  for (auto &inst : block->getOperations())
    print(&inst);

  print(block->getTerminator());
}

void CFGFunctionState::print(const Instruction *inst) {
  switch (inst->getKind()) {
  case Instruction::Kind::Operation:
    return print(cast<OperationInst>(inst));
  case TerminatorInst::Kind::Branch:
    return print(cast<BranchInst>(inst));
  case TerminatorInst::Kind::Return:
    return print(cast<ReturnInst>(inst));
  }
}

void CFGFunctionState::print(const OperationInst *inst) {
  // TODO: escape name if necessary.
  os << "  \"" << inst->getName().str() << "\"()\n";
}

void CFGFunctionState::print(const BranchInst *inst) {
  os << "  br bb" << getBBID(inst->getDest()) << "\n";
}
void CFGFunctionState::print(const ReturnInst *inst) {
  os << "  return\n";
}

//===----------------------------------------------------------------------===//
// ML Function printing
//===----------------------------------------------------------------------===//

namespace {
class MLFunctionState {
public:
  MLFunctionState(const MLFunction *function, raw_ostream &os);

  const MLFunction *getFunction() const { return function; }

  void print();

  void print(const Statement *stmt);
  void print(const ForStmt *stmt);
  void print(const IfStmt *stmt);
  void print(const ElseClause *stmt, bool isLast);

private:
  // Print statements nested within this node statement.
  void printNestedStatements(const NodeStmt *stmt);

  const MLFunction *function;
  raw_ostream &os;
  int numSpaces;
};
} // end anonymous namespace

MLFunctionState::MLFunctionState(const MLFunction *function, raw_ostream &os)
    : function(function), os(os), numSpaces(2) {}

void MLFunctionState::print() {
  os << "mlfunc ";
  // FIXME: should print argument names rather than just signature
  printFunctionSignature(function, os);
  os << " {\n";
  for (auto *stmt : function->stmtList)
    print(stmt);
  os << "  return\n";
  os << "}\n\n";
}

void MLFunctionState::print(const Statement *stmt) {
  os.indent(numSpaces);
  switch (stmt->getKind()) {
  case Statement::Kind::Operation: // TODO
    assert(0 && "Operation statement is not yet implemented");
  case Statement::Kind::For:
    return print(cast<ForStmt>(stmt));
  case Statement::Kind::If:
    return print(cast<IfStmt>(stmt));
  case Statement::Kind::Else:
    return print(cast<ElseClause>(stmt));
  }
}

void MLFunctionState::printNestedStatements(const NodeStmt *stmt) {
  os << "{\n";
  numSpaces += 2;
  for (auto * nestedStmt : stmt->children)
    print(nestedStmt);
  numSpaces -= 2;
  os.indent(numSpaces) << "}";
}

void MLFunctionState::print(const ForStmt *stmt) {
  os << "for ";
  printNestedStatements(stmt);
  os << "\n";
}

void MLFunctionState::print(const IfStmt *stmt) {
  os << "if ";
  printNestedStatements(stmt);

  int numClauses = stmt->elseClauses.size();
  for (auto e : stmt->elseClauses)
    print(e, e->getClauseNumber() == numClauses - 1);
  os << "\n";
}

void MLFunctionState::print(const ElseClause *stmt, bool isLast) {
  if (!isLast)
    os << " if";
  os << " else ";;
  printNestedStatements(stmt);
}

//===----------------------------------------------------------------------===//
// print and dump methods
//===----------------------------------------------------------------------===//

void Instruction::print(raw_ostream &os) const {
  CFGFunctionState state(getFunction(), os);
  state.print(this);
}

void Instruction::dump() const {
  print(llvm::errs());
}

void AffineExpr::print(raw_ostream &os) const {
  // TODO(bondhugula): print out affine expression
}

void AffineMap::print(raw_ostream &os) const {
  // TODO(andydavis) Print out affine map based on dimensionCount and
  // symbolCount: (d0, d1) [S0, S1] -> (d0 + S0, d1 + S1)
}

void BasicBlock::print(raw_ostream &os) const {
  CFGFunctionState state(getFunction(), os);
  state.print();
}

void BasicBlock::dump() const {
  print(llvm::errs());
}

void Statement::print(raw_ostream &os) const {
  MLFunctionState state(getFunction(), os);
  state.print(this);
}

void Statement::dump() const {
  print(llvm::errs());
}
void Function::print(raw_ostream &os) const {
  switch (getKind()) {
  case Kind::ExtFunc: return cast<ExtFunction>(this)->print(os);
  case Kind::CFGFunc: return cast<CFGFunction>(this)->print(os);
  case Kind::MLFunc:  return cast<MLFunction>(this)->print(os);
  }
}

void Function::dump() const {
  print(llvm::errs());
}

void CFGFunction::print(raw_ostream &os) const {
  CFGFunctionState state(this, os);
  state.print();
}

void MLFunction::print(raw_ostream &os) const {
  MLFunctionState state(this, os);
  state.print();
}

void Module::print(raw_ostream &os) const {
  for (auto *map : affineMapList)
    map->print(os);
  for (auto *fn : functionList)
    fn->print(os);
}

void Module::dump() const {
  print(llvm::errs());
}
