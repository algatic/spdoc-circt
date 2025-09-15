#include <tuple>
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"  
#include "mlir/IR/Location.h"  
#include "llvm/ADT/DenseMap.h"  
#include "llvm/ADT/StringRef.h"  
#include <map>  
#include <set>  
#include <vector>  
#include <string>  
#include <algorithm> 
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
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLInstanceGraph.h"
#include <iostream>
#include "circt/Dialect/FSM/FSMOps.h"
//#define GEN_PASS_DEF_SPDOCINSTRPASS
//#include "circt/Dialect/FIRRTL/Passes.h.inc"

using namespace circt;
using namespace firrtl;

using StringSet = std::set<std::string>;
using StringToStringSetMap = std::map<std::string, std::set<std::string>>;
using StringAndStringSetMap = std::pair<std::string, std::set<std::pair<std::string, std::set<std::string>>>>;
using ComplexMap = std::map<std::string, StringAndStringSetMap>; //for getInstanceMap

using StringToNodeSetMap = std::map<std::string, std::set<Node*>>;
using WDefInstanceToStringSetMap = std::map<InstanceOp*, std::set<std::string>>;
using ComplexTuple = std::pair<StringToNodeSetMap, WDefInstanceToStringSetMap>; //for findPortSrcs

//using StringToNodeSetMap = DenseMap<StringAttr, DenseSet<Node*>>;
//using WDefInstanceToStringSetMap = DenseMap<InstanceOp*, DenseSet<StringAttr>>;
using ComplexTuple_connected = std::pair<StringToNodeSetMap, WDefInstanceToStringSetMap>;//for findOutPortsConnected

//static const std::set<std::string> circt::firrtl::Node::types = {
//        "firrtl.wire",      // DefWire  
//        "firrtl.reg",       // DefRegister    
//        "firrtl.regreset",  // DefRegister with reset  
//        "firrtl.node",      // DefNode  
//        "firrtl.mem",       // DefMemory  
//        "firrtl.instance"   // WDefInstance  
//};
//std::string circt::firrtl::Node::name = "";

const std::set<llvm::StringRef>& Node::getTypes() {
  // 延迟初始化：首次调用时构造，确保线程安全（C++11 起保证）
  static const std::set<llvm::StringRef> types = {
    "firrtl.wire",      // DefWire
    "firrtl.reg",       // DefRegister
    "firrtl.regreset",  // DefRegister with reset
    "firrtl.node",      // DefNode
    "firrtl.mem",       // DefMemory
    "firrtl.instance"   // WDefInstance
  };
  return types;
}

circt::firrtl::Node::Node(mlir::Operation* node, const std::string name)
    : node(node), name(name) {
    // 可添加其他初始化逻辑
}

circt::firrtl::Node::Node(PortInfo node, const std::string name)
    : node(node), name(name) {
    //  ^o     ^j  ^e   ^v ^h^}  ^k ^l^v ^`   ^q
}

std::string serializePortInfo(const circt::firrtl::PortInfo& portInfo) {  
    std::string result = portInfo.getName().str();  
    std::string typeStr;  
    llvm::raw_string_ostream typeStream(typeStr);  
    portInfo.type.print(typeStream);  
    result += " : " + typeStr;
    //result += " : " + portInfo.type.toString();  
    result += std::string(" (") + (portInfo.direction == Direction::In ? "input" : "output") + std::string(")");  
    return result;  
}

std::string circt::firrtl::Node::serialize() const {
    if (std::holds_alternative<mlir::Operation*>(node) && !std::get<mlir::Operation*>(node)) return "";

    std::string result;
    llvm::raw_string_ostream stream(result);
    //node->print(stream);
    if (std::holds_alternative<mlir::Operation*>(node)) {  
        std::get<mlir::Operation*>(node)->print(stream);  
    } else if (std::holds_alternative<circt::firrtl::PortInfo>(node)) {  
        serializePortInfo(std::get<circt::firrtl::PortInfo>(node));
    }
    return result;
}
template <typename T > T circt::firrtl::Node::get() {
    //static_assert(std::is_base_of<mlir::Operation*, T>::value,
        //"T must be a subclass of FirrtlNode");
    Operation* op = std::get<Operation*>(node);
    if (auto result = dyn_cast<T>(op)) {
        return result;
    }//“你想做1啊？”我笑死了谁能像我看刘小别哑然失笑一样来欣赏我这句如何意味深长（所以后面得做吧，i reckon，得来一下证明一下吧
    else {
        std::string serialized = serialize();  // Use the serialize method from previous conversion  
        llvm::errs() << serialized << " mismatch";
    }
}
std::string circt::firrtl::Node::c() {
    if (std::holds_alternative<mlir::Operation*>(node) && !std::get<mlir::Operation*>(node)) return "Unknown"; //这里是否应该直接报错啊？mark-1
    //return node->getName().getStringRef().str();
    if (std::holds_alternative<mlir::Operation*>(node)) {  
        return std::get<mlir::Operation*>(node)->getName().getStringRef().str();  
    } else if (std::holds_alternative<circt::firrtl::PortInfo>(node)) {  
        return std::get<circt::firrtl::PortInfo>(node).getName().str();  
    }
}
bool circt::firrtl::Node::usedIn(Value expr, bool imp) {
    auto* defOp = expr.getDefiningOp();
    if (!defOp) {
        // 对于模块端口，获取端口名称进行比较  
        if (auto blockArg = dyn_cast<BlockArgument>(expr)) {
            auto module = cast<FModuleLike>(blockArg.getParentBlock()->getParentOp());
            return module.getPortName(blockArg.getArgNumber()).str() == name;
        }
        return false;
    }

    // 对于命名操作，比较名称  
    if (auto wireOp = dyn_cast<WireOp>(defOp)) {
        return wireOp.getName() == name;
    }
    else if (auto regOp = dyn_cast<RegOp>(defOp)) {
        return regOp.getName() == name;
    }
    else if (auto nodeOp = dyn_cast<NodeOp>(defOp)) {
        return nodeOp.getName() == name;
    }
    else if (auto subfieldOp = dyn_cast<SubfieldOp>(defOp)) {
        return usedIn(subfieldOp.getInput(), imp);
    }
    else if (auto subindexOp = dyn_cast<SubindexOp>(defOp)) {
        return usedIn(subindexOp.getInput(), imp);
    }


    else if (auto subaccessOp = dyn_cast<SubaccessOp>(defOp)) {
        return usedIn(subaccessOp.getInput(), imp);
    }
    else if (auto openSubfieldOp = dyn_cast<OpenSubfieldOp>(defOp)) {
        return usedIn(openSubfieldOp.getInput(), imp);
    }
    else if (auto openSubindexOp = dyn_cast<OpenSubindexOp>(defOp)) {
        return usedIn(openSubindexOp.getInput(), imp);
    }
    else if (auto muxOp = dyn_cast<MuxPrimOp>(defOp)) {
        if (imp) {
            return usedIn(muxOp.getSel(), imp) ||
                usedIn(muxOp.getHigh(), imp) ||
                usedIn(muxOp.getLow(), imp);
        }
        else {
            return usedIn(muxOp.getHigh(), imp) ||
                usedIn(muxOp.getLow(), imp);
        }
    }
    else if (isa<AddPrimOp, SubPrimOp, MulPrimOp, DivPrimOp, RemPrimOp,
        AndPrimOp, OrPrimOp, XorPrimOp, NotPrimOp,
        EQPrimOp, NEQPrimOp, LTPrimOp, LEQPrimOp, GTPrimOp, GEQPrimOp,
        CatPrimOp, DShlPrimOp, DShrPrimOp, ShlPrimOp, ShrPrimOp,
        BitsPrimOp, HeadPrimOp, TailPrimOp, PadPrimOp>(defOp)) {
        // Handle primitive operations (DoPrim equivalent)  
        for (auto operand : defOp->getOperands()) {
            if (usedIn(operand, imp)) {
                return true;
            }
        }
        return false;
    }

    return false;
}
std::set<std::pair<std::string, std::vector<std::string>>> circt::firrtl::Node::portIn(Value expr) {
    auto portInfo = _getPort(expr);  // Equivalent to _getPort(expr)  

    // Transform to only include (name, _) pairs  
    std::set<std::pair<std::string, std::vector<std::string>>> ret;
    for (const auto& seq : portInfo) {
        //ret.insert(info.first, info.second);
        ret.emplace(seq[0], std::vector<std::string>(seq.begin() + 1, seq.end()));
    }

    // Connected WDefInstance should have at least one connected port  
    if (ret.empty()) {
        std::string serialized;
        llvm::raw_string_ostream stream(serialized);
        expr.print(stream);
        llvm::errs() << "Does not have port: [" << serialized << "]";
    }

    return ret;
}

