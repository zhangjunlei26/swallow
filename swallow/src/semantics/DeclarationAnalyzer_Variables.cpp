/* DeclarationAnalyzer_Variables.cpp --
 *
 * Copyright (c) 2014, Lex Chou <lex at chou dot it>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Swallow nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "semantics/DeclarationAnalyzer.h"
#include "ast/ast.h"
#include "semantics/Symbol.h"
#include "semantics/SymbolScope.h"
#include "semantics/SymbolRegistry.h"
#include "common/Errors.h"
#include "semantics/ScopedNodes.h"
#include "semantics/TypeBuilder.h"
#include "common/CompilerResults.h"
#include <cassert>
#include "semantics/FunctionSymbol.h"
#include "semantics/FunctionOverloadedSymbol.h"
#include "semantics/GlobalScope.h"
#include "ast/NodeFactory.h"
#include "common/ScopedValue.h"
#include "semantics/SemanticContext.h"
#include "semantics/SemanticAnalyzer.h"
#include "semantics/SemanticUtils.h"

USE_SWALLOW_NS
using namespace std;

void DeclarationAnalyzer::registerSymbol(const SymbolPlaceHolderPtr& symbol, const NodePtr& node)
{
    SymbolScope* scope = symbolRegistry->getCurrentScope();
    NodeType::T nodeType = scope->getOwner()->getNodeType();
    //if(nodeType != NodeType::Program)
    //    symbol->setFlags(SymbolFlagMember, true);
    scope->addSymbol(symbol);
    switch(nodeType)
    {
        case NodeType::Class:
        case NodeType::Struct:
        case NodeType::Protocol:
        case NodeType::Extension:
        case NodeType::Enum:
            assert(ctx->currentType != nullptr);
            declarationFinished(symbol->getName(), symbol, node);
            break;
        default:
            break;
    }
}

void DeclarationAnalyzer::checkTupleDefinition(const TuplePtr& tuple, const ExpressionPtr& initializer)
{
    //this is a tuple definition, the corresponding declared type must be a tuple type
    TypeNodePtr declaredType = tuple->getDeclaredType();
    TypePtr type = semanticAnalyzer->lookupType(declaredType);
    if(!type)
    {
        error(tuple, Errors::E_USE_OF_UNDECLARED_TYPE_1, toString(declaredType));
        return;
    }
    if(!(type->getCategory() == Type::Tuple))
    {
        //tuple definition must have a tuple type definition
        error(tuple, Errors::E_TUPLE_PATTERN_MUST_MATCH_TUPLE_TYPE_1, toString(declaredType));
        return;
    }
    if(tuple->numElements() != type->numElementTypes())
    {
        //tuple pattern has the wrong length for tuple type '%'
        error(tuple, Errors::E_TUPLE_PATTERN_MUST_MATCH_TUPLE_TYPE_1, toString(declaredType));
        return;
    }
    //check if initializer has the same type with the declared type
    if(initializer)
    {
        TypePtr valueType = semanticAnalyzer->evaluateType(initializer);
        if(valueType && !Type::equals(valueType, type))
        {
            //tuple pattern has the wrong length for tuple type '%'
            //tuple types '%0' and '%1' have a different number of elements (%2 vs. %3)
            wstring expectedType = type->toString();
            wstring got = toString(valueType->numElementTypes());
            wstring expected = toString(type->numElementTypes());
            error(initializer, Errors::E_TUPLE_TYPES_HAVE_A_DIFFERENT_NUMBER_OF_ELEMENT_4, toString(declaredType), expectedType, got, expected);
            return;
        }
    }


    for(const PatternPtr& p : *tuple)
    {
        NodeType::T nodeType = p->getNodeType();
        if(nodeType != NodeType::Identifier)
        {

        }

    }
}


void DeclarationAnalyzer::visitValueBinding(const ValueBindingPtr& node)
{
    if(node->getOwner()->isReadOnly() && !node->getInitializer() && ctx->currentType == nullptr)
    {
        error(node, Errors::E_LET_REQUIRES_INITIALIZER);
        return;
    }

    //add implicitly constructor for Optional


    //TypePtr type = evaluateType(node->initializer);
    if(IdentifierPtr id = std::dynamic_pointer_cast<Identifier>(node->getName()))
    {
        TypePtr declaredType = node->getType() ? node->getType() : semanticAnalyzer->lookupType(node->getDeclaredType());//node->getDeclaredType() == nullptr ? id->getDeclaredType() : node->getDeclaredType());
        SCOPED_SET(ctx->contextualType, declaredType);
        if(!declaredType && !node->getInitializer())
        {
            error(node, Errors::E_TYPE_ANNOTATION_MISSING_IN_PATTERN);
            return;
        }
        SymbolPtr sym = symbolRegistry->getCurrentScope()->lookup(id->getIdentifier());
        assert(sym != nullptr);
        SymbolPlaceHolderPtr placeholder = std::dynamic_pointer_cast<SymbolPlaceHolder>(sym);
        assert(placeholder != nullptr);
        if(declaredType)
        {
            placeholder->setType(declaredType);
        }
        if(node->getInitializer())
        {
            placeholder->setFlags(SymbolFlagInitializing, true);
            ExpressionPtr initializer = semanticAnalyzer->transformExpression(declaredType, node->getInitializer());
            node->setInitializer(initializer);
            TypePtr actualType = initializer->getType();
            assert(actualType != nullptr);
            if(declaredType)
            {
                if(!Type::equals(actualType, declaredType) && !semanticAnalyzer->canConvertTo(initializer, declaredType))
                {
                    error(initializer, Errors::E_CANNOT_CONVERT_EXPRESSION_TYPE_2, actualType->toString(), declaredType->toString());
                    return;
                }
            }

            if(!declaredType)
                placeholder->setType(actualType);
        }
        assert(placeholder->getType() != nullptr);
        placeholder->setFlags(SymbolFlagInitializing, false);
    }
    else if(TuplePtr id = std::dynamic_pointer_cast<Tuple>(node->getName()))
    {
        TypeNodePtr declaredType = id->getDeclaredType();
        if(declaredType)
        {
            checkTupleDefinition(id, node->getInitializer());
        }
    }
    if(ctx->currentType && ctx->currentType->getCategory() == Type::Protocol)
    {
        error(node, Errors::E_PROTOCOL_VAR_MUST_BE_COMPUTED_PROPERTY_1);
    }
}

static void explodeValueBinding(SemanticAnalyzer* semanticAnalyzer, const ValueBindingsPtr& valueBindings, ValueBindings::Iterator& iter)
{
    ValueBindingPtr var = *iter;
    TuplePtr name = dynamic_pointer_cast<Tuple>(var->getName());
    TypePtr declaredType = var->getDeclaredType() ? semanticAnalyzer->lookupType(var->getDeclaredType()) : nullptr;
    TypePtr initializerType;
    if(var->getInitializer())
    {
        SemanticContext* ctx = semanticAnalyzer->getContext();
        SCOPED_SET(ctx->contextualType, declaredType);
        var->getInitializer()->accept(semanticAnalyzer);
        initializerType = var->getInitializer()->getType();
        assert(initializerType != nullptr);
    }

    if(declaredType && initializerType)
    {
        //it has both type definition and initializer, then we need to check if the initializer expression matches the type annotation
        if(!initializerType->canAssignTo(declaredType))
        {
            semanticAnalyzer->error(var, Errors::E_CANNOT_CONVERT_EXPRESSION_TYPE_2, initializerType->toString(), declaredType->toString());
            return;
        }
    }
    //expand it into tuple assignment
    NodeFactory* nodeFactory = valueBindings->getNodeFactory();
    //need a temporay variable to hold the initializer
    std::wstring tempName = semanticAnalyzer->generateTempName();
    ValueBindingPtr tempVar = nodeFactory->createValueBinding(*var->getSourceInfo());
    IdentifierPtr tempVarId = nodeFactory->createIdentifier(*var->getSourceInfo());
    tempVarId->setIdentifier(tempName);
    tempVar->setName(tempVarId);
    tempVar->setInitializer(var->getInitializer());
    tempVar->setType(declaredType ? declaredType : initializerType);
    tempVar->setTemporary(true);
    valueBindings->insertBefore(tempVar, iter);
    //now expand tuples
    vector<TupleExtractionResult> result;
    vector<int> indices;
    semanticAnalyzer->expandTuple(result, indices, name, tempName, tempVar->getType(), valueBindings->isReadOnly() ? AccessibilityConstant : AccessibilityVariable);
    for(auto v : result)
    {
        if(v.name->getIdentifier() == L"_")
            continue;//ignore the placeholder
        ValueBindingPtr var = nodeFactory->createValueBinding(*v.name->getSourceInfo());
        var->setName(v.name);
        var->setType(v.type);
        var->setInitializer(v.initializer);
        valueBindings->add(var);
    }
}

/*!
 * Need to explode a tuple variable definition into a sequence of single variable definitions
 */
