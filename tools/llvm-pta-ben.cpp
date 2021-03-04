#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <set>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cassert>
#include <cstdio>
#include <cstdlib>

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>

#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif
SILENCE_LLVM_WARNINGS_POP

#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/PointerAnalysis/PointerAnalysisFI.h"
#include "dg/PointerAnalysis/PointerAnalysisFS.h"
#include "dg/PointerAnalysis/PointerAnalysisFSInv.h"
#include "dg/PointerAnalysis/Pointer.h"

#include "dg/tools/TimeMeasure.h"

using namespace dg;
using namespace dg::pta;
using dg::debug::TimeMeasure;
using llvm::errs;

enum PTType {
    FLOW_SENSITIVE = 1,
    FLOW_INSENSITIVE,
    WITH_INVALIDATE,
};

static std::string
getInstName(const llvm::Value *val)
{
  std::ostringstream ostr;
  llvm::raw_os_ostream ro(ostr);

  assert(val);
  if (llvm::isa<llvm::Function>(val))
    ro << val->getName().data();
  else
    ro << *val;

  ro.flush();

  // break the string if it is too long
  return ostr.str();
}

void printPSNodeType(enum PSNodeType type) {
    printf("%s", PSNodeTypeToCString(type));
}

static void
printName(PSNode *node, bool dot)
{
  std::string nm;
  const char *name = nullptr;
  if (node->isNull()) {
    name = "null";
  } else if (node->isUnknownMemory()) {
    name = "unknown";
  }

  if (!name) {
    if (!node->getUserData<llvm::Value>()) {
      printPSNodeType(node->getType());
      if (dot)
	printf(" %p\\n", node);
      else
	printf(" %p\n", node);

      return;
    }

    nm = getInstName(node->getUserData<llvm::Value>());
    name = nm.c_str();
  }

  // escape the " character
  for (int i = 0; name[i] != '\0'; ++i) {
    // crop long names
    if (i >= 70) {
      printf(" ...");
      break;
    }

    if (name[i] == '"')
      putchar('\\');

    putchar(name[i]);
  }
}

static int dump_pointer(const Pointer& ptr, const char *name)
{
  printf("target %s=", name);
  printName(ptr.target, 0);
  printf("\n");
  return 0;
}


using AliasResult = int;
const AliasResult NoAlias      = 1;
const AliasResult MayAlias     = 2;
const AliasResult MustAlias    = 3;
const AliasResult PartialAlias = 4;

static int compare_pointer(const Pointer& ptr1,
			               const Pointer& ptr2)
{
  dump_pointer(ptr1, "1");
  dump_pointer(ptr2, "2");

  if (ptr1.isUnknown() || ptr2.isUnknown())
      return MayAlias;

  if (ptr1.target == ptr2.target) {
    if (ptr1.offset.isUnknown() ||
        ptr2.offset.isUnknown()) {
      return MayAlias;
    } else if (ptr1.offset == ptr2.offset) {
      return MustAlias;
    }
    // fall-through to NoAlias
  }

  return NoAlias;
}

static int check_pointer(const Pointer& ptr, const char *name)
{
  printf("target %s=", name);
  printName(ptr.target, 0);
  if (ptr.isUnknown()) {
    printf("Unknown Ptr\n");
    return MayAlias;
  }
  if (ptr.isNull()) {
    printf("Null Ptr\n");
    return MayAlias;
  }
  printf("\n");
  return NoAlias;
}

