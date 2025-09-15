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
#include "circt/Dialect/FIRRTL/FIRRTLUtils.h"
#include <random>
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

//std::string circt::firrtl::stateChecker::mName = "";
//int circt::firrtl::stateChecker::mID = 0;
//int circt::firrtl::stateChecker::iID = 0;

std::pair<std::vector<mlir::Operation*>, mlir::Value>
circt::firrtl::stateChecker::connectDone(OpBuilder& builder, Location loc,
	const std::vector<mlir::Value>& dones, mlir::Value donePort) {

	auto uint1Type = UIntType::get(builder.getContext(), 1);
	std::vector<mlir::Operation*> doneAnds;

	// 创建初始的常量1节点  
	auto oneConst = uLit(builder, loc, APInt(1,1), 1);
	auto initialNodeName = mName_ + "_done_and0";
	auto initialNode = builder.create<NodeOp>(loc, oneConst.getResult(),
		builder.getStringAttr(initialNodeName));
	doneAnds.push_back(oneConst);
	doneAnds.push_back(initialNode);

	mlir::Value currentResult = initialNode.getResult();

	// 依次AND每个done信号  
	for (size_t i = 0; i < dones.size(); ++i) {
		auto andNodeName = mName_ + "_done_and" + std::to_string(i + 1);
		auto andOp = builder.create<AndPrimOp>(loc, uint1Type, currentResult, dones[i]);
		auto andNode = builder.create<NodeOp>(loc, andOp.getResult(),
			builder.getStringAttr(andNodeName));

		doneAnds.push_back(andOp);
		doneAnds.push_back(andNode);
		currentResult = andNode.getResult();
	}

	// 连接最终结果到done端口  
	emitConnect(builder, loc, donePort, currentResult);

	return std::make_pair(doneAnds, currentResult);
}//引入了一个专为编译器开发的smallvector，可用

std::pair<std::vector<PortInfo>, NodeOp>
circt::firrtl::stateChecker::getID(OpBuilder& builder, Location loc, FModuleOp module, int moduleID) {

	auto uint8Type = UIntType::get(builder.getContext(), 8);
	std::vector<PortInfo> ports;
	Value idValue;
	bool isTopModule = (mName_ == topModuleName_);

	// 检查是否是顶层模块  
	if (mName_ != topModuleName_) {
		// 非顶层模块需要创建io_mid输入端口  
		PortInfo idPort(
		  builder.getStringAttr("io_mid"),
		  uint8Type,
		  Direction::In,
		  {},
		  loc,
		  AnnotationSet(builder.getContext())
		);
		ports.push_back(idPort);

	        size_t portIndex = module.getNumPorts(); // assuming this is added as new port
        	idValue = module.getArgument(portIndex);
	}
	Value nodeValue;  
    	if (!isTopModule && idValue) {  
        	// Create: id + moduleID  
        	auto moduleIDConst = builder.create<ConstantOp>(  
            	loc,   
            	UIntType::get(builder.getContext(), 8),  
            	APInt(8, moduleID)  
        	);  
        	nodeValue = builder.create<AddPrimOp>(loc, idValue, moduleIDConst);  
    	} else {  
        // Just moduleID constant  
        	nodeValue = builder.create<ConstantOp>(  
            	loc,  
            	UIntType::get(builder.getContext(), 8),   
            	APInt(8, moduleID)  
        	);  
    	}
	auto nodeName = mName_ + "_mid";  
	    auto nodeOp = builder.create<NodeOp>(  
        	loc,  
        	nodeValue,  
        	nodeName,  
        	NameKindEnum::InterestingName  
    	);  
      
    	return std::make_pair(ports, nodeOp);
}

void circt::firrtl::stateChecker::conPorts(OpBuilder& builder, Location loc, FModuleOp module,
	mlir::Value onSignal, std::vector<mlir::Value>& doneSignals) {

	// 遍历模块中的所有实例操作  
	module.walk([&](InstanceOp inst) {
		auto moduleName = inst.getModuleName().str();

		// 检查是否是需要连接的模块  
		if (modules_.find(moduleName) != modules_.end()) {
			iID_++;

			// 创建done节点  
			auto instName = inst.getName().str();
			auto doneNodeName = instName + "_done";
			auto spdocDonePort = getInstancePort(inst, "io_spdoc_done");

			if (spdocDonePort) {
				auto uint1Type = UIntType::get(builder.getContext(), 1);
				auto doneNode = builder.create<NodeOp>(loc, spdocDonePort,
					builder.getStringAttr(doneNodeName));
				doneSignals.push_back(doneNode.getResult());
			}

			// 连接io_mid端口 (idRef XOR iID)  
			auto ioMidPort = getInstancePort(inst, "io_mid");
			if (ioMidPort) {
				auto uint8Type = UIntType::get(builder.getContext(), 8);
				auto iidConst = uLit(builder, loc, APInt(8, iID_), 8);
				auto xorOp = builder.create<XorPrimOp>(loc, uint8Type,
					idRef_, iidConst.getResult());
				emitConnect(builder, loc, ioMidPort, xorOp.getResult());
			}

			// 连接io_spdoc_check端口  
			auto spdocCheckPort = getInstancePort(inst, "io_spdoc_check");
			if (spdocCheckPort) {
				emitConnect(builder, loc, spdocCheckPort, onSignal);
			}
		}
		});
}

// 辅助函数：获取实例的指定端口  
//mlir::Value circt::firrtl::stateChecker::getInstancePort(InstanceOp instance, const std::string& portName) {
//	auto portNames = instance.getPortNames();
//	for (size_t i = 0; i < portNames.size(); ++i) {
//		if (cast<StringAttr>(portNames[i]).getValue() == portName) {
//			return instance.getResult(i);
//		}
//	}
//	return nullptr;
//}

