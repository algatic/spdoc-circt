#pragma once

#ifndef CIRCT_DIALECT_FIRRTL_MODULEINFO_H
#define CIRCT_DIALECT_FIRRTL_MODULEINFO_H

#include <set>
#include "circt/Dialect/FIRRTL/spdocInstrPass.h"
#include "circt/Dialect/FIRRTL/graphLedger.h"
#include "circt/Dialect/FIRRTL/moduleInfo.h"
#include "circt/Dialect/FIRRTL/stateChecker.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "circt/Support/LLVM.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"
#include "mlir/IR/BuiltinOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLInstanceGraph.h"
//#define GEN_PASS_DEF_SPDOCINSTRPASS
//#include "circt/Dialect/FIRRTL/Passes.h.inc"

using namespace circt;
using namespace firrtl;

namespace circt {
namespace firrtl {
class Node; //  ^i^m ^p^q    ^x^n  ^h ^k     ^v^g       ^l^e ^p   ^i
class graphLedger;
//class netNode;
} // namespace firrtl
} // namespace circt

//using Node = circt::firrtl::Node;

using StringSet = std::set<std::string>;

using StringToStringSetMap = std::map<std::string, std::set<std::string>>;
using StringAndStringSetMap = std::pair<std::string, std::set<std::pair<std::string, std::set<std::string> > > >;
using ComplexMap = std::map<std::string, StringAndStringSetMap>; //for getInstanceMap

using StringToNodeSetMap = std::map<std::string, std::set<circt::firrtl::Node*>>;
using WDefInstanceToStringSetMap = std::map<InstanceOp*, std::set<std::string>>;
using ComplexTuple = std::pair<StringToNodeSetMap, WDefInstanceToStringSetMap>; //for findPortSrcs

using ComplexTuple_connected = std::pair<StringToNodeSetMap, WDefInstanceToStringSetMap>;//for findOutPortsConnected


//class Node; //  ^i^m ^p^q

namespace circt {
namespace firrtl {
class netNode {
private:
    //std::string name;
    //llvm::SmallVector<netNode*> parents;
    //llvm::SmallVector<netNode*> children;

    //   ^x ^b   ^m ^o  ^o^x ^i  ^|  ^t   ^nreset
    mutable llvm::SmallVector<netNode*> immParents;
    mutable llvm::SmallVector<netNode*> immChildren;
    //mutable bool initialized;

    void ensureInitialized() ;

public:
    std::string n;
    mutable bool initialized;

    llvm::SmallVector<netNode*> parents;
    llvm::SmallVector<netNode*> children;
    //std::string name;
    //  ^~^d ^`  ^g  ^u  -      ^t Scala  ^z^d case class  ^~^d ^`
    netNode(llvm::StringRef n,
        llvm::ArrayRef<netNode*> parentNodes = {},
        llvm::ArrayRef<netNode*> childNodes = {});

    //  ^g^m    ^v   ^u -      ^t Scala  ^z^d reset  ^v   ^u
    void reset();

    //     ^w  ^y  ^v   ^u
    llvm::StringRef getName() ;
    llvm::ArrayRef<netNode*> getParents() const;
    llvm::ArrayRef<netNode*> getChildren() const;

    //     ^t  ^y  ^v   ^u
    llvm::MutableArrayRef<netNode*> getParents();
    llvm::MutableArrayRef<netNode*> getChildren();
};


class moduleInfo {
private:
    //std::map<std::string, std::set<Node*>> vPortSrcs;

    //std::map<InstanceOp*, std::set<std::string>> fInstPorts;

    //moduleInfo(const std::string& name, circt::firrtl::graphLedger& ledger);

public:
    std::string mName;
    circt::firrtl::graphLedger& gLedger;
    std::map<llvm::StringRef, llvm::SetVector<circt::firrtl::Node*>> vPortSrcs; // 端口到节点的映射，表示端口连接到哪些节点
    std::map<circt::firrtl::InstanceOp, llvm::SetVector<llvm::StringRef>> fInstPorts;

    moduleInfo(std::string& name, circt::firrtl::graphLedger& ledger);

    moduleInfo create(circt::firrtl::graphLedger& gLedger);

    static int findModules(std::map<std::string, std::shared_ptr<circt::firrtl::graphLedger>>, llvm::StringRef top, std::string& module);

    template <typename T> std::set<T*> getSrcNode();
    //template<> std::set<PortInfo*> circt::firrtl::moduleInfo::getSrcNode<PortInfo>();

    std::set<std::string> getAllNodes();

    std::set<std::string> getAllPorts();

    std::set<std::pair<std::string, circt::firrtl::MemOp>> getMemory();

    std::set<std::pair<std::string, circt::firrtl::RegOp>> getRegister();

    int getNumNodes();

    void phase0(llvm::SetVector<llvm::StringRef>& ports);
    void phase1(llvm::DenseSet<mlir::Value> exprs);

    llvm::SetVector<llvm::StringRef> getPorts(llvm::DenseMap<llvm::StringRef, moduleInfo*>& mInfos);//listbuffer就用vector算了,加个const因为是有序的

    llvm::DenseSet<mlir::Value> getInstCons(llvm::DenseMap<llvm::StringRef, moduleInfo*> mInfos);

    void printInfo();

};

class moduleNet {
private:
    llvm::SmallVector<std::unique_ptr<netNode>> nodes;
    llvm::DenseMap<StringAttr, netNode*> nodeMap;

    //int numNodes;
    SmallVector<netNode*, 0> roots;
    SmallVector<netNode*, 0> leaves;

public:
    int numNodes;
    //moduleNet(llvm::SmallVector<std::unique_ptr<netNode>> nodeList);

    moduleNet(std::map<std::string, std::shared_ptr<circt::firrtl::graphLedger>> gLedgers, std::string topModuleName);

    void reset();

    std::optional<std::string> popT();

    std::optional<std::string> popB();

    void printNet(bool dir = true);

};//就是他俩能不能赶紧99，我有点lay

}
}
#endif