static AliasResult doAlias(DGLLVMPointerAnalysis *pta,
			               llvm::Value *V1, llvm::Value*V2)
{
  PSNode *p1 = pta->getPointsToNode(V1);
  PSNode *p2 = pta->getPointsToNode(V2);
  int count1 = 0;
  int count2 = 0;
  for (const Pointer& ptr1 : p1->pointsTo) {
    count1++;
    if (!ptr1.isValid())
      continue;
  }
  for (const Pointer& ptr2 : p2->pointsTo) {
    count2++;
    if (!ptr2.isValid())
      continue;
  }

  printf("counts = %d %d\n", count1, count2);
  if (count1 > 1 || count2 > 1) {
    for (const Pointer& ptr1 : p1->pointsTo) {
      if (!ptr1.isValid())
        continue;
      dump_pointer(ptr1, "1");
    }
    for (const Pointer& ptr2 : p2->pointsTo) {
      if (!ptr2.isValid())
        continue;
      dump_pointer(ptr2, "2");
    }
    return MayAlias;
  }
  Pointer ptr1(UNKNOWN_MEMORY, Offset::UNKNOWN);
  Pointer ptr2(UNKNOWN_MEMORY, Offset::UNKNOWN);
  if (count1 == 0 && count2 == 0) {
    return NoAlias;
  }
  if (count1 == 1) {
    auto itr1 = p1->pointsTo.begin();
    ptr1 = *itr1;
    for (;itr1 !=p1->pointsTo.end();++itr1) {
      ptr1 = *itr1;
      if (!ptr1.isValid())
	continue;
      break;
    }
  }
  if (count2 == 1) {
    auto itr2 = p2->pointsTo.begin();
    ptr2 = *itr2;
    for (;itr2 !=p2->pointsTo.end();++itr2) {
      ptr2 = *itr2;
      if (!ptr2.isValid())
	continue;
      break;
    }
  }
  if (count1 == 0) {
    return check_pointer(ptr2, "2");
  }
  if (count2 == 0) {
    return check_pointer(ptr1, "1");
  }
  return compare_pointer(ptr1, ptr2);
}


static llvm::StringRef
  NOALIAS("NOALIAS"),
  MAYALIAS("MAYALIAS"),
  MUSTALIAS("MUSTALIAS"),
  PARTIALALIAS("PARTIALALIAS"),
  EXPECTEDFAIL_MAYALIAS = ("EXPECTEDFAIL_MAYALIAS"),
  EXPECTEDFAIL_NOALIAS("EXPECTEDFAIL_NOALIAS");

static int test_checkfunc(const llvm::StringRef &fun)
{
  if (fun.equals(NOALIAS)) {
    return true;
  } else if (fun.equals(MAYALIAS)) {
    return true;
  } else if (fun.equals(MUSTALIAS)) {
    return true;
  } else if (fun.equals(PARTIALALIAS)) {
    return true;
  } else if (fun.equals(EXPECTEDFAIL_MAYALIAS)) {
    return true;
  } else if (fun.equals(EXPECTEDFAIL_NOALIAS)) {
    return true;
  }
  return false;
}

static void
evalPSNode(DGLLVMPointerAnalysis *pta, PSNode *node)
{
  enum PSNodeType nodetype = node->getType();
  if (nodetype != PSNodeType::CALL) {
    return;
  }
  if (node->isNull() || node->isUnknownMemory()) {
    return;
  }

  const llvm::Value *val = node->getUserData<llvm::Value>();
  if (!val)
    return;
  const llvm::CallInst *call = llvm::dyn_cast<llvm::CallInst>(val);
  if (!call)
    return;

  const llvm::Value *v = call->getCalledFunction();
  if (v == nullptr)
      return;

  const llvm::Function *called = llvm::dyn_cast<llvm::Function>(v);
  const llvm::StringRef &fun = called->getName();
  if (call->getNumArgOperands() != 2)
      return ;
  if (!test_checkfunc(fun))
      return;

  llvm::Value* V1 = call->getArgOperand(0);
  llvm::Value* V2 = call->getArgOperand(1);
  const char *ex, *s, *score;
  AliasResult aares = doAlias(pta, V1, V2);
  bool r = false;

  if (fun.equals(NOALIAS)) {
    r = (aares == NoAlias);
    ex = "NO";

    if (aares == NoAlias)
      score = "true";
    else if (aares == MayAlias)
      score = "inadequate";
    else if (aares == MustAlias)
      score = "buggy";
    else if (aares == PartialAlias)
      score = "inadequate";
    else
      score = "unknown";
  } else if (fun.equals(MAYALIAS)) {
    r = (aares == MayAlias || aares == MustAlias);
    ex = "MAY";

    if (aares == NoAlias)
      score = "false";
    else if (aares == MayAlias)
      score = "true";
    else if (aares == MustAlias)
      score = "toomuch";
    else if (aares == PartialAlias)
      score = "true";
    else
      score = "unknown";
  } else if (fun.equals(MUSTALIAS)) {
    r = (aares == MustAlias);
    ex = "MUST";

    if (aares == NoAlias)
      score = "false";
    else if (aares == MayAlias)
      score = "inadequate";
    else if (aares == MustAlias)
      score = "true";
    else
      score = "unknown";
  } else if (fun.equals(PARTIALALIAS)) {
    r = (aares == MayAlias || aares == MustAlias);
    ex = "MAY";

    if (aares == NoAlias)
      score = "false";
    else if (aares == MayAlias)
      score = "true";
    else if (aares == MustAlias)
      score = "toomuch";
    else if (aares == PartialAlias)
      score = "true";
    else
      score = "unknown";
  } else if (fun.equals(EXPECTEDFAIL_MAYALIAS)) {
    r = (aares != MayAlias && aares != MustAlias);
    ex = "EXPECTEDFAIL_MAY";

    if (aares == NoAlias)
      score = "true";
    else if (aares == MayAlias)
      score = "inadequate"; // suspected
    else if (aares == MustAlias)
      score = "true";    // suspected
    else if (aares == PartialAlias)
      score = "inadequate";
    else
      score = "unknown";
  } else if (fun.equals(EXPECTEDFAIL_NOALIAS)) {
    r = (aares != NoAlias);
    ex = "EXPECTEDFAIL_NO";

    if (aares == NoAlias)
      score = "false";
    else if (aares == MayAlias)
      score = "true";
    else if (aares == MustAlias)
      score = "true";
    else if (aares == PartialAlias)
      score = "true";
    else
      score = "unknown";
  } else {
    return;
  }

  if (aares == NoAlias)
      s = "NO";
  else if (aares == MayAlias)
      s = "MAY";
  else if (aares == MustAlias)
      s = "MUST";
  else s = "UNKNOWN";
  if (r)
    printf("  pta %s %s ex %s ", score, s, ex);
  else
    printf("  pta %s %s ex %s ", score, s, ex);

  printf("\n");
}