void circt::firrtl::stateChecker::instrMem(OpBuilder& builder, Location loc, FModuleOp module,
	const std::set<MemOp>& atkMems, mlir::Value onSignal,
	std::vector<mlir::Value>& doneSignals) {

	// 遍历模块中的所有操作  
	module.walk([&](mlir::Operation* op) {
		if (auto memOp = dyn_cast<MemOp>(op)) {
			// 检查是否是需要监控的内存  
			if (atkMems.find(memOp) != atkMems.end()) {
				// 创建done信号的线网  
				auto uint1Type = UIntType::get(builder.getContext(), 1);
				auto doneWireName = memOp.getName().str() + "_done";
				auto doneWire = builder.create<WireOp>(loc, uint1Type,
					builder.getStringAttr(doneWireName));

				// 将done信号添加到列表中  
				doneSignals.push_back(doneWire.getResult());

				// 为内存创建哈希监控逻辑  
				auto hashMemStmts = hashMem(builder, loc, memOp, onSignal,
					doneWire.getResult());

				// 所有生成的语句会自动插入到当前的插入点  
			}
		}
		});
}

std::vector<mlir::Operation*> circt::firrtl::stateChecker::hashMem(
	OpBuilder& builder, Location loc, MemOp mem,
	mlir::Value onSignal, mlir::Value doneSignal) {

	// 检查内存数据类型是否为UInt  
	auto memDataType = mem.getDataType();
	auto uintType = dyn_cast<UIntType>(memDataType);
	if (!uintType || !uintType.hasWidth()) {
		llvm::errs() << mem.getName().str() << " has unsupported type";
	}

	int32_t w = uintType.getWidth().value();
	uint64_t d = mem.getDepth();
	int32_t dBits = static_cast<int32_t>(std::ceil(std::log2(d + 1)));

	std::vector<mlir::Operation*> allStatements;

	// 创建新的内存，添加spdoc读端口  
	auto newMemPorts = mem.getPorts();
	// 在CIRCT中，需要重新创建内存操作来添加端口  
	// 这里假设我们可以修改现有内存或创建新的内存实例

	// 创建哈希寄存器（带复位）  
	auto hashRegName = mem.getName().str() + "_hash";
	auto [hashReg, hashRegRst] = defReg(builder, loc, hashRegName, clock_,
		hashSz_, reset_, 0);
	allStatements.push_back(hashReg.getOperation());



	// 创建计数寄存器（带复位）  
	auto cntRegName = mem.getName().str() + "_cnt";
	auto [cntReg, cntRegRst] = defReg(builder, loc, cntRegName, clock_,
		dBits, reset_, 0);
	allStatements.push_back(cntReg.getOperation());

	// 创建内存连接逻辑  
	auto enableConst = uLit(builder, loc, APInt(1,1), 1);
	auto [memConStmts, dataRef] = memCon(builder, loc, mem,
		cntReg.getResult(),
		enableConst.getResult(), w);
	allStatements.insert(allStatements.end(), memConStmts.begin(), memConStmts.end());

	// 创建计数器逻辑  
	auto [cLogic, cntOn, cntMux] = counterLogic(builder, loc,
		cntReg.getResult(),
		onSignal, d, dBits);
	allStatements.insert(allStatements.end(), cLogic.begin(), cLogic.end());

	// 创建哈希逻辑  
	auto [hLogic, hashMux] = hashLogic(builder, loc,
		hashReg.getResult(),
		dataRef, cntOn);
	allStatements.insert(allStatements.end(), hLogic.begin(), hLogic.end());

	// 嵌入复位逻辑  
	if (cntRegRst.has_value()) {
		cntRegRst.value()(cntMux.getResult());
	}
	if (hashRegRst.has_value()) {
		hashRegRst.value()(hashMux.getResult());
	}

	// 创建完成脉冲逻辑  
	auto [doneStmts, doneReg, doneSig] = donePulse(builder, loc,
		cntReg.getResult(), d);
	allStatements.insert(allStatements.end(), doneStmts.begin(), doneStmts.end());

	// 连接done信号  
	emitConnect(builder, loc, doneSignal, doneReg);

	// 创建打印语句用于监控  
	std::string formatStr = "[" + mName_ + "(%d)](" + mem.getName().str() + ")=[%h]\\n";
	auto printOp = builder.create<PrintFOp>(
		loc,
		clock_,
		doneSig,
		builder.getStringAttr(formatStr),
		mlir::ValueRange{ idRef_, hashReg.getResult() },
                builder.getStringAttr("")
	);
	allStatements.push_back(printOp);

	return allStatements;
}