std::set<std::vector<std::string>> circt::firrtl::Node::_getPort(Value expr) {
    auto* defOp = expr.getDefiningOp();
    if (!defOp) {
        return std::set<std::vector<std::string>>();
    }

    // Handle WSubField equivalent - SubfieldOp  
    if (auto subfieldOp = dyn_cast<SubfieldOp>(defOp)) {
        Value input = subfieldOp.getInput();
        std::string fieldName = subfieldOp.getFieldName().str();

        auto* inputDefOp = input.getDefiningOp();
        if (inputDefOp) {
            // Check if input is a reference to our target node  
            if (auto wireOp = dyn_cast<WireOp>(inputDefOp)) {
                if (wireOp.getName() == name) {
                    return { std::vector<std::string>{fieldName} };
                }
            }
            else if (auto regOp = dyn_cast<RegOp>(inputDefOp)) {
                if (regOp.getName() == name) {
                    return { std::vector<std::string>{fieldName} };
                }
            }
            else if (auto nodeOp = dyn_cast<NodeOp>(inputDefOp)) {
                if (nodeOp.getName() == name) {
                    return { std::vector<std::string>{fieldName} };
                }
            }
        }

        // Recursively get ports from input and append field name  
        auto inputPorts = _getPort(input);
        std::set<std::vector<std::string>> result;
        for (const auto& port : inputPorts) {
            std::vector<std::string> newPort = port;
            newPort.push_back(fieldName);
            result.insert(newPort);
        }
        return result;
    }

    // Handle WSubAccess equivalent - SubaccessOp  
    else if (auto subaccessOp = dyn_cast<SubaccessOp>(defOp)) {
        Value input = subaccessOp.getInput();

        auto* inputDefOp = input.getDefiningOp();
        if (inputDefOp) {
            // Check if input is a reference to our target node  
            if (auto wireOp = dyn_cast<WireOp>(inputDefOp)) {
                if (wireOp.getName() == name) {
                    std::string serialized;
                    llvm::raw_string_ostream stream(serialized);
                    expr.print(stream);
                    llvm::errs() << "_getPort: " << serialized;
                }
            }
            // Similar checks for other named operations...  
        }

        return _getPort(input);
    }

    // Handle WSubIndex equivalent - SubindexOp  
    else if (auto subindexOp = dyn_cast<SubindexOp>(defOp)) {
        return _getPort(subindexOp.getInput());
    }

    // Handle Mux equivalent - MuxPrimOp  
    else if (auto muxOp = dyn_cast<MuxPrimOp>(defOp)) {
        auto condPorts = _getPort(muxOp.getSel());
        auto tvalPorts = _getPort(muxOp.getHigh());
        auto fvalPorts = _getPort(muxOp.getLow());

        std::set<std::vector<std::string>> result;
        result.insert(condPorts.begin(), condPorts.end());
        result.insert(tvalPorts.begin(), tvalPorts.end());
        result.insert(fvalPorts.begin(), fvalPorts.end());
        return result;
    }

    // Handle DoPrim equivalent - various primitive operations  
    else if (isa<AddPrimOp, SubPrimOp, MulPrimOp, DivPrimOp, RemPrimOp,
        AndPrimOp, OrPrimOp, XorPrimOp, NotPrimOp,
        EQPrimOp, NEQPrimOp, LTPrimOp, LEQPrimOp, GTPrimOp, GEQPrimOp,
        CatPrimOp, DShlPrimOp, DShrPrimOp, ShlPrimOp, ShrPrimOp,
        BitsPrimOp, HeadPrimOp, TailPrimOp, PadPrimOp>(defOp)) {
        std::set<std::vector<std::string>> result;
        for (auto operand : defOp->getOperands()) {
            auto operandPorts = _getPort(operand);
            result.insert(operandPorts.begin(), operandPorts.end());
        }
        return result;
    }

    // Default case - return empty set  
    return std::set<std::vector<std::string>>();
}

Node circt::firrtl::Node::create(PortInfo portInfo){
    if(!portInfo.name) {
        llvm::errs() << "Node is null";
    }
    return Node(portInfo, portInfo.getName().str());
}

Node circt::firrtl::Node::create(mlir::Operation* node) {
    if (!node) {
        llvm::errs() << "Node is null";
    }

    // Check if the operation type is supported  
    std::string operationName = node->getName().getStringRef().str();
    if (Node::getTypes().find(operationName) == Node::getTypes().end()) {
        std::string serialized;
        llvm::raw_string_ostream stream(serialized);
        node->print(stream);
        llvm::errs() << serialized << " is not an instance of Port/DefStatement";
    }

    // Extract name based on operation type  
    std::string name;
    if (auto blockArg = dyn_cast<BlockArgument>(node->getResult(0))) {
        // Handle Port case - module port  
        auto module = cast<FModuleLike>(blockArg.getParentBlock()->getParentOp());
        name = module.getPortName(blockArg.getArgNumber()).str();
    }
    else if (auto wireOp = dyn_cast<WireOp>(node)) {
        name = wireOp.getName().str();
    }
    else if (auto regOp = dyn_cast<RegOp>(node)) {
        name = regOp.getName().str();
    }
    else if (auto regResetOp = dyn_cast<RegResetOp>(node)) {
        name = regResetOp.getName().str();
    }
    else if (auto nodeOp = dyn_cast<NodeOp>(node)) {
        name = nodeOp.getName().str();
    }
    else if (auto memOp = dyn_cast<MemOp>(node)) {
        name = memOp.getName().str();
    }
    else if (auto instOp = dyn_cast<InstanceOp>(node)) {
        name = instOp.getName().str();
    }
    else {
        std::string serialized;
        llvm::raw_string_ostream stream(serialized);
        node->print(stream);
        llvm::errs() << serialized << " does not have name";
    }

    return Node(node, name);
}

//Node operator()(mlir::Operation* node) {
//    return apply(node);
//}



bool circt::firrtl::Node::hasType(mlir::Operation* n) {
    if (!n) return false;

    llvm::StringRef operationName = n->getName().getStringRef();

    return Node::getTypes().find(operationName) != Node::getTypes().end();
}

bool circt::firrtl::Node::hasType(PortInfo portInfo) {
    if (portInfo.name.empty() || !portInfo.type) return false;

    //std::string operationName = n->getName().getStringRef().str();
    //return n->getName().getStringRef().contains(operationName);
    return true;
}

llvm::StringRef circt::firrtl::Node::findName(mlir::Value expr) {
    auto* defOp = expr.getDefiningOp();
    if (!defOp) {
        // This is a block argument (module port)  
        if (auto blockArg = dyn_cast<BlockArgument>(expr)) {
            auto module = cast<FModuleLike>(blockArg.getParentBlock()->getParentOp());
            return module.getPortName(blockArg.getArgNumber());
        }
        llvm::errs() << "Value does not have a defining operation";
    }

    // Handle different FIRRTL operations  
    if (auto wireOp = dyn_cast<WireOp>(defOp)) {
        return wireOp.getName();
    }
    else if (auto regOp = dyn_cast<RegOp>(defOp)) {
        return regOp.getName();
    }
    else if (auto regResetOp = dyn_cast<RegResetOp>(defOp)) {
        return regResetOp.getName();
    }
    else if (auto nodeOp = dyn_cast<NodeOp>(defOp)) {
        return nodeOp.getName();
    }
    else if (auto subfieldOp = dyn_cast<SubfieldOp>(defOp)) {
        return findName(subfieldOp.getInput());
    }
    else if (auto subindexOp = dyn_cast<SubindexOp>(defOp)) {
        return findName(subindexOp.getInput());
    }
    else if (auto subaccessOp = dyn_cast<SubaccessOp>(defOp)) {
        return findName(subaccessOp.getInput());
    }
    else if (auto openSubfieldOp = dyn_cast<OpenSubfieldOp>(defOp)) {
        return findName(openSubfieldOp.getInput());
    }
    else if (auto openSubindexOp = dyn_cast<OpenSubindexOp>(defOp)) {
        return findName(openSubindexOp.getInput());
    }
    else {
        // For operations like Mux, primitives, etc.  
        llvm::errs() << "Expression does not have a statement name";
    }
}
llvm::SetVector<llvm::StringRef> circt::firrtl::Node::findNames(Value expr) {
    auto* defOp = expr.getDefiningOp();
    if (!defOp) {
        // This is a block argument (module port)  
        if (auto blockArg = dyn_cast<BlockArgument>(expr)) {
            auto module = cast<FModuleLike>(blockArg.getParentBlock()->getParentOp());
            //return { module.getPortName(blockArg.getArgNumber()) };
            llvm::SetVector<llvm::StringRef> result;  
            result.insert(module.getPortName(blockArg.getArgNumber()));  
            return result;
        }
        return llvm::SetVector<llvm::StringRef>();
    }

    // Handle different FIRRTL operations  
    if (auto wireOp = dyn_cast<WireOp>(defOp)) {
        //return { wireOp.getName() };
        llvm::SetVector<llvm::StringRef> result;  
        result.insert(wireOp.getName());  
        return result;
    }
    else if (auto regOp = dyn_cast<RegOp>(defOp)) {
        //return { regOp.getName() };
        llvm::SetVector<llvm::StringRef> result;  
        result.insert(regOp.getName());  
        return result;
    }
    else if (auto regResetOp = dyn_cast<RegResetOp>(defOp)) {
        //return { regResetOp.getName() };
        llvm::SetVector<llvm::StringRef> result;  
        result.insert(regResetOp.getName());  
        return result;
    }
    else if (auto nodeOp = dyn_cast<NodeOp>(defOp)) {
        //return { nodeOp.getName() };
        llvm::SetVector<llvm::StringRef> result;  
        result.insert(nodeOp.getName());  
        return result;
    }
    else if (auto subfieldOp = dyn_cast<SubfieldOp>(defOp)) {
        return findNames(subfieldOp.getInput());
    }
    else if (auto subindexOp = dyn_cast<SubindexOp>(defOp)) {
        return findNames(subindexOp.getInput());
    }
    else if (auto subaccessOp = dyn_cast<SubaccessOp>(defOp)) {
        return findNames(subaccessOp.getInput());
    }
    else if (auto openSubfieldOp = dyn_cast<OpenSubfieldOp>(defOp)) {
        return findNames(openSubfieldOp.getInput());
    }
    else if (auto openSubindexOp = dyn_cast<OpenSubindexOp>(defOp)) {
        return findNames(openSubindexOp.getInput());
    }
    else if (auto muxOp = dyn_cast<MuxPrimOp>(defOp)) {
        // Handle Mux: combine names from both true and false values  
        auto tvalNames = findNames(muxOp.getHigh());
        auto fvalNames = findNames(muxOp.getLow());

        llvm::SetVector<llvm::StringRef> result;
        result.insert(tvalNames.begin(), tvalNames.end());
        result.insert(fvalNames.begin(), fvalNames.end());
        return result;
    }
    else if (isa<AddPrimOp, SubPrimOp, MulPrimOp, DivPrimOp, RemPrimOp,
        AndPrimOp, OrPrimOp, XorPrimOp, NotPrimOp,
        EQPrimOp, NEQPrimOp, LTPrimOp, LEQPrimOp, GTPrimOp, GEQPrimOp,
        CatPrimOp, DShlPrimOp, DShrPrimOp, ShlPrimOp, ShrPrimOp,
        BitsPrimOp, HeadPrimOp, TailPrimOp, PadPrimOp>(defOp)) {
        // Handle DoPrim equivalent: collect names from all operands  
        llvm::SetVector<llvm::StringRef> result;
        for (auto operand : defOp->getOperands()) {
            auto operandNames = findNames(operand);
            result.insert(operandNames.begin(), operandNames.end());
        }
        return result;
    }

    // Default case - return empty set  
    return llvm::SetVector<llvm::StringRef>();
}

