// Copyright (c) 2015-2016 The Khronos Group Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and/or associated documentation files (the
// "Materials"), to deal in the Materials without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Materials, and to
// permit persons to whom the Materials are furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Materials.
//
// MODIFICATIONS TO THIS FILE MAY MEAN IT NO LONGER ACCURATELY REFLECTS
// KHRONOS STANDARDS. THE UNMODIFIED, NORMATIVE VERSIONS OF KHRONOS
// SPECIFICATIONS AND HEADER INFORMATION ARE LOCATED AT
//    https://www.khronos.org/registry/
//
// THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.

#include "headers/spirv.h"
#include "validate_types.h"

#include <algorithm>
#include <cassert>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

using std::find;
using std::string;
using std::unordered_set;
using std::vector;
using namespace libspirv;

namespace {
bool IsInstructionInLayoutSection(ModuleLayoutSection layout, SpvOp op) {
  // See Section 2.4
  bool out = false;
  // clang-format off
  switch (layout) {
    case kLayoutCapabilities:  out = op == SpvOpCapability;    break;
    case kLayoutExtensions:    out = op == SpvOpExtension;     break;
    case kLayoutExtInstImport: out = op == SpvOpExtInstImport; break;
    case kLayoutMemoryModel:   out = op == SpvOpMemoryModel;   break;
    case kLayoutEntryPoint:    out = op == SpvOpEntryPoint;    break;
    case kLayoutExecutionMode: out = op == SpvOpExecutionMode; break;
    case kLayoutDebug1:
      switch (op) {
        case SpvOpSourceContinued:
        case SpvOpSource:
        case SpvOpSourceExtension:
        case SpvOpString:
          out = true;
          break;
        default: break;
      }
      break;
    case kLayoutDebug2:
      switch (op) {
        case SpvOpName:
        case SpvOpMemberName:
          out = true;
          break;
        default: break;
      }
      break;
    case kLayoutAnnotations:
      switch (op) {
        case SpvOpDecorate:
        case SpvOpMemberDecorate:
        case SpvOpGroupDecorate:
        case SpvOpGroupMemberDecorate:
        case SpvOpDecorationGroup:
          out = true;
          break;
        default: break;
      }
      break;
    case kLayoutTypes:
      switch (op) {
        case SpvOpTypeVoid:
        case SpvOpTypeBool:
        case SpvOpTypeInt:
        case SpvOpTypeFloat:
        case SpvOpTypeVector:
        case SpvOpTypeMatrix:
        case SpvOpTypeImage:
        case SpvOpTypeSampler:
        case SpvOpTypeSampledImage:
        case SpvOpTypeArray:
        case SpvOpTypeRuntimeArray:
        case SpvOpTypeStruct:
        case SpvOpTypeOpaque:
        case SpvOpTypePointer:
        case SpvOpTypeFunction:
        case SpvOpTypeEvent:
        case SpvOpTypeDeviceEvent:
        case SpvOpTypeReserveId:
        case SpvOpTypeQueue:
        case SpvOpTypePipe:
        case SpvOpTypeForwardPointer:
        case SpvOpConstantTrue:
        case SpvOpConstantFalse:
        case SpvOpConstant:
        case SpvOpConstantComposite:
        case SpvOpConstantSampler:
        case SpvOpConstantNull:
        case SpvOpSpecConstantTrue:
        case SpvOpSpecConstantFalse:
        case SpvOpSpecConstant:
        case SpvOpSpecConstantComposite:
        case SpvOpSpecConstantOp:
        case SpvOpVariable:
        case SpvOpLine:
          out = true;
          break;
        default: break;
      }
      break;
    case kLayoutFunctionDeclarations:
    case kLayoutFunctionDefinitions:
      // NOTE: These instructions should NOT be in these layout sections
      switch (op) {
        case SpvOpCapability:
        case SpvOpExtension:
        case SpvOpExtInstImport:
        case SpvOpMemoryModel:
        case SpvOpEntryPoint:
        case SpvOpExecutionMode:
        case SpvOpSourceContinued:
        case SpvOpSource:
        case SpvOpSourceExtension:
        case SpvOpString:
        case SpvOpName:
        case SpvOpMemberName:
        case SpvOpDecorate:
        case SpvOpMemberDecorate:
        case SpvOpGroupDecorate:
        case SpvOpGroupMemberDecorate:
        case SpvOpDecorationGroup:
        case SpvOpTypeVoid:
        case SpvOpTypeBool:
        case SpvOpTypeInt:
        case SpvOpTypeFloat:
        case SpvOpTypeVector:
        case SpvOpTypeMatrix:
        case SpvOpTypeImage:
        case SpvOpTypeSampler:
        case SpvOpTypeSampledImage:
        case SpvOpTypeArray:
        case SpvOpTypeRuntimeArray:
        case SpvOpTypeStruct:
        case SpvOpTypeOpaque:
        case SpvOpTypePointer:
        case SpvOpTypeFunction:
        case SpvOpTypeEvent:
        case SpvOpTypeDeviceEvent:
        case SpvOpTypeReserveId:
        case SpvOpTypeQueue:
        case SpvOpTypePipe:
        case SpvOpTypeForwardPointer:
        case SpvOpConstantTrue:
        case SpvOpConstantFalse:
        case SpvOpConstant:
        case SpvOpConstantComposite:
        case SpvOpConstantSampler:
        case SpvOpConstantNull:
        case SpvOpSpecConstantTrue:
        case SpvOpSpecConstantFalse:
        case SpvOpSpecConstant:
        case SpvOpSpecConstantComposite:
        case SpvOpSpecConstantOp:
          out = false;
          break;
      default:
        out = true;
        break;
      }
  }
  // clang-format on
  return out;
}
}