std::pair<std::vector<mlir::Operation*>, mlir::Value>
circt::firrtl::stateChecker::memCon(OpBuilder& builder, Location loc, MemOp mem,
	mlir::Value addr, mlir::Value en, int32_t w) {

	std::vector<mlir::Operation*> allStatements;

	// 获取内存的spdoc端口（假设是读端口）  
	auto memReadPort = mem.getResult(0); // 假设第一个结果是读端口  
	auto memReadSF = builder.create<SubfieldOp>(loc, memReadPort, "spdoc");

	// 计算地址宽度  
	auto memDepth = mem.getDepth();
	int32_t addrW = static_cast<int32_t>(std::ceil(std::log2(memDepth)));

	// 创建地址连接  
	auto addrField = builder.create<SubfieldOp>(loc, memReadSF.getResult(), "addr");
	auto addrType = UIntType::get(builder.getContext(), addrW);
	auto addrBits = builder.create<BitsPrimOp>(loc, addrType, addr, addrW - 1, 0);
	emitConnect(builder, loc, addrField.getResult(), addrBits.getResult());
	allStatements.insert(allStatements.end(), { addrField, addrBits });

	// 创建使能连接  
	auto enField = builder.create<SubfieldOp>(loc, memReadSF.getResult(), "en");
	emitConnect(builder, loc, enField.getResult(), en);
	allStatements.push_back(enField);

	// 创建时钟连接  
	auto clkField = builder.create<SubfieldOp>(loc, memReadSF.getResult(), "clk");
	emitConnect(builder, loc, clkField.getResult(), clock_);
	allStatements.push_back(clkField);

	// 获取内存端口名称  
	auto memName = getValueName(memReadSF.getResult());

	// 创建数据节点  
	auto dataField = builder.create<SubfieldOp>(loc, memReadSF.getResult(), "data");
	auto dataNodeName = memName + "_node";
	auto dataNode = builder.create<NodeOp>(loc, 
		dataField.getResult(),
		builder.getStringAttr(dataNodeName));
	allStatements.insert(allStatements.end(), { dataField, dataNode });

	// 创建切片节点  
	std::vector<NodeOp> cutNodes;
	for (int32_t i = 0; i < w; i += hashSz_) {
		int32_t index = i / hashSz_;
		mlir::Value cutValue;

		if (w > i + hashSz_) {
			// 完整的hashSz位切片  
			auto cutType = UIntType::get(builder.getContext(), hashSz_);
			cutValue = builder.create<BitsPrimOp>(loc, cutType, dataNode.getResult(),
				i + hashSz_ - 1, i).getResult();
		}
		else {
			// 最后一个切片，需要填充  
			auto remainingBits = w - i;
			auto remainingType = UIntType::get(builder.getContext(), remainingBits);
			auto bitsOp = builder.create<BitsPrimOp>(loc, remainingType,
				dataNode.getResult(),
				w - 1, i);
			auto padType = UIntType::get(builder.getContext(), hashSz_);
			cutValue = builder.create<PadPrimOp>(loc, padType, bitsOp.getResult(),
				hashSz_).getResult();
			allStatements.push_back(bitsOp);
		}

		auto cutNodeName = memName + "_cut" + std::to_string(index);
		auto cutNode = builder.create<NodeOp>(loc, cutValue,
			builder.getStringAttr(cutNodeName));
		cutNodes.push_back(cutNode);
		allStatements.push_back(cutNode);
	}

	// 创建合并节点  
	std::vector<NodeOp> mergeNodes;
	auto hashType = UIntType::get(builder.getContext(), hashSz_);

	if (cutNodes.size() == 1) {
		// 只有一个切片，直接引用  
		auto mergeNodeName = memName + "_merge0";
		auto mergeNode = builder.create<NodeOp>(loc, 
			cutNodes[0].getResult(),
			builder.getStringAttr(mergeNodeName));
		mergeNodes.push_back(mergeNode);
		allStatements.push_back(mergeNode);
	}
	else {
		// 多个切片，需要XOR合并  
		// 首先创建初始的零值节点  
		auto zeroConst = uLit(builder, loc, APInt(1,0), hashSz_);
		auto merge0Name = memName + "_merge0";
		auto merge0 = builder.create<NodeOp>(loc,  zeroConst.getResult(),
			builder.getStringAttr(merge0Name));
		mergeNodes.push_back(merge0);
		allStatements.insert(allStatements.end(), { zeroConst, merge0 });

		// 依次XOR每个切片  
		for (size_t i = 0; i < cutNodes.size(); ++i) {
			auto xorOp = builder.create<XorPrimOp>(loc, hashType,
				mergeNodes.back().getResult(),
				cutNodes[i].getResult());
			auto mergeNodeName = memName + "_merge" + std::to_string(i + 1);
			auto mergeNode = builder.create<NodeOp>(loc,  xorOp.getResult(),
				builder.getStringAttr(mergeNodeName));
			mergeNodes.push_back(mergeNode);
			allStatements.insert(allStatements.end(), { xorOp, mergeNode });
		}
	}

	return std::make_pair(allStatements, mergeNodes.back().getResult());
}

std::tuple<std::vector<mlir::Operation*>, mlir::Value, MuxPrimOp>
circt::firrtl::stateChecker::counterLogic(OpBuilder& builder, Location loc,
	mlir::Value cntReg, mlir::Value on, uint64_t d, int32_t dBits) {

	auto uint1Type = UIntType::get(builder.getContext(), 1);
	auto uintDBitsType = UIntType::get(builder.getContext(), dBits);

	auto cntRegName = getValueName(cntReg);

	// 创建比较常量  
	auto dConst = uLit(builder, loc, APInt(8,d), dBits);

	// 创建"未完成"检测节点 (cntReg != d)  
	auto cntIngName = cntRegName + "_not_done";
	auto neqOp = builder.create<NEQPrimOp>(loc, uint1Type, cntReg, dConst.getResult());
	auto cntIngNode = builder.create<NodeOp>(loc, neqOp.getResult(),
		builder.getStringAttr(cntIngName));

	// 创建计数使能节点 (on && not_done)  
	auto cntOnName = cntRegName + "_on";
	auto andOp = builder.create<AndPrimOp>(loc, uint1Type, on, cntIngNode.getResult());
	auto cntOnNode = builder.create<NodeOp>(loc, andOp.getResult(),
		builder.getStringAttr(cntOnName));

	// 创建递增常量  
	auto oneConst = uLit(builder, loc, APInt(1,1), dBits);

	// 创建加法操作 (cntReg + 1)  
	auto addOp = builder.create<AddPrimOp>(loc, uintDBitsType, cntReg, oneConst.getResult());

	// 创建多路选择器 (cntOn ? cntReg + 1 : cntReg)  
	auto cntMux = builder.create<MuxPrimOp>(loc, uintDBitsType, cntOnNode.getResult(),
		addOp.getResult(), cntReg);

	// 收集所有操作  
	std::vector<mlir::Operation*> statements = {
	  dConst,
	  neqOp,
	  cntIngNode,
	  andOp,
	  cntOnNode,
	  oneConst,
	  addOp
	};

	return std::make_tuple(statements, cntOnNode.getResult(), cntMux);
}
std::pair<std::vector<mlir::Operation*>, MuxPrimOp>
circt::firrtl::stateChecker::hashLogic(OpBuilder& builder, Location loc,
	mlir::Value hashReg, mlir::Value dataRef, mlir::Value cntOn) {

	auto hashType = UIntType::get(builder.getContext(), hashSz_);

	// 生成随机单位值  
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(1, hashSz_ - 2);
	int unit = dis(gen);

	auto hashRegName = getValueName(hashReg);

	// 创建左移节点  
	auto shlNodeName = hashRegName + "_shl";
	auto bitsType = UIntType::get(builder.getContext(), hashSz_ - unit);
	auto bitsOp = builder.create<BitsPrimOp>(loc, bitsType, hashReg,
		hashSz_ - unit - 1, 0);
	auto shlOp = builder.create<ShlPrimOp>(loc, hashType, bitsOp.getResult(), unit);
	auto shlNode = builder.create<NodeOp>(loc, shlOp.getResult(),
		builder.getStringAttr(shlNodeName));

	// 创建右移节点  
	auto shrNodeName = hashRegName + "_shr";
	auto shrType = UIntType::get(builder.getContext(), hashSz_ - unit);
	auto shrOp = builder.create<ShrPrimOp>(loc, shrType, hashReg, hashSz_ - unit);
	auto zeroConst = uLit(builder, loc, APInt(1,0), hashSz_ - unit);
	auto catOp = builder.create<CatPrimOp>(loc, hashType,
		zeroConst.getResult(), shrOp.getResult());
	auto shrNode = builder.create<NodeOp>(loc, catOp.getResult(),
		builder.getStringAttr(shrNodeName));

	// 创建XOR移位节点  
	auto shNodeName = hashRegName + "_sh";
	auto shXorOp = builder.create<XorPrimOp>(loc, hashType,
		shlNode.getResult(), shrNode.getResult());
	auto shNode = builder.create<NodeOp>(loc,  shXorOp.getResult(),
		builder.getStringAttr(shNodeName));

	// 创建哈希更新的XOR操作  
	auto hashXorOp = builder.create<XorPrimOp>(loc, hashType,
		shNode.getResult(), dataRef);

	// 创建多路选择器  
	auto hashMux = builder.create<MuxPrimOp>(loc, hashType, cntOn,
		hashXorOp.getResult(), hashReg);

	// 收集所有操作  
	std::vector<mlir::Operation*> statements = {
	  bitsOp,
	  shlOp,
	  shlNode,
	  shrOp,
	  zeroConst,
	  catOp,
	  shrNode,
	  shXorOp,
	  shNode,
	  hashXorOp
	};

	return std::make_pair(statements, hashMux);
}

