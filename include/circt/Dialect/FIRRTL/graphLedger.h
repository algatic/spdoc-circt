#pragma once

#ifndef CIRCT_DIALECT_FIRRTL_GRAPHLEDGER_H
#define CIRCT_DIALECT_FIRRTL_GRAPHLEDGER_H

#include "circt/Dialect/FIRRTL/spdocInstrPass.h"
#include "circt/Analysis/FIRRTLInstanceInfo.h"
#include <set>
#include <unordered_set>
#include "circt/Dialect/FIRRTL/graphLedger.h"
#include "circt/Dialect/FIRRTL/moduleInfo.h"
#include "circt/Dialect/FIRRTL/stateChecker.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
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
//class Node; //  ^i^m ^p^q    ^x^n  ^h ^k     ^v^g       ^l^e ^p   ^i
//class graphLedger;
//class netNode;
} // namespace firrtl
} // namespace circt

//using Node = circt::firrtl::Node;

using StringSet = std::set<std::string>;

using StringToStringSetMap = std::map<std::string, std::set<std::string>>;
using StringAndStringSetMap = std::pair<std::string, std::set<std::pair<std::string, std::set<std::string> > > >;
using ComplexMap = std::map<std::string, StringAndStringSetMap>; //for getInstanceMap

using StringToNodeSetMap = std::map<std::string, std::set<Node*>>;
using WDefInstanceToStringSetMap = std::map<InstanceOp*, std::set<std::string>>;
using ComplexTuple = std::pair<StringToNodeSetMap, WDefInstanceToStringSetMap>; //for findPortSrcs

using ComplexTuple_connected = std::pair<StringToNodeSetMap, WDefInstanceToStringSetMap>;//for findOutPortsConnected

namespace circt {
namespace firrtl {
//static bool isStatement(mlir::Operation *op);
class Node {
private:
    //static std::set<std::string>& types;
    static const std::set<llvm::StringRef>& getTypes();
    //std::string name;
public:
    std::variant<mlir::Operation*, circt::firrtl::PortInfo> node;
    std::string name;
    Node(mlir::Operation* node, const std::string name);
    Node() = default;
    Node(PortInfo node, const std::string name);
    std::string serialize() const;
    template <typename T > T get();
    std::string c();
    bool usedIn(Value expr, bool imp = true);
    std::set<std::pair<std::string, std::vector<std::string>>> portIn(Value expr);

    std::set<std::vector<std::string>> _getPort(Value expr);

    static Node create(mlir::Operation* node);
    static Node create(PortInfo portInfo);


    //Node operator()(mlir::Operation* node);

    static bool hasType(mlir::Operation* n);
    static bool hasType(PortInfo portInfo);

    static llvm::StringRef findName(mlir::Value expr);
    static llvm::SetVector<llvm::StringRef> findNames(Value expr);

    //object当中定义的函数static就可以了

};

class graphLedger {
private:
    std::unordered_map<std::string, SmallVector<std::string>> expG;
    std::unordered_map<std::string, SmallVector<std::string>> G;
    std::unordered_map<std::string, SmallVector<std::string>> R;
    std::unordered_set<std::string> visited;

    //std::string mName;
    llvm::DenseSet<circt::firrtl::PortInfo> mPorts;

    std::map<std::string, std::set<std::pair<std::string, std::string>>> rN2IP;
    std::map<std::string, std::set<std::pair<std::string, std::string>>> rN2MP;
    std::map<std::string, mlir::Value> N2E;
    std::map<std::pair<std::string,std::string>, std::map<std::string, std::string>> rMP2N;
    std::vector<InstanceOp> instances;  // Collection of instance information  
    std::vector<FMemModuleOp> memorys;      // Collection of memory information  
    std::map<std::string, Node> Nodes;    // Map of node names to Node objects  
    //std::map<std::pair<std::string, std::string>, mlir::Value> rIP2E;  // Instance port to expression mapping  ;
    //std::unordered_set<std::string> visited;


    void findNode(mlir::Operation* s); //这个是那个firrtlnode的替代！这个type
    void findNode(PortInfo portInfo); 
    //statement这个类就是没有代餐的，因为各个不同语句都被实现成各个op了，并非统一继承自某一个statement类（本身大家实现的方式就不太一样）。

    void findEdgeExp(Node n, SmallVector<std::string>& sinks, mlir::Operation* stmt);

    void buildG();

    void reverseG();

    //node的替代是InstanceGraphNode
    void findEdge(circt::firrtl::Node n, SmallVector<std::string>& sinks, mlir::Operation* s); //这个是那个firrtlnode的替代！这个type

    void updateN2XP(const std::string& sink, mlir::Value srcE, Node& node);

    void updateXP2X(const std::string& sink, mlir::Value sinkE, mlir::Value srcE, const Node& node);

    void updateN2E(std::string sink, mlir::Value srcE);

    //ComplexMap getInstanceMap();

    //ComplexTuple findPortSrcs(llvm::SetVector<llvm::StringRef>& ports);

    //ComplexTuple findExprSrcs(llvm::DenseSet<mlir::Operation*>& exprs);

    std::tuple<llvm::SetVector<llvm::StringRef>, std::map<InstanceOp, llvm::SetVector<llvm::StringRef>>,llvm::SetVector<llvm::StringRef>> findOutPortsConnected(std::map<circt::firrtl::InstanceOp, llvm::SetVector<llvm::StringRef>> ips);

    std::pair<std::map<llvm::StringRef, llvm::SetVector<Node*>>, std::map<InstanceOp, llvm::SetVector<llvm::StringRef>>> findNodeSrcs(llvm::SetVector<llvm::StringRef> nodes);

    //bool isStatePreserving(const RegOp& reg);

    //std::map<std::string, std::set<RegOp*>> findVecRegs(std::set<RegOp> regs);

    SmallVector<std::pair<llvm::StringRef, std::optional<llvm::StringRef>>> findSrcs(llvm::StringRef sink, llvm::StringRef prev);

    SmallVector<llvm::StringRef> findNodesSinks(std::string n);

    std::pair<SmallVector<llvm::StringRef>, SmallVector<llvm::StringRef>> findMemSinks(const Node& mem);

    SmallVector<llvm::StringRef> getOuter(const SmallVector<llvm::StringRef> sinks);

    SmallVector<llvm::StringRef> findSinks(const std::string& source);

public:
    std::string mName;
    circt::firrtl::FModuleLike module;
    graphLedger(circt::firrtl::FModuleLike module);
    std::map<std::pair<std::string, std::string>, mlir::Value> rIP2E;

    void parse();

    ComplexMap getInstanceMap();
    bool isStatePreserving(const RegOp& reg);

    std::pair<std::map<llvm::StringRef, llvm::SetVector<Node*>>, std::map<InstanceOp, llvm::SetVector<llvm::StringRef>>> findPortSrcs(llvm::SetVector<llvm::StringRef>& ports);

    std::pair<std::map<llvm::StringRef, llvm::SetVector<Node*>>, std::map<InstanceOp, llvm::SetVector<llvm::StringRef>>> findExprSrcs(llvm::DenseSet<mlir::Value> exprs);

    template <typename T > std::set<T*> getNodes();
    std::map<std::pair<std::string, std::string>, mlir::Value> IP2E();

    std::map<std::string, std::set<RegOp>> findVecRegs(std::set<RegOp>& regs);
    void clear();

    std::set<std::string> filterOut(std::set<Node>& nodes);

    void printLog();

    std::pair<int, int> getStat();
};

}
}
#endif