void DeclarationAnalyzer::explodeValueBindings(const ValueBindingsPtr& node)
{
    auto iter = node->begin();
    for(; iter != node->end(); iter++)
    {
        ValueBindingPtr var = *iter;
        TuplePtr tuple = dynamic_pointer_cast<Tuple>(var->getName());
        if(!tuple)
            continue;
        explodeValueBinding(semanticAnalyzer, node, iter);
    }
}

void DeclarationAnalyzer::visitValueBindings(const ValueBindingsPtr& node)
{
    if(!(ctx->flags & SemanticContext::FLAG_PROCESS_DECLARATION))
        return;

    explodeValueBindings(node);

    //this will make untyped bindings has the type in following forms:
    //let a, b, c : Int
    //a and b will both be Int
    TypeNodePtr lastType = nullptr;
    for(auto iter = node->rbegin(); iter != node->rend(); iter++)
    {
        ValueBindingPtr binding = *iter;
        if(binding->getDeclaredType())
            lastType = binding->getDeclaredType();
        else
            binding->setDeclaredType(lastType);

    }

    if(node->isReadOnly() && ctx->currentType && ctx->currentType->getCategory() == Type::Protocol)
    {
        error(node, Errors::E_PROTOCOL_CANNOT_DEFINE_LET_CONSTANT_1);
        return;
    }

    int flags = SymbolFlagReadable;
    if(dynamic_cast<TypeDeclaration*>(symbolRegistry->getCurrentScope()->getOwner()))
        flags |= SymbolFlagMember;
    if(node->isReadOnly())
        flags |= SymbolFlagNonmutating;
    else
        flags |= SymbolFlagWritable;
    if(node->hasModifier(DeclarationModifiers::Static) || node->hasModifier(DeclarationModifiers::Class))
        flags |= SymbolFlagStatic;
    if(node->hasModifier(DeclarationModifiers::Lazy))
        flags |= SymbolFlagLazy;

    SymbolPlaceHolder::Role role = SymbolPlaceHolder::R_LOCAL_VARIABLE;
    if(!ctx->currentFunction)
    {
        if (ctx->currentType)
            role = SymbolPlaceHolder::R_PROPERTY;
        else
            role = SymbolPlaceHolder::R_TOP_LEVEL_VARIABLE;
    }

    if(role == SymbolPlaceHolder::R_PROPERTY)
        flags |= SymbolFlagStoredProperty;

    validateDeclarationModifiers(node);
    AccessLevel accessLevel = parseAccessLevel(node->getModifiers());
    for(const ValueBindingPtr& var : *node)
    {
        PatternPtr name = var->getName();
        if(name->getNodeType() != NodeType::Identifier)
            continue;//The tuple has been exploded into a sequence of single variable bindings, no need to handle tuple again
        IdentifierPtr id = std::static_pointer_cast<Identifier>(name);
        SymbolPtr s = nullptr;
        SymbolScope* scope = nullptr;
        symbolRegistry->lookupSymbol(id->getIdentifier(), &scope, &s);
        if(s && scope == symbolRegistry->getCurrentScope())
        {
            //already defined in current scope
            error(id, Errors::E_DEFINITION_CONFLICT, id->getIdentifier());
        }
        else
        {
            SymbolPlaceHolderPtr pattern(new SymbolPlaceHolder(id->getIdentifier(), id->getType(), role, flags));
            pattern->setAccessLevel(accessLevel);
            registerSymbol(pattern, id);
        }
    }
    GlobalScope* global = symbolRegistry->getGlobalScope();
    for(const ValueBindingPtr& v : *node)
    {
        PatternPtr name = v->getName();
        if (name->getNodeType() != NodeType::Identifier)
            continue;
        //skip placeholder
        IdentifierPtr id = std::static_pointer_cast<Identifier>(name);
        SymbolPtr s = symbolRegistry->lookupSymbol(id->getIdentifier());
        assert(s != nullptr);
        ExpressionPtr initializer = v->getInitializer();
        SymbolPlaceHolderPtr placeholder = std::dynamic_pointer_cast<SymbolPlaceHolder>(s);
        assert(placeholder != nullptr);
        if (initializer)
            placeholder->setFlags(SymbolFlagHasInitializer, true);
        if(v->isTemporary())
        {
            placeholder->setFlags(SymbolFlagTemporary, true);
            semanticAnalyzer->markInitialized(placeholder);
        }
        v->accept(semanticAnalyzer);

        //optional type always considered initialized, compiler will make it has a default value nil
        TypePtr symbolType = s->getType();
        if(initializer || global->isOptional(symbolType) || global->isImplicitlyUnwrappedOptional(symbolType))
            semanticAnalyzer->markInitialized(placeholder);
        if(!v->isTemporary())
        {
            //check access control level
            int decl = ctx->currentType ? D_PROPERTY : D_VARIABLE;
            verifyAccessLevel(node, placeholder->getType(), decl, C_TYPE);
        }
    }
}