// 辅助函数：获取值的名称  
std::string circt::firrtl::stateChecker::getValueName(mlir::Value value) {
	if (auto defOp = value.getDefiningOp()) {
		if (auto nameAttr = defOp->getAttrOfType<StringAttr>("name")) {
			return nameAttr.getValue().str();
		}
		return defOp->getName().getStringRef().str();
	}
	return "unnamed";
}

std::tuple<std::vector<mlir::Operation*>, mlir::Value, mlir::Value>
circt::firrtl::stateChecker::donePulse(OpBuilder& builder, Location loc, mlir::Value cntReg, uint64_t d) {

	auto uint1Type = UIntType::get(builder.getContext(), 1);

	// 创建比较常量  
	auto dConst = uLit(builder, loc, APInt(8,d), width(cntReg.getDefiningOp()));

	// 创建相等比较节点  
	auto doneName = getValueName(cntReg) + "_done";
	auto eqOp = builder.create<EQPrimOp>(loc, uint1Type, cntReg, dConst.getResult());
	auto doneNode = builder.create<NodeOp>(loc, eqOp.getResult(),
		builder.getStringAttr(doneName));

	// 创建延迟寄存器  
	auto delayRegName = doneName + "_d";
	auto [doneDelayReg, _] = defReg(builder, loc, delayRegName, clock_, 1);

	// 连接延迟寄存器  
	emitConnect(builder, loc, doneDelayReg.getResult(), doneNode.getResult());

	// 创建XOR操作产生脉冲  
	auto xorOp = builder.create<XorPrimOp>(loc, uint1Type,
		doneNode.getResult(),
		doneDelayReg.getResult());

	// 收集所有操作  
	//std::vector<mlir::Operation*> statements = std::vector<mlir::Operation*>{
	//  dConst,
	//  eqOp,
	//  doneNode,
	//  doneDelayReg
	//};
        std::vector<mlir::Operation*> statements;  
        statements.push_back(dConst);  
        statements.push_back(eqOp);   
        statements.push_back(doneNode);  
        statements.push_back(doneDelayReg.getOperation()); 

	return std::make_tuple(statements, doneDelayReg.getResult(), xorOp.getResult());
}

