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
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"
#include "mlir/IR/BuiltinOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLInstanceGraph.h"
#include <set>
#include "circt/Support/LLVM.h" 
#include "circt/Dialect/FSM/FSMOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLOpInterfaces.h"

namespace {
template<typename T> char* getTypeNameImpl();
template<> char* getTypeNameImpl<MemOp>() { return "MemOp"; }
template<> char* getTypeNameImpl<RegOp>() { return "RegOp"; }
template<> char* getTypeNameImpl<PortInfo>() { return "PortInfo"; }
template<> char* getTypeNameImpl<InstanceOp>() { return "InstanceOp"; }

template<typename T>
char* getTypeName() {
    return getTypeNameImpl<T>();
}
}


//#define GEN_PASS_DEF_SPDOCINSTRPASS
//#include "circt/Dialect/FIRRTL/Passes.h.inc"

using namespace circt;
using namespace firrtl;

using StringSet = std::set<std::string>;
using StringToStringSetMap = std::map<std::string, std::set<std::string>>;
using StringAndStringSetMap = std::pair<std::string, std::set<std::pair<std::string, std::set<std::string>>>>;
using ComplexMap = std::map<std::string, StringAndStringSetMap>; //for getInstanceMap

using StringToNodeSetMap = std::map<std::string, std::set<circt::firrtl::Node*>>;
using WDefInstanceToStringSetMap = std::map<InstanceOp*, std::set<std::string>>;
using ComplexTuple = std::pair<StringToNodeSetMap, WDefInstanceToStringSetMap>; //for findPortSrcs

//using StringToNodeSetMap = DenseMap<StringAttr, DenseSet<Node*>>;
//using WDefInstanceToStringSetMap = DenseMap<InstanceOp*, DenseSet<StringAttr>>;
using ComplexTuple_connected = std::pair<StringToNodeSetMap, WDefInstanceToStringSetMap>;//for findOutPortsConnected

circt::firrtl::moduleInfo::moduleInfo(std::string& name, circt::firrtl::graphLedger& ledger)
    : mName(name), gLedger(ledger) {
}

moduleInfo circt::firrtl::moduleInfo::create(circt::firrtl::graphLedger& gLedger) {

    return moduleInfo(gLedger.mName, gLedger);
}

int circt::firrtl::moduleInfo::findModules(std::map<std::string, std::shared_ptr<circt::firrtl::graphLedger>> gLedgers_ptr, llvm::StringRef top, std::string& module) {
    std::map<std::string, std::shared_ptr<circt::firrtl::graphLedger>> gLedgers;
    for (auto& [key, ledgerPtr] : gLedgers_ptr) {  
        if (ledgerPtr) {  
            gLedgers[key] = ledgerPtr;  
        }   
    }
    if (top == module) {
        return 1;
    }
    else {
         //auto it = gLedgers.find(top.str());  
         //if (it == gLedgers.end()) {  
         // 使用MLIR诊断系统报告错误，但仍返回错误值  
             //if (context) {  
                 //emitError(UnknownLoc::get(context))  << "Top module " << top << " not found in gLedgers map";  
             //}  
           //return -1; // 返回错误码而不是抛出异常  
         //}
         auto it = gLedgers.find(top.str());
         if (it == gLedgers.end()) {
            // Handle error: top module not found in gLedgers  
            //throw std::runtime_error("Top module " + top + " not found in gLedgers map.");
            llvm::errs() << "Top module " << top.str() << " not found in gLedgers map";
         }
        // Get the graphLedger for the 'top' module  
        //auto it = gLedgers.find(top.str());
        //if (it == gLedgers.end()) {
            // Handle error: top module not found in gLedgers  
            //mlir::emitError(mlir::UnknownLoc::get(context),"Top module ") << top << " not found in gLedgers map.";
            //throw std::runtime_error(("Top module " + top + " not found in gLedgers map.").str());
        //}
        graphLedger& currentGLedger = *(it->second);

        // Get all WDefInstance (firrtl.instance) nodes within the current top module  
        // This assumes graphLedger has a getNodes<InstanceOp>() method  
        std::set<circt::firrtl::InstanceOp*> instances = currentGLedger.getNodes<circt::firrtl::InstanceOp>();

        int num = 0;
        // Equivalent to foldLeft for accumulating count  
        for (auto& inst : instances) {
            // Recursively call findModules for each instance  
            // inst.module corresponds to inst.getModuleName().str() in CIRCT  
            num += findModules(gLedgers, inst->getModuleName().str(), module);
        }
        return num;
    }
}