void circt::firrtl::graphLedger::findNode(mlir::Operation* s) {
    if(Node::hasType(s)){
        auto n = Node::create(s);
        Nodes[n.name] = n;
        G[n.name] = llvm::SmallVector<std::string>();
    }
    s->walk([&](Operation *op){
        findNode(op);
    });
}

void circt::firrtl::graphLedger::findNode(PortInfo portInfo){
    if(Node::hasType(portInfo)){
        auto n = Node::create(portInfo);
        Nodes[n.name] = n;
        G[n.name] = llvm::SmallVector<std::string>();
    }
}
//statement这个类就是没有代餐的，因为各个不同语句都被实现成各个op了，并非统一继承自某一个statement类（本身大家实现的方式就不太一样）。

void circt::firrtl::graphLedger::findEdgeExp(Node n, SmallVector<std::string>& sinks, mlir::Operation* stmt) {
    if (auto regOp = dyn_cast<RegResetOp>(stmt)) {
        if (n.usedIn(regOp.getResetSignal(), false)) {
            sinks.push_back(regOp.getName().str());

        }
    }
    else if (auto nodeOp = dyn_cast<NodeOp>(stmt)) {
        if (n.usedIn(nodeOp.getInput(), false)) {
            sinks.push_back(nodeOp.getName().str());
        }
    }
    else if (auto connectOp = dyn_cast<ConnectOp>(stmt)) {
        if (n.usedIn(connectOp.getSrc(), false)) {
            sinks.push_back(Node::findName(connectOp.getDest()).str());
        }
    }

    // Recursively traverse nested statements  
    stmt->walk([&](Operation* nestedOp) {
        if (nestedOp != stmt) {
            findEdgeExp(n, sinks, nestedOp);
        }
    });
} //这个是那个firrtlnode的替代！这个typege

void circt::firrtl::graphLedger::buildG() {
    // 遍历模块中的端口和语句，查找节点
    //module->foreachPort([this](std::shared_ptr<mlir::Operation*> node) { findNode(node); });
    for (auto& portInfo : module.getPorts()) {  
        //auto portInfo = module.getPorts(i);  
        findNode(portInfo);  
    }
    //module->foreachStmt([this](std::shared_ptr<mlir::Operation*> node) { findNode(node); });
    module->walk([&](mlir::Operation *op){
        findNode(op);
    });
    //o，不用害怕，是我的四个号（。
    // 根据节点构建图
    for (const auto& n : G) {
        SmallVector<std::string> sinks;
        //module->foreachStmt([this, &sinks, &n](std::shared_ptr<mlir::Operation*> stmt) {//[]里面是当前环境要提供的，括号里是方法本身要提供的
        module->walk([&](Operation *op){
            findEdge(Nodes[n.first], sinks, op);
            });
        G[n.first] = SmallVector<std::string>(sinks.begin(), sinks.end());

        expG[n.first] = SmallVector<std::string>();
    }

    // 处理表达式图
    for (const auto& n : expG) {
        SmallVector<std::string> sinks;
        //module->forEachStmt([this, &sinks, &n](std::shared_ptr<mlir::Operation*> stmt) {
        module->walk([&](Operation *op){
            findEdgeExp(Nodes[n.first], sinks, op);
            });
        expG[n.first] = SmallVector<std::string>(sinks.begin(), sinks.end());
    }
}

void circt::firrtl::graphLedger::reverseG() {
    // 反向构建图
    for (const auto& n : G) {
        SmallVector<std::string> sources;
        for (const auto& m : G) {
            if (std::find(m.second.begin(), m.second.end(), n.first) != m.second.end()) {
                sources.push_back(m.first);
            }
        }
        R[n.first] = SmallVector<std::string>(sources.begin(), sources.end());
    }//感觉腿好酸。也好想一直跑着。跑呀跑呀，跑到烧尽我自己的整个身体为尽头。啊！！
}

//node的替代是InstanceGraphNode
void circt::firrtl::graphLedger::findEdge(circt::firrtl::Node n, SmallVector<std::string>& sinks, mlir::Operation* s) {
    // Handle different FIRRTL operations  
    if (auto regOp = dyn_cast<RegResetOp>(s)) {
        if (n.usedIn(regOp.getResetSignal())) {
            sinks.push_back(regOp.getName().str());
        }
    }
    else if (auto nodeOp = dyn_cast<NodeOp>(s)) {
        if (n.usedIn(nodeOp.getInput())) {
            StringRef nodeName = nodeOp.getName();
            sinks.push_back(nodeName.str());

            // Call your update functions here  
            updateN2XP(nodeName.str(), nodeOp.getInput(), n);
        }
        // Always call updateN2E for nodes  
        updateN2E(nodeOp.getName().str(), nodeOp.getInput());

    }
    else if (auto connectOp = dyn_cast<ConnectOp>(s)) {
        Value dest = connectOp.getDest();
        Value src = connectOp.getSrc();
        StringRef lName = Node::findName(dest);  // Use the findName function from previous conversion  

        if (n.usedIn(src)) {
            sinks.push_back(lName.str());

            // Call your update functions here  
            updateN2XP(lName.str(), src, n);
            updateXP2X(lName.str(), dest, src, n);
            // updateP2E(lName, src);  // Commented out as in original  
        }
        // Always call updateN2E for connections  
        updateN2E(lName.str(), src);
    }
    // Handle other cases like Port, DefWire, DefMemory, WDefInstance as needed  

    // Recursively traverse nested statements  
    s->walk([&](Operation* nestedOp) {
        if (nestedOp != s) {
            findEdge(n, sinks, nestedOp);
        }
        });
} //这个是那个firrtlnode的替代！这个type

void circt::firrtl::graphLedger::updateN2XP(const std::string& sink, mlir::Value srcE, Node& node) {
    std::string nodeClass = node.name;

    if (nodeClass == "firrtl.instance") {
        // Get existing set or create empty one  
        auto& currentSet = rN2IP[sink];

        // Get port information and transform it  
        auto portInfo = node.portIn(srcE);
        for (const auto& info : portInfo) {
            if (!info.second.empty()) {
                currentSet.insert({ info.first, info.second[0] });
            }
        }
    }
    else if (nodeClass == "firrtl.mem") {
        // Get existing set or create empty one    
        auto& currentSet = rN2MP[sink];

        // Get port information and transform it  
        auto portInfo = node.portIn(srcE);
        for (const auto& info : portInfo) {
            if (!info.second.empty()) {
                currentSet.insert({ info.first, info.second[0] });
            }
        }
    }
    // Default case - do nothing (equivalent to Unit in Scala)  
}

void circt::firrtl::graphLedger::updateXP2X(const std::string& sink, mlir::Value sinkE, mlir::Value srcE, const Node& node) {
    bool inInstances = std::any_of(instances.begin(), instances.end(),
        [&sink](auto& inst) { return inst.getName() == sink; });
    bool inMemorys = std::any_of(memorys.begin(), memorys.end(),
        [&sink](auto& mem) { return mem.getName() == sink; });

    if (inInstances && inMemorys) {
        llvm::errs() << sink << " contains in both instances and memorys";
    }
    else if (inInstances && !inMemorys) {
        // Only one port is connected at once  
        auto portInfo = Nodes[sink].portIn(sinkE);
        if (!portInfo.empty()) {
            auto IP = *portInfo.begin();
            if (!IP.second.empty()) {
                std::pair<std::string, std::string> key = { IP.first, IP.second[0] };
                rIP2E[key] = srcE;
            }
        }
    }
    else if (!inInstances && inMemorys) {
        // Only one port is connected at once  
        auto portInfo = Nodes[sink].portIn(sinkE);
        if (!portInfo.empty()) {
            auto MPF = *portInfo.begin();
            if (!MPF.second.empty()) {
                std::pair<std::string, std::string> MP = { MPF.first, MPF.second[0] };

                // Get existing map or create empty one  
                auto& currentMap = rMP2N[MP];
                if (!MPF.second.empty()) {
                    currentMap[MPF.second.back()] = node.name;
                }
            }
        }
    }
    // Default case - do nothing (equivalent to Unit in Scala)  
}