std::vector<mlir::Operation*> circt::firrtl::stateChecker::instrRegs(
	OpBuilder& builder, Location loc,
	std::map<std::string, std::set<RegOp>>& atkRegs,
	mlir::Value onSignal) {

	// Filter registers that are within hash size limit  
	std::map<std::string, std::set<RegOp>> sAtkRegs;
	for (auto& [pfx, regSet] : atkRegs) {
		bool hasLargeReg = false;
		std::vector<std::string> regNames;

		for (auto& reg : regSet) {
			int32_t regWidth = width(const_cast<RegOp&>(reg).getOperation());
			regNames.push_back(name(const_cast<circt::firrtl::RegOp&>(reg).getOperation()));
			if (regWidth > hashSz_) {
				hasLargeReg = true;
			}
		}

		if (hasLargeReg) {
			std::cout << "[" << mName_ << "] " << pfx << "(";
			for (size_t i = 0; i < regNames.size(); ++i) {
				if (i > 0) std::cout << ", ";
				std::cout << regNames[i];
			}
			std::cout << ") contains too large register" << std::endl;
		}

		// Only include register sets where all registers are within size limit  
		bool allWithinLimit = true;
		for (auto& reg : regSet) {
			if (width(const_cast<RegOp&>(reg).getOperation()) > hashSz_) {
				allWithinLimit = false;
				break;
			}
		}

		if (allWithinLimit) {
			sAtkRegs[pfx] = regSet;
		}
	}

	// Create shift and padding operations for each register group  
	std::map<std::string, std::pair<std::vector<mlir::Value>, std::vector<mlir::Operation*>>> padRes;

	for (const auto& [pfx, regSet] : sAtkRegs) {
		std::vector<RegOp*> regPtrs;
		for (const auto& reg : regSet) {
			regPtrs.push_back(const_cast<RegOp*>(&reg));
		}

		auto regOffs = getOffset(regPtrs, hashSz_);
		auto shiftResult = makeShift(builder, loc, regOffs, hashSz_);
		padRes[pfx] = shiftResult;
	}

	// Extract padded references and statements  
	std::map<std::string, std::vector<mlir::Value>> padRefs;
	std::vector<mlir::Operation*> padStmts;

	for (auto& [pfx, result] : padRes) {
		padRefs[pfx] = result.first;
		padStmts.insert(padStmts.end(), result.second.begin(), result.second.end());
	}

	// Create XOR operations for each register group  
	std::map<std::string, std::pair<mlir::Value, std::vector<mlir::Operation*>>> xorRes;

	for (auto& [pfx, regs] : padRefs) {
		auto xorResult = makeXor(builder, loc, pfx, regs, 0, hashSz_);
		xorRes[pfx] = xorResult;
	}

	// Extract XOR statements and top-level XOR results  
	std::vector<mlir::Operation*> xorStmts;
	std::map<std::string, mlir::Value> topXors;

	for (auto& [pfx, result] : xorRes) {
		topXors[pfx] = result.first;
		xorStmts.insert(xorStmts.end(), result.second.begin(), result.second.end());
	}

	// Create print statements for monitoring  
	std::vector<mlir::Operation*> prints;

	for (auto& [pfx, xorValue] : topXors) {
		// Create format string for printf  
		std::string formatStr = "[" + mName_ + "(%d)](" +
			pfx.substr(0, pfx.length() - 1) + ")=[%h]\\n";

		auto printOp = builder.create<PrintFOp>(
			loc,
			clock_,
			onSignal,
			builder.getStringAttr(formatStr),
			mlir::ValueRange{ idRef_, xorValue },
                        builder.getStringAttr("")
		);

		prints.push_back(printOp);
	}

	// Combine all statements  
	std::vector<mlir::Operation*> allStatements;
	allStatements.insert(allStatements.end(), padStmts.begin(), padStmts.end());
	allStatements.insert(allStatements.end(), xorStmts.begin(), xorStmts.end());
	allStatements.insert(allStatements.end(), prints.begin(), prints.end());

	return allStatements;
}

template<typename T> std::vector<std::pair<T*, int>>
circt::firrtl::stateChecker::getOffset(const std::vector<T*>& stmts, int32_t size) {

	//static_assert(std::is_base_of<mlir::Operation, T>::value,
	//	"T must be a subclass of mlir::Operation");

	// 计算每个节点的宽度  
	std::vector<int32_t> widthSeq;
	for (const auto& stmt : stmts) {
		widthSeq.push_back(width(stmt->getOperation()));
	}

	// 计算总位宽  
	int32_t totBitWidth = std::accumulate(widthSeq.begin(), widthSeq.end(), 0);

	// 创建节点和宽度的配对  
	std::vector<std::pair<T*, int32_t>> zipWidth;
	for (size_t i = 0; i < stmts.size(); ++i) {
		zipWidth.push_back({ stmts[i], widthSeq[i] });
	}

	std::vector<std::pair<T*, int>> result;

	if (totBitWidth == 0) {
		// 总位宽为0，返回空序列  
		return result;
	}
	else if (totBitWidth <= size) {
		// 总位宽小于等于目标大小，顺序分配偏移量  
		int sumOffset = 0;
		for (const auto& [node, nodeWidth] : zipWidth) {
			int offset = sumOffset;
			sumOffset += nodeWidth;
			result.push_back({ node, offset });
		}
	}
	else {
		// 总位宽大于目标大小，随机分配偏移量  
		std::random_device rd;
		std::mt19937 gen(rd());

		for (const auto& [node, nodeWidth] : zipWidth) {
			std::uniform_int_distribution<> dis(0, size - nodeWidth);
			int randomOffset = dis(gen);
			result.push_back({ node, randomOffset });
		}
	}

	return result;
}

template<typename T> std::pair<std::vector<mlir::Value>, std::vector<mlir::Operation*>>
circt::firrtl::stateChecker::makeShift(OpBuilder& builder, Location loc,
	const std::vector<std::pair<T*, int>>& stOffs, int size) {

	//static_assert(std::is_base_of<mlir::Operation, T>::value,
	//	"T must be a subclass of mlir::Operation");
	
	std::vector<mlir::Operation*> stmts;
	std::vector<mlir::Value> refs;

	for (auto& [s, o] : stOffs) {
		auto w = width(s->getOperation());
		auto p = size - w - o;

		//auto nodeNameStr = name(node->getOperation());
		//auto nodeValue = wref(node->getOperation());
		auto shlType = UIntType::get(builder.getContext(), w + o);  
	        auto shlOp = builder.create<ShlPrimOp>(loc, shlType, s->getResult(),   
                                          builder.getI32IntegerAttr(o));  
    		auto shlNode = builder.create<NodeOp>(loc, shlOp.getResult(),   
                                         (Twine(name(s->getOperation())) + "_shl").str());
                stmts.push_back(shlOp);  
    		stmts.push_back(shlNode);

		mlir::Value padVal;
		if (p == 0) {
			padVal = shlNode.getResult();
		}
		else {
			auto zeroType = UIntType::get(builder.getContext(), p);  
      			auto zeroConst = builder.create<ConstantOp>(loc, zeroType,   
                                                 APInt::getZero(p));  
      			auto catType = UIntType::get(builder.getContext(), size);  
      			auto catOp = builder.create<CatPrimOp>(loc, catType,   
                                           zeroConst, shlNode.getResult());  
      			padVal = catOp.getResult();  
      			stmts.push_back(zeroConst);  
      			stmts.push_back(catOp);
		}

		auto padNode = builder.create<NodeOp>(loc, padVal,   
                                         (Twine(name(s->getOperation())) + "_pad").str());  
    		stmts.push_back(padNode);  
    		refs.push_back(padNode.getResult());  
  	}
	// 展平所有语句  
	return std::make_pair(refs, stmts);
}