template <typename T> std::set<T*> circt::firrtl::moduleInfo::getSrcNode() {
    char* typeName = getTypeName<T>();

    auto it = vPortSrcs.find(typeName); 
    if (it == vPortSrcs.end()) {
        return {}; // 返回空set
    }

    //if(std::string(typeName).find("PortInfo") != std::string::npos){
    //    std::set<T*> results;
    //    for (auto* op : it->second) {
    //        if (std::holds_alternative<PortInfo*>(op->node)) {
    //            if (auto* portInfo = std::get_if<PortInfo>(&(op->node))) {
    //                results.insert(portInfo);
    //            }
    //        }
    //    }
    //    return results;
    //}

    std::set<T*> result;

    //for (auto* op : it->second) {  
        //mlir::Operation* n = op->node;
    //    if (std::holds_alternative<mlir::Operation*>(op->node)) {
    //        mlir::Operation* n = std::get<mlir::Operation*>(op->node);
    //        if(auto* Op = std::get_if<mlir::Operation*>(&(op->node))){
    //            if (auto right_Op = dyn_cast<T>(*Op)) {
    //                 result.insert(right_Op);
    //            }
    //        }
    //    }
    //}

    if constexpr (std::is_same_v<T, PortInfo>) {  
        // 只有当 T 是 PortInfo 时才处理 PortInfo 分支  
        for (auto* op : it->second) {  
            if (std::holds_alternative<PortInfo>(op->node)) {  
                if (auto* portInfo = std::get_if<PortInfo>(&(op->node))) {  
                    result.insert(portInfo);  
                }  
            }  
        }  
    } else {  
        // 处理其他类型（如 MemOp）  
        for (auto* op : it->second) {  
            if (std::holds_alternative<mlir::Operation*>(op->node)) {  
                mlir::Operation* n = std::get<mlir::Operation*>(op->node);  
                if (auto right_Op = dyn_cast<T>(n)) {  
                    result.insert(&right_Op);  
                }  
            }  
        }  
    }

    return result;
}


std::set<std::string> circt::firrtl::moduleInfo::getAllNodes() {
    std::set<std::string> resultNames;
    std::set<std::string> filterKeys = {
        "firrtl.node",      // DefNode
        "firrtl.wire",      // DefWire
        "firrtl.reg",       // DefRegister (covers RegOp and RegResetOp)
        "Port"              // Port
    };

    for (auto& pair : vPortSrcs) {
        if (filterKeys.count((pair.first).str())) {
            for (auto& node : pair.second) {
                resultNames.insert(node->name);
            }
        }
    }
    return resultNames;
}

std::set<std::string> circt::firrtl::moduleInfo::getAllPorts() {
    std::set<std::string> resultNames;
    for (auto& pair : fInstPorts) {
        auto& wdi = pair.first; // circt::firrtl::InstanceOp  
        auto& ports = pair.second; // std::set<std::string>  
        for (auto& p : ports) {
            auto& nonConstWdi = const_cast<circt::firrtl::InstanceOp&>(wdi);
            resultNames.insert((nonConstWdi.getInstanceName().str() + "." + p).str());
        }
    }
    return resultNames;
}

std::set<std::pair<std::string, circt::firrtl::MemOp>> circt::firrtl::moduleInfo::getMemory() {
    std::set<std::pair<std::string, circt::firrtl::MemOp>> result;
    std::set<circt::firrtl::MemOp*> memories = getSrcNode<circt::firrtl::MemOp>();
    for (auto& mem : memories) {
        result.insert(std::make_pair(mName, *mem));
    }
    return result;
}

std::set<std::pair<std::string, circt::firrtl::RegOp>> circt::firrtl::moduleInfo::getRegister() {
    std::set<std::pair<std::string, circt::firrtl::RegOp>> result;
    // getSrcNode<circt::firrtl::RegOp>() will return RegOp, but DefRegister in Scala covers RegOp and RegResetOp  
    // You might need to adjust getSrcNode to return a common base or handle both types.  
    // For simplicity, assuming RegOp here.  
    std::set<circt::firrtl::RegOp*> registers = getSrcNode<circt::firrtl::RegOp>();
    for (auto& reg : registers) {
        result.insert(std::make_pair(mName, *reg));
    }
    return result;
}

int circt::firrtl::moduleInfo::getNumNodes() {
    int portNodesSize = 0;
    auto it = vPortSrcs.find("Port");
    if (it != vPortSrcs.end()) {
        portNodesSize = it->second.size();
    }

    int fInstPortsFlattenSize = 0;
    for (auto& pair : fInstPorts) {
        fInstPortsFlattenSize += pair.second.size();
    }
    return portNodesSize + fInstPortsFlattenSize;
}

