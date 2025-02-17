//===-- ClangPersistentVariables.cpp ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangPersistentVariables.h"
#include "ClangASTImporter.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Core/Value.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

#include "clang/AST/Decl.h"

#include "llvm/ADT/StringMap.h"

using namespace lldb;
using namespace lldb_private;

ClangPersistentVariables::ClangPersistentVariables()
    : lldb_private::PersistentExpressionState(LLVMCastKind::eKindClang) {}

ExpressionVariableSP ClangPersistentVariables::CreatePersistentVariable(
    const lldb::ValueObjectSP &valobj_sp) {
  return AddNewlyConstructedVariable(new ClangExpressionVariable(valobj_sp));
}

ExpressionVariableSP ClangPersistentVariables::CreatePersistentVariable(
    ExecutionContextScope *exe_scope, ConstString name,
    const CompilerType &compiler_type, lldb::ByteOrder byte_order,
    uint32_t addr_byte_size) {
  return AddNewlyConstructedVariable(new ClangExpressionVariable(
      exe_scope, name, compiler_type, byte_order, addr_byte_size));
}

void ClangPersistentVariables::RemovePersistentVariable(
    lldb::ExpressionVariableSP variable) {
  RemoveVariable(variable);

  // Check if the removed variable was the last one that was created. If yes,
  // reuse the variable id for the next variable.

  // Nothing to do if we have not assigned a variable id so far.
  if (m_next_persistent_variable_id == 0)
    return;

  llvm::StringRef name = variable->GetName().GetStringRef();
  // Remove the prefix from the variable that only the indes is left.
  if (!name.consume_front(GetPersistentVariablePrefix(false)))
    return;

  // Check if the variable contained a variable id.
  uint32_t variable_id;
  if (name.getAsInteger(10, variable_id))
    return;
  // If it's the most recent variable id that was assigned, make sure that this
  // variable id will be used for the next persistent variable.
  if (variable_id == m_next_persistent_variable_id - 1)
    m_next_persistent_variable_id--;
}

llvm::Optional<CompilerType>
ClangPersistentVariables::GetCompilerTypeFromPersistentDecl(
    ConstString type_name) {
  PersistentDecl p = m_persistent_decls.lookup(type_name.GetCString());

  if (p.m_decl == nullptr)
    return llvm::None;

  if (clang::TypeDecl *tdecl = llvm::dyn_cast<clang::TypeDecl>(p.m_decl)) {
    opaque_compiler_type_t t = static_cast<opaque_compiler_type_t>(
        const_cast<clang::Type *>(tdecl->getTypeForDecl()));
    return CompilerType(p.m_context, t);
  }
  return llvm::None;
}

void ClangPersistentVariables::RegisterPersistentDecl(ConstString name,
                                                      clang::NamedDecl *decl,
                                                      TypeSystemClang *ctx) {
  PersistentDecl p = {decl, ctx};
  m_persistent_decls.insert(std::make_pair(name.GetCString(), p));

  if (clang::EnumDecl *enum_decl = llvm::dyn_cast<clang::EnumDecl>(decl)) {
    for (clang::EnumConstantDecl *enumerator_decl : enum_decl->enumerators()) {
      p = {enumerator_decl, ctx};
      m_persistent_decls.insert(std::make_pair(
          ConstString(enumerator_decl->getNameAsString()).GetCString(), p));
    }
  }
}

clang::NamedDecl *
ClangPersistentVariables::GetPersistentDecl(ConstString name) {
  return m_persistent_decls.lookup(name.GetCString()).m_decl;
}
