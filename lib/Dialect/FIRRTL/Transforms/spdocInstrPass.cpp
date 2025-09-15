#include "mlir/IR/Builders.h"  
#include "mlir/IR/ImplicitLocOpBuilder.h"
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
#include "circt/Support/InstanceGraph.h"
#include <iostream>
#include "circt/Dialect/FSM/FSMOps.h"
//#define GEN_PASS_DEF_SPDOCINSTRPASS
//#include "circt/Dialect/FIRRTL/Passes.h.inc"

using namespace circt;
using namespace firrtl;

//using StringSet = std::set<std::string>;
//using StringToStringSetMap = std::map<std::string, StringSet>;
//using StringAndStringSetMap = std::pair<std::string, std::set<std::pair<std::string, StringSet>>>;
//using ComplexMap = std::map<std::string, StringAndStringSetMap>; //for getInstanceMap

//using StringToNodeSetMap = std::map<std::string, std::set<Node*>>;
//using WDefInstanceToStringSetMap = std::map<InstanceOp*, std::set<std::string>>;
//using ComplexTuple = std::pair<StringToNodeSetMap, WDefInstanceToStringSetMap>; //for findPortSrcs

//using StringToNodeSetMap = DenseMap<StringAttr, DenseSet<Node*>>;
//using WDefInstanceToStringSetMap = DenseMap<InstanceOp*, DenseSet<StringAttr>>;
//using ComplexTuple_connected = std::pair<StringToNodeSetMap, WDefInstanceToStringSetMap>;//for findOutPortsConnected


StringRef circt::firrtl::SpdocInstrPass::getArgument() const { return "firrtl-spdoc-instr"; }

StringRef circt::firrtl::SpdocInstrPass::getDescription() const {
    return "Added by me.";
}

// Implementation of the pass

//namespace circt{
//namespace firrtl{

void circt::firrtl::SpdocInstrPass::phase0(moduleNet& mNet, llvm::DenseMap<llvm::StringRef, moduleInfo*>& mInfos,  
           llvm::StringRef& topModuleName,   
           std::set<llvm::StringRef> topPorts) {  
      
    mNet.reset();  
      
    llvm::DenseMap<llvm::StringRef, llvm::SetVector<llvm::StringRef>> fPorts;  
    for (auto& [k, v] : mInfos) {  
        fPorts[k] = v->getPorts(mInfos);  
    }  
    fPorts[topModuleName].insert(topPorts.begin(), topPorts.end());  
      
    bool done = false;  
    while (!done) {  
        auto nNameOpt = mNet.popT();  
        if (nNameOpt.has_value()) {  
            std::string nName = nNameOpt.value();  
            mInfos[nName]->phase0(fPorts[nName]);  
              
            auto instances = mInfos[nName]->template getSrcNode<circt::firrtl::InstanceOp>();  
            for (auto& cinst : instances) {
              	//InstanceOp* inst = const_cast<InstanceOp*>(*cinst);  
                auto instPorts = mInfos[nName]->fInstPorts[*cinst];  
                fPorts[cinst->getModuleName()].insert(instPorts.begin(), instPorts.end());  
            }  
        } else {  
            done = true;  
        }  
    }  
}