void circt::firrtl::moduleInfo::phase0(llvm::SetVector<llvm::StringRef>& ports) {
    auto [ret0, ret1] = gLedger.findPortSrcs(ports);

    // vPortSrcs = ret0 //按类型分类的硬件节点  
    vPortSrcs = ret0;

    // fInstPorts = ret1 //实例化模块的端口映射  
    fInstPorts = ret1;
}

void circt::firrtl::moduleInfo::phase1(llvm::DenseSet<mlir::Value> exprs) {
    auto [ret0, ret1] = gLedger.findExprSrcs(exprs);

    // vPortSrcs = ret0  
    vPortSrcs = ret0;

    // fInstPorts = ret1  
    fInstPorts = ret1;
}

//void circt::firrtl::moduleInfo::phase0(llvm::SetVector<llvm::StringRef>& ports) {  
//    auto [portSrcs, instPorts] = findPortSrcs(ports);  
//    vPortSrcs = std::move(portSrcs);  
//    fInstPorts = std::move(instPorts);  
//  }  
    
  // Phase 1: 处理表达式集合    
//void circt::firrtl::moduleInfo::phase1(const llvm::SetVector<mlir::Value>& exprs) {  
//    auto [portSrcs, instPorts] = findExprSrcs(exprs);  
//    vPortSrcs = std::move(portSrcs);  
//    fInstPorts = std::move(instPorts);  
//}

llvm::SetVector<llvm::StringRef> circt::firrtl::moduleInfo::getPorts(llvm::DenseMap<llvm::StringRef, moduleInfo*>& mInfos) {
    //std::vector<std::string> result;

    // 对应原代码：mInfos.values.filter(_.getSrcNode[WDefInstance].map(_.module).contains(mName))  
    //for (const auto& entry : mInfos) {
    //    const moduleInfo& info = *entry.second;
    //    // 检查这个 moduleInfo 是否有指向当前模块的实例  
    //    if (auto instanceNode = info->getSrcNode<InstanceOp>()) {
    //        if (instanceNode.getModuleName() == mName) {
    //            // 对应原代码：_.fInstPorts.filterKeys(_.module == mName).values.flatten  
    //            auto ports = info.getInstancePortsForModule(mName);
    //            result.insert(result.end(), ports.begin(), ports.end());
    //        }
    //    }
    //}
    llvm::SetVector<moduleInfo*> parents;   
    for (auto& [key, mInfo] : mInfos) {  
        auto srcNodes = mInfo->getSrcNode<InstanceOp>();
        for (auto& instOp : srcNodes){
            //circt::firrtl::InstanceOp& nonConstinstOp;
            //auto instOpPtr = *instOp;
            //auto nonConstinstOp = const_cast<circt::firrtl::InstanceOp*>(instOpPtr);
        if(instOp->getModuleName() == mName){
            parents.insert(mInfo);
            break;}
        }  
    }

    llvm::SetVector<llvm::StringRef> result;  
    for (auto& parent : parents) {  
        for (auto& [instOp, portSet] : parent->fInstPorts) {  
            //auto& nonConstinstOp = const_cast<circt::firrtl::InstanceOp>(*instOp);
            if (const_cast<circt::firrtl::InstanceOp&>(instOp).getModuleName() == mName) {  
                for (auto& port : portSet) {  
                    result.insert(port);  
                }  
            }  
        }  
    }

    return result;
}//listbuffer就用vector算了,加个const因为是有序的