void circt::firrtl::graphLedger::updateN2E(std::string sink, mlir::Value srcE) {
    // Check if sink is an instance or memory - if so, return early  
    std::set<std::string> skipTypes = { "firrtl.instance", "firrtl.mem" };
    if (skipTypes.find(Nodes[sink].name) != skipTypes.end()) {
        return;
    }

    // Check if sink already exists in N2E mapping - if so, return early  
    if (N2E.find(sink) != N2E.end()) {
        return;
    }

    // Add the mapping  
    N2E[sink] = srcE;
}

ComplexMap circt::firrtl::graphLedger::getInstanceMap() {
    std::vector<std::tuple<Node, std::string, std::set<std::string>>> I2IP;
    

    for (const auto& instance : instances) {
        visited.clear();
        llvm::SetVector<llvm::StringRef> instanceSet;
        instanceSet.insert(const_cast<InstanceOp&>(instance).getName());
        // Get all upstream nodes and their ports - equivalent to findNodeSrcs  
        auto [srcs, ips] = findNodeSrcs(instanceSet);

        // Extract WDefInstance nodes and their port information  
        if (srcs.find("firrtl.instance") != srcs.end()) {
            for (const auto& x : srcs["firrtl.instance"]) {
                auto instanceOp = x->template get<InstanceOp>();
                //if(instanceOp.getNumResults() > 0){
                    //auto ports = ips[instanceOp.getResult(0)];
                    //std::set<str::string> convertedPorts;  
                    //for (size_t i = 0; i < instanceOp.getNumResults(); ++i) {  
                        //auto port = instanceOp.getResult(i);  
                        //convertedPorts.insert(port);  
                    //}
                  auto ports = ips[instanceOp];
                  std::set<std::string> convertedPorts;  
                  for (const auto& port : ports) {  
                      convertedPorts.insert(port.str());  
                  }
                  I2IP.push_back(std::make_tuple(*x, const_cast<InstanceOp&>(instance).getName().str(), convertedPorts));
                
            }
        }
    }

    // Build the final mapping - equivalent to the second part  
    std::map<std::string, std::pair<std::string, std::set<std::pair<std::string, std::set<std::string>>>>> result;

    for (const auto& instance : instances) {
        std::set<std::pair<std::string, std::set<std::string>>> sink_srcPs;

        for (const auto& [srcNode, sink, ports] : I2IP) {
            //auto& nodeElement = std::get<0>(srcNode); 
            //auto& nodeElement = std::get<0>(static_cast<const std::tuple<Node, std::string, std::set<std::string>>&>(srcNode));
            //auto srcInstance = nodeElement->template get<InstanceOp>();
            auto& [nodeElement, nodeName] = srcNode;
            if (auto* op = std::get_if<mlir::Operation*>(&nodeElement)){
                auto srcInstance = dyn_cast<InstanceOp>(*op); 
                if (srcInstance == instance) {
            // Filter ports based on conditions 
                    std::set<std::string> filteredPorts;
                    for (const auto& port : ports) {
                        if (port.find("valid") != std::string::npos ||
                            (nodeName == "frontend" && port.find("io_imem_req_bits_addr") != std::string::npos)) {
                            filteredPorts.insert(port);
                        }
                    }
                    if (!filteredPorts.empty()) {
                        sink_srcPs.insert({ sink, filteredPorts });
                    }
                }
            }
        }

        // Get module name from instance  
        result[const_cast<InstanceOp&>(instance).getName().str()] = std::make_pair(  
            const_cast<InstanceOp&>(instance).getModuleName().str(),   
            sink_srcPs  
        );
    }

    return result;
}

std::pair<std::map<llvm::StringRef, llvm::SetVector<Node*>>, std::map<InstanceOp, llvm::SetVector<llvm::StringRef>>> circt::firrtl::graphLedger::findPortSrcs(llvm::SetVector<llvm::StringRef>& ports) {
    llvm::SetVector<llvm::StringRef> modulePortNames;
    for (auto& port : mPorts) {
        modulePortNames.insert(port.getName());
    }

    // Check that all requested ports exist in the module  
    llvm::SetVector<llvm::StringRef> missingPorts;
    //std::set_difference(ports.begin(), ports.end(),
        //modulePortNames.begin(), modulePortNames.end(),
        //std::inserter(missingPorts, missingPorts.begin()));
    for (const auto& item : ports) {  
    if (modulePortNames.count(item) == 0) {  
        missingPorts.insert(item.str());  
        }  
    }

    if (!missingPorts.empty()) {
        std::string errorMsg = mName + " does not have ports: {";
        bool first = true;
        for (auto& port : missingPorts) {
            if (!first) errorMsg += ", ";
            errorMsg += port.str();
            first = false;
        }
        errorMsg += "}";
        llvm::errs() << errorMsg;
    }

    // Filter to only output ports  
    llvm::SetVector<llvm::StringRef> oPorts;
    for (auto& portName : ports) {
        // Find the corresponding port in mPorts and check if it's an output  
        for (auto& port : mPorts) {
            if (port.getName().str() == portName &&
                port.direction == Direction::Out) {
                oPorts.insert(portName);
                break;
            }
        }
    }

    return findNodeSrcs(oPorts);
}

std::pair<std::map<llvm::StringRef, llvm::SetVector<Node*>>, std::map<InstanceOp, llvm::SetVector<llvm::StringRef>>> circt::firrtl::graphLedger::findExprSrcs(llvm::DenseSet<mlir::Value> exprs) {
    llvm::DenseSet<llvm::StringRef> exprNodes;
    for (auto& expr : exprs) {
        auto* defOp = expr.getDefiningOp();
        if (defOp && circt::firrtl::isExpression(defOp)) {
            auto names = Node::findNames(expr);
            exprNodes.insert(names.begin(), names.end());
        }
        //else if (!defOp) {
            // 处理块参数（模块端口）  
            //if (auto blockArg = dyn_cast<BlockArgument>(expr)) {
                //auto module = cast<FModuleLike>(blockArg.getParentBlock()->getParentOp());
                //exprNodes.insert(module.getPortName(blockArg.getArgNumber()).str());
            //}
        //}
    }

    // Select exact (input) ports  
    llvm::SetVector<llvm::StringRef> inPortNames;
    for (auto& port : mPorts) {
        if (port.direction == Direction::In) {
            inPortNames.insert(port.getName());
        }
    }

    llvm::SetVector<llvm::StringRef> inputs;
    //std::set_intersection(exprNodes.begin(), exprNodes.end(),
    //    inPortNames.begin(), inPortNames.end(),
    //    std::inserter(inputs, inputs.begin()));

    for (const auto& item : exprNodes) {  
    if (inPortNames.count(item.str()) > 0) {  
        inputs.insert(item);  
        }  
    }

    // Remove already visited  
    std::set<std::string> inputsFiltered;
    //std::set_difference(inputs.begin(), inputs.end(),
        //visited.begin(), visited.end(),
        //std::inserter(inputsFiltered, inputsFiltered.begin()));

    for (const auto& item : inputs) {  
    if (visited.count(item.str()) == 0) {  
        inputsFiltered.insert(item.str());  
        }  
    }

    // Add to visited  
    visited.insert(inputsFiltered.begin(), inputsFiltered.end());

    // Select memories  
    llvm::SetVector<llvm::StringRef> memoryNames;
    for (auto& mem : memorys) {
        memoryNames.insert(const_cast<FMemModuleOp&>(mem).getName());
    }

    llvm::SetVector<llvm::StringRef> mems;
    //std::set_intersection(exprNodes.begin(), exprNodes.end(),
        //memoryNames.begin(), memoryNames.end(),
        //std::inserter(mems, mems.begin()));

    for (const auto& item : exprNodes) {  
    if (memoryNames.count(item.str()) > 0) {  
        mems.insert(item);  
        }  
    }

    // Remove already visited  
    std::set<std::string> memsFiltered;
    //std::set_difference(mems.begin(), mems.end(),
        //visited.begin(), visited.end(),
        //std::inserter(memsFiltered, memsFiltered.begin()));

    for (const auto& item : mems) {  
    if (visited.count(item.str()) == 0) {  
        memsFiltered.insert(item.str());  
        }  
    }

    // Add to visited  
    visited.insert(memsFiltered.begin(), memsFiltered.end());

    // Select instances (and their ports)  
    llvm::SetVector<llvm::StringRef> instanceNames;
    for (auto& inst : instances) {
        instanceNames.insert(const_cast<InstanceOp&>(inst).getName());
    }

    llvm::SetVector<llvm::StringRef> instNames;
    //std::set_intersection(exprNodes.begin(), exprNodes.end(),
        //instanceNames.begin(), instanceNames.end(),
        //std::inserter(instNames, instNames.begin()));

    for (const auto& item : exprNodes) {  
    if (instanceNames.count(item.str()) > 0) {  
        instNames.insert(item);  
        }  
    }


    llvm::SetVector<Node*> insts;
    for (auto& instName : instNames) {
        insts.insert(&Nodes[instName.str()]);
    }

    // Build instance ports mapping  
    std::map<InstanceOp, llvm::SetVector<llvm::StringRef>> iPorts;
    for (auto& inst : insts) {
        if (auto* op = std::get_if<mlir::Operation*>(&(inst->node))) {
            //auto instanceOp = (*inst).template get<InstanceOp>();
            auto instanceOp = dyn_cast<InstanceOp>(*op);
            llvm::SetVector<llvm::StringRef> portSet;

            for (auto& expr : exprs) {
                if (inst->usedIn(expr)) {
                    auto portInfo = inst->portIn(expr);
                    for (auto& info : portInfo) {
                        if (!info.second.empty()) {
                            portSet.insert(info.second[0]);
                        }
                    }
                }
            }
            iPorts[instanceOp] = portSet;
        }
    }

    // Find output ports connected  
    auto [iNodes, oIPorts, iMems] = findOutPortsConnected(iPorts);

    // Build nodes set  
    llvm::SetVector<llvm::StringRef> instAndMemNames;
    for (const auto& instName : instNames) {
        instAndMemNames.insert(instName);
    }
    for (const auto& memName : memsFiltered) {
        instAndMemNames.insert(llvm::StringRef(memName));
    }

    llvm::SetVector<llvm::StringRef> nodes;
    //std::set_difference(exprNodes.begin(), exprNodes.end(),
        //instAndMemNames.begin(), instAndMemNames.end(),
        //std::inserter(nodes, nodes.begin()));

    for (const auto& item : exprNodes) {  
    if (instAndMemNames.count(item.str()) == 0) {  
        nodes.insert(item);  
        }  
    }

    nodes.insert(iNodes.begin(), iNodes.end());

    // Find node sources  
    auto [nodeSrcs, instPorts] = findNodeSrcs(nodes);

    // Build return node sources  
    std::map<llvm::StringRef, llvm::SetVector<Node*>> retNodeSrcs;

    retNodeSrcs["firrtl.reg"] = nodeSrcs["firrtl.reg"];

    // Combine memory sources  
    llvm::SetVector<Node*> memSources = nodeSrcs["firrtl.mem"];
    for (const auto& memName : memsFiltered) {
        memSources.insert(&Nodes[memName]);
    }
    for (const auto& iMemName : iMems) {
        memSources.insert(&Nodes[iMemName.str()]);
    }
    retNodeSrcs["firrtl.mem"] = memSources;

    // Combine instance sources  
    llvm::SetVector<Node*> instSources = nodeSrcs["firrtl.instance"];
    for (const auto& [instOp, _] : oIPorts) {
        instSources.insert(&Nodes[const_cast<InstanceOp&>(instOp).getName().str()]);
    }
    retNodeSrcs["firrtl.instance"] = instSources;

    // Combine port sources  
    llvm::SetVector<Node*> portSources = nodeSrcs["Port"];
    for (const auto& inputName : inputsFiltered) {
        portSources.insert(&Nodes[inputName]);
    }
    retNodeSrcs["Port"] = portSources;

    // Build return instance ports  
    std::map<InstanceOp, llvm::SetVector<llvm::StringRef>> retInstPorts;
    llvm::SetVector<InstanceOp> allInstKeys;
    for (const auto& [key, _] : oIPorts) {
        allInstKeys.insert(key);
    }
    for (const auto& [key, _] : instPorts) {
        allInstKeys.insert(key);
    }

    for (const auto& key : allInstKeys) {
        llvm::SetVector<llvm::StringRef> combinedPorts;
        if (oIPorts.find(key) != oIPorts.end()) {
            combinedPorts.insert(oIPorts[key].begin(), oIPorts[key].end());
        }
        if (instPorts.find(key) != instPorts.end()) {
            combinedPorts.insert(instPorts[key].begin(), instPorts[key].end());
        }
        retInstPorts[key] = combinedPorts;
    }

    return std::make_pair(retNodeSrcs, retInstPorts);
}