static void
evalPTA(DGLLVMPointerAnalysis *pta)
{
  for (auto& node : pta->getNodes()) {
    if (!node)
        continue;
    evalPSNode(pta, node.get());
  }
}

int main(int argc, char *argv[])
{
    llvm::Module *M;
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    const char *module = nullptr;
    const char *entry_func = "main";
    PTType type = FLOW_INSENSITIVE;
    uint64_t field_sensitivity = Offset::UNKNOWN;

    // parse options
    for (int i = 1; i < argc; ++i) {
        // run given points-to analysis
        if (strcmp(argv[i], "-pta") == 0) {
            if (strcmp(argv[i+1], "fs") == 0)
                type = FLOW_SENSITIVE;
            else if (strcmp(argv[i+1], "inv") == 0)
                type = WITH_INVALIDATE;
        } else if (strcmp(argv[i], "-pta-field-sensitive") == 0) {
            field_sensitivity = static_cast<uint64_t>(atoll(argv[i + 1]));
        } else if (strcmp(argv[i], "-entry") == 0) {
            entry_func = argv[i + 1];
        } else {
            module = argv[i];
        }
    }

    if (!module) {
        errs() << "Usage: % IR_module\n";
        return 1;
    }

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR <= 5))
    M = llvm::ParseIRFile(module, SMD, context);
#else
    auto _M = llvm::parseIRFile(module, SMD, context);
    // _M is unique pointer, we need to get Module *
    M = _M.get();
#endif

    if (!M) {
        llvm::errs() << "Failed parsing '" << module << "' file:\n";
        SMD.print(argv[0], errs());
        return 1;
    }

    TimeMeasure tm;

    LLVMPointerAnalysisOptions opts;

    if (type == FLOW_INSENSITIVE) {
      opts.analysisType = dg::LLVMPointerAnalysisOptions::AnalysisType::fi;
    } else if (type == WITH_INVALIDATE) {
      opts.analysisType = dg::LLVMPointerAnalysisOptions::AnalysisType::inv;
    } else {
      opts.analysisType = dg::LLVMPointerAnalysisOptions::AnalysisType::fs;
    }

    opts.entryFunction = entry_func;
    opts.fieldSensitivity = field_sensitivity;

    DGLLVMPointerAnalysis PTA(M, opts);

    tm.start();

    PTA.run();

    tm.stop();
    tm.report("INFO: Pointer analysis took");

    evalPTA(&PTA);

    return 0;
}