std::pair<mlir::Value, std::vector<mlir::Operation*>>
circt::firrtl::stateChecker::makeXor(OpBuilder& builder, Location loc, const std::string& name,
	const std::vector<mlir::Value>& refs, int i, int32_t size) {

	auto uintType = UIntType::get(builder.getContext(), size);

	switch (refs.size()) {
	case 1:
		// 只有一个输入，直接返回  
		return std::make_pair(refs[0], std::vector<mlir::Operation*>());

	case 2: {
		// 两个输入，创建一个XOR操作  
		auto wireName = name + "_xor" + std::to_string(i);
		auto xorWire = builder.create<WireOp>(loc, uintType,
			builder.getStringAttr(wireName));

		auto xorOp = builder.create<XorPrimOp>(loc, uintType, refs[0], refs[1]);
		emitConnect(builder, loc, xorWire.getResult(), xorOp.getResult());

		std::vector<mlir::Operation*> statements = { xorWire, xorOp };
		return std::make_pair(xorWire.getResult(), statements);
	}

	default: {
		// 多个输入，递归分割  
		size_t mid = refs.size() / 2;
		std::vector<mlir::Value> leftRefs(refs.begin(), refs.begin() + mid);
		std::vector<mlir::Value> rightRefs(refs.begin() + mid, refs.end());

		auto [xor1, stmts1] = makeXor(builder, loc, name, leftRefs, 2 * i + 1, size);
		auto [xor2, stmts2] = makeXor(builder, loc, name, rightRefs, 2 * i + 2, size);

		auto wireName = name + "_xor" + std::to_string(i);
		auto xorWire = builder.create<WireOp>(loc, uintType,
			builder.getStringAttr(wireName));

		auto xorOp = builder.create<XorPrimOp>(loc, uintType, xor1, xor2);
		emitConnect(builder, loc, xorWire.getResult(), xorOp.getResult());

		// 合并所有语句  
		std::vector<mlir::Operation*> allStatements;
		allStatements.insert(allStatements.end(), stmts1.begin(), stmts1.end());
		allStatements.insert(allStatements.end(), stmts2.begin(), stmts2.end());
		allStatements.push_back(xorWire);
		allStatements.push_back(xorOp);

		return std::make_pair(xorWire.getResult(), allStatements);
	}
	}
}

std::tuple<std::optional<PortInfo>, std::optional<PortInfo>, bool>
circt::firrtl::stateChecker::hasClockAndReset(FModuleOp module) {
	auto ports = module.getPorts();

	std::optional<PortInfo> clockPort;
	std::optional<PortInfo> resetPort;

	// Iterate through all ports to find clock and reset  
	for (const auto& port : ports) {
		auto portName = port.getName();

		if (portName == "clock" || portName == "gated_clock") {
			clockPort = port;
		}
		else if (portName == "reset") {
			resetPort = port;
		}
	}

	bool hasCNR = clockPort.has_value() && resetPort.has_value();

	return std::make_tuple(clockPort, resetPort, hasCNR);
}

void circt::firrtl::stateChecker::findTopInst(std::vector<InstanceOp>& insts, mlir::Operation* op) {
	if (auto instOp = dyn_cast<InstanceOp>(op)) {
		if (instOp.getModuleName() == topModuleName_) {
			insts.push_back(instOp);
		}
	}

	// 递归遍历所有子操作  
	for (auto& region : op->getRegions()) {
		for (auto& block : region) {
			for (auto& childOp : block) {
				findTopInst(insts, &childOp);
			}
		}
	}
}

// 连接spec doctor端口  
void circt::firrtl::stateChecker::connectSpdoc(OpBuilder& builder, Location loc,
	mlir::Value onPort, mlir::Value donePort,
	InstanceOp topInst) {

	// 查找需要替换的连接操作  
	std::vector<ConnectOp> connectsToReplace;

	topInst->getParentOp()->walk([&](ConnectOp connectOp) {
		// 检查是否是连接到done端口的操作  
		if (connectOp.getDest() == donePort) {
			connectsToReplace.push_back(connectOp);
		}
		});

	// 替换找到的连接操作  
	for (auto connectOp : connectsToReplace) {
		builder.setInsertionPoint(connectOp);

		// 创建连接到topInst的io_spdoc_check端口  
		auto spdocCheckPort = getInstancePort(topInst, "io_spdoc_check");
		if (spdocCheckPort) {
			emitConnect(builder, loc, spdocCheckPort, onPort);
		}

		// 创建从topInst的io_spdoc_done端口到done端口的连接  
		auto spdocDonePort = getInstancePort(topInst, "io_spdoc_done");
		if (spdocDonePort) {
			emitConnect(builder, loc, donePort, spdocDonePort);
		}

		// 删除原始连接  
		connectOp.erase();
	}
}

// 辅助函数：获取实例的指定端口  
mlir::Value circt::firrtl::stateChecker::getInstancePort(InstanceOp instance, const std::string& portName) {
	auto portNames = instance.getPortNames();
	for (size_t i = 0; i < portNames.size(); ++i) {
		if (cast<StringAttr>(portNames[i]).getValue() == portName) {
			return instance.getResult(i);
		}
	}
	return nullptr;
}

//这种分括号统统合起来，到时候直接变成一个函数处理：https://deepwiki.com/search/wrefwrefinstance_c785e693-d60b-4764-81f3-414743b0ae95

/// 封装可能返回的操作类型
//class RegisterResult {
//  mlir::Operation* op_; // 存储原始操作指针

//public:
  // 构造函数（显式禁止隐式转换）
//  explicit RegisterResult(RegOp op) : op_(op.getOperation()) {}
//  explicit RegisterResult(RegResetOp op) : op_(op.getOperation()) {}

  // 类型检查接口
//  bool isRegOp() const { return mlir::isa<RegOp>(op_); }
//  bool isRegResetOp() const { return mlir::isa<RegResetOp>(op_); }

  // 安全类型转换
//  RegOp getRegOp() const {
//    assert(isRegOp());
//    return mlir::cast<RegOp>(op_);
//  }
//  RegResetOp getRegResetOp() const {
//    assert(isRegResetOp());
//    return mlir::cast<RegResetOp>(op_);
//  }

  // 通用操作接口（所有寄存器类型共享的功能）