std::tuple<llvm::SetVector<llvm::StringRef>, std::map<InstanceOp, llvm::SetVector<llvm::StringRef>>,llvm::SetVector<llvm::StringRef>> circt::firrtl::graphLedger::findOutPortsConnected(std::map<circt::firrtl::InstanceOp, llvm::SetVector<llvm::StringRef>> ips) {
    std::map<InstanceOp, llvm::SetVector<llvm::StringRef>> oIPorts;
    for (auto& [wdi, portSet] : ips) {
        llvm::SetVector<llvm::StringRef> filteredPorts;
        for (auto& port : portSet) {
            std::pair<std::string, std::string> key = { const_cast<InstanceOp&>(wdi).getName().str(), port.str() };
            if (rIP2E.find(key) == rIP2E.end()) {
                filteredPorts.insert(port);
            }
        }
        if (!filteredPorts.empty()) {
            oIPorts[wdi] = filteredPorts;
        }
    }

    // Build set of all port keys from input instances  
    llvm::SetVector<std::pair<llvm::StringRef, llvm::StringRef>> allPortKeys;
    for (auto& [wdi, portSet] : ips) {
        for (auto& port : portSet) {
            allPortKeys.insert({ const_cast<InstanceOp&>(wdi).getName(), port });
        }
    }

    // Extract expressions connected to these ports  
    llvm::SetVector<mlir::Value> exprs;
    for (auto& [key, value] : rIP2E) {
        if (allPortKeys.count(key)) {
            exprs.insert(value);
        }
    }

    // Extract all names from these expressions  
    llvm::SetVector<llvm::StringRef> exprNodes;
    for (auto& expr : exprs) {
        auto names = Node::findNames(expr);
        exprNodes.insert(names.begin(), names.end());
    }

    // Categorize nodes into instances, memories, and other nodes  
    llvm::SetVector<llvm::StringRef> instanceNames;
    for (auto& inst : instances) {
        instanceNames.insert(const_cast<InstanceOp&>(inst).getName());
    }

    llvm::SetVector<llvm::StringRef> memoryNames;
    for (auto& mem : memorys) {
        memoryNames.insert(const_cast<FMemModuleOp&>(mem).getName());
    }

    llvm::SetVector<llvm::StringRef> insts;
    //std::set_intersection(exprNodes.begin(), exprNodes.end(),
    //    instanceNames.begin(), instanceNames.end(),
    //    std::inserter(insts, insts.begin()));

    for (const auto& item : exprNodes) {  
    if (instanceNames.count(item.str()) > 0) {  
        insts.insert(item);  
        }  
    }

    llvm::SetVector<llvm::StringRef> mems;
    //std::set_intersection(exprNodes.begin(), exprNodes.end(),
        //memoryNames.begin(), memoryNames.end(),
        //std::inserter(mems, mems.begin()));

   for (const auto& item : exprNodes) {
    if (memoryNames.count(item.str()) > 0) {
        mems.insert(item);
        }
    }

    llvm::SetVector<llvm::StringRef> nodes = exprNodes;
    for (auto& inst : insts) {
        //nodes.erase(inst);
        auto it = std::find(nodes.begin(), nodes.end(), inst);  
        if (it != nodes.end()) {  
            nodes.erase(it);  
        }
    }
    for (auto& mem : mems) {
        //nodes.erase(mem);
        auto it = std::find(nodes.begin(), nodes.end(), mem);  
        if (it != nodes.end()) {  
            nodes.erase(it);  
        }
    }

    // Build new instance ports mapping for recursion  
    std::map<InstanceOp, llvm::SetVector<llvm::StringRef>> iPorts;
    for (auto& instName : insts) {
        auto& n = Nodes[instName.str()];
        auto instanceOp = n.template get<InstanceOp>();

        llvm::SetVector<llvm::StringRef> portSet;
        for (auto& expr : exprs) {
            if (n.usedIn(expr)) {
                auto portInfo = n.portIn(expr);
                for (auto& info : portInfo) {
                    if (!info.second.empty()) {
                        portSet.insert(info.second[0]);
                    }
                }
            }
        }

        if (!portSet.empty()) {
            iPorts[instanceOp] = portSet;
            //for (auto* inst : instanceOp) {  
                //iPorts[*inst] = portSet;  
            //}
        }
    }

    // Base case: if no more instance ports to process  
    if (iPorts.empty()) {
        return std::make_tuple(nodes, oIPorts, mems);
    }
    else {
        // Recursive case: continue processing  
        auto [contNodes, contOIPorts, contMems] = findOutPortsConnected(iPorts);

        // Merge output instance ports  
        std::map<InstanceOp, llvm::SetVector<llvm::StringRef>> retOIPorts;
        llvm::SetVector<InstanceOp> allKeys;
        for (const auto& [key, _] : oIPorts) {
            allKeys.insert(key);
        }
        for (const auto& [key, _] : contOIPorts) {
            allKeys.insert(key);
        }

        for (const auto& key : allKeys) {
            llvm::SetVector<llvm::StringRef> combinedPorts;
            if (oIPorts.find(key) != oIPorts.end()) {
                combinedPorts.insert(oIPorts[key].begin(), oIPorts[key].end());
            }
            if (contOIPorts.find(key) != contOIPorts.end()) {
                combinedPorts.insert(contOIPorts[key].begin(), contOIPorts[key].end());
            }
            retOIPorts[key] = combinedPorts;
        }

        // Combine nodes and memories  
        llvm::SetVector<llvm::StringRef> combinedNodes = nodes;
        combinedNodes.insert(contNodes.begin(), contNodes.end());

        llvm::SetVector<llvm::StringRef> combinedMems = mems;
        combinedMems.insert(contMems.begin(), contMems.end());

        return std::make_tuple(combinedNodes, retOIPorts, combinedMems);
    }
}