namespace libspirv {

ValidationState_t::ValidationState_t(spv_diagnostic* diagnostic,
                                     uint32_t options)
    : diagnostic_(diagnostic),
      instruction_counter_(0),
      defined_ids_{},
      unresolved_forward_ids_{},
      validation_flags_(options),
      operand_names_{},
      current_layout_stage_(kLayoutCapabilities),
      module_functions_(*this) {}

spv_result_t ValidationState_t::defineId(uint32_t id) {
  if (defined_ids_.find(id) == end(defined_ids_)) {
    defined_ids_.insert(id);
  } else {
    return diag(SPV_ERROR_INVALID_ID) << "ID cannot be assigned multiple times";
  }
  return SPV_SUCCESS;
}

spv_result_t ValidationState_t::forwardDeclareId(uint32_t id) {
  unresolved_forward_ids_.insert(id);
  return SPV_SUCCESS;
}

spv_result_t ValidationState_t::removeIfForwardDeclared(uint32_t id) {
  unresolved_forward_ids_.erase(id);
  return SPV_SUCCESS;
}

void ValidationState_t::assignNameToId(uint32_t id, string name) {
  operand_names_[id] = name;
}

string ValidationState_t::getIdName(uint32_t id) const {
  std::stringstream out;
  out << id;
  if (operand_names_.find(id) != end(operand_names_)) {
    out << "[" << operand_names_.at(id) << "]";
  }
  return out.str();
}

size_t ValidationState_t::unresolvedForwardIdCount() const {
  return unresolved_forward_ids_.size();
}

vector<uint32_t> ValidationState_t::unresolvedForwardIds() const {
  vector<uint32_t> out(begin(unresolved_forward_ids_),
                       end(unresolved_forward_ids_));
  return out;
}

bool ValidationState_t::isDefinedId(uint32_t id) const {
  return defined_ids_.find(id) != end(defined_ids_);
}

bool ValidationState_t::is_enabled(spv_validate_options_t flag) const {
  return (flag & validation_flags_) == flag;
}

// Increments the instruction count. Used for diagnostic
int ValidationState_t::incrementInstructionCount() {
  return instruction_counter_++;
}

ModuleLayoutSection ValidationState_t::getLayoutStage() const {
  return current_layout_stage_;
}

void ValidationState_t::progressToNextLayoutStageOrder() {
  // Guard against going past the last element(kLayoutFunctionDefinitions)
  if (current_layout_stage_ <= kLayoutFunctionDefinitions) {
    current_layout_stage_ =
        static_cast<ModuleLayoutSection>(current_layout_stage_ + 1);
  }
}

bool ValidationState_t::isOpcodeInCurrentLayoutStage(SpvOp op) {
  return IsInstructionInLayoutSection(current_layout_stage_, op);
}

DiagnosticStream ValidationState_t::diag(spv_result_t error_code) const {
  return libspirv::DiagnosticStream(
      {0, 0, static_cast<size_t>(instruction_counter_)}, diagnostic_,
      error_code);
}

Functions& ValidationState_t::get_functions() { return module_functions_; }

bool ValidationState_t::in_function_body() const {
  return module_functions_.in_function_body();
}

bool ValidationState_t::in_block() const {
  return module_functions_.in_block();
}

Functions::Functions(ValidationState_t& module)
    : module_(module), in_function_(false), in_block_(false) {}

bool Functions::in_function_body() const { return in_function_; }

bool Functions::in_block() const { return in_block_; }

spv_result_t Functions::RegisterFunction(uint32_t id, uint32_t ret_type_id,
                                         uint32_t function_control,
                                         uint32_t function_type_id) {
  assert(in_function_ == false &&
         "Function instructions can not be declared in a function");
  in_function_ = true;
  id_.emplace_back(id);
  type_id_.emplace_back(function_type_id);
  declaration_type_.emplace_back(FunctionDecl::kFunctionDeclUnknown);
  block_ids_.emplace_back();
  variable_ids_.emplace_back();
  parameter_ids_.emplace_back();

  // TODO(umar): validate function type and type_id
  (void)ret_type_id;
  (void)function_control;

  return SPV_SUCCESS;
}

spv_result_t Functions::RegisterFunctionParameter(uint32_t id,
                                                  uint32_t type_id) {
  assert(in_function_ == true &&
         "Function parameter instructions cannot be declared outside of a "
         "function");
  if (in_block()) {
    return module_.diag(SPV_ERROR_INVALID_LAYOUT)
           << "Function parameters cannot be called in blocks";
  }
  if (block_ids_.back().size() != 0) {
    return module_.diag(SPV_ERROR_INVALID_LAYOUT)
           << "Function parameters must only appear immediatly after the "
              "function definition";
  }
  // TODO(umar): Validate function parameter type order and count
  // TODO(umar): Use these variables to validate parameter type
  (void)id;
  (void)type_id;
  return SPV_SUCCESS;
}

spv_result_t Functions::RegisterSetFunctionDeclType(FunctionDecl type) {
  assert(in_function_ == true &&
         "Function can not be declared inside of another function");
  if (declaration_type_.size() <= 1 || type == *(end(declaration_type_) - 2) ||
      type == FunctionDecl::kFunctionDeclDeclaration) {
    declaration_type_.back() = type;
  } else if (type == FunctionDecl::kFunctionDeclDeclaration) {
    return module_.diag(SPV_ERROR_INVALID_LAYOUT)
           << "Function declartions must appear before function definitions";
  } else {
    declaration_type_.back() = type;
  }
  return SPV_SUCCESS;
}

spv_result_t Functions::RegisterBlock(uint32_t id) {
  assert(in_function_ == true && "Labels can only exsist in functions");
  if (module_.getLayoutStage() ==
      ModuleLayoutSection::kLayoutFunctionDeclarations) {
    return module_.diag(SPV_ERROR_INVALID_LAYOUT)
           << "Function declartions must appear before function definitions";
  }
  if (declaration_type_.back() != FunctionDecl::kFunctionDeclDefinition) {
    // NOTE: This should not happen. We should know that this function is a
    // definition at this point.
    return module_.diag(SPV_ERROR_INTERNAL)
           << "Function declaration type should have already been defined";
  }

  block_ids_.back().push_back(id);
  in_block_ = true;
  return SPV_SUCCESS;
}

spv_result_t Functions::RegisterFunctionEnd() {
  assert(in_function_ == true &&
         "Function end can only be called in functions");
  if (in_block()) {
    return module_.diag(SPV_ERROR_INVALID_LAYOUT)
           << "Function end cannot be called in blocks";
  }
  in_function_ = false;
  return SPV_SUCCESS;
}

spv_result_t Functions::RegisterBlockEnd() {
  assert(in_block_ == true &&
         "Branch instruction can only be called in a block");
  in_block_ = false;
  return SPV_SUCCESS;
}

size_t Functions::get_block_count() {
  assert(in_function_ == true &&
         "Branch instruction can only be called in a block");
  return block_ids_.back().size();
}
}