llvm::DenseSet<mlir::Value> circt::firrtl::moduleInfo::getInstCons(llvm::DenseMap<llvm::StringRef, moduleInfo*> mInfos) {
    llvm::DenseSet<mlir::Value> result;

    // 对应原代码：gLedger.getNodes[WDefInstance].map(wdi => (wdi.name, wdi.module)).toMap  
    llvm::DenseMap<StringAttr, StringAttr> instModules;
    for (auto instanceOp : gLedger.getNodes<InstanceOp>()) {
        //instModules[instanceOp->getNameAttr()] = instanceOp->getModuleNameAttr().getAttr().getValue());
        MLIRContext *context = instanceOp->getContext();  
        instModules[instanceOp->getNameAttr()] = StringAttr::get(context, instanceOp->getModuleNameAttr().getAttr().getValue());
    }

    // 对应原代码：instModules.map{ case (k, v) => (k, mInfos(v)) }  
    llvm::DenseMap<StringAttr, moduleInfo*> instMInfos;
    for (auto& [instName, moduleName] : instModules) {
        auto it = mInfos.find(moduleName);
        if (it != mInfos.end()) {
            instMInfos[instName] = it->second;
        }
    }

    // 对应原代码：instPorts = instMInfos.toSeq.flatMap { case (inst, mInfo) => ... }  
    llvm::DenseSet<std::pair<StringAttr, StringAttr>> instPorts;
    for (auto& [instName, mInfo] : instMInfos) {
        // 获取模块的所有端口  
        for (auto portOp : const_cast<circt::firrtl::moduleInfo*>(mInfo)->getSrcNode<PortInfo>()) {
            instPorts.insert(std::make_pair(instName, portOp->name));
        }
    }

    // 对应原代码：gLedger.IP2E.filterKeys(instPorts.contains).values.toSet  
    //for (auto& instPort : instPorts) {
        //if (auto expr = gLedger.getIP2E(instPort)) {
            //result.insert(expr);
        //}
    //}
    //DenseSet<Operation*> result;  
    llvm::DenseSet<std::pair<llvm::StringRef, llvm::StringRef>> stringPairs;  
    for (auto& [first, second] : instPorts) {  
        stringPairs.insert({  
            first.getValue(),  
            second.getValue()  
        });  
    }

    for (auto& [key, value] : gLedger.IP2E()) {  
        if (stringPairs.contains(key)) {  
            result.insert(value);  
        }  
    }
    return result;
}

void circt::firrtl::moduleInfo::printInfo() {
    llvm::outs() << "------------[" << mName << "]------------\n";

    // 对应原代码：vPortSrcs.foreach(tup => println(s"${tup._1}: {${tup._2.map(_.name).mkString(", ")}}"))  

    for (auto& [portName, portSources] : vPortSrcs) {
        llvm::outs() << portName << ": {";
        bool first = true;
        for (auto& _: portSources) {
            if (!first) llvm::outs() << ", ";
            llvm::outs() << portName;
            first = false;
        }
        llvm::outs() << "}\n";
    }

    // 打印空行  
    llvm::outs() << "\n";

    // 对应原代码：fInstPorts.foreach(tup => println(s"[${tup._1.name}] -- {${tup._2.mkString(", ")}}"))  
    for (auto& [instanceOp, portNames] : fInstPorts) {
        llvm::outs() << "[" << const_cast<circt::firrtl::InstanceOp&>(instanceOp).getName() << "] -- {";
        bool first = true;
        for (auto& portName : portNames) {
            if (!first) llvm::outs() << ", ";
            llvm::outs() << portName;
            first = false;
        }
        llvm::outs() << "}\n";
    }

    // 打印结束分隔线  
    llvm::outs() << "-----------------------------------\n";
}

//int circt::firrtl::moduleNet::numNodes = 0;

//circt::firrtl::moduleNet::moduleNet(llvm::SmallVector<std::unique_ptr<netNode>> nodeList)
//    : nodes(std::move(nodeList)) {
    // 构建名称到节点的映射  
//    for (auto& node : nodes) {
//        nodeMap[node->name] = node.get();
//    }
//}

circt::firrtl::moduleNet::moduleNet(std::map<std::string, std::shared_ptr<graphLedger>> gLedgers, std::string topModuleName)
 : roots(), leaves(), numNodes(0)
 {
    //apply在这
        // 对应原代码：nodeInsts = gLedgers.toSeq.map(...)  
    llvm::SmallVector<std::pair<std::unique_ptr<netNode>,
        llvm::SmallVector<InstanceOp*>>> nodeInsts;

    for (auto& [moduleName, ledger] : gLedgers) {
        auto node = std::make_unique<netNode>(moduleName);
        llvm::SmallVector<InstanceOp*> instances;

        // 收集该模块中的所有实例
        auto instanceOps = ledger->template getNodes<InstanceOp>();
        
        if (!instanceOps.empty()) {
            for (auto* instance : instanceOps) {
                instances.push_back(instance);
            }
        }

        nodeInsts.emplace_back(std::move(node), std::move(instances));
    }

    // 对应原代码：设置子节点关系  
    for (auto& [node, instances] : nodeInsts) {
        for (auto* instance : instances) {
            StringAttr targetModuleName = instance->getModuleNameAttr().getAttr();

            // 查找目标模块对应的节点  
            for (auto& [otherNode, _] : nodeInsts) {
                if (otherNode->n == targetModuleName) {
                    node->children.push_back(otherNode.get());
                    break;
                }
            }
        }
    }

    // 对应原代码：设置父节点关系  
    for (auto& [node, _] : nodeInsts) {
        for (auto& [otherNode, _] : nodeInsts) {
            // 检查 otherNode 是否包含指向 node 的子节点  
            for (auto* child : otherNode->children) {
                if (child->n == node->n) {
                    node->parents.push_back(otherNode.get());
                    break;
                }
            }
        }
    }

    // 提取节点并创建 ModuleNet  
    llvm::SmallVector<std::unique_ptr<netNode>> nodes;
    for (auto& [node, _] : nodeInsts) {
        nodes.push_back(std::move(node));
    }

    //return std::make_unique<circt::firrtl::moduleNet>(std::move(nodes));
}

