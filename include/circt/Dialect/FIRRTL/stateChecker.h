#pragma once

#ifndef CIRCT_DIALECT_FIRRTL_STATECHECKER_H
#define CIRCT_DIALECT_FIRRTL_STATECHECKER_H

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
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"
#include "mlir/IR/BuiltinOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLInstanceGraph.h"
//#define GEN_PASS_DEF_SPDOCINSTRPASS
//#include "circt/Dialect/FIRRTL/Passes.h.inc"

using namespace circt;
using namespace firrtl;

//using StringSet = std::set<std::string>;

//using StringToStringSetMap = std::map<std::string, std::set<std::string>>;
//using StringAndStringSetMap = std::pair<std::string, std::set<std::pair<std::string, std::set<std::string> > > >;
//using ComplexMap = std::map<std::string, StringAndStringSetMap>; //for getInstanceMap

//using StringToNodeSetMap = std::map<std::string, std::set<Node*>>;
//using WDefInstanceToStringSetMap = std::map<InstanceOp*, std::set<std::string>>;
//using ComplexTuple = std::pair<StringToNodeSetMap, WDefInstanceToStringSetMap>; //for findPortSrcs

//using StringToNodeSetMap = DenseMap<StringAttr, DenseSet<Node*>>;
//using WDefInstanceToStringSetMap = DenseMap<InstanceOp*, DenseSet<StringAttr>>;
//using ComplexTuple_connected = std::pair<StringToNodeSetMap, WDefInstanceToStringSetMap>;//for findOutPortsConnected

namespace circt {
namespace firrtl {
class Node; // 前向声明（若头文件已包含）
//bool isStatement(mlir::Operation *op);
} // namespace firrtl
} // namespace circt

using Node = circt::firrtl::Node;

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

class RegisterResult {
  mlir::Operation* op_; //   ^x ^b  ^n^=  ^k ^s^m  ^| ^l^g ^r^h

public:
  //  ^~^d ^`  ^g  ^u   ^h ^x   ^o  ^a    ^z^p  ^o    ^m   ^i
  explicit RegisterResult(RegOp op) : op_(op.getOperation()) {}
  explicit RegisterResult(RegResetOp op) : op_(op.getOperation()) {}

  RegisterResult() = default;

  //     ^~^k  ^` ^=  ^n  ^o
  bool isRegOp() const { return mlir::isa<RegOp>(op_); }
  bool isRegResetOp() const { return mlir::isa<RegResetOp>(op_); }

  //   ^i ^e     ^~^k    ^m
  RegOp getRegOp() const {
    assert(isRegOp());
    return mlir::cast<RegOp>(op_);
  }
  RegResetOp getRegResetOp() const {
    assert(isRegResetOp());
    return mlir::cast<RegResetOp>(op_);
  }

  //  ^`^z ^t  ^s^m  ^| ^n  ^o   ^h ^i^` ^|^i  ^d  ^x ^y     ^~^k ^e     ^z^d ^j^= ^c   ^i
  mlir::Value getResult() const { return op_->getResult(0); }
  mlir::Type getType() const { return getResult().getType(); }
  mlir::Operation* getOperation() const { return op_; }
};

class stateChecker {
private:
	std::string mName_ = "";
	int mID_ = 0;
	int iID_ = 0;
        std::string& outerName_;
        llvm::StringRef& topModuleName_;
        std::set<llvm::StringRef>& modules_;
        int hashSz_ = 64;
	//WRef是RefType
	mlir::Value clock_;
	mlir::Value reset_;
	mlir::Value idRef_;

	std::pair<std::vector<mlir::Operation*>, mlir::Value>
		connectDone(OpBuilder& builder, Location loc,
			const std::vector<mlir::Value>& dones, mlir::Value donePort);

	std::pair<std::vector<PortInfo>, NodeOp>
		getID(OpBuilder& builder, Location loc, FModuleOp module, int moduleID);

	void conPorts(OpBuilder& builder, Location loc, FModuleOp module,
		mlir::Value onSignal, std::vector<mlir::Value>& doneSignals);

	// 辅助函数：获取实例的指定端口  
	mlir::Value getInstancePort(InstanceOp instance, const std::string& portName);

	void instrMem(OpBuilder& builder, Location loc, FModuleOp module,
		const std::set<MemOp>& atkMems, mlir::Value onSignal,
		std::vector<mlir::Value>& doneSignals);