std::pair<std::map<llvm::StringRef, llvm::SetVector<Node*>>, std::map<InstanceOp, llvm::SetVector<llvm::StringRef>>> circt::firrtl::graphLedger::findNodeSrcs(llvm::SetVector<llvm::StringRef> nodes) {
    std::map<llvm::StringRef, llvm::SetVector<Node*>> nodeSrcs = {
    {"firrtl.reg", llvm::SetVector<Node*>()},
    {"firrtl.wire", llvm::SetVector<Node*>()},
    {"firrtl.node", llvm::SetVector<Node*>()},
    {"firrtl.mem", llvm::SetVector<Node*>()},
    {"firrtl.instance", llvm::SetVector<Node*>()},
    {"Port", llvm::SetVector<Node*>()}
    };
    std::map<InstanceOp, llvm::SetVector<llvm::StringRef>> instPorts;

    // Collect all sources from the input nodes  
    llvm::SetVector<std::pair<llvm::StringRef, std::optional<llvm::StringRef>>> srcs;
    for (auto& n : nodes) {
        auto fSrcs = R[n.str()];  // Get sources from R mapping  
        visited.insert(n.str());

        for (auto& src : fSrcs) {
            llvm::StringRef srcCopy = src;  
            llvm::StringRef nCopy = n;  
            auto foundSrcs = findSrcs(srcCopy, nCopy);
            //auto foundSrcs = findSrcs(src, n);  // Call helper function  
            for (auto& src : foundSrcs) {  
                srcs.insert(src);  
            }
            //srcs.insert(srcs.end(), foundSrcs.begin(), foundSrcs.end());
        }
    }

    // Build allSrcs mapping  
    std::map<llvm::StringRef, llvm::SetVector<llvm::StringRef>> allSrcs;
    for (auto& s : srcs) {
        auto& seq = allSrcs[s.first];
        if (s.second.has_value()) {
            seq.insert(s.second.value());
        }
    }

    // Process each source and categorize by node type  
    for (auto& [srcName, srcPorts] : allSrcs) {
        auto& node = Nodes[srcName.str()];
        std::string nodeClass = node.name;

        if (nodeClass == "firrtl.reg" || nodeClass == "firrtl.regreset") {
            nodeSrcs["firrtl.reg"].insert(&node);
        }
        else if (nodeClass == "firrtl.wire") {
            nodeSrcs["firrtl.wire"].insert(&node);
        }
        else if (nodeClass == "firrtl.node") {
            nodeSrcs["firrtl.node"].insert(&node);
        }
        else if (nodeClass == "firrtl.mem") {
            nodeSrcs["firrtl.mem"].insert(&node);
        }
        else if (nodeClass == "firrtl.instance") {
            auto instanceOp = const_cast<Node&>(node).template get<InstanceOp>();
            instPorts[instanceOp] = srcPorts;
            //for (auto* inst : instanceOp) {  
                //instPorts[*inst] = srcPorts;  
            //}
            nodeSrcs["firrtl.instance"].insert(&node);
        }
        else if (nodeClass == "Port") {
            // Check if it's an input port and not clock/reset  
            if (auto* portInfo = std::get_if<circt::firrtl::PortInfo>(&node.node)) {
                if (portInfo->direction == circt::firrtl::Direction::In &&  
                // Check if port name is not "clock" or "reset"  
                    portInfo->name.getValue() != "clock" &&   
                    portInfo->name.getValue() != "reset") {  
                    // Add the node to the Port category in nodeSrcs  
                    nodeSrcs["Port"].insert(&node);  
                }
            }
        }
        else {
            llvm::errs() << srcName << " not in Node type";
        }
    }

    // Convert vectors to sets for return value  
    std::map<llvm::StringRef, llvm::SetVector<Node*>> retNodeSrcs;
    for (auto& [key, nodeVec] : nodeSrcs) {        
        llvm::SetVector<Node*> nodeSet;  
        for (auto& node : nodeVec) {  
            nodeSet.insert(node);  
        }  
        retNodeSrcs[key] = nodeSet;
        //retNodeSrcs[key] = llvm::setVector<Node>(nodeVec.begin(), nodeVec.end());
    }

    return std::make_pair(retNodeSrcs, instPorts);
}

bool circt::firrtl::graphLedger::isStatePreserving(const RegOp& reg) {
    // Get the name of the register  
    std::string regName = const_cast<RegOp&>(reg).getName().str();

    // Get the sources of the register from the R mapping  
    // R is assumed to be a member of the GraphLedger class, e.g., std::map<std::string, std::set<std::string>> R;  
    auto sources = R[regName];

    // Iterate through the sources and check if any of them are the register itself  
    // This is equivalent to Scala's .map(...).reduce(_ || _)  
    for (const auto& s : sources) {
        // Get the Node object for the source 's'  
        // Nodes is assumed to be a member of the GraphLedger class, e.g., std::map<std::string, Node> Nodes;  
        const auto& sourceNode = Nodes[s];

        // Check the class name of the source node  
        std::string sourceNodeClass = sourceNode.name;

        // If the source is a "DefRegister" (firrtl.reg or firrtl.regreset)  
        // and its name matches the current register's name, then it's state-preserving.  
        if ((sourceNodeClass == "firrtl.reg" || sourceNodeClass == "firrtl.regreset") && s == regName) {
            return true;
        }
    }
    return false;
}

std::map<std::string, std::set<RegOp>> circt::firrtl::graphLedger::findVecRegs(std::set<RegOp>& regs) {
    // Helper to extract Info from a Node (assuming Node wraps a RegOp/RegResetOp)  
    // You'll need to implement how to get 'info' from your Node class,  
    // which would typically come from the operation's location or a custom attribute.  
    const int MINVECSIZE = 2;  
      
    // Step 1: Group registers by their location info  
    std::map<LocationAttr, std::vector<RegOp>> infoRegMap;  
      
    for (const auto& reg : regs) {  
        LocationAttr loc = const_cast<circt::firrtl::RegOp&>(reg).getLoc().dyn_cast<LocationAttr>();  
        if (loc) {  
            infoRegMap[loc].push_back(reg);  
        }  
    }  
      
    // Step 2: Sort and filter groups  
    std::map<LocationAttr, std::vector<RegOp>> sInfoRegMap;  
      
    for (auto& [info, regVec] : infoRegMap) {  
        // Sort by name  
        std::sort(regVec.begin(), regVec.end(),   
                  [](const RegOp& a, const RegOp& b) {  
                      return const_cast<circt::firrtl::RegOp&>(a).getName() < const_cast<circt::firrtl::RegOp&>(b).getName();  
                  });  
          
        // Filter: exclude NoInfo and require minimum size  
        if ((!info.isa<mlir::UnknownLoc>()) && regVec.size() >= MINVECSIZE) {  
            sInfoRegMap[info] = std::move(regVec);  
        }  
    }

    // val retInfoRegs = mutable.Map[String, Set[DefRegister]]()  
    std::map<std::string, std::set<RegOp>> retInfoRegs;

    for (const auto& pair : sInfoRegMap) {
        const std::vector<RegOp>& seq = pair.second;

        // val regs = seq.map(_.name)  
        std::vector<std::string> regNames;
        for (const auto& reg : seq) {
            regNames.push_back(const_cast<circt::firrtl::RegOp&>(reg).getName().str());
        }

        // val prefix = regs.foldLeft(regs.head.inits.toSet)((set, reg) => { reg.inits.toSet.intersect(set) }).maxBy(_.length)  
        std::string prefix = "";
        if (!regNames.empty()) {
            std::string firstRegName = regNames[0];
            std::set<std::string> commonPrefixes;
            for (size_t i = 0; i <= firstRegName.length(); ++i) {
                commonPrefixes.insert(firstRegName.substr(0, i));
            }

            for (size_t k = 1; k < regNames.size(); ++k) {
                std::string currentRegName = regNames[k];
                std::set<std::string> currentPrefixes;
                for (size_t i = 0; i <= currentRegName.length(); ++i) {
                    currentPrefixes.insert(currentRegName.substr(0, i));
                }

                std::set<std::string> intersection;
                //std::set_intersection(commonPrefixes.begin(), commonPrefixes.end(),
                    //currentPrefixes.begin(), currentPrefixes.end(),
                    //std::inserter(intersection, intersection.begin()));
                for (const auto& item : commonPrefixes) {  
                    if (currentPrefixes.count(item) > 0) {  
                        intersection.insert(item);  
                    }  
                }
                commonPrefixes = intersection;
            }

            // Find the longest common prefix  
            size_t maxLength = 0;
            for (const auto& p : commonPrefixes) {
                if (p.length() > maxLength) {
                    maxLength = p.length();
                    prefix = p;
                }
            }
        }

        if (prefix.length() == 0) {
            // Unit equivalent, do nothing  
        }
        else {
            // val bodies = regs.map(x => {x.substring(n, x.length)} )  
            std::vector<std::string> bodies;
            for (const auto& regName : regNames) {
                bodies.push_back(regName.substr(prefix.length()));
            }

            // if (bodies.forall(b => b.length > 0 && b(0).isDigit))  
            bool allBodiesStartWithDigit = true;
            for (const auto& body : bodies) {
                if (body.empty() || !std::isdigit(body[0])) {
                    allBodiesStartWithDigit = false;
                    break;
                }
            }

            if (allBodiesStartWithDigit) {
                // val hyphenOrEnd = (b: String) => {if (b.contains('_')) b.indexOf('_') else b.length}  
                auto hyphenOrEnd = [](const std::string& b) {
                    size_t pos = b.find('_');
                    return (pos != std::string::npos) ? pos : b.length();
                    };

                // val idxs = bodies.map(b => b.substring(0, hyphenOrEnd(b)))  
                std::vector<std::string> idxStrs;
                for (const auto& body : bodies) {
                    idxStrs.push_back(body.substr(0, hyphenOrEnd(body)));
                }

                // val vElems = idxs.map(i => (i.toInt, bodies.collect{ case b if b.substring(0, hyphenOrEnd(b)) == i => b.substring(i.length, b.length) })).toMap  
                std::map<int, std::set<std::string>> vElems; // Using set for inner collection to match Scala's toSet  
                for (size_t i = 0; i < idxStrs.size(); ++i) {
                    int idx = std::stoi(idxStrs[i]);
                    std::string suffix = bodies[i].substr(idxStrs[i].length());
                    vElems[idx].insert(suffix);
                }

                // val idxStream = vElems.keySet.toSeq.sorted.sliding(2)  
                std::vector<int> sortedIdxs;
                for (const auto& entry : vElems) {
                    sortedIdxs.push_back(entry.first);
                }
                std::sort(sortedIdxs.begin(), sortedIdxs.end());

                bool contiguous = true;
                if (sortedIdxs.size() > 1) {
                    for (size_t i = 0; i < sortedIdxs.size() - 1; ++i) {
                        if (sortedIdxs[i] + 1 != sortedIdxs[i + 1]) {
                            contiguous = false;
                            break;
                        }
                    }
                }

                // if (idxStream.count(k => k(0) + 1 == k(1)) == vElems.keySet.size - 1 &&  
                //   vElems.keySet.toSeq.head == 0 &&  
                //   vElems.forall(_._2.toSet == vElems.head._2.toSet))  
                bool allSuffixesMatch = true;
                if (!vElems.empty()) {
                    const auto& firstSuffixSet = vElems.begin()->second;
                    for (const auto& entry : vElems) {
                        if (entry.second != firstSuffixSet) {
                            allSuffixesMatch = false;
                            break;
                        }
                    }
                }

                if (contiguous && !sortedIdxs.empty() && sortedIdxs[0] == 0 && allSuffixesMatch) {
                    retInfoRegs[prefix] = std::set<RegOp>(seq.begin(), seq.end());
                }
            }
        }
    }

    return retInfoRegs;
}