//  mlir::Value getResult() const { return op_->getResult(0); }
//  mlir::Type getType() const { return getResult().getType(); }
//};

std::pair<circt::firrtl::RegisterResult, std::optional<std::function<void(mlir::Value)>>>
circt::firrtl::stateChecker::defReg(OpBuilder& builder, Location loc, const std::string& name,
	mlir::Value clk, int32_t width,
	std::optional<mlir::Value> rst, int init) {

	auto uint1Type = UIntType::get(builder.getContext(), 1);
	auto uintWidthType = UIntType::get(builder.getContext(), width);

	// 创建寄存器操作  
	circt::firrtl::RegisterResult regResult;
        //std::optional<std::function<void(mlir::Value)>> resetConnection;
	if (rst.has_value()) {
		// 创建带复位的寄存器  
                //RegResetOp regOp;
		auto initValue = uLit(builder, loc, APInt(32, init), width);
		auto regReset = builder.create<RegResetOp>(loc, uintWidthType, clk, rst.value(),
			initValue.getResult(),
			builder.getStringAttr(name));
                regResult = circt::firrtl::RegisterResult(regReset);
	}
	else {
                //RegOp regOp;
		// 创建普通寄存器  
		auto reg = builder.create<RegOp>(loc, uintWidthType, clk,
			builder.getStringAttr(name));
                regResult = circt::firrtl::RegisterResult(reg);
	}

	// 创建复位连接函数（如果有复位信号）  
	std::optional<std::function<void(mlir::Value)>> resetConnection;
	if (rst.has_value()) {
		resetConnection = [=, &builder](mlir::Value value) {
			auto initConst = uLit(builder, loc, APInt(32, init), width);
			auto muxOp = builder.create<MuxPrimOp>(loc, uintWidthType,
				rst.value(), initConst.getResult(), value);
			emitConnect(builder, loc, regResult.getResult(), muxOp.getResult());
			};
	}

	return std::make_pair(regResult, resetConnection);
}

// 获取节点的值引用  
mlir::Value circt::firrtl::stateChecker::wref(mlir::Operation* op) {
	// 根据操作类型返回相应的结果值  
	if (auto memOp = dyn_cast<MemOp>(op)) {
		// 对于内存操作，需要返回特定的端口  
		llvm::errs() <<"Memory wref requires port specification";
	}
	else if (auto instOp = dyn_cast<InstanceOp>(op)) {
		// 对于实例操作，需要返回特定的端口  
		llvm::errs() << "Instance wref requires port specification";
	}
	else if (auto regOp = dyn_cast<RegOp>(op)) {
		return regOp.getResult();
	}
	else if (auto regResetOp = dyn_cast<RegResetOp>(op)) {
		return regResetOp.getResult();
	}
	else if (auto wireOp = dyn_cast<WireOp>(op)) {
		return wireOp.getResult();
	}
	else if (auto nodeOp = dyn_cast<NodeOp>(op)) {
		return nodeOp.getResult();
	}
	else {
		llvm::errs() << "wref not supported on operation: " <<
			op->getName().getStringRef().str();
	}
}

// 获取模块端口的引用  
mlir::Value circt::firrtl::stateChecker::wref(FModuleOp module, size_t portIndex) {
	return module.getArgument(portIndex);
}

// 获取实例端口的引用  
mlir::Value circt::firrtl::stateChecker::wref(InstanceOp instance, size_t portIndex) {
	return instance.getResult(portIndex);
}

// 获取内存端口的引用  
mlir::Value circt::firrtl::stateChecker::wref(MemOp memory, size_t portIndex) {
	return memory.getResult(portIndex);
}

int circt::firrtl::stateChecker::width(llvm::PointerUnion<Operation*, Value> input) {
	Type resultType;  
    
        if (auto *op = input.dyn_cast<Operation*>()) {  
      
            if (auto regOp = dyn_cast<RegOp>(op)) {  
                resultType = regOp.getResult().getType();  
            } else if (auto wireOp = dyn_cast<WireOp>(op)) {  
                resultType = wireOp.getResult().getType();  
            } else if (auto regResetOp = dyn_cast<RegResetOp>(op)) {  
                resultType = regResetOp.getResult().getType();  
            } else {  
      
            return -1;  
          }  
        } else if (auto value = input.dyn_cast<Value>()) {  
      
            resultType = value.getType();  
        } else {  
            return -1;  
        }  
     
        auto firrtlType = type_dyn_cast<FIRRTLBaseType>(resultType);  
        if (!firrtlType) {  
            return -1;  
        }   
        return getBitWidth(firrtlType).value();
}

std::string circt::firrtl::stateChecker::name(mlir::Operation* op) {
	if (auto regOp = dyn_cast<RegOp>(op)) {
		return regOp.getName().str();
	}
	else if (auto wireOp = dyn_cast<WireOp>(op)) {
		return wireOp.getName().str();
	}
	else if (auto nodeOp = dyn_cast<NodeOp>(op)) {
		return nodeOp.getName().str();
	}
	else {
		llvm::errs() << "name not supported on operation: " <<
			op->getName().getStringRef().str();
	}
}

std::string circt::firrtl::stateChecker::name(FModuleLike module, size_t portIndex) {
	return module.getPortName(portIndex).str();
}

UIntType circt::firrtl::stateChecker::uTp(OpBuilder& builder, int32_t width) {
	return UIntType::get(builder.getContext(), width);
}

ConstantOp circt::firrtl::stateChecker::uLit(OpBuilder& builder, Location loc, const APInt& value, int32_t width) {
	UIntType type;
	APInt adjustedValue = value;

	if (width == 0) {
		// 如果没有指定宽度，使用值的最小宽度  
		type = UIntType::get(builder.getContext(), value.getBitWidth());
	}
	else {
		// 如果指定了宽度，调整值的位宽  
		type = UIntType::get(builder.getContext(), width);
		adjustedValue = value.zextOrTrunc(width);
	}

	return builder.create<ConstantOp>(loc, type, adjustedValue);
}