	std::vector<mlir::Operation*> hashMem(
		OpBuilder& builder, Location loc, MemOp mem,
		mlir::Value onSignal, mlir::Value doneSignal);

	std::pair<std::vector<mlir::Operation*>, mlir::Value>
		memCon(OpBuilder& builder, Location loc, MemOp mem,
			mlir::Value addr, mlir::Value en, int32_t w);

	std::tuple<std::vector<mlir::Operation*>, mlir::Value, MuxPrimOp>
		counterLogic(OpBuilder& builder, Location loc,
			mlir::Value cntReg, mlir::Value on, uint64_t d, int32_t dBits);
	std::pair<std::vector<mlir::Operation*>, MuxPrimOp>
		hashLogic(OpBuilder& builder, Location loc,
			mlir::Value hashReg, mlir::Value dataRef, mlir::Value cntOn);

	// 辅助函数：获取值的名称  
	std::string getValueName(mlir::Value value);

	std::tuple<std::vector<mlir::Operation*>, mlir::Value, mlir::Value>
		donePulse(OpBuilder& builder, Location loc, mlir::Value cntReg, uint64_t d);

	std::vector<mlir::Operation*> instrRegs(
		OpBuilder& builder, Location loc,
		std::map<std::string, std::set<RegOp>>& atkRegs,
		mlir::Value onSignal);

	template<typename T> std::vector<std::pair<T*, int>>
	getOffset(const std::vector<T*>& stmts, int32_t size);

	template<typename T>
	std::pair<std::vector<mlir::Value>, std::vector<mlir::Operation*>>
		makeShift(OpBuilder& builder, Location loc,
			const std::vector<std::pair<T*, int>>& stOffs, int size);

	std::pair<mlir::Value, std::vector<mlir::Operation*>>
		makeXor(OpBuilder& builder, Location loc, const std::string& name,
			const std::vector<mlir::Value>& refs, int i, int32_t size);

	std::tuple<std::optional<PortInfo>, std::optional<PortInfo>, bool>
		hasClockAndReset(FModuleOp module);

	void findTopInst(std::vector<InstanceOp>& insts, mlir::Operation* op);

	// 连接spec doctor端口  
	void connectSpdoc(OpBuilder& builder, Location loc,
		mlir::Value onPort, mlir::Value donePort,
		InstanceOp topInst);

	// 辅助函数：获取实例的指定端口  
	//mlir::Value getInstancePort(InstanceOp instance, const std::string& portName);

	//这种分括号统统合起来，到时候直接变成一个函数处理：https://deepwiki.com/search/wrefwrefinstance_c785e693-d60b-4764-81f3-414743b0ae95

	std::pair<RegisterResult, std::optional<std::function<void(mlir::Value)>>>
		defReg(OpBuilder& builder, Location loc, const std::string& name,
			mlir::Value clk, int32_t width,
			std::optional<mlir::Value> rst = std::nullopt, int init = 0);

	// 获取节点的值引用  
	mlir::Value wref(mlir::Operation* op);
        mlir::Value wref(circt::firrtl::FModuleOp module, size_t portIndex);  
        mlir::Value wref(circt::firrtl::InstanceOp instance, size_t portIndex);  
        mlir::Value wref(circt::firrtl::MemOp memory, size_t portIndex);
	// 获取模块端口的引用  
	//mlir::Value wref(FModuleOp module, size_t portIndex);

	// 获取实例端口的引用  
	//mlir::Value wref(InstanceOp instance, size_t portIndex);

	// 获取内存端口的引用  
	//mlir::Value wref(MemOp memory, size_t portIndex);

	int width(llvm::PointerUnion<Operation*, Value> input);

	std::string name(mlir::Operation* op);

	std::string name(FModuleLike module, size_t portIndex);

	UIntType uTp(OpBuilder& builder, int32_t width);

	ConstantOp uLit(OpBuilder& builder, Location loc, const APInt& value, int32_t width = 0);


public:

	FModuleOp instrument(
		FModuleOp module,
		const std::set<MemOp>& atkMems,
		const std::map<std::string, std::set<RegOp>>& atkRegs);

	stateChecker(std::string outerName_,
		llvm::StringRef topModuleName_,
		std::set<llvm::StringRef> modules_,
		int hashSz_ = 64);
};
}//firrtl
}//circt
#endif