SmallVector<std::pair<llvm::StringRef, std::optional<llvm::StringRef>>> circt::firrtl::graphLedger::findSrcs(llvm::StringRef sink, llvm::StringRef prev) {
    if (R.find(sink.str()) == R.end()) {
        llvm::errs() << "R does not contain " << sink;
    }

    // if (visited.contains(sink)) return Seq()  
    if (visited.count(sink.str())) {
        return {}; // Return empty vector  
    }

    const auto& sources = R[sink.str()];

    // WDefInstance does not form a loop & Can be reached multiple times with different ports  
    // if (!Set("WDefInstance", "DefMemory").contains(Nodes(sink).c))  
    std::string sinkNodeClass = Nodes[sink.str()].name;
    if (!(sinkNodeClass == "firrtl.instance" || sinkNodeClass == "firrtl.mem")) {
        visited.insert(sink.str());
    }

    // Initial list based on node type  
    SmallVector<std::pair<llvm::StringRef, std::optional<llvm::StringRef>>> initialList;

    // Nodes(sink).node match { ... }  
    if (sinkNodeClass == "firrtl.instance") {
        // case winst: WDefInstance => rN2IP(prev).map(x => (x._1, Option.apply[String](x._2))).toSeq  
        // TODO: If inst-port is input, continue over  
        if (rN2IP.count(prev.str())) {
            for (const auto& pair : rN2IP[prev.str()]) {
                initialList.push_back({ llvm::StringRef(pair.first), std::make_optional(llvm::StringRef(pair.second)) });
            }
        }
    }
    else if (sinkNodeClass == "firrtl.mem") {
        // case mem: DefMemory => ...  
        // TODO: When memory (data port) is detected, two fields should be sliced  
        // 1. the address selection logic  
        // 2. the writer logic, which modifies the memory value  
        std::set<std::string> cons;
        if (rN2MP.count(prev.str())) {
            for (const auto& x : rN2MP[prev.str()]) {
                std::pair<std::string, std::string> key = x;
                if (rMP2N.count(key)) {
                    // Assuming Seq().contains is always false, so filterKeys is always empty  
                    // This part of Scala code seems to filter by an empty sequence, which means it will always be empty.  
                    // If Seq().contains is meant to be a placeholder for actual filtering logic, this needs adjustment.  
                    // For now, it's translated as if it always results in an empty set.  
                    // cons.insert(rMP2N[key].values.begin(), rMP2N[key].values.end()); // If filterKeys was not empty  
                }
                else {
                    llvm::errs() << key.first << ", " << key.second << " not in rMP2N";
                }
            }
        }

        if (rN2MP.count(prev.str())) {
            for (const auto& pair : rN2MP[prev.str()]) {
                initialList.push_back({ llvm::StringRef(pair.first), std::make_optional(llvm::StringRef(pair.second)) });
            }
        }
        for (const auto& c : cons) {
            auto recursiveSrcs = findSrcs(c, sink);
            initialList.insert(initialList.end(), recursiveSrcs.begin(), recursiveSrcs.end());
        }
    }
    else {
        // case _ => Seq((sink, Option.empty[String])) // Port, DefRegister, DefWire, DefNode  
        initialList.push_back({ llvm::StringRef(sink.str()), std::nullopt });
    }

    // sources.foldLeft(...) ((list, str) => Nodes(sink).c match { ... })  
    SmallVector<std::pair<llvm::StringRef, std::optional<llvm::StringRef>>> resultList = initialList;
    for (const auto& str : sources) {
        // Nodes(sink).c match { case "WDefInstance" | "DefMemory" => list }  
        if (sinkNodeClass == "firrtl.instance" || sinkNodeClass == "firrtl.mem") {
            // Do nothing, list is already initialList  
        }
        else {
            // case _ => list ++ findSrcs(str, sink) // DefNode, DefWire, Port  
            auto recursiveSrcs = findSrcs(str, sink);
            resultList.insert(resultList.end(), recursiveSrcs.begin(), recursiveSrcs.end());
        }
    }
    return resultList;
}

SmallVector<llvm::StringRef> circt::firrtl::graphLedger::findNodesSinks(std::string n) {
    SmallVector<llvm::StringRef> sinks = findSinks(n);

    // Call getOuter on the set of sinks  
    return getOuter(sinks);
}

std::pair<SmallVector<llvm::StringRef>, SmallVector<llvm::StringRef>> circt::firrtl::graphLedger::findMemSinks(const Node& mem) {
    //auto memOp = (&mem.node).get<circt::firrtl::MemOp>();

    if (auto* op = std::get_if<mlir::Operation*>(&mem.node)) {  
    if (auto memOp = dyn_cast<circt::firrtl::MemOp>(*op)) {
    // val readers = mem.readers  
    // In CIRCT, readers are typically port names associated with read ports.  
    // You'll need to extract these from the MemOp.  
    // This is a placeholder for how you might get reader port names.  
    std::set<std::string> readers;
    for (size_t i = 0; i < memOp.getNumResults(); ++i) {
        if (memOp.getPortKind(i) == circt::firrtl::MemOp::PortKind::Read ||
            memOp.getPortKind(i) == circt::firrtl::MemOp::PortKind::ReadWrite) {
            readers.insert(memOp.getPortName(i).str());
        }
    }

    // val addrs = rMP2N.flatMap{ case (k, map) => ... }  
    SmallVector<llvm::StringRef> addrs;
    for (const auto& entry : rMP2N) {
        const auto& k = entry.first; // k is std::pair<std::string, std::string> (mem.name, portName)  
        const auto& map = entry.second; // map is std::map<std::string, std::string> (field -> nodeName)  

        if (k.first == memOp.getName() && readers.count(k.second)) {
            // map.mapValues(Some(_)).getOrElse("addr", None)  
            // This means if 'addr' key exists in 'map', get its value, otherwise None.  
            // In C++, check if 'addr' exists and add it.  
            if (map.count("addr")) {
                addrs.push_back(llvm::StringRef(map.at("addr")));
            }
        }
    }

    // val datas = rN2MP.collect{ case (n, mp) if mp.exists(x => x._1 == mem.name && readers.contains(x._2)) => n }  
    std::set<std::string> datas;
    for (const auto& entry : rN2MP) {
        const auto& n = entry.first; // n is the node name  
        const auto& mpSet = entry.second; // mp is std::set<std::pair<std::string, std::string>>  

        bool foundMatch = false;
        for (const auto& x : mpSet) {
            // x is std::pair<std::string, std::string> (mem.name, portName)  
            if (x.first == memOp.getName() && readers.count(x.second)) {
                foundMatch = true;
                break;
            }
        }
        if (foundMatch) {
            datas.insert(n);
        }
    }

    // val nodes = datas.flatMap(findSinks)  
    SmallVector<llvm::StringRef> nodes;
    for (const auto& dataNodeName : datas) {
        auto sinksForDataNode = findSinks(dataNodeName); // Assuming findSinks returns std::set<std::string>  
        nodes.append(sinksForDataNode.begin(), sinksForDataNode.end());
    }

    // (addrs.toSet, getOuter(nodes.toSet))  
    return std::make_pair(addrs, getOuter(nodes));
    }
    }
}