void circt::firrtl::SpdocInstrPass::phase1(moduleNet& mNet, llvm::DenseMap<llvm::StringRef, moduleInfo*>& mInfos) {  
      
    mNet.reset();  
      
    llvm::DenseMap<llvm::StringRef, llvm::DenseSet<mlir::Value>> bCons;  
    for (auto& [k, v] : mInfos) {  
        bCons[k] = v->getInstCons(mInfos);  
    }  
      
    bool done = false;  
    while (!done) {  
        auto nNameOpt = mNet.popB();  
        if (nNameOpt.has_value()) {  
            std::string nName = nNameOpt.value();  
            auto exprs = mInfos[nName]->getInstCons(mInfos);  
              
            // 合并 bCons[nName] 和 exprs  
            llvm::DenseSet<mlir::Value> combined = bCons[nName];  
            combined.insert(exprs.begin(), exprs.end());  
              
            mInfos[nName]->phase1(combined);  
        } else {  
            done = true;  
        }  
    }  
}
//}
//}
void circt::firrtl::SpdocInstrPass::runOnOperation() {
    // 1. 扒拉出现在电路上的全部modules
    modules.clear();
    //auto circuitOp = dyn_cast<CircuitOp>(getOperation());
    circt::firrtl::CircuitOp circuitOp = getOperation();
    //auto circuit = dyn_cast<CircuitOp>(getOperation());
    if (!circuitOp) {
        if (auto moduleOp = dyn_cast<circt::firrtl::FModuleOp>(getOperation())) {
            for (auto& innerOp : moduleOp.getBodyBlock()->getOperations()) {
                if (auto circuit = dyn_cast<CircuitOp>(innerOp)) {
                    circuitOp = circuit;
                    break;
                }
            }
        }

        if (!circuitOp) {
            llvm::errs() << "Error: Not a valid CircuitOp\n";
            return;
        }
    }

    for (auto& op : circuitOp.getBodyBlock()->getOperations()) {
        if (auto moduleLike = dyn_cast<FModuleLike>(op)) {
            modules[moduleLike.getModuleNameAttr()] = &op;
        }
    }

    // 2. 获取顶层模块及环境变量
    StringRef topLevelName;

    auto debug = isDebugEnabled();

    if (auto envValue = llvm::sys::Process::GetEnv("TOPMODULE")) {
        topLevelName = *envValue;
    }
    else {
        topLevelName = circuitOp.getName();  // Main module has same name as circuit
    }

    mlir::SymbolTable symbolTable(circuitOp);
    auto topModule = dyn_cast_or_null<FModuleLike>(symbolTable.lookup(topLevelName));

    if (!topModule) {
        llvm::errs() << "Error: Top module " << topLevelName << " not found\n";
        return;
    }

    // 3. 处理顶层模块（从这里开始）
    // Add your custom processing logic here
    if (debug) {
        llvm::outs() << "Debug Mode: Top module is " << topLevelName << "\n";
    }

    llvm::outs() << "* Finding Attackable Memories ...\n";

    std::map<std::string, std::shared_ptr<graphLedger>> gLedgers;
    llvm::DenseMap<llvm::StringRef, moduleInfo*> mInfos;
    std::set<std::string> fInsts = {
        "frontend", "Backend_ooo", "SimpleBusCrossbarNto1",
        "SimpleBusAutoIDCrossbarNto1", "TLB", "Cache",
        "Cache_1", "TLB_1", "SimpleBusUCExpender" };

    std::set<std::pair<std::string, MemOp>> atkMems;
    std::set<std::pair<std::string, RegOp>> atkRegs;

    // 遍历所有模块，创建 GraphLedger
    for (auto module : circuitOp.getOps<circt::firrtl::FModuleOp>()) {
        std::string moduleName = module.getName().str();
        gLedgers[moduleName] = std::move(std::make_unique<graphLedger>(module));
    }
    //调用parse函数
    for (auto& entry : gLedgers) {
        entry.second->parse();
    }
    //std::map<std::string, moduleInfo> mInfos;

    //circt::firrtl::InstanceGraph instanceGraph(circuitOp);
    // 遍历所有模块节点，类似于gLedgers.map  
    for (auto& tup : gLedgers) {
        //auto moduleName = node->getModule().getModuleName().str();
        //auto moduleInfo_save = moduleInfo(moduleName, gLedgers);
        //mInfos[moduleName] = moduleInfo_save;
        std::string nameStr = tup.first;
        auto moduleInfoInstance = moduleInfo(nameStr, *tup.second);
        mInfos.try_emplace(llvm::StringRef(tup.first), &moduleInfoInstance);
    }

    if (debug) {
        llvm::errs() << "** Building 1-level Instance Map ...\n";
    }

    // 获取顶层模块的实例映射并过滤  
    
    //std::map<std::string, std::pair<std::string, std::set<std::pair<std::string, std::set<std::string>>>> instMap;
    using InstanceData = std::pair<std::string, std::set<std::pair<std::string, std::set<std::string>>>>;  
    std::map<std::string, InstanceData> instMap;

    //auto topLevelNode = instanceGraph.lookup(topLevelName);
    std::string topLevelNameStr = topLevelName.str();
    auto instanceMap = gLedgers[topLevelNameStr]->getInstanceMap();
    //if (topLevelNode) {
    //    // 遍历顶层模块的所有实例  
    //    for (auto& instanceRecord : *topLevelNode) {
    //        auto instanceName = instanceRecord.getInstance()->getName().str();
    //        if (fInsts.find(instanceName) != fInsts.end()) {
    //            instMap[instanceName] = &instanceRecord;
    //        }
    //    }
    //}
    for (auto& [key, value] : instanceMap) {  
        if (fInsts.find(key) != fInsts.end()) {  // 或者 fInsts.contains(key) 在C++20中  
            instMap[key] = value;  
        }  
    }

    //std::set<std::pair<std::string, MemOp>> atkMems;
    //std::set<std::pair<std::string, RegOp>> atkRegs;

    // 遍历实例映射  
    for (auto& [_, v] : instMap) {
        //auto module = instanceRecord.getTarget();
        auto& module = v.first;
        // 过滤相关的模块账本  
        std::map<std::string, std::shared_ptr<circt::firrtl::graphLedger>> ledgers;//std::map<std::string, graphLedger> ledgers;
        
        //std::string keyStr = key;
        for (auto& [key, ledger] : gLedgers) {
            std::string keyStr = key;
            llvm::StringRef moduleRef = module;
            //moduleInfo info;
            //moduleInfo info = moduleInfo(keyStr, *ledger);
            if (moduleInfo::findModules(gLedgers, moduleRef, keyStr) > 0) {
                ledgers[keyStr] = std::move(gLedgers[keyStr]);
            }
        }

        // 构建模块网络  
        auto mNet = moduleNet(ledgers, module);

        bool done = false;
        int iter = 0;

        // 获取顶层端口  
        std::set<llvm::StringRef> topPorts;
        //for (const auto& portGroup : instanceRecord->getPortGroups()) {
            //for (const auto& port : portGroup.second) {
                //topPorts.insert(port);
            //}
        //}
        for (auto& x : v.second) {  
            auto& ports = x.second;  
            topPorts.insert(ports.begin(), ports.end());  
        }

        // 迭代分析过程  
        while (!done && iter < 10) {
            if (iter == 0) {
                llvm::StringRef moduleRef = module;
                phase0(mNet, mInfos, moduleRef, topPorts);
            }
            else {
                llvm::StringRef moduleRef = module;
                std::set<llvm::StringRef> empty_ports = std::set<llvm::StringRef>();
                phase0(mNet, mInfos, moduleRef, empty_ports);
//there's no way for me to be normal le woc
            }

            // 收集攻击内存  
            for (auto& [key, info] : mInfos) {
                auto memory = info->getMemory();
                for (auto& _ : memory) {
                    atkMems.insert(memory.begin(), memory.end());//atkMems.insert({ moduleName, mem });
                }
            }

            // 收集攻击寄存器  
            for (auto& [key, info] : mInfos) {
                auto registers = info->getRegister();
                for (auto& _ : registers) {
                    atkRegs.insert(registers.begin(), registers.end());//atkRegs.insert({ moduleName, reg });
                }
            }

            phase1(mNet, mInfos);

            // 再次收集攻击内存和寄存器  
            for (auto& [key, info] : mInfos) {
                auto memory = info->getMemory();
                for (auto& _ : memory) {
                    atkMems.insert(memory.begin(), memory.end());//atkMems.insert({ moduleName, mem });
                }
            }
            for (auto& [key, info] : mInfos) {
                auto registers = info->getRegister();
                for (auto& _ : registers) {
                    atkRegs.insert(registers.begin(), registers.end());//atkRegs.insert({ moduleName, reg });
                }
            }

            iter++;

            // 检查是否完成  
            int totalNodes = 0;
            for (auto& [key, info] : mInfos) {
                totalNodes += info->getAllNodes().size();
            }
            done = (totalNodes == 0);
        }
    }

    llvm::outs() << "*** Detection Result ...\n";

    // 排除深度为1的内存  
    auto atkMemsFiltered = atkMems;
    for (auto it = atkMemsFiltered.begin(); it != atkMemsFiltered.end();) {
        auto& [moduleName, constMemOp] = *it;
        //auto& pair = *it;  
        //auto& moduleName = pair.first;  
        auto& memOp = const_cast<circt::firrtl::MemOp&>(constMemOp);
        if (memOp.getDepth() <= 1) {
            it = atkMemsFiltered.erase(it);
        }
        else {
            ++it;
        }
    }
    atkMems = atkMemsFiltered;

    llvm::outs() << "* [Memory] *\n";

    // 构建攻击内存映射  
    std::map<std::string, std::set<MemOp>> atkMemMap;
    for (auto& [moduleName, ledger] : gLedgers) {
        std::set<MemOp> moduleMemories;

        for (auto& [memModuleName, memOp] : atkMems) {
            if (memModuleName == moduleName) {
                moduleMemories.insert(memOp);
            }
        }

        if (!moduleMemories.empty()) {
            atkMemMap[moduleName] = moduleMemories;
        }
    }

    // 输出攻击内存信息  
    for (auto& [moduleName, memories] : atkMemMap) {
        llvm::outs() << moduleName << ": ";

        std::vector<std::string> memNames;
        for (auto& constMem : memories) {
            auto& mem = const_cast<circt::firrtl::MemOp&>(constMem);
            memNames.push_back(mem.getName().str());
        }

        for (size_t i = 0; i < memNames.size(); ++i) {
            if (i > 0) llvm::outs() << ", ";
            llvm::outs() << memNames[i];
        }
        llvm::outs() << "\n";
    }

    llvm::outs() << "\n* [Register] *\n";

    // 构建攻击寄存器映射  
    std::map<std::string, std::map<std::string, std::set<RegOp>>> atkRegMap;

    for (auto& [moduleName, ledger] : gLedgers) {
        // 收集该模块的所有状态保持寄存器  
        std::set<RegOp> allRegs;
        for (auto& [regModuleName, regOp] : atkRegs) {
            if (regModuleName == moduleName && ledger->isStatePreserving(regOp)) {
                allRegs.insert(regOp);
            }
        }

        if (allRegs.empty()) continue;

        // 查找向量寄存器  
        auto vecRegs = ledger->findVecRegs(allRegs);

        // 计算剩余的标量寄存器  
        std::set<RegOp> vecRegSet;
        for (auto& [vecName, vecRegGroup] : vecRegs) {
            for (auto& reg : vecRegGroup) {
                vecRegSet.insert(cast<circt::firrtl::RegOp>(*reg));
            }
        }

        std::set<RegOp> scalarRegs;
        std::set_difference(allRegs.begin(), allRegs.end(),
            vecRegSet.begin(), vecRegSet.end(),
            std::inserter(scalarRegs, scalarRegs.begin()));
        std::map<std::string, std::set<RegOp>> moduleRegMap;  
        for (auto& [moduleName, regPtrs] : vecRegs) {  
            std::set<RegOp> regSet;  
            for (auto& regPtr : regPtrs) {  
                regSet.insert(cast<circt::firrtl::RegOp>(*regPtr));  // 解引用指针  
            }  
            moduleRegMap[moduleName] = regSet;  
        }
        //std::map<std::string, std::set<RegOp>> moduleRegMap = vecRegs;
        if (!scalarRegs.empty()) {
            moduleRegMap["remaining_"] = scalarRegs;
        }

        if (!moduleRegMap.empty()) {
            atkRegMap[moduleName] = moduleRegMap;
        }
    }

    // 输出攻击寄存器信息  
    for (auto& [moduleName, regMap] : atkRegMap) {
        llvm::outs() << moduleName << ": ";

        std::vector<std::string> regGroupNames;
        for (auto& [groupName, regs] : regMap) {
            // 移除末尾的下划线  
            std::string cleanName = groupName;
            if (!cleanName.empty() && cleanName.back() == '_') {
                cleanName.pop_back();
            }
            regGroupNames.push_back(cleanName);
        }

        for (size_t i = 0; i < regGroupNames.size(); ++i) {
            if (i > 0) llvm::outs() << ", ";
            llvm::outs() << regGroupNames[i];
        }
        llvm::outs() << "\n";
    }

    //std::set<std::pair<std::string, circt::firrtl::MemOp>> atkMems; // 示例，实际应从execute参数或成员变量获取  
    //std::map<std::string, std::map<std::string, std::set<RegOp>>> atkRegMap; // 示例  

    // 假设 gLedgers 和 moduleInfo 已经可用  
    //std::map<std::string, circt::firrtl::graphLedger> gLedgers; // 示例  
    // moduleInfo moduleInfo; // 示例  

    /* Find modules b/w TopModule and atkMem */
    std::set<llvm::StringRef> parentMs;
    // 遍历所有模块，查找父模块  
    for (auto moduleOp : circuitOp.getOps<circt::firrtl::FModuleOp>()) { // 遍历电路中的所有FModuleOp  
        llvm::StringRef moduleName = moduleOp.getModuleName();
        bool foundPath = false;

        // 检查与atkMems的路径  
        for (auto& memEntry_c : atkMems) {
            auto& memEntry = const_cast<std::string&>(memEntry_c.first);
            if (moduleInfo::findModules(gLedgers, moduleName, memEntry) > 0) {
                foundPath = true;
                break;
            }
        }

        // 检查与atkRegMap的路径  
        if (!foundPath) {
            for (auto& regEntry_c : atkRegMap) {
                auto& regEntry = const_cast<std::string&>(regEntry_c.first);
                if (!regEntry_c.second.empty() && moduleInfo::findModules(gLedgers, moduleName, regEntry) > 0) {
                    foundPath = true;
                    break;
                }
            }
        }

        if (foundPath) {
            parentMs.insert(moduleName);
        }
    }

    std::set<llvm::StringRef> intmMs;
    // 过滤中间模块  
    for (auto& moduleName : parentMs) {
        // 假设 circuit.lookupModule(pMod) 叻�找到 FModuleOp  
        //circt::firrtl::FModuleOp pModOp = circuitOp.lookupModule<circt::firrtl::FModuleOp>(pMod);
        
        llvm::StringRef moduleNameStr = moduleName;  
        //llvm::StringRef topModuleNameStr = topLevelName.str();
        if (moduleInfo::findModules(gLedgers, moduleNameStr, topLevelNameStr) == 0) {
            intmMs.insert(moduleName);
        }
    }
    intmMs.insert(topLevelName); // 添加顶层模块  

    // 实例化 StateChecker  
    stateChecker stChecker("XiangShan", topLevelName, intmMs);

    // 插桩电路  
    // 在CIRCT中，通常通过PassManager和Operation::walk来修改IR，而不是map操作  
    // 这里模拟原始map的行为，对每个模块进行instrument  
    circuitOp.walk([&](circt::firrtl::FModuleOp m) {
        std::string moduleName = m.getModuleName().str();

        // 获取当前模块的攻击内存和寄存器  
        std::set<MemOp> mems;
        if (atkMemMap.count(moduleName)) {
            mems = atkMemMap.at(moduleName);
        }

        std::map<std::string, std::set<RegOp>> regs;
        if (atkRegMap.count(moduleName)) {
            regs = atkRegMap.at(moduleName);
        }

        // 调用 StateChecker 的 instrument 方法  
        // 注意：instrument 方法在C++版本中返回 FModuleOp，  
        // 但由于MLIR的IR是可变的，通常直接在函数内部修改模块，而不是返回新模块。  
        // 这里假设 instrument 已经修改了传入的 FModuleOp m。  
        stChecker.instrument(m, mems, regs);
        });

}