void circt::firrtl::moduleNet::reset() {
    numNodes = nodes.size();

    // 对应原代码：nodes.foreach(_.reset)  
    for (auto& node : nodes) {
        node->reset();
    }

    // 清空之前的根节点和叶节点列表  
    roots.clear();
    leaves.clear();

    // 对应原代码：roots = nodes.filter(_.parents.isEmpty).to[ListBuffer]  
    for (auto& node : nodes) {
        if (node->parents.empty()) {
            roots.push_back(node.get());
        }//nothing 2b scared about. It's just death :)
    }

    // 对应原代码：leaves = nodes.filter(_.childs.isEmpty).to[ListBuffer]  
    for (auto& node : nodes) {
        if (node->children.empty()) {
            leaves.push_back(node.get());
        }
    }
}

std::optional<std::string> circt::firrtl::moduleNet::popT() {
    assert(numNodes >= 0 && "Incorrect moduleNet");

    if (roots.empty()) {
        return std::nullopt;
    }

    // 获取第一个根节点  
    netNode* root = roots[0];
    roots.erase(roots.begin());
    numNodes = numNodes - 1;

    // 标记该节点为已处理  
    //processedNodes.insert(root);

    for (auto* child : root->children) {
    
        //for (auto* parent : child->parents){
        child->parents.erase(std::remove(child->parents.begin(), child->parents.end(), root),
        child->parents.end());
        //}
        if (child->parents.empty()){roots.push_back(child);}

    }

    return std::make_optional(root->n);
}

std::optional<std::string> circt::firrtl::moduleNet::popB() {
    assert(numNodes >= 0 && "Incorrect moduleNet");

    if (leaves.empty()) {
        return std::nullopt;
    }

    //  ^n  ^o^v     ^`       ^j^b ^b
    netNode* leaf = leaves[0];
    leaves.erase(leaves.begin());
    numNodes = numNodes - 1;

    //   ^g       ^j^b ^b         ^d ^p^f
    //processedNodes.insert(root);

    for (auto* parent : leaf->parents) {

        //for (auto* parent : parent->children){
        parent->children.erase(std::remove(parent->children.begin(), parent->children.end(), leaf),
        parent->children.end());
        //}
        if (parent->children.empty()){leaves.push_back(parent);}

    }

    return std::make_optional(leaf->n);
}

void circt::firrtl::moduleNet::printNet(bool dir) {
    reset();

    // 对应原代码：val pop = () => if (dir) popT else popB  
    auto pop = [this, dir]() -> std::optional<StringRef> {
        return dir ? popT() : popB();
        };

    // 对应原代码：var done = false; while (!done) { ... }  
    bool done = false;
    while (!done) {
        // 对应原代码：pop() match { case Some(nName) => ... case None => ... }  
        if (auto nName = pop()) {
            // 对应原代码：println(s"$nName")  
            llvm::outs() << nName.value() << "\n";
        }
        else {
            // 对应原代码：done = true  
            done = true;
        }
    }
}

//mutable bool circt::firrtl::netNode::initialized = false;

void circt::firrtl::netNode::ensureInitialized() {
    if (!initialized) {
        immParents = parents;
        immChildren = children;
        initialized = true;
    }
}

circt::firrtl::netNode::netNode(llvm::StringRef n,
    llvm::ArrayRef<netNode*> parentNodes,
    llvm::ArrayRef<netNode*> childNodes)
    : n(n.str()), initialized(false),
    parents(parentNodes.begin(), parentNodes.end()),
    children(childNodes.begin(), childNodes.end()) {}

// 重置方法 - 对应 Scala 的 reset 方法  
void circt::firrtl::netNode::reset() {
    ensureInitialized();
    parents = immParents;
    children = immChildren;
}

// 访问器方法  
llvm::StringRef circt::firrtl::netNode::getName() { return n; }
llvm::ArrayRef<netNode*> circt::firrtl::netNode::getParents() const { return parents; }
llvm::ArrayRef<netNode*> circt::firrtl::netNode::getChildren() const { return children; }

// 修改器方法  
llvm::MutableArrayRef<netNode*> circt::firrtl::netNode::getParents() { return parents; }
llvm::MutableArrayRef<netNode*> circt::firrtl::netNode::getChildren() { return children; }