FModuleOp circt::firrtl::stateChecker::instrument(
	FModuleOp module,
	const std::set<MemOp>& atkMems,
	const std::map<std::string, std::set<RegOp>>& atkRegs) {

	mName_ = module.getModuleName().str();
	iID_ = 0;

	auto builder = OpBuilder::atBlockBegin(module.getBodyBlock());
	auto loc = module.getLoc();

	// Check if this module should be instrumented  
	if (modules_.find(mName_) != modules_.end()) {
		// Find clock and reset in the module  
		auto [clockPort, resetPort, hasCNR] = hasClockAndReset(module);
		if (!clockPort.has_value()) {
			llvm:: errs() << "Module " << mName_ << " does not have clock";
		}
		// Get clock value from port  
		//size_t portIndex = getPortIndex(module, clockPort.value().getName());  
		auto ports = module.getPorts();  
		size_t portIndex = 0;  
		for (size_t i = 0; i < ports.size(); ++i) {  
    			if (ports[i].getName() == clockPort.value().getName()) {  
        			portIndex = i;  
        			break;  
    			}  
		}
		mlir::Value clockValue = module.getArgument(portIndex);
		clock_ = builder.create<RefSendOp>(loc, clockValue);//module.getArgument(getPortIndex(module, clockPort->getName()));

		// Create input and output ports for spec doctor  
		auto uint1Type = UIntType::get(builder.getContext(), 1);

		// Create delay register for reset logic  
		auto delayRegName = mName_ + "_spdoc_reset";
		auto [onDelayReg, _] = defReg(builder, loc, delayRegName, clock_, 1);

		// Get reference to spdoc_check port (will be added to module)  
		auto onPortIndex = module.getNumPorts(); // Index for new port  

		// Create NOT gate for delay connection  
		auto notOp = builder.create<NotPrimOp>(loc, uint1Type,
			module.getArgument(onPortIndex));
		emitConnect(builder, loc, onDelayReg.getResult(), notOp.getResult());

		// Create AND gate for reset signal  
		auto andOp = builder.create<AndPrimOp>(loc, uint1Type,
			module.getArgument(onPortIndex),
			onDelayReg.getResult());
		reset_ = andOp.getResult();

		// Generate unique ID for this module instance  
		auto [idPort, idValue] = getID(builder, loc, module, mID_++);
		idRef_ = idValue.getResult();

		std::vector<mlir::Value> doneSignals;

		// Connect specdoctor ports  
		conPorts(builder, loc, module, module.getArgument(onPortIndex), doneSignals);

		// Instrument memories  
		instrMem(builder, loc, module, atkMems,
			module.getArgument(onPortIndex), doneSignals);

		// Instrument registers
                auto atkRegsCopy = atkRegs;
		auto regConnections = instrRegs(builder, loc, atkRegsCopy, reset_);

		// Connect done signals  
		auto donePortIndex = onPortIndex + 1; // Index for done port  
		auto [doneWires, doneRef] = connectDone(builder, loc, doneSignals,
			module.getArgument(donePortIndex));

		// Create new ports  
		auto newPorts = module.getPorts();

		// Add ID port if needed  
		if (!idPort.empty()) {
			//newPorts.push_back(idPort.value());
			std::vector<std::pair<unsigned, PortInfo>> ports1 = {{module.getNumPorts(), idPort[0]}};
			module.insertPorts(ports1);
		}

		// Add spdoc_check input port  
		std::vector<std::pair<unsigned, PortInfo>> ports2 = {{module.getNumPorts(), PortInfo{
                  builder.getStringAttr("io_spdoc_check"),
                  uint1Type,
                  Direction::In,
                  {},
                  loc
                        }}};
		module.insertPorts(ports2);

		// Add spdoc_done output port
             std::vector<std::pair<unsigned, PortInfo>> ports3 = {{module.getNumPorts(), PortInfo{
                  builder.getStringAttr("io_spdoc_done"),
                  uint1Type,
                  Direction::Out,
                  {},
                  loc
                        }}};
                module.insertPorts(ports3);    
		

		return module;

	}
	else if (mName_ == outerName_) {
		// Handle top-level outer module connection  
		auto ports = module.getPorts();

		// Find spdoc ports  
		PortInfo* spdocCheckPort = nullptr;
		PortInfo* spdocDonePort = nullptr;

		for (auto& port : ports) {
			auto portName = port.getName();
			if (portName.contains("spdoc_check")) {
				spdocCheckPort = &port;
			}
			else if (portName.contains("spdoc_done")) {
				spdocDonePort = &port;
			}
		}

		if (!spdocCheckPort) {
			llvm::errs() << mName_ << " does not have spdoc_check";
		}
		if (!spdocDonePort) {
			llvm::errs() << mName_ << " does not have spdoc_done";
		}

		// Find the top module instance  
		std::vector<InstanceOp> topInstances;
		findTopInst(topInstances, module.getOperation());

		if (topInstances.size() != 1) {
			if (topInstances.empty()) {
				llvm::errs() << mName_ << " does not have " << topModuleName_;
			}
			else {
				llvm::errs() << mName_ << " has multiple " << topModuleName_;
			}
		}

		auto topInst = topInstances[0];
		//std::optional<size_t> getPortIndex(FModuleLike module, StringRef portName) {
		size_t checkIndex;
		size_t doneIndex;
		for (size_t i = 0, e = module.getNumPorts(); i < e; ++i) {
			if (module.getPortName(i) == spdocCheckPort->getName()) {
      				checkIndex = i;
    			}else if(module.getPortName(i) == spdocDonePort->getName()){
				doneIndex = i;
			}
  		}
		//}
		connectSpdoc(builder, loc,
			module.getArgument(checkIndex),
			module.getArgument(doneIndex),
			topInst);

		return module;
	}

	// Return module unchanged if no instrumentation needed  
	return module;
}

circt::firrtl::stateChecker::stateChecker(std::string outerName_,
	llvm::StringRef topModuleName_,
	std::set<llvm::StringRef> modules_,
	int hashSz_)
	: outerName_(outerName_), topModuleName_(topModuleName_),
	modules_(modules_), hashSz_(hashSz_), mName_(""), mID_(0), iID_(0) {
}