SmallVector<llvm::StringRef> circt::firrtl::graphLedger::getOuter(SmallVector<llvm::StringRef> sinks) {
    // val nodes = N2E.filterKeys(sinks.contains).flatMap{ ... }.toSet  
    SmallVector<llvm::StringRef> nodes;
    
    for (auto& [s, e] : N2E) {  
    if (llvm::find(sinks, s) != sinks.end()) {  
        TypeSwitch<Operation*>(e.getDefiningOp())  
            .Case<MuxPrimOp>([&](MuxPrimOp mux) {  
                // Node.findNames(c) equivalent  
                auto names = Node::findNames(mux.getSel());  
                nodes.append(names.begin(), names.end());  
            })  
            .Case<EQPrimOp>([&](EQPrimOp eq) {  
                // args.flatMap(Node.findNames) equivalent  
                for (auto arg : eq.getOperands()) {  
                    auto names = Node::findNames(arg);  
                    nodes.append(names.begin(), names.end());  
                }  
            })  
            .Default([](Operation*) {  
                // case _ => Seq() equivalent  
            });  
        }  
    }   // nodes -- sinks  
    
    SmallVector<llvm::StringRef> result;
    //std::set_difference(nodes.begin(), nodes.end(),
        //sinks.begin(), sinks.end(),
        //std::inserter(result, result.begin()));

    for (const auto& item : nodes) {
    if (llvm::find(sinks, item) == sinks.end()) {
        result.push_back(item);
        }
    }

    return result;
}

SmallVector<llvm::StringRef> circt::firrtl::graphLedger::findSinks(const std::string& source) {
    if (expG.find(source) == expG.end()) {
        llvm::errs() << "G does not contain " << source;
    }

    // if (visited.contains(source)) return Seq()  
    if (visited.count(source)) {
        return {}; // Return empty vector  
    }

    const auto& sinks = expG[source];

    visited.insert(source);

    // sinks.foldLeft(Seq(source))((list, str) => ... )  
    SmallVector<llvm::StringRef> resultList = { llvm::StringRef(source) }; // Initial list with the source itself  
    for (const auto& str : sinks) {
        std::string nodeClass = Nodes[str].name;
        if (nodeClass == "firrtl.instance" || nodeClass == "firrtl.mem") {
            // case "WDefInstance" | "DefMemory" => list (do nothing, just append the current list)  
            // The Scala foldLeft accumulates the list, so we just continue with the current resultList  
        }
        else {
            // case _ => list ++ findSinks(str) (recursively find sinks and append)  
            auto recursiveSinks = findSinks(str);
            resultList.insert(resultList.end(), recursiveSinks.begin(), recursiveSinks.end());
        }
    }
    return resultList;
}
circt::firrtl::graphLedger::graphLedger(circt::firrtl::FModuleLike module)
    : mName(module.getModuleName().str()), module(module) {
    // Equivalent to module.ports.toSet  
    auto ports = module.getPorts();
    mPorts = llvm::DenseSet<circt::firrtl::PortInfo>(ports.begin(), ports.end());
}

void circt::firrtl::graphLedger::parse() {
    // Assuming we have a way to distinguish between different module types
    if (isa<FExtModuleOp>(module)/* module is ExtModule */) {
        std::cout << mName << " is an external module\n";
    }
    else {
        buildG();
        reverseG();
    }
}

template <typename T > std::set<T*> circt::firrtl::graphLedger::getNodes() {
    //static_assert(std::is_base_of<mlir::Operation*, T>::value,
        //"T must be a subclass of mlir::Operation*");//这里得调一下因为，它还是需要被判定为是一种firrtlOperation吧？但如果枚举的话真的要死了= =但其实枚举一次也还好，我觉得最后还是会编出来额外的东西
    std::set<T*> result;
    for (const auto& pair : Nodes) {
        // pair.second is a Node object, which wraps an mlir::Operation*  
        // We need to check if the underlying operation is of type T  
        if (auto* op = std::get_if<Operation*>(&(pair.second.node))) {
            if (auto typedOp = dyn_cast<T>(*op)){
                result.insert(&typedOp);}
        }
    }
    return result;
}
std::map<std::pair<std::string, std::string>, mlir::Value> circt::firrtl::graphLedger::IP2E() {
    return rIP2E;
}

void circt::firrtl::graphLedger::clear() {
    visited.clear();
}

std::set<std::string> circt::firrtl::graphLedger::filterOut(std::set<Node>& nodes) {
    std::set<std::string> outNames;

    for (auto& node : nodes) {
        std::string nodeClass = node.name;

        if (nodeClass == "firrtl.reg" || nodeClass == "firrtl.regreset") {
            outNames.insert(node.name);
        }
        else if (nodeClass == "firrtl.mem") {
            outNames.insert(node.name);
        }
        else {
            std::string serialized = node.serialize();
            llvm::errs() << serialized << " is not reg/mem";
        }
    }

    // Filter Nodes collection and get difference (equivalent to Nodes.filter...diff)  
    std::set<std::string> filteredNodeNames;

    for (const auto& [nodeName, node] : Nodes) {
        std::string nodeClass = node.name;
        if (nodeClass == "firrtl.reg" || nodeClass == "firrtl.regreset" ||
            nodeClass == "firrtl.mem") {
            filteredNodeNames.insert(nodeName);
        }
    }

    // Compute set difference: filteredNodeNames - outNames  
    std::set<std::string> result;
    //std::set_difference(filteredNodeNames.begin(), filteredNodeNames.end(),
        //outNames.begin(), outNames.end(),
        //std::inserter(result, result.begin()));

    for (const auto& item : filteredNodeNames) {
    if (outNames.count(item) == 0) {
        result.insert(item);
        }
    }

    return result;
}

void circt::firrtl::graphLedger::printLog() {
    std::cout << "====================" << mName << "=========================" << std::endl;

    std::cout << "---------R---------" << std::endl;
    for (const auto& [key, valueSet] : R) {
        std::cout << "[" << key << "] -- {";
        bool first = true;
        for (const auto& value : valueSet) {
            if (!first) std::cout << ", ";
            std::cout << value;
            first = false;
        }
        std::cout << "}" << std::endl;
    }
    std::cout << std::endl;

    std::cout << "---------rN2IP---------" << std::endl;
    for (const auto& [key, pairSet] : rN2IP) {
        std::cout << "[" << key << "] -- {";
        bool first = true;
        for (const auto& pair : pairSet) {
            if (!first) std::cout << ", ";
            std::cout << "(" << pair.first << ", " << pair.second << ")";
            first = false;
        }
        std::cout << "}" << std::endl;
    }
    std::cout << std::endl;

    std::cout << "---------rIP2E---------" << std::endl;
    for (const auto& [key, value] : rIP2E) {
        std::cout << "(" << key.first << ", " << key.second << ") -- {";
        std::string serialized;
        llvm::raw_string_ostream stream(serialized);
        value.print(stream);
        std::cout << serialized << "}" << std::endl;
    }
    std::cout << std::endl;

    std::cout << "---------rN2MP---------" << std::endl;
    for (const auto& [key, pairSet] : rN2MP) {
        std::cout << "[" << key << "] -- {";
        bool first = true;
        for (const auto& pair : pairSet) {
            if (!first) std::cout << ", ";
            std::cout << "(" << pair.first << ", " << pair.second << ")";
            first = false;
        }
        std::cout << "}" << std::endl;
    }
    std::cout << std::endl;

    std::cout << "---------rMP2N---------" << std::endl;
    for (const auto& [key, valueMap] : rMP2N) {
        std::cout << "[(" << key.first << ", " << key.second << ")] -- {";
        bool first = true;
        for (const auto& [mapKey, mapValue] : valueMap) {
            if (!first) std::cout << ", ";
            std::cout << mapKey << " -> " << mapValue;
            first = false;
        }
        std::cout << "}" << std::endl;
    }
    std::cout << std::endl;
}

std::pair<int, int> circt::firrtl::graphLedger::getStat() {
    int reg = 0;
    int mem = 0;

    for (const auto& [nodeName, node] : Nodes) {
        std::string nodeClass = node.name;
        if (nodeClass == "firrtl.reg" || nodeClass == "firrtl.regreset") {
            reg++;
        }
        else if (nodeClass == "firrtl.mem") {
            mem++;
        }
    }

    return std::make_pair(reg, mem);
};

namespace llvm {  
template<>  
struct DenseMapInfo<std::optional<StringRef>> {  
    static inline std::optional<StringRef> getEmptyKey() {  
        return std::optional<StringRef>{};  
    }  
    static inline std::optional<StringRef> getTombstoneKey() {  
        return std::optional<StringRef>{StringRef{}};  
    }  
    static unsigned getHashValue(const std::optional<StringRef>& val) {  
        return val ? DenseMapInfo<StringRef>::getHashValue(*val) : 0;  
    }  
    static bool isEqual(const std::optional<StringRef>& lhs,  
                       const std::optional<StringRef>& rhs) {  
        return lhs == rhs;  
    }  
};  
}
//namespace llvm {
//template <>
//struct DenseMapInfo<mlir::LocationAttr> {
//  static inline mlir::LocationAttr getEmptyKey() {
//    return mlir::LocationAttr::getFromOpaquePointer(
//        DenseMapInfo<void *>::getEmptyKey());
//  }
//  static inline mlir::LocationAttr getTombstoneKey() {
//    return mlir::LocationAttr::getFromOpaquePointer(
//        DenseMapInfo<void *>::getTombstoneKey());
//  }
//  static unsigned getHashValue(mlir::LocationAttr val) {
//    return mlir::hash_value(val);
//  }
//  static bool isEqual(mlir::LocationAttr LHS, mlir::LocationAttr RHS) {
//   return LHS == RHS;
//  }
//};
//}

namespace mlir {  
inline bool operator<(LocationAttr lhs, LocationAttr rhs) {
    return lhs.getAsOpaquePointer() < rhs.getAsOpaquePointer();
}
}

template std::set<InstanceOp*> circt::firrtl::graphLedger::getNodes<InstanceOp>();
