#pragma once

#ifndef CIRCT_DIALECT_FIRRTL_SPDOCINSTRPASS_H
#define CIRCT_DIALECT_FIRRTL_SPDOCINSTRPASS_H

#include "circt/Dialect/FIRRTL/spdocInstrPass.h"
#include <set>
#include "circt/Dialect/FIRRTL/graphLedger.h"
#include "circt/Dialect/FIRRTL/moduleInfo.h"
#include "circt/Dialect/FIRRTL/stateChecker.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/BuiltinOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLInstanceGraph.h"

namespace circt {
namespace firrtl {

#define GEN_PASS_DECL_SPDOCINSTRPASS
#include "circt/Dialect/FIRRTL/Passes.h.inc"

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

		struct SpdocInstrPass : public mlir::PassWrapper<SpdocInstrPass, mlir::OperationPass<circt::firrtl::CircuitOp>> {
			//StringRef getArgument() const override { return "firrtl-spdoc-instr"; }
		public:
			MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(SpdocInstrPass);
			SpdocInstrPass() = default;
			SpdocInstrPass(const SpdocInstrPass& other) : PassWrapper(other) {}
			void runOnOperation() override;

			StringRef getArgument() const override;
			StringRef getDescription() const override;

			//void phase0(circt::igraph::InstanceGraph& instanceGraph,
			//	llvm::DenseMap<llvm::StringRef, moduleInfo>& moduleInfos,
			//	llvm::StringRef topModuleName,
			//	const llvm::SetVector<llvm::StringRef>& topPorts = {});
			//void phase1(circt::igraph::InstanceGraph& instanceGraph,
			//	llvm::DenseMap<llvm::StringRef, moduleInfo>& moduleInfos);
			void phase0(moduleNet& mNet, llvm::DenseMap<llvm::StringRef, moduleInfo*>& mInfos,
           			llvm::StringRef& topModuleName,
           			std::set<llvm::StringRef> topPorts);
			void phase1(moduleNet& mNet, llvm::DenseMap<llvm::StringRef, moduleInfo*>& mInfos);

		private:
			// Helper method to check if debug mode is enabled
			bool isDebugEnabled();
			void printd(std::string& str, bool d);

			// Helper method to find the top-level module
			FModuleLike findTopModule(CircuitOp circuitOp);

			// Maps to store module information
			llvm::DenseMap<StringAttr, Operation*> modules;
		};

		// Declaration of the pass registration function
		std::unique_ptr<mlir::Pass> createspdocInstrPass();

	} // namespace firrtl
} // namespace circt

#endif // CIRCT_DIALECT_FIRRTL_SPDOCINSTRPASS_H