void circt::firrtl::SpdocInstrPass::printd(std::string& str, bool d) {
    if (d) {
        std::cout << str << std::endl; // 如果d为true，打印字符串
    }
}

bool circt::firrtl::SpdocInstrPass::isDebugEnabled() {
    // Check debug environment variable
    if (auto envValue = llvm::sys::Process::GetEnv("debug")) {
        // Return true if environment variable is "1"
        return *envValue == "1";
    }
    // Return false if environment variable doesn't exist or isn't "1"
    return false;
}


FModuleLike circt::firrtl::SpdocInstrPass::findTopModule(CircuitOp circuitOp) {
    StringRef topLevelName;

    if (auto envValue = llvm::sys::Process::GetEnv("TOPMODULE")) {
        topLevelName = *envValue;
    }
    else {
        topLevelName = circuitOp.getName();  // Main module has same name as circuit
    }

    mlir::SymbolTable symbolTable(circuitOp);
    auto topModule = dyn_cast_or_null<FModuleLike>(symbolTable.lookup(topLevelName));

    if (!topModule) {
        llvm::errs() << "Error: Top module " << topLevelName << " not found\n";
        return nullptr;//这个是none吧
    }

    return topModule;
}


// Pass registration
std::unique_ptr<mlir::Pass> circt::firrtl::createspdocInstrPass() {
    return std::make_unique<circt::firrtl::SpdocInstrPass>();
}

// Register the pass with the CIRCT system
namespace {
#define GEN_PASS_DEF_SPDOCINSTRPASS
#include "circt/Dialect/FIRRTL/Passes.h.inc"
}


